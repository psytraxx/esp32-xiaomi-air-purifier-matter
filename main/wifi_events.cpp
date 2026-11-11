#include "wifi_events.h"

#include <cinttypes>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"

#include "wifi_diag.h"

static const char *TAG = "wifi";

// Only touched from the default event loop task.
static wifi_diag::DisconnectFilter s_filter;

static void on_connected(const wifi_event_sta_connected_t *e)
{
    uint8_t last_reason = 0;
    uint32_t failures = s_filter.on_connected(last_reason);
    if (failures > 0)
        ESP_LOGI(TAG, "Connected after %" PRIu32 " failed attempt(s), last reason %u %s", failures, last_reason,
                 wifi_diag::reason_name(last_reason));
    ESP_LOGI(TAG, "Connected to '%.*s' (channel %u), security %s", static_cast<int>(e->ssid_len),
             reinterpret_cast<const char *>(e->ssid), e->channel, wifi_diag::authmode_name(e->authmode));
}

static void on_disconnected(const wifi_event_sta_disconnected_t *e)
{
    auto d = s_filter.on_disconnect(e->reason);
    if (d.previous_count > 1)
        ESP_LOGW(TAG, "Previous failure repeated %" PRIu32 " times: reason %u %s", d.previous_count, d.previous_reason,
                 wifi_diag::reason_name(d.previous_reason));
    if (!d.log)
        return;
    const char *hint = wifi_diag::reason_hint(e->reason);
    ESP_LOGW(TAG, "Disconnected from '%.*s' (RSSI %d dBm): reason %u %s%s%s%s", static_cast<int>(e->ssid_len),
             reinterpret_cast<const char *>(e->ssid), e->rssi, e->reason, wifi_diag::reason_name(e->reason),
             hint ? " — " : "", hint ? hint : "", d.repeats > 1 ? " (repeating)" : "");
}

static void on_wifi_event(void *, esp_event_base_t, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_CONNECTED && data)
        on_connected(static_cast<const wifi_event_sta_connected_t *>(data));
    else if (id == WIFI_EVENT_STA_DISCONNECTED && data)
        on_disconnected(static_cast<const wifi_event_sta_disconnected_t *>(data));
}

void wifi_events_start()
{
    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr, nullptr);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Wi-Fi event logging not available: %s", esp_err_to_name(err));
}
