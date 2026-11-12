#include "wifi_events.h"

#include <cinttypes>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#include "wifi_diag.h"

static const char *TAG = "wifi";

// Only touched from the default event loop task.
static wifi_diag::DisconnectFilter s_filter;

// Station options the Matter SDK doesn't set. Its ESP32 Wi-Fi driver zeroes
// the whole station config before storing new credentials, so they're put
// back on the stored config: once at start-up and after every failed join,
// so the next attempt uses them. Stored with the credentials, so normally
// this only writes once after commissioning.
//   - disable_wpa3_compatible_mode: ignore WPA3 compatibility (RSN override)
//     elements and join with the AP's base RSN element. The FRITZ!Box 4040
//     advertises an RSNXE override in its beacon but leaves it out of
//     handshake message 3, which fails every join with reason 17
//     (IE_IN_4WAY_DIFFERS); same fix as gravity-cube-espidf. Such routers
//     are then joined with WPA2; WPA3 is still used where the base element
//     offers it, and on WPA3-only networks.
//   - sae_pwe_h2e: both SAE methods, ESP-IDF's normal default (zeroing leaves
//     it "unspecified").
// Host name sent with DHCP. CONFIG_LWIP_LOCAL_HOSTNAME is the same on every
// unit, so two purifiers would share one name on the router.
static char s_hostname[33] = {};

static void apply_hostname()
{
    if (!s_hostname[0])
        return;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    const char *current = nullptr;
    if (!netif || (esp_netif_get_hostname(netif, &current) == ESP_OK && current && strcmp(current, s_hostname) == 0))
        return;
    esp_err_t err = esp_netif_set_hostname(netif, s_hostname);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Host name: %s", s_hostname);
    else
        ESP_LOGW(TAG, "Couldn't set host name: %s", esp_err_to_name(err));
}

static void apply_station_options(const char *when)
{
    apply_hostname();
    wifi_config_t cfg = {};
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK || cfg.sta.ssid[0] == 0)
        return; // not provisioned yet
    if (cfg.sta.disable_wpa3_compatible_mode && cfg.sta.sae_pwe_h2e == WPA3_SAE_PWE_BOTH)
        return;
    cfg.sta.disable_wpa3_compatible_mode = 1;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "Station options set %s (WPA3 compatibility elements ignored)", when);
    else
        ESP_LOGW(TAG, "Couldn't set station options %s: %s", when, esp_err_to_name(err));
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
    if (id == WIFI_EVENT_STA_START)
        apply_station_options("at station start");
    else if (id == WIFI_EVENT_STA_CONNECTED && data)
        on_connected(static_cast<const wifi_event_sta_connected_t *>(data));
    else if (id == WIFI_EVENT_STA_DISCONNECTED && data)
    {
        apply_station_options("after a failed join");
        on_disconnected(static_cast<const wifi_event_sta_disconnected_t *>(data));
    }
}

void wifi_events_start(const char *hostname)
{
    if (hostname)
        strlcpy(s_hostname, hostname, sizeof(s_hostname));
    esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, nullptr, nullptr);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "Wi-Fi event logging not available: %s", esp_err_to_name(err));
    // The station may already have started before this handler existed.
    apply_station_options("at start-up");
}
