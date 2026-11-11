#include "wifi_events.h"

#include <cinttypes>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#include "wifi_diag.h"

static const char *TAG = "wifi";

// Only touched from the default event loop task.
static wifi_diag::DisconnectFilter s_filter;

// The Matter SDK zeroes the whole station config before filling in SSID and
// password, which leaves sae_pwe_h2e at WPA3_SAE_PWE_UNSPECIFIED instead of
// ESP-IDF's normal "both methods" (hunt-and-peck and hash-to-element). On
// mixed WPA2/WPA3 networks that can make the WPA3 handshake fail. Put the
// usual values back while the station isn't connected; the next attempt
// uses them, and they're stored with the Wi-Fi credentials. Harmless when
// WPA3 support is compiled out.
static void restore_security_defaults(const char *when)
{
    wifi_config_t cfg = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK || cfg.sta.ssid[0] == 0)
        return; // not provisioned yet
    if (cfg.sta.sae_pwe_h2e == WPA3_SAE_PWE_BOTH && !cfg.sta.pmf_cfg.required)
        return;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    cfg.sta.pmf_cfg.required = false; // PMF stays optional; the stack offers it whenever the AP does
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Restored WPA3 handshake defaults (SAE both methods, PMF optional) %s", when);
    else
        ESP_LOGD(TAG, "Couldn't restore WPA3 handshake defaults %s: %s", when, esp_err_to_name(err));
}

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
    restore_security_defaults("after a disconnect");
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
#if CONFIG_ESP_WIFI_ENABLE_WPA3_SAE
    if (wifi_diag::is_security_mismatch(e->reason) && d.repeats == 1)
        ESP_LOGW(TAG, "This is the WPA3-enabled build. If this keeps happening on a mixed WPA2/WPA3 network, "
                      "flash the default (WPA2-only) build instead");
#endif
}

static void on_wifi_event(void *, esp_event_base_t, int32_t id, void *data)
{
    if (id == WIFI_EVENT_STA_START)
        restore_security_defaults("at station start");
    else if (id == WIFI_EVENT_STA_CONNECTED && data)
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
