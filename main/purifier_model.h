#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Mi Air Purifier 3H (zhimi.airpurifier.mb3): property table, tracked device
// state and every conversion between MIoT values and Matter attributes.
//
// The mapping is specified in docs/PLAN.md ("Feature → Matter cluster
// mapping"); keep the two in sync. Matter types appear here only as plain
// enums/integers with the spec's numeric values, so this file has no CHIP
// dependency and is unit-tested on the host.
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "miot_protocol.h"
#include "miot_session.h"

namespace purifier
{

using miot::ActionId;
using miot::PropertyId;

// ── MIoT IDs (https://home.miot-spec.com/spec/zhimi.airpurifier.mb3) ─────────

namespace prop
{
inline constexpr PropertyId FAULT{ 2, 1 };
inline constexpr PropertyId POWER{ 2, 2 };
inline constexpr PropertyId FAN_LEVEL{ 2, 4 }; // 1–3, used in Manual mode
inline constexpr PropertyId MODE{ 2, 5 };      // see Mode
inline constexpr PropertyId PM25{ 3, 6 };      // µg/m³
inline constexpr PropertyId HUMIDITY{ 3, 7 };  // %
inline constexpr PropertyId TEMPERATURE{ 3, 8 }; // °C
inline constexpr PropertyId FILTER_LIFE{ 4, 3 };  // %
inline constexpr PropertyId FILTER_HOURS{ 4, 5 }; // h used
inline constexpr PropertyId BUZZER{ 5, 1 };
inline constexpr PropertyId BRIGHTNESS{ 6, 1 };
inline constexpr PropertyId CHILD_LOCK{ 7, 1 };
inline constexpr PropertyId MOTOR_RPM{ 10, 8 };
inline constexpr PropertyId FAVORITE_LEVEL{ 10, 10 }; // 0–14, used in Favorite mode
inline constexpr PropertyId USE_TIME{ 12, 1 };
inline constexpr PropertyId PURIFIED_VOLUME{ 13, 1 };
inline constexpr PropertyId AVERAGE_AQI{ 13, 2 };
inline constexpr PropertyId AQI_STATE{ 13, 8 }; // 0 Best … 5 Unhealthy
inline constexpr PropertyId HEARTBEAT{ 13, 9 };
inline constexpr PropertyId FILTER_DOOR{ 15, 5 };
} // namespace prop

namespace action
{
inline constexpr ActionId RESET_FILTER{ 4, 1 };
// Arguments sent with RESET_FILTER, as used by the community firmwares.
// To verify against a stock UART capture (Phase 0).
inline constexpr const char *RESET_FILTER_ARGS = "3 0";
} // namespace action

// Properties fetched on start-up and on every periodic poll.
const std::vector<PropertyId> &polled_properties();

// The value the stock gateway writes to HEARTBEAT once a minute.
inline constexpr int64_t HEARTBEAT_VALUE = 60;

enum class Mode : uint8_t
{
    Auto = 0,
    Sleep = 1,
    Favorite = 2,
    Manual = 3,
};

inline constexpr int FAN_LEVEL_MIN = 1;
inline constexpr int FAN_LEVEL_MAX = 3;
inline constexpr int FAVORITE_LEVEL_MIN = 0;
inline constexpr int FAVORITE_LEVEL_MAX = 14;
inline constexpr int MOTOR_RPM_MIN = 300;
inline constexpr int MOTOR_RPM_MAX = 2300;

// ── Matter values (numeric values from the Matter spec) ──────────────────────

// Fan Control FanModeEnum.
enum class FanMode : uint8_t
{
    Off = 0,
    Low = 1,
    Medium = 2,
    High = 3,
    On = 4,
    Auto = 5,
    Smart = 6,
};

// Fan Control SpeedMax: favorite levels 0–14 are speeds 1–15; speed 0 is off.
inline constexpr uint8_t SPEED_MAX = 15;

// The FanMode → Percent/Speed values the Fan Control cluster itself assigns
// when a controller writes FanMode (connectedhomeip FanControlCluster:
// Low 33 %/1, Medium 66 %/(SpeedMax+1)/2, High 100 %/SpeedMax). A fan-drive
// state with exactly these values came from a FanMode write and maps to the
// purifier's manual levels; anything else came from a speed/percent write and
// maps to favorite mode.
inline constexpr uint8_t PERCENT_LOW = 33;
inline constexpr uint8_t PERCENT_MEDIUM = 66;
inline constexpr uint8_t PERCENT_HIGH = 100;
inline constexpr uint8_t SPEED_LOW = 1;
inline constexpr uint8_t SPEED_MEDIUM = (SPEED_MAX + 1) / 2;
inline constexpr uint8_t SPEED_HIGH = SPEED_MAX;

// The (PercentSetting, SpeedSetting) pair the cluster assigns for FanMode
// Low/Medium/High; empty for every other mode.
std::optional<std::pair<uint8_t, uint8_t>> canonical_settings(FanMode mode);

// Air Quality AirQualityEnum.
enum class AirQuality : uint8_t
{
    Unknown = 0,
    Good = 1,
    Fair = 2,
    Moderate = 3,
    Poor = 4,
    VeryPoor = 5,
    ExtremelyPoor = 6,
};

// Resource Monitoring ChangeIndicationEnum.
enum class ChangeIndication : uint8_t
{
    Ok = 0,
    Warning = 1,
    Critical = 2,
};

// Filter life (%) at or below which ChangeIndication becomes Warning.
// To verify against the purifier's own display.
inline constexpr int FILTER_WARNING_PERCENT = 10;

// ── Tracked device state ──────────────────────────────────────────────────────

// What the firmware knows about the purifier, from the MCU's reports. Each
// field stays empty until the MCU has reported it at least once.
struct State
{
    std::optional<bool> power;
    std::optional<Mode> mode;
    std::optional<int> fan_level;
    std::optional<int> favorite_level;
    std::optional<int> motor_rpm;
    std::optional<float> pm25;
    std::optional<int> aqi_state;
    std::optional<float> temperature_c;
    std::optional<float> humidity_pct;
    std::optional<int> filter_life_pct;
    std::optional<int> filter_hours;
    std::optional<int> fault;
    std::optional<bool> filter_door_open;
};

// Groups of Matter attributes that need refreshing after a state change.
enum Changed : uint32_t
{
    CHANGED_NONE = 0,
    CHANGED_FAN = 1u << 0,         // power, mode, levels, motor speed
    CHANGED_PM25 = 1u << 1,
    CHANGED_AIR_QUALITY = 1u << 2,
    CHANGED_TEMPERATURE = 1u << 3,
    CHANGED_HUMIDITY = 1u << 4,
    CHANGED_FILTER = 1u << 5,
    CHANGED_FAULT = 1u << 6,
    CHANGED_FILTER_DOOR = 1u << 7,
};

// Fold one reported property into `state`. Returns the Changed bits for the
// attribute groups whose value actually changed (CHANGED_NONE for unknown
// properties, unchanged values and values out of range).
uint32_t apply(State &state, const miot::PropertyUpdate &update);

// ── Matter → purifier ─────────────────────────────────────────────────────────

struct SetCommand
{
    PropertyId id;
    miot::Value value;
};

// The Fan Control cluster's fan-drive state after a controller write.
// Empty optionals mean null.
struct FanDrive
{
    FanMode mode = FanMode::Off;
    std::optional<uint8_t> percent_setting;
    std::optional<uint8_t> speed_setting;
};

// What the purifier should do for a fan-drive state: power, mode, level.
struct FanTarget
{
    bool power = false;
    Mode mode = Mode::Auto;
    std::optional<int> fan_level;      // Manual
    std::optional<int> favorite_level; // Favorite
};

FanTarget fan_target(const FanDrive &drive);

// The set_properties needed to reach `target` from `current`, in the order
// the MCU should apply them (power first). Properties already at the target
// value are skipped; unknown ones are always sent.
std::vector<SetCommand> commands_for(const FanTarget &target, const State &current);

// The set_properties for a Fan Control WindSetting change: SleepWind on
// enters sleep mode, off returns to Auto (only when currently sleeping).
std::vector<SetCommand> commands_for_sleep(bool sleep_wind, const State &current);

// ── Purifier → Matter ─────────────────────────────────────────────────────────

// How the Fan Control cluster should present the current state.
struct FanView
{
    enum class Kind : uint8_t
    {
        Mode,  // apply with SetFanMode(mode)
        Speed, // apply with SetSpeedSetting(speed)
    };
    Kind kind = Kind::Mode;
    FanMode mode = FanMode::Off;
    uint8_t speed = 0;
    bool sleep_wind = false;
};

// Empty until power (and, when on, mode) are known.
std::optional<FanView> fan_view(const State &state);

// PercentCurrent / SpeedCurrent from the motor speed: 0 when off, otherwise
// 1–100 % across the motor's 300–2300 rpm range.
uint8_t percent_current(const State &state);
uint8_t speed_current(uint8_t percent);

AirQuality air_quality(int aqi_state);
ChangeIndication change_indication(int filter_life_pct);

// Matter measurement units: 0.01 °C (int16) and 0.01 % (uint16), clamped to
// the ranges the endpoints are created with.
int16_t temperature_centi(float c);
uint16_t humidity_centi(float pct);

// LastChangedTime (epoch seconds) from the hours the filter has been used:
// now − hours. Empty while the clock is not set (before 2024).
std::optional<uint32_t> filter_last_changed(uint32_t now_epoch_s, int filter_hours);

} // namespace purifier
