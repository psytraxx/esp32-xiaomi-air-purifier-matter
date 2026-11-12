// Xiaomi Mi Air Purifier 3H / 3C → Matter firmware for the purifier's ESP32
// module.
//
// Boot order:
//   1. boot diagnostics (reset reason, reboot-loop guard; owns NVS init)
//   2. purifier link: UART to the STM32 and the MIoT session; probes the model
//   3. model choice: forced at build time, else remembered from an earlier
//      boot, else detected now (waiting up to MODEL_DETECT_TIMEOUT_MS)
//   4. Matter: node, the model's endpoints, stack start
// After that the main task marks the boot healthy, logs free heap, and acts
// on later model detections (remember it; restart if it's a different model).

#include <atomic>
#include <cinttypes>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "esp_wifi.h"

#include "app_config.h"
#include "boot_diag.h"
#include "matter_setup.h"
#include "ota_server.h"
#include "purifier_link.h"
#include "purifier_model.h"

static const char *TAG = "app_main";

#define NVS_NS "purifier"
#define NVS_KEY_MODEL "model"

// Main-task inbox bits (separate from the Matter event group).
#define MAIN_BIT_MODEL_DETECTED (1u << 0)

static EventGroupHandle_t s_main_events = nullptr;
static std::atomic<uint8_t> s_detected{ static_cast<uint8_t>(purifier::Model::Unknown) };
static esp_timer_handle_t s_restore_timer = nullptr;

// ── Remembered model (NVS) ────────────────────────────────────────────────────

static purifier::Model load_model()
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK)
        return purifier::Model::Unknown;
    uint8_t v = 0;
    nvs_get_u8(h, NVS_KEY_MODEL, &v);
    nvs_close(h);
    auto m = static_cast<purifier::Model>(v);
    return (m == purifier::Model::Mb3 || m == purifier::Model::Mb4) ? m : purifier::Model::Unknown;
}

static void save_model(purifier::Model m)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u8(h, NVS_KEY_MODEL, static_cast<uint8_t>(m));
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "Remembered model: %s", purifier::model_name(m));
}

// ── Link callbacks (run on the link task: don't block) ────────────────────────

static void on_model_detected(purifier::Model model)
{
    s_detected = static_cast<uint8_t>(model);
    if (s_main_events)
        xEventGroupSetBits(s_main_events, MAIN_BIT_MODEL_DETECTED);
}

// The MCU asked for a network reset (the user's Wi-Fi reset on the purifier).
// Reply "ok" first (the session does), wipe pairings a moment later.
static void on_restore()
{
    if (!s_restore_timer || esp_timer_is_active(s_restore_timer))
        return;
    ESP_LOGW(TAG, "Network reset requested on the purifier");
    esp_timer_start_once(s_restore_timer, static_cast<uint64_t>(RESTORE_DELAY_MS) * 1000u);
}

static void restore_timer_cb(void *)
{
    ota_keep_before_restart("network reset");
    matter_factory_reset();
}

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool wifi_up()
{
    wifi_ap_record_t ap = {};
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
}

static void log_heap()
{
    ESP_LOGI(TAG, "heap: free=%u min_free=%u largest=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

static purifier::Model detected()
{
    return static_cast<purifier::Model>(s_detected.load());
}

// Act on a detection: remember it, or restart into the right model.
static void handle_detection(purifier::Model forced, purifier::Model &remembered, purifier::Model active)
{
    purifier::Model found = detected();
    switch (purifier::on_model_detected(forced, remembered, active, found))
    {
    case purifier::DetectAction::None:
        break;
    case purifier::DetectAction::Remember:
        save_model(found);
        remembered = found;
        break;
    case purifier::DetectAction::RememberAndRestart:
        ESP_LOGW(TAG, "This is a Mi Air Purifier %s, not a %s — restarting with its endpoints",
                 purifier::model_name(found), purifier::model_name(active));
        save_model(found);
        ota_keep_before_restart("model change");
        vTaskDelay(pdMS_TO_TICKS(200));
        esp_restart();
        break;
    case purifier::DetectAction::WarnForced:
        ESP_LOGW(TAG, "Purifier reports itself as a %s, but the build forces a %s",
                 purifier::model_name(found), purifier::model_name(forced));
        break;
    }
}

// ── Entry point ───────────────────────────────────────────────────────────────

extern "C" void app_main(void)
{
    boot_diag_init(); // also initialises NVS

    s_main_events = xEventGroupCreate();

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = restore_timer_cb;
    timer_args.name = "restore";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_restore_timer));

    PurifierLinkCallbacks callbacks;
    callbacks.on_state_changed = matter_on_purifier_changed;
    callbacks.on_restore = on_restore;
    callbacks.on_model_detected = on_model_detected;
    purifier_link_start(callbacks);

    // Choose the model before Matter starts: the endpoints depend on it.
    const auto forced = static_cast<purifier::Model>(PURIFIER_MODEL);
    purifier::Model remembered = load_model();
    purifier::Model model = purifier::boot_model(forced, remembered);
    if (model == purifier::Model::Unknown)
    {
        ESP_LOGI(TAG, "Detecting the purifier model (up to %" PRIu32 " s)…", MODEL_DETECT_TIMEOUT_MS / 1000);
        xEventGroupWaitBits(s_main_events, MAIN_BIT_MODEL_DETECTED, pdFALSE, pdTRUE, pdMS_TO_TICKS(MODEL_DETECT_TIMEOUT_MS));
        model = detected();
        if (model == purifier::Model::Unknown)
        {
            model = static_cast<purifier::Model>(FALLBACK_MODEL);
            ESP_LOGW(TAG, "No answer from the purifier MCU — assuming a %s for now", purifier::model_name(model));
        }
    }
    ESP_LOGI(TAG, "Running as Mi Air Purifier %s%s", purifier::model_name(model),
             forced != purifier::Model::Unknown ? " (forced by build)" : "");

    purifier_link_set_model(model);

    EventGroupHandle_t matter_events = xEventGroupCreate();
    matter_setup(matter_events, model);
    ota_server_start();

    // A boot counts as healthy once the Matter server is up.
    if (xEventGroupWaitBits(matter_events, MATTER_BIT_SERVER_READY, pdFALSE, pdTRUE, pdMS_TO_TICKS(60000)) & MATTER_BIT_SERVER_READY)
        boot_diag_mark_healthy();
    else
        ESP_LOGW(TAG, "Matter server not ready after 60 s");

    log_heap();
    int64_t last_heap_log_us = esp_timer_get_time();
    for (;;)
    {
        // Decide on a freshly updated image: keep it once everything works,
        // roll back if it doesn't within ota_policy::VERIFY_TIMEOUT_MS.
        if (ota_pending_verify())
        {
            const bool matter_ready = xEventGroupGetBits(matter_events) & MATTER_BIT_SERVER_READY;
            ota_check_image(static_cast<uint32_t>(esp_timer_get_time() / 1000), matter_ready, wifi_up(),
                            purifier_link_mcu_alive());
        }

        EventBits_t bits = xEventGroupWaitBits(s_main_events, MAIN_BIT_MODEL_DETECTED, pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(ota_pending_verify() ? 1000 : 10000));
        // The bit is left set by the boot-time wait, so a detection made
        // during boot is handled on the first pass.
        if (bits & MAIN_BIT_MODEL_DETECTED)
            handle_detection(forced, remembered, model);

        if (esp_timer_get_time() - last_heap_log_us >= static_cast<int64_t>(HEAP_LOG_INTERVAL_MS) * 1000)
        {
            last_heap_log_us = esp_timer_get_time();
            log_heap();
            if (!purifier_link_mcu_alive())
                ESP_LOGW(TAG, "No traffic from the purifier MCU");
        }
    }
}
