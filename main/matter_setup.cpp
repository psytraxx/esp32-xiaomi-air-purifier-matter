#include "matter_setup.h"

#include <atomic>
#include <cinttypes>
#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "esp_matter.h"

#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/air-quality-server/AirQualityCluster.h>
#include <app/clusters/fan-control-server/CodegenIntegration.h>
#include <app/clusters/fan-control-server/FanControlCluster.h>
#include <app/clusters/fan-control-server/fan-control-delegate.h>
#include <app/clusters/relative-humidity-measurement-server/RelativeHumidityMeasurementCluster.h>
#include <app/clusters/resource-monitoring-server/ResourceMonitoringCluster.h>
#include <app/clusters/resource-monitoring-server/ResourceMonitoringDelegate.h>
#include <app/clusters/temperature-measurement-server/TemperatureMeasurementCluster.h>
#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <clusters/resource_monitor/integration.h>
#include <data_model_provider/esp_matter_data_model_provider.h>
#include <platform/CHIPDeviceLayer.h>
#include <setup_payload/OnboardingCodesUtil.h>

#include "app_config.h"
#include "boot_diag.h"
#include "purifier_link.h"
#include "purifier_model.h"

static const char *TAG = "matter_setup";

using namespace chip::app::Clusters;
using chip::Protocols::InteractionModel::Status;

static constexpr uint16_t EP_INVALID = 0xFFFF;

static uint16_t s_ep_purifier = EP_INVALID;
static uint16_t s_ep_air_quality = EP_INVALID;
static uint16_t s_ep_temperature = EP_INVALID;
static uint16_t s_ep_humidity = EP_INVALID;

static EventGroupHandle_t s_events = nullptr;

// Connectivity, mirrored to the purifier link for the MCU's Wi-Fi LED.
// Written on the CHIP task only.
static bool s_commissioned = false;
static bool s_wifi_connected = false;

// Set while the CHIP task pushes purifier state into the Fan Control cluster.
// The cluster notifies its delegate for every change, whether it came from a
// controller or from us; this flag tells the delegate to ignore our own
// changes so they are not sent straight back to the MCU.
static bool s_applying_purifier_state = false;

// Changed-groups accumulated since the last refresh ran on the CHIP task.
static std::atomic<uint32_t> s_pending_changes{ 0 };

// ── Measured-value ranges ─────────────────────────────────────────────────────
// Measurement clusters reject values outside Min/MaxMeasuredValue, and the
// config default for both is 0 (see the plant monitor's CLAUDE.md).

static constexpr int16_t TEMP_MIN_CENTI_C = -4000;    // -40 °C
static constexpr int16_t TEMP_MAX_CENTI_C = 8500;     //  85 °C
static constexpr uint16_t HUMIDITY_MIN_CPCT = 0;      //   0 %
static constexpr uint16_t HUMIDITY_MAX_CPCT = 10000;  // 100 %
static constexpr float PM25_MIN_UGM3 = 0.0f;
static constexpr float PM25_MAX_UGM3 = 1000.0f;

// Spec enum values used when creating clusters.
static constexpr uint8_t FAN_MODE_SEQUENCE_OFF_LOW_MED_HIGH_AUTO = 2;
static constexpr uint8_t WIND_BITMAP_SLEEP_WIND = 0x01;
static constexpr uint8_t DEGRADATION_DIRECTION_DOWN = 1;
static constexpr uint8_t MEASUREMENT_MEDIUM_AIR = 0;
static constexpr uint8_t MEASUREMENT_UNIT_UGM3 = 4;

// ── Fan Control delegate (controller → purifier) ──────────────────────────────

class PurifierFanDelegate : public FanControl::Delegate
{
public:
    Status HandleStep(FanControl::StepDirectionEnum, bool, bool) override
    {
        return Status::UnsupportedCommand; // Step feature not enabled
    }

    void OnFanDriveStateChanged(const FanControl::FanDriveState &state) override
    {
        if (s_applying_purifier_state)
            return;
        purifier::FanDrive drive;
        drive.mode = static_cast<purifier::FanMode>(chip::to_underlying(state.mode));
        if (!state.percentSetting.IsNull())
            drive.percent_setting = state.percentSetting.Value();
        if (!state.speedSetting.IsNull())
            drive.speed_setting = state.speedSetting.Value();
        ESP_LOGI(TAG, "Fan Control write: mode=%u percent=%d speed=%d", static_cast<unsigned>(drive.mode),
                 drive.percent_setting ? *drive.percent_setting : -1, drive.speed_setting ? *drive.speed_setting : -1);
        purifier_link_apply_fan(drive);
    }

    void OnWindSettingChanged(chip::BitMask<FanControl::WindBitmap> value) override
    {
        if (s_applying_purifier_state)
            return;
        bool sleep = value.Has(FanControl::WindBitmap::kSleepWind);
        ESP_LOGI(TAG, "Fan Control write: sleep wind %s", sleep ? "on" : "off");
        purifier_link_set_sleep(sleep);
    }
};

static PurifierFanDelegate s_fan_delegate;

// ── HEPA filter delegate ──────────────────────────────────────────────────────

class PurifierFilterDelegate : public ResourceMonitoring::Delegate
{
public:
    CHIP_ERROR Init() override { return CHIP_NO_ERROR; }

    // ResetCondition: tell the purifier; the cluster then sets Condition back
    // to 100 % itself, and the next MCU report confirms it.
    Status PreResetCondition() override
    {
        ESP_LOGI(TAG, "HEPA ResetCondition");
        purifier_link_reset_filter();
        return Status::Success;
    }
};

static PurifierFilterDelegate s_filter_delegate;

// ── Cluster lookup ────────────────────────────────────────────────────────────

// Code-driven clusters (esp_matter 1.6) are served by objects in the data-model
// provider registry; attribute::update() on them is ignored, so values are set
// on the objects. MUST be called on the CHIP task (stack lock held).
template <typename ClusterT>
static ClusterT *find_cluster(uint16_t ep, chip::ClusterId cluster_id)
{
    if (ep == EP_INVALID)
        return nullptr;
    auto *iface = esp_matter::data_model::provider::get_instance().registry().Get(
        chip::app::ConcreteClusterPath(ep, cluster_id));
    return static_cast<ClusterT *>(iface);
}

static FanControlCluster *fan_cluster()
{
    if (s_ep_purifier == EP_INVALID)
        return nullptr;
    return FanControl::FindClusterOnEndpoint(s_ep_purifier);
}

static ResourceMonitoring::ResourceMonitoringCluster *filter_cluster()
{
    if (s_ep_purifier == EP_INVALID)
        return nullptr;
    return ResourceMonitoring::GetClusterInstance(s_ep_purifier, HepaFilterMonitoring::Id);
}

static void warn_status(const char *what, Status status)
{
    if (status != Status::Success)
        ESP_LOGW(TAG, "%s rejected: 0x%02x", what, static_cast<unsigned>(chip::to_underlying(status)));
}

// ── Purifier → Matter (CHIP task) ─────────────────────────────────────────────

static void refresh_fan(const purifier::State &state)
{
    FanControlCluster *fan = fan_cluster();
    if (!fan)
        return;

    auto view = purifier::fan_view(state);
    s_applying_purifier_state = true;
    if (view)
    {
        if (view->kind == purifier::FanView::Kind::Speed)
        {
            warn_status("SpeedSetting", fan->SetSpeedSetting(chip::app::DataModel::MakeNullable(view->speed)));
        }
        else
        {
            auto mode = static_cast<FanControl::FanModeEnum>(view->mode);
            // The cluster ignores a FanMode it already has, so going from a
            // favorite speed to the manual level with the same name (e.g.
            // speed 3 "Low" → manual level 1 "Low") would keep the old speed.
            // Step through Auto to make it re-apply the mode's own values.
            auto expected = purifier::canonical_settings(view->mode);
            if (expected && fan->GetFanMode() == mode)
            {
                auto pct = fan->GetPercentSetting();
                auto spd = fan->GetSpeedSetting();
                bool canonical = !pct.IsNull() && !spd.IsNull() && pct.Value() == expected->first &&
                                 spd.Value() == expected->second;
                if (!canonical)
                    fan->SetFanMode(FanControl::FanModeEnum::kAuto);
            }
            warn_status("FanMode", fan->SetFanMode(mode));
        }

        chip::BitMask<FanControl::WindBitmap> wind;
        if (view->sleep_wind)
            wind.Set(FanControl::WindBitmap::kSleepWind);
        warn_status("WindSetting", fan->SetWindSetting(wind));
    }
    uint8_t pct = purifier::percent_current(state);
    fan->SetPercentCurrent(pct);
    fan->SetSpeedCurrent(purifier::speed_current(pct));
    s_applying_purifier_state = false;
}

static void refresh_filter(const purifier::State &state)
{
    auto *filter = filter_cluster();
    if (!filter || !state.filter_life_pct)
        return;
    int life = *state.filter_life_pct;
    warn_status("Condition", filter->UpdateCondition(static_cast<uint8_t>(life)));
    warn_status("ChangeIndication", filter->UpdateChangeIndication(
                                        static_cast<ResourceMonitoring::ChangeIndicationEnum>(purifier::change_indication(life))));
    if (state.filter_hours)
    {
        auto changed = purifier::filter_last_changed(static_cast<uint32_t>(time(nullptr)), *state.filter_hours);
        if (changed)
            warn_status("LastChangedTime", filter->UpdateLastChangedTime(chip::app::DataModel::MakeNullable(*changed)));
    }
}

static void refresh_air_quality(const purifier::State &state)
{
    if (state.aqi_state)
    {
        if (auto *aq = find_cluster<AirQualityCluster>(s_ep_air_quality, AirQuality::Id))
            warn_status("AirQuality", aq->SetAirQuality(static_cast<AirQuality::AirQualityEnum>(purifier::air_quality(*state.aqi_state))));
    }
}

static void refresh_pm25(const purifier::State &state)
{
    if (!state.pm25 || s_ep_air_quality == EP_INVALID)
        return;
    // PM2.5 Concentration Measurement is still served from esp_matter's
    // attribute store, so attribute::update() is the right call here.
    esp_matter_attr_val_t val = esp_matter_nullable_float(nullable<float>(*state.pm25));
    esp_matter::attribute::update(s_ep_air_quality, Pm25ConcentrationMeasurement::Id,
                                  Pm25ConcentrationMeasurement::Attributes::MeasuredValue::Id, &val);
}

static void refresh_temperature(const purifier::State &state)
{
    if (!state.temperature_c)
        return;
    if (auto *c = find_cluster<TemperatureMeasurementCluster>(s_ep_temperature, TemperatureMeasurement::Id))
    {
        CHIP_ERROR err = c->SetMeasuredValue(chip::app::DataModel::MakeNullable(purifier::temperature_centi(*state.temperature_c)));
        if (err != CHIP_NO_ERROR)
            ESP_LOGW(TAG, "Temperature: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

static void refresh_humidity(const purifier::State &state)
{
    if (!state.humidity_pct)
        return;
    if (auto *c = find_cluster<RelativeHumidityMeasurementCluster>(s_ep_humidity, RelativeHumidityMeasurement::Id))
    {
        CHIP_ERROR err = c->SetMeasuredValue(chip::app::DataModel::MakeNullable(purifier::humidity_centi(*state.humidity_pct)));
        if (err != CHIP_NO_ERROR)
            ESP_LOGW(TAG, "Humidity: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

// Runs on the CHIP task. Takes everything that changed since the last run and
// pushes the latest state for those groups.
static void refresh_pending(intptr_t)
{
    uint32_t changed = s_pending_changes.exchange(0);
    if (!changed)
        return;
    const purifier::State state = purifier_link_state();

    if (changed & purifier::CHANGED_FAN)
        refresh_fan(state);
    if (changed & purifier::CHANGED_FILTER)
        refresh_filter(state);
    if (changed & purifier::CHANGED_AIR_QUALITY)
        refresh_air_quality(state);
    if (changed & purifier::CHANGED_PM25)
        refresh_pm25(state);
    if (changed & purifier::CHANGED_TEMPERATURE)
        refresh_temperature(state);
    if (changed & purifier::CHANGED_HUMIDITY)
        refresh_humidity(state);
}

// Changes whose refresh could not be scheduled; folded into the next attempt.
static std::atomic<uint32_t> s_unscheduled_changes{ 0 };

void matter_on_purifier_changed(uint32_t changed)
{
    changed |= s_unscheduled_changes.exchange(0);
    // Only the first change since the last refresh schedules work; later ones
    // ride along with it.
    if (s_pending_changes.fetch_or(changed) != 0)
        return;
    CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(refresh_pending, 0);
    if (err != CHIP_NO_ERROR)
    {
        // Nothing is scheduled now, so the pending bits must be cleared or no
        // later change would ever schedule again. Keep them for the next try.
        s_unscheduled_changes.fetch_or(s_pending_changes.exchange(0));
        ESP_LOGW(TAG, "ScheduleWork failed: %" CHIP_ERROR_FORMAT, err.Format());
    }
}

// ── Commissioning helpers ─────────────────────────────────────────────────────

static bool fabric_commissioned()
{
    chip::Server &server = chip::Server::GetInstance();
    return server.GetFabricTable().FabricCount() > 0 && !server.GetFailSafeContext().IsFailSafeArmed();
}

static void open_commissioning_window()
{
    auto &mgr = chip::Server::GetInstance().GetCommissioningWindowManager();
    if (mgr.IsCommissioningWindowOpen())
        return;
    CHIP_ERROR err = mgr.OpenBasicCommissioningWindow(chip::System::Clock::Seconds16(COMMISSIONING_WINDOW_TIMEOUT_S),
                                                      chip::CommissioningWindowAdvertisement::kAllSupported);
    if (err != CHIP_NO_ERROR)
        ESP_LOGE(TAG, "OpenBasicCommissioningWindow: %" CHIP_ERROR_FORMAT, err.Format());
}

static void log_pairing_codes()
{
    char manual[32] = {};
    char qr[128] = {};
    chip::RendezvousInformationFlags flags(chip::RendezvousInformationFlag::kBLE);

    chip::MutableCharSpan manual_span(manual);
    if (GetManualPairingCode(manual_span, flags) == CHIP_NO_ERROR)
        manual[manual_span.size()] = '\0';
    chip::MutableCharSpan qr_span(qr);
    if (GetQRCode(qr_span, flags) == CHIP_NO_ERROR)
        qr[qr_span.size()] = '\0';

    ESP_LOGI(TAG, "Matter manual pairing code: %s", manual);
    ESP_LOGI(TAG, "Matter QR payload         : %s", qr);
}

static void publish_network_state()
{
    purifier_link_set_network(s_commissioned, s_wifi_connected);
}

static void start_sntp()
{
    static bool started = false;
    if (started)
        return;
    started = true;
    setenv("TZ", APP_TIMEZONE, 1);
    tzset();
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(APP_SNTP_SERVER);
    esp_err_t err = esp_netif_sntp_init(&cfg);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "SNTP init failed: %s", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "SNTP started (%s)", APP_SNTP_SERVER);
}

// Delegates are handed to esp_matter in the endpoint configs, but esp_matter
// can only attach them once the cluster objects exist. Attach them again once
// the server is up so a ResetCondition can never reach a cluster without one.
static void attach_delegates()
{
    if (s_ep_purifier == EP_INVALID)
        return;
    FanControl::SetDefaultDelegate(s_ep_purifier, &s_fan_delegate);
    CHIP_ERROR err = ResourceMonitoring::SetDefaultDelegate(s_ep_purifier, HepaFilterMonitoring::Id, &s_filter_delegate);
    if (err != CHIP_NO_ERROR)
        ESP_LOGE(TAG, "HEPA delegate: %" CHIP_ERROR_FORMAT, err.Format());
}

static void set_node_label()
{
    esp_matter::attribute_t *attr = esp_matter::attribute::get(0, BasicInformation::Id, BasicInformation::Attributes::NodeLabel::Id);
    if (!attr)
        return;
    // Same approach as the plant monitor: sets the initial value only, a name
    // given in the controller is kept.
    char label[33] = {};
    strncpy(label, NODE_LABEL, sizeof(label) - 1);
    esp_matter_attr_val_t val = esp_matter_char_str(label, static_cast<uint16_t>(strlen(label)));
    esp_matter::attribute::set_val(attr, &val);
}

// ── Matter events (CHIP task) ─────────────────────────────────────────────────

static void matter_event_cb(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t)
{
    using namespace chip::DeviceLayer;
    if (!event)
        return;

    switch (event->Type)
    {
    case DeviceEventType::kServerReady:
        ESP_LOGI(TAG, "Matter server ready");
        attach_delegates();
        set_node_label();
        s_commissioned = fabric_commissioned();
        s_wifi_connected = ConnectivityMgr().IsWiFiStationConnected();
        publish_network_state();
        if (!s_commissioned && chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
            open_commissioning_window();
        // Push whatever the MCU has reported so far, so no attribute is null
        // when a controller first reads the device.
        matter_on_purifier_changed(0xFFFFFFFFu);
        if (s_events)
            xEventGroupSetBits(s_events, MATTER_BIT_SERVER_READY | (s_commissioned ? MATTER_BIT_COMMISSIONED : 0));
        break;

    case DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        log_pairing_codes();
        break;

    case DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        s_commissioned = true;
        publish_network_state();
        if (s_events)
            xEventGroupSetBits(s_events, MATTER_BIT_COMMISSIONED);
        break;

    case DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGW(TAG, "Commissioning failed (fail-safe expired)");
        break;

    case DeviceEventType::kFabricRemoved:
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0)
        {
            ESP_LOGI(TAG, "Last fabric removed — reopening commissioning");
            s_commissioned = false;
            publish_network_state();
            open_commissioning_window();
        }
        break;

    case DeviceEventType::kWiFiConnectivityChange:
        s_wifi_connected = event->WiFiConnectivityChange.Result == kConnectivity_Established;
        ESP_LOGI(TAG, "Wi-Fi %s", s_wifi_connected ? "connected" : "disconnected");
        publish_network_state();
        if (s_wifi_connected)
            start_sntp();
        break;

    case DeviceEventType::kInterfaceIpAddressChanged:
        if (event->InterfaceIpAddressChanged.Type == InterfaceIpChangeType::kIpV4_Assigned)
            start_sntp();
        break;

    default:
        break;
    }
}

// ── Attribute and identify callbacks ──────────────────────────────────────────

static esp_err_t attr_update_cb(esp_matter::attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                uint32_t attribute_id, esp_matter_attr_val_t *, void *)
{
    // Fan Control and HEPA writes reach us through their delegates; this only
    // traces attribute-store writes for debugging.
    if (type == esp_matter::attribute::PRE_UPDATE)
        ESP_LOGD(TAG, "attr write ep=%u cluster=0x%04" PRIX32 " attr=0x%04" PRIX32, endpoint_id, cluster_id, attribute_id);
    return ESP_OK;
}

static esp_err_t identify_cb(esp_matter::identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                             uint8_t, void *)
{
    // No identify output on this device yet (the display and buzzer are not
    // exposed, decision D2); just log it.
    ESP_LOGI(TAG, "Identify ep=%u type=%d effect=%u", endpoint_id, static_cast<int>(type), effect_id);
    return ESP_OK;
}

// ── Endpoint creation ─────────────────────────────────────────────────────────

static esp_err_t create_purifier_endpoint(esp_matter::node_t *node)
{
    using namespace esp_matter;

    endpoint::air_purifier::config_t cfg;
    cfg.fan_control.fan_mode = 0; // Off until the MCU reports
    cfg.fan_control.fan_mode_sequence = FAN_MODE_SEQUENCE_OFF_LOW_MED_HIGH_AUTO;
    cfg.fan_control.percent_setting = nullable<uint8_t>(0);
    cfg.fan_control.percent_current = 0;
    cfg.fan_control.delegate = &s_fan_delegate;
    endpoint_t *ep = endpoint::air_purifier::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!ep)
        return ESP_FAIL;
    s_ep_purifier = endpoint::get_id(ep);

    // Fan Control features: 15-step speed (favorite levels), Auto, Sleep wind.
    cluster_t *fan = cluster::get(ep, FanControl::Id);
    if (!fan)
        return ESP_FAIL;
    cluster::fan_control::feature::multi_speed::config_t speed_cfg;
    speed_cfg.speed_max = purifier::SPEED_MAX;
    speed_cfg.speed_setting = nullable<uint8_t>(0);
    speed_cfg.speed_current = 0;
    cluster::fan_control::feature::multi_speed::add(fan, &speed_cfg);
    cluster::fan_control::feature::fan_auto::add(fan);
    cluster::fan_control::feature::wind::config_t wind_cfg;
    wind_cfg.wind_support = WIND_BITMAP_SLEEP_WIND;
    wind_cfg.wind_setting = 0;
    cluster::fan_control::feature::wind::add(fan, &wind_cfg);

    // HEPA Filter Monitoring: condition (%), warning, reset command,
    // InPlaceIndicator and LastChangedTime.
    cluster::hepa_filter_monitoring::config_t hepa_cfg;
    hepa_cfg.delegate = &s_filter_delegate;
    cluster_t *hepa = cluster::hepa_filter_monitoring::create(ep, &hepa_cfg, CLUSTER_FLAG_SERVER);
    if (!hepa)
        return ESP_FAIL;
    cluster::hepa_filter_monitoring::feature::condition::config_t cond_cfg;
    cond_cfg.condition = 100;
    cond_cfg.degradation_direction = DEGRADATION_DIRECTION_DOWN;
    cluster::hepa_filter_monitoring::feature::condition::add(hepa, &cond_cfg);
    cluster::hepa_filter_monitoring::feature::warning::add(hepa);
    cluster::hepa_filter_monitoring::command::create_reset_condition(hepa);
    cluster::hepa_filter_monitoring::attribute::create_in_place_indicator(hepa, true);
    cluster::hepa_filter_monitoring::attribute::create_last_changed_time(hepa, nullable<uint8_t>());
    return ESP_OK;
}

static esp_err_t create_air_quality_endpoint(esp_matter::node_t *node)
{
    using namespace esp_matter;

    endpoint::air_quality_sensor::config_t cfg;
    cfg.air_quality.air_quality = 0; // Unknown until the MCU reports
    endpoint_t *ep = endpoint::air_quality_sensor::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
    if (!ep)
        return ESP_FAIL;
    s_ep_air_quality = endpoint::get_id(ep);

    // All six levels are used (Good … ExtremelyPoor).
    cluster_t *aq = cluster::get(ep, AirQuality::Id);
    if (!aq)
        return ESP_FAIL;
    cluster::air_quality::feature::fair::add(aq);
    cluster::air_quality::feature::moderate::add(aq);
    cluster::air_quality::feature::very_poor::add(aq);
    cluster::air_quality::feature::extremely_poor::add(aq);

    cluster::pm25_concentration_measurement::config_t pm_cfg;
    pm_cfg.measurement_medium = MEASUREMENT_MEDIUM_AIR;
    pm_cfg.feature_flags = cluster::concentration_measurement::feature::numeric_measurement::get_id();
    pm_cfg.features.numeric_measurement.measurement_unit = MEASUREMENT_UNIT_UGM3;
    pm_cfg.features.numeric_measurement.min_measured_value = nullable<float>(PM25_MIN_UGM3);
    pm_cfg.features.numeric_measurement.max_measured_value = nullable<float>(PM25_MAX_UGM3);
    if (!cluster::pm25_concentration_measurement::create(ep, &pm_cfg, CLUSTER_FLAG_SERVER))
        return ESP_FAIL;
    return ESP_OK;
}

static esp_err_t create_endpoints(esp_matter::node_t *node)
{
    using namespace esp_matter;

    if (create_purifier_endpoint(node) != ESP_OK)
    {
        ESP_LOGE(TAG, "air purifier endpoint failed");
        return ESP_FAIL;
    }
    if (create_air_quality_endpoint(node) != ESP_OK)
    {
        ESP_LOGE(TAG, "air quality endpoint failed");
        return ESP_FAIL;
    }
    {
        endpoint::temperature_sensor::config_t cfg;
        cfg.temperature_measurement.min_measured_value = nullable<int16_t>(TEMP_MIN_CENTI_C);
        cfg.temperature_measurement.max_measured_value = nullable<int16_t>(TEMP_MAX_CENTI_C);
        endpoint_t *ep = endpoint::temperature_sensor::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
        if (!ep)
            return ESP_FAIL;
        s_ep_temperature = endpoint::get_id(ep);
    }
    {
        endpoint::humidity_sensor::config_t cfg;
        cfg.relative_humidity_measurement.min_measured_value = nullable<uint16_t>(HUMIDITY_MIN_CPCT);
        cfg.relative_humidity_measurement.max_measured_value = nullable<uint16_t>(HUMIDITY_MAX_CPCT);
        endpoint_t *ep = endpoint::humidity_sensor::create(node, &cfg, ENDPOINT_FLAG_NONE, nullptr);
        if (!ep)
            return ESP_FAIL;
        s_ep_humidity = endpoint::get_id(ep);
    }

    ESP_LOGI(TAG, "Endpoints: purifier=%u air_quality=%u temperature=%u humidity=%u", s_ep_purifier, s_ep_air_quality,
             s_ep_temperature, s_ep_humidity);
    return ESP_OK;
}

// ── Public API ────────────────────────────────────────────────────────────────

void matter_setup(EventGroupHandle_t events)
{
    s_events = events;

    esp_matter::node::config_t node_cfg;
    esp_matter::node_t *node = esp_matter::node::create(&node_cfg, attr_update_cb, identify_cb);
    if (!node)
    {
        ESP_LOGE(TAG, "node::create failed — restarting");
        esp_restart();
    }
    if (create_endpoints(node) != ESP_OK)
    {
        ESP_LOGE(TAG, "endpoint creation failed — restarting");
        esp_restart();
    }

    esp_err_t err = esp_matter::start(matter_event_cb);
    if (err != ESP_OK)
    {
        // Most likely corrupt Matter NVS after repeated failures: wipe it so the
        // device comes back commissionable instead of reboot-looping.
        ESP_LOGE(TAG, "esp_matter::start failed: %s", esp_err_to_name(err));
        if (boot_diag_record_matter_fault())
        {
            ESP_LOGE(TAG, "repeated Matter start failures — erasing Matter NVS");
            nvs_flash_erase_partition(CONFIG_ESP_MATTER_NVS_PART_NAME);
        }
        esp_restart();
    }
    ESP_LOGI(TAG, "Matter stack started");
}

void matter_factory_reset()
{
    ESP_LOGW(TAG, "Factory reset: wiping Matter pairings");
    esp_matter::factory_reset();
}
