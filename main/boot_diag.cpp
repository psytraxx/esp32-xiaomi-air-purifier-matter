#include "boot_diag.h"

#include <inttypes.h>

#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "boot_diag";

#define NVS_NS            "bootdiag"
#define KEY_BOOT_STREAK   "streak"      // consecutive boots without mark_healthy
#define KEY_MATTER_FAULTS "mtr_faults"  // consecutive esp_matter::start() failures

// After this many consecutive Matter-init failures, erase the Matter NVS
// partition on the next restart. A truncated NVS write during a power cut is
// the realistic cause; re-commissioning is recoverable, a reboot loop is not.
#define MATTER_FAULT_ERASE_THRESHOLD 5

static const char *reset_reason_str(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_POWERON:   return "power-on";
    case ESP_RST_EXT:       return "external";
    case ESP_RST_SW:        return "software";
    case ESP_RST_PANIC:     return "panic";
    case ESP_RST_INT_WDT:   return "int-wdt";
    case ESP_RST_TASK_WDT:  return "task-wdt";
    case ESP_RST_WDT:       return "other-wdt";
    case ESP_RST_BROWNOUT:  return "brownout";
    case ESP_RST_DEEPSLEEP: return "deep-sleep";
    case ESP_RST_SDIO:      return "sdio";
    case ESP_RST_USB:       return "usb";
    case ESP_RST_JTAG:      return "jtag";
    case ESP_RST_EFUSE:     return "efuse";
    case ESP_RST_PWR_GLITCH: return "power-glitch";
    case ESP_RST_CPU_LOCKUP: return "cpu-lockup";
    default:                return "unknown";
    }
}

static uint32_t s_boot_streak = 0;

// Open the default-partition namespace, initialising NVS if needed. Returns an
// NVS handle, or 0 on failure (in which case boot diagnostics just aren't
// persisted this boot — logged by the caller, not fatal).
static nvs_handle_t open_ns(void)
{
    // boot_diag_init() is the first NVS user (it runs before esp_matter), so it
    // owns the default-partition init. Standard recovery: on a full or
    // version-bumped partition, erase and retry once.
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK)
        return 0;

    nvs_handle_t h = 0;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
        return 0;
    return h;
}

static uint32_t get_u32(nvs_handle_t h, const char *key)
{
    uint32_t v = 0;
    nvs_get_u32(h, key, &v); // leaves v at 0 if not found
    return v;
}

void boot_diag_init(void)
{
    esp_reset_reason_t reason = esp_reset_reason();

    nvs_handle_t h = open_ns();
    if (!h)
    {
        ESP_LOGW(TAG, "NVS unavailable — boot diagnostics not persisted (reset: %s)",
                 reset_reason_str(reason));
        return;
    }

    s_boot_streak = get_u32(h, KEY_BOOT_STREAK) + 1;
    uint32_t matter_faults = get_u32(h, KEY_MATTER_FAULTS);

    nvs_set_u32(h, KEY_BOOT_STREAK, s_boot_streak);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGI(TAG, "reset=%s | boot streak=%" PRIu32 " | matter faults=%" PRIu32,
             reset_reason_str(reason), s_boot_streak, matter_faults);
    if (s_boot_streak >= 5)
        ESP_LOGW(TAG, "device has restarted %" PRIu32 " times without reaching a "
                      "steady state", s_boot_streak);
}

uint32_t boot_diag_boot_streak(void)
{
    return s_boot_streak;
}

void boot_diag_mark_healthy(void)
{
    nvs_handle_t h = open_ns();
    if (!h)
        return;

    // Reached a good state — clear both counters.
    nvs_set_u32(h, KEY_BOOT_STREAK, 0);
    nvs_set_u32(h, KEY_MATTER_FAULTS, 0);
    nvs_commit(h);
    nvs_close(h);
    s_boot_streak = 0;
    ESP_LOGI(TAG, "steady state reached — boot counters cleared");
}

bool boot_diag_record_matter_fault(void)
{
    nvs_handle_t h = open_ns();
    if (!h)
        return false;

    uint32_t faults = get_u32(h, KEY_MATTER_FAULTS) + 1;
    nvs_set_u32(h, KEY_MATTER_FAULTS, faults);
    nvs_commit(h);
    nvs_close(h);

    ESP_LOGE(TAG, "Matter init fault #%" PRIu32, faults);
    return faults >= MATTER_FAULT_ERASE_THRESHOLD;
}
