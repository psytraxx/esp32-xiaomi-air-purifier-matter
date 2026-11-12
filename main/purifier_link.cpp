// Compile the per-line MCU log in when PURIFIER_LINK_DEBUG is set (see
// app_config.h). Must come before esp_log.h, which reads LOG_LOCAL_LEVEL.
#if defined(PURIFIER_LINK_DEBUG) && PURIFIER_LINK_DEBUG
#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#endif

#include "purifier_link.h"

#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <ctime>
#include <string>

#include "driver/uart.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "app_config.h"
#include "board_pins.h"
#include "miot_session.h"

static const char *TAG = "purifier_link";

static PurifierLinkCallbacks s_cb;
static SemaphoreHandle_t s_mutex = nullptr;
static miot::Session *s_session = nullptr;
static purifier::State s_state;
// Set once by purifier_link_set_model(); read without the lock afterwards.
static std::atomic<const purifier::Profile *> s_profile{ nullptr };
static purifier::Model s_detected = purifier::Model::Unknown;

static std::atomic<bool> s_commissioned{ false };
static std::atomic<bool> s_wifi_connected{ false };
static std::atomic<bool> s_mcu_alive{ false };

// ── Locking ──────────────────────────────────────────────────────────────────

// The mutex is recursive: session hooks run inside handle_line() with the
// lock held and may queue further commands.
class LinkLock
{
public:
    LinkLock() { xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY); }
    ~LinkLock() { xSemaphoreGiveRecursive(s_mutex); }
    LinkLock(const LinkLock &) = delete;
    LinkLock &operator=(const LinkLock &) = delete;
};

static uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// ── Session hooks (run on the link task, lock held) ──────────────────────────

static void report_detected(purifier::Model model, const char *how)
{
    if (model == purifier::Model::Unknown || model == s_detected)
        return;
    s_detected = model;
    ESP_LOGI(TAG, "Purifier detected (%s): Mi Air Purifier %s", how, purifier::model_name(model));
    if (s_cb.on_model_detected)
        s_cb.on_model_detected(model);
}

static void on_model(const std::string &name)
{
    purifier::Model m = purifier::model_from_string(name);
    if (m == purifier::Model::Unknown)
        ESP_LOGW(TAG, "Unsupported purifier model '%s'", name.c_str());
    report_detected(m, "model announcement");
}

static void on_properties(const std::vector<miot::PropertyUpdate> &updates)
{
    report_detected(purifier::model_from_probe(updates), "probe");

    const purifier::Profile *profile = s_profile.load();
    if (!profile)
        return; // model not chosen yet: values arrive again with the first poll

    uint32_t changed = 0;
    for (const auto &u : updates)
    {
        ESP_LOGD(TAG, "prop %u.%u = %s", u.id.siid, u.id.piid, u.value.to_wire().c_str());
        changed |= purifier::apply(*profile, s_state, u);
    }

    if ((changed & purifier::CHANGED_FAULT) && s_state.fault && *s_state.fault != 0)
        ESP_LOGW(TAG, "Purifier reports fault code %d", *s_state.fault);
    if ((changed & purifier::CHANGED_FILTER_DOOR) && s_state.filter_door_open && *s_state.filter_door_open)
        ESP_LOGW(TAG, "Filter door open");

    if (changed && s_cb.on_state_changed)
        s_cb.on_state_changed(changed);
}

static void on_set_failed(miot::PropertyId id, int code)
{
    ESP_LOGW(TAG, "MCU rejected set %u.%u (code %d) — re-reading it", id.siid, id.piid, code);
    // Re-read so Matter shows what the purifier is really doing.
    s_session->queue_get({ id });
}

static std::string time_reply(bool posix)
{
    time_t now = time(nullptr);
    struct tm tm_local = {};
    localtime_r(&now, &tm_local);
    if (tm_local.tm_year + 1900 < 2024)
        return "0"; // clock not set yet (no SNTP sync)
    if (posix)
        return std::to_string(static_cast<long long>(now));
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
    return buf;
}

static std::string mac_reply()
{
    uint8_t mac[6] = {};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK)
        return "error";
    char buf[18];
    snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return buf;
}

static void on_restore()
{
    if (s_cb.on_restore)
        s_cb.on_restore();
}

// ── Link task ────────────────────────────────────────────────────────────────

static void send_reply(const std::string &reply)
{
    ESP_LOGD(TAG, "-> %s", reply.c_str());
    uart_write_bytes(PURIFIER_UART_NUM, reply.data(), reply.size());
    uart_write_bytes(PURIFIER_UART_NUM, "\r", 1);
}

static void queue_poll()
{
    const purifier::Profile *profile = s_profile.load();
    LinkLock lock;
    if (profile)
        s_session->queue_get(purifier::polled_properties(*profile));
    else if (s_detected == purifier::Model::Unknown)
        s_session->queue_get(purifier::probe_properties());
}

static void link_task(void *)
{
    static char line[miot::MAX_LINE_LENGTH + 1];
    size_t len = 0;
    uint32_t last_byte_ms = 0;
    uint32_t last_poll_ms = now_ms();
    uint32_t last_heartbeat_ms = now_ms();
    bool overflow = false;

    // Discard whatever the MCU sent before we were listening.
    uart_flush_input(PURIFIER_UART_NUM);

    for (;;)
    {
        uint8_t buf[64];
        int n = uart_read_bytes(PURIFIER_UART_NUM, buf, sizeof(buf), pdMS_TO_TICKS(LINK_READ_TIMEOUT_MS));
        uint32_t now = now_ms();

        for (int i = 0; i < n; i++)
        {
            char c = static_cast<char>(buf[i]);
            last_byte_ms = now;
            if (c == '\n')
                continue;
            if (c != '\r')
            {
                if (len < miot::MAX_LINE_LENGTH)
                    line[len++] = c;
                else
                    overflow = true;
                continue;
            }

            if (overflow)
            {
                ESP_LOGW(TAG, "Dropping over-long MCU line");
            }
            else
            {
                std::string_view received(line, len);
                std::string reply;
                {
                    LinkLock lock;
                    reply = s_session->handle_line(received, now);
                }
                // get_down arrives 5x a second: keep it out of the debug log.
                if (received != "get_down" || reply != "down none")
                    ESP_LOGD(TAG, "<- %.*s", static_cast<int>(len), line);
                send_reply(reply);
            }
            len = 0;
            overflow = false;
        }

        // A partial line that stalls is noise; don't glue it to the next one.
        if (len > 0 && now - last_byte_ms > LINK_LINE_TIMEOUT_MS)
        {
            ESP_LOGW(TAG, "Dropping incomplete MCU line: %.*s", static_cast<int>(len), line);
            len = 0;
            overflow = false;
        }

        if (now - last_poll_ms >= POLL_INTERVAL_MS)
        {
            last_poll_ms = now;
            queue_poll();
        }
        if (now - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS)
        {
            last_heartbeat_ms = now;
            if (const purifier::Profile *profile = s_profile.load())
            {
                LinkLock lock;
                s_session->queue_set(profile->heartbeat, miot::Value::of_int(purifier::HEARTBEAT_VALUE));
            }
        }

        bool alive;
        {
            LinkLock lock;
            alive = s_session->mcu_seen() && now - s_session->last_rx_ms() < MCU_SILENCE_TIMEOUT_MS;
        }
        if (alive != s_mcu_alive.exchange(alive))
        {
            if (alive)
                ESP_LOGI(TAG, "MCU is talking");
            else
                ESP_LOGW(TAG, "MCU silent for %" PRIu32 " ms", MCU_SILENCE_TIMEOUT_MS);
        }
    }
}

// ── Public API ───────────────────────────────────────────────────────────────

void purifier_link_start(const PurifierLinkCallbacks &callbacks)
{
    s_cb = callbacks;
    s_mutex = xSemaphoreCreateRecursiveMutex();

    uart_config_t cfg = {};
    cfg.baud_rate = PURIFIER_UART_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(PURIFIER_UART_NUM, LINK_UART_RX_BUF, LINK_UART_TX_BUF, 0, nullptr, ESP_INTR_FLAG_IRAM));
    ESP_ERROR_CHECK(uart_param_config(PURIFIER_UART_NUM, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(PURIFIER_UART_NUM, PIN_MCU_TX, PIN_MCU_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    miot::Session::Hooks hooks;
    hooks.on_properties = on_properties;
    hooks.on_set_failed = on_set_failed;
    hooks.on_event = [](uint16_t siid, uint16_t eiid, const std::vector<miot::PropertyUpdate> &) {
        ESP_LOGI(TAG, "MCU event %u.%u", siid, eiid);
    };
    hooks.on_restore = on_restore;
    hooks.on_model = on_model;
    hooks.net_state = []() {
        return miot::net_state_for(s_commissioned.load(), s_wifi_connected.load());
    };
    hooks.time_reply = time_reply;
    hooks.mac_reply = mac_reply;
    hooks.log = [](const std::string &msg) { ESP_LOGI(TAG, "%s", msg.c_str()); };

    s_session = new miot::Session(hooks);
    {
        LinkLock lock;
        s_session->start();
        // Tell the models apart even if the MCU never announces itself (it
        // only does so when it boots, and the ESP32 may restart on its own).
        s_session->queue_get(purifier::probe_properties());
    }

#if PURIFIER_LINK_DEBUG
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
#endif
    xTaskCreate(link_task, "purifier_link", LINK_TASK_STACK, nullptr, LINK_TASK_PRIORITY, nullptr);
    ESP_LOGI(TAG, "MCU link on UART%d (TX GPIO%d, RX GPIO%d, %d baud)", PURIFIER_UART_NUM, PIN_MCU_TX, PIN_MCU_RX, PURIFIER_UART_BAUD);
}

void purifier_link_set_network(bool commissioned, bool wifi_connected)
{
    s_commissioned = commissioned;
    s_wifi_connected = wifi_connected;
    if (!s_session)
        return;
    LinkLock lock;
    s_session->announce_net_state();
}

purifier::State purifier_link_state()
{
    LinkLock lock;
    return s_state;
}

static void queue_sets(const std::vector<purifier::SetCommand> &cmds, const char *why)
{
    for (const auto &c : cmds)
    {
        ESP_LOGI(TAG, "%s: set %u.%u = %s", why, c.id.siid, c.id.piid, c.value.to_wire().c_str());
        s_session->queue_set(c.id, c.value);
    }
}

void purifier_link_set_model(purifier::Model model)
{
    const purifier::Profile &profile = purifier::profile(model);
    s_profile.store(&profile);
    ESP_LOGI(TAG, "Using the Mi Air Purifier %s property table", purifier::model_name(profile.model));
    LinkLock lock;
    s_session->queue_get(purifier::polled_properties(profile));
}

const purifier::Profile *purifier_link_profile()
{
    return s_profile.load();
}

void purifier_link_apply_fan(const purifier::FanDrive &drive)
{
    const purifier::Profile *profile = s_profile.load();
    if (!profile)
        return;
    LinkLock lock;
    purifier::FanTarget target = purifier::fan_target(drive);
    queue_sets(purifier::commands_for(*profile, target, s_state), "fan");
}

void purifier_link_set_sleep(bool sleep_wind)
{
    const purifier::Profile *profile = s_profile.load();
    if (!profile)
        return;
    LinkLock lock;
    queue_sets(purifier::commands_for_sleep(*profile, sleep_wind, s_state), "sleep");
}

void purifier_link_reset_filter()
{
    const purifier::Profile *profile = s_profile.load();
    if (!profile)
        return;
    LinkLock lock;
    ESP_LOGI(TAG, "filter: reset");
    s_session->queue_action(profile->reset_filter, profile->reset_filter_args);
    s_session->queue_get({ profile->filter_life, profile->filter_hours });
}

bool purifier_link_mcu_alive()
{
    return s_mcu_alive.load();
}
