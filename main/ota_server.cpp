#include "ota_server.h"

#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "ota_policy.h"
#include "purifier_link.h"

static const char *TAG = "ota";

// Time for the reply to leave before the restart.
static constexpr uint32_t RESTART_DELAY_MS = 500;

// Receive timeouts in a row (recv_wait_timeout = 10 s each) before an upload
// counts as stalled.
static constexpr uint32_t MAX_RECV_TIMEOUTS = 3;

static bool s_pending = false;
static bool s_decided = false;

static esp_err_t get_status(httpd_req_t *req)
{
    const esp_app_desc_t *app = esp_app_get_description();
    const purifier::Profile *profile = purifier_link_profile();
    char body[192];
    snprintf(body, sizeof(body), "%s %s, running from %s, Mi Air Purifier %s%s\n", app->project_name, app->version,
             esp_ota_get_running_partition()->label, profile ? purifier::model_name(profile->model) : "unknown",
             s_pending && !s_decided ? " (new firmware, not yet kept)" : "");
    httpd_resp_set_type(req, "text/plain");
    return httpd_resp_sendstr(req, body);
}

static bool authorized(httpd_req_t *req)
{
    if (CONFIG_PURIFIER_OTA_TOKEN[0] == '\0')
        return true;
    char header[sizeof("Bearer ") + sizeof(CONFIG_PURIFIER_OTA_TOKEN) + 1] = {};
    if (httpd_req_get_hdr_value_str(req, "Authorization", header, sizeof(header)) != ESP_OK)
        return false;
    return ota_policy::token_matches(header, CONFIG_PURIFIER_OTA_TOKEN);
}

// The body is the app image, build/air_purifier_matter.bin.
static esp_err_t post_ota(httpd_req_t *req)
{
    if (!authorized(req))
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Wrong or missing token");

    const esp_partition_t *slot = esp_ota_get_next_update_partition(nullptr);
    if (!slot)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No update slot (old partition table?)");
    if (req->content_len == 0 || req->content_len > slot->size)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Image empty or too big");
    ESP_LOGI(TAG, "Receiving %u bytes into %s", static_cast<unsigned>(req->content_len), slot->label);

    // Erase as it goes rather than the whole slot up front, so Matter and the
    // purifier link only stall for short moments.
    esp_ota_handle_t ota = 0;
    esp_err_t err = esp_ota_begin(slot, OTA_WITH_SEQUENTIAL_WRITES, &ota);
    if (err != ESP_OK)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, esp_err_to_name(err));

    // The server handles one request at a time.
    static char buf[2048];
    size_t left = req->content_len;
    uint32_t timeouts = 0;
    while (left > 0)
    {
        const int n = httpd_req_recv(req, buf, left < sizeof(buf) ? left : sizeof(buf));
        if (n == HTTPD_SOCK_ERR_TIMEOUT)
        {
            // A sender that stalls would otherwise hold the slot (and one of
            // the server's two sockets) for good.
            if (++timeouts < MAX_RECV_TIMEOUTS)
                continue;
            ESP_LOGW(TAG, "Upload stalled with %u bytes to go", static_cast<unsigned>(left));
            esp_ota_abort(ota);
            return httpd_resp_send_err(req, HTTPD_408_REQ_TIMEOUT, "Upload stalled");
        }
        timeouts = 0;
        if (n <= 0)
        {
            ESP_LOGW(TAG, "Upload broke off with %u bytes to go", static_cast<unsigned>(left));
            esp_ota_abort(ota);
            return ESP_FAIL;
        }
        err = esp_ota_write(ota, buf, n);
        if (err != ESP_OK)
        {
            esp_ota_abort(ota);
            return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
        }
        left -= static_cast<size_t>(n);
    }

    // Checks the image: chip, header and checksum.
    err = esp_ota_end(ota);
    if (err == ESP_OK)
        err = esp_ota_set_boot_partition(slot);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Update rejected: %s", esp_err_to_name(err));
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Update written to %s, restarting", slot->label);
    httpd_resp_sendstr(req, "Update written, restarting\n");
    vTaskDelay(pdMS_TO_TICKS(RESTART_DELAY_MS));
    esp_restart();
    return ESP_OK;
}

void ota_server_start()
{
    esp_ota_img_states_t state;
    s_pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) == ESP_OK &&
                state == ESP_OTA_IMG_PENDING_VERIFY;
    if (s_pending)
        ESP_LOGI(TAG, "New firmware: waiting for Matter, Wi-Fi and the purifier before keeping it");

    if (CONFIG_PURIFIER_OTA_TOKEN[0] == '\0')
        ESP_LOGW(TAG, "No update token set: anyone on the network can update this purifier");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_open_sockets = 2; // Matter needs most of the sockets
    config.max_uri_handlers = 2;
    config.lru_purge_enable = true;
    config.recv_wait_timeout = 10;
    httpd_handle_t server = nullptr;
    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Update server failed to start: %s", esp_err_to_name(err));
        return;
    }
    static const httpd_uri_t status = { "/", HTTP_GET, get_status, nullptr };
    static const httpd_uri_t update = { "/ota", HTTP_POST, post_ota, nullptr };
    httpd_register_uri_handler(server, &status);
    httpd_register_uri_handler(server, &update);
    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "Update server on port 80 (%s %s, %s)", app->project_name, app->version,
             esp_ota_get_running_partition()->label);
}

bool ota_pending_verify()
{
    return s_pending && !s_decided;
}

void ota_keep_before_restart(const char *why)
{
    // Asks the partition, not s_pending: a model change restarts before the
    // update server has started.
    esp_ota_img_states_t state;
    if (s_decided || esp_ota_get_state_partition(esp_ota_get_running_partition(), &state) != ESP_OK ||
        state != ESP_OTA_IMG_PENDING_VERIFY)
        return;
    s_decided = true;
    if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
        ESP_LOGI(TAG, "New firmware kept before restarting (%s)", why);
    else
        ESP_LOGE(TAG, "Couldn't mark the new firmware as kept");
}

void ota_check_image(uint32_t elapsed_ms, bool matter_ready, bool wifi_up, bool mcu_alive)
{
    if (!ota_pending_verify())
        return;
    switch (ota_policy::verdict(elapsed_ms, matter_ready, wifi_up, mcu_alive))
    {
    case ota_policy::Verdict::Wait:
        break;
    case ota_policy::Verdict::Keep:
        s_decided = true;
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK)
            ESP_LOGI(TAG, "New firmware kept");
        else
            ESP_LOGE(TAG, "Couldn't mark the new firmware as kept");
        break;
    case ota_policy::Verdict::RollBack:
        s_decided = true;
        ESP_LOGE(TAG, "New firmware failed its check (Matter %s, Wi-Fi %s, purifier %s) — going back to the previous one",
                 matter_ready ? "up" : "down", wifi_up ? "up" : "down", mcu_alive ? "talking" : "silent");
        esp_ota_mark_app_invalid_rollback_and_reboot();
        break;
    }
}
