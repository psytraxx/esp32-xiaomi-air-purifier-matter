// Mi Air Purifier 3H → Matter bridge firmware for the purifier's ESP32 module.
//
// Boot order:
//   1. boot diagnostics (reset reason, reboot-loop guard; owns NVS init)
//   2. purifier link: UART to the STM32 and the MIoT session, so the MCU's
//      first property reports are in before any controller reads
//   3. Matter: node, endpoints, stack start
// After that the main task only marks the boot healthy and logs free heap.

#include <cinttypes>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "app_config.h"
#include "boot_diag.h"
#include "matter_setup.h"
#include "purifier_link.h"

static const char *TAG = "app_main";

static esp_timer_handle_t s_restore_timer = nullptr;

// The MCU asked for a network reset (the user's Wi-Fi reset on the purifier).
// Runs on the link task: reply "ok" first, wipe pairings a moment later.
static void on_restore()
{
    if (!s_restore_timer || esp_timer_is_active(s_restore_timer))
        return;
    ESP_LOGW(TAG, "Network reset requested on the purifier");
    esp_timer_start_once(s_restore_timer, static_cast<uint64_t>(RESTORE_DELAY_MS) * 1000u);
}

static void restore_timer_cb(void *)
{
    matter_factory_reset();
}

static void log_heap()
{
    ESP_LOGI(TAG, "heap: free=%u min_free=%u largest=%u",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
             static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
}

extern "C" void app_main(void)
{
    boot_diag_init();

    esp_timer_create_args_t timer_args = {};
    timer_args.callback = restore_timer_cb;
    timer_args.name = "restore";
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_restore_timer));

    PurifierLinkCallbacks callbacks;
    callbacks.on_state_changed = matter_on_purifier_changed;
    callbacks.on_restore = on_restore;
    purifier_link_start(callbacks);

    EventGroupHandle_t events = xEventGroupCreate();
    matter_setup(events);

    // A boot counts as healthy once the Matter server is up.
    if (xEventGroupWaitBits(events, MATTER_BIT_SERVER_READY, pdFALSE, pdTRUE, pdMS_TO_TICKS(60000)) & MATTER_BIT_SERVER_READY)
        boot_diag_mark_healthy();
    else
        ESP_LOGW(TAG, "Matter server not ready after 60 s");

    log_heap();
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(HEAP_LOG_INTERVAL_MS));
        log_heap();
        if (!purifier_link_mcu_alive())
            ESP_LOGW(TAG, "No traffic from the purifier MCU");
    }
}
