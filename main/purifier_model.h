#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// Supported purifiers: per-model property tables (profiles), the tracked
// device state and every conversion between MIoT values and Matter attributes.
//
//   Mi Air Purifier 3/3H  zhimi.airpurifier.mb3
//   Mi Air Purifier 3C    zhimi.airp.mb4a, zhimi.airpurifier.mb4
//
// The mapping is specified in docs/PLAN.md ("Feature → Matter cluster
// mapping"); keep the two in sync. Matter types appear here only as plain
// enums/integers with the spec's numeric values, so this file has no CHIP
// dependency and is unit-tested on the host.
// ─────────────────────────────────────────────────────────────────────────────

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "miot_protocol.h"
#include "miot_session.h"

namespace purifier
{

using miot::ActionId;
using miot::PropertyId;

// ── Models ────────────────────────────────────────────────────────────────────

enum class Model : uint8_t
{
    Unknown = 0,
    Mb3 = 1, // Mi Air Purifier 3/3H
    Mb4 = 2, // Mi Air Purifier 3C
};

// From the string the MCU announces (`model zhimi.airpurifier.mb3`).
Model model_from_string(std::string_view s);
const char *model_name(Model m); // e.g. "3H"

// A get of these two properties tells the models apart when the MCU doesn't
// announce its model: the 3H answers the first, the 3C the second.
inline constexpr PropertyId PROBE_MB3{ 10, 8 };
inline constexpr PropertyId PROBE_MB4{ 9, 1 };
std::vector<PropertyId> probe_properties();
// The model whose probe property came back in a successful get result.
Model model_from_probe(const std::vector<miot::PropertyUpdate> &updates);

// Which model to run with at boot: the forced one (build override) if set,
// else the one remembered from an earlier boot. Unknown means "detect now".
Model boot_model(Model forced, Model remembered);

// What to do when the MCU's model is detected while running as `active`.
enum class DetectAction : uint8_t
{
    None,               // nothing new
    Remember,           // matches what's running; store it for next boot
    RememberAndRestart, // a different purifier: store it and restart, since
                        // the Matter endpoints depend on the model
    WarnForced,         // differs from the forced model: keep it, log a warning
};
DetectAction on_model_detected(Model forced, Model remembered, Model active, Model detected);

// Purifier operating mode, independent of its wire value.
enum class Mode : uint8_t
{
    Auto,
    Sleep,
    Favorite,
    Manual, // 3H only: fan levels 1–3
};

// How a model sets its fan speed outside Auto/Sleep.
enum class FanScheme : uint8_t
{
    LevelsAndFavoriteLevel, // 3H: Manual levels 1–3, Favorite level 0–14
    FavoriteRpm,            // 3C: Favorite rpm only
};

struct Profile
{
    Model model;
    FanScheme fan_scheme;

    PropertyId power;
    PropertyId mode;
    PropertyId motor_rpm;
    PropertyId pm25;
    PropertyId filter_life;
    PropertyId filter_hours;
    PropertyId fault;
    PropertyId heartbeat;
    std::optional<PropertyId> fan_level;      // 3H
    std::optional<PropertyId> favorite_level; // 3H
    std::optional<PropertyId> favorite_rpm;   // 3C
    std::optional<PropertyId> aqi_state;      // 3H; the 3C's level is derived from PM2.5
    std::optional<PropertyId> temperature;    // 3H
    std::optional<PropertyId> humidity;       // 3H
    std::optional<PropertyId> filter_door;    // 3H

    ActionId reset_filter;
    const char *reset_filter_args;

    // Wire values of the modes; Manual is absent on the 3C.
    int mode_auto;
    int mode_sleep;
    int mode_favorite;
    std::optional<int> mode_manual;

    // Motor speed range, for PercentCurrent.
    int rpm_min;
    int rpm_max;

    // 3C favorite rpm: range, step, and the presets used for Low/Medium/High.
    int favorite_rpm_min;
    int favorite_rpm_max;
    int favorite_rpm_step;
    std::array<int, 3> preset_rpm;
};

// Profile for a known model; Model::Unknown gets the 3H's.
const Profile &profile(Model m);

// Properties fetched on start-up and on every periodic poll.
std::vector<PropertyId> polled_properties(const Profile &p);

// The value the stock gateway writes to the heartbeat property once a minute.
inline constexpr int64_t HEARTBEAT_VALUE = 60;

inline constexpr int FAN_LEVEL_MIN = 1;
inline constexpr int FAN_LEVEL_MAX = 3;
inline constexpr int FAVORITE_LEVEL_MIN = 0;
inline constexpr int FAVORITE_LEVEL_MAX = 14;

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

// Fan Control SpeedMax. On the 3H speeds 1–15 are favorite levels 0–14; on
// the 3C they spread evenly over the favorite rpm range. Speed 0 is off.
inline constexpr uint8_t SPEED_MAX = 15;

// The FanMode → Percent/Speed values the Fan Control cluster itself assigns
// when a controller writes FanMode (connectedhomeip FanControlCluster:
// Low 33 %/1, Medium 66 %/(SpeedMax+1)/2, High 100 %/SpeedMax). A fan-drive
// state with exactly these values came from a FanMode write and maps to the
// purifier's Low/Medium/High; anything else came from a speed/percent write.
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

// General Diagnostics HardwareFaultEnum values used for the purifier's fault
// code (Home Assistant shows any active fault as a "Hardware faults" problem
// sensor).
enum class HardwareFault : uint8_t
{
    Unspecified = 0x00,
    Sensor = 0x02,
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
    std::optional<int> fan_level;      // 3H
    std::optional<int> favorite_level; // 3H
    std::optional<int> favorite_rpm;   // 3C
    std::optional<int> motor_rpm;
    std::optional<float> pm25;
    std::optional<int> aqi_state; // 3H
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
    CHANGED_FAN = 1u << 0, // power, mode, levels, motor speed
    CHANGED_PM25 = 1u << 1,
    CHANGED_AIR_QUALITY = 1u << 2,
    CHANGED_TEMPERATURE = 1u << 3,
    CHANGED_HUMIDITY = 1u << 4,
    CHANGED_FILTER = 1u << 5,
    CHANGED_FAULT = 1u << 6,
    CHANGED_FILTER_DOOR = 1u << 7,
};

// Fold one reported property into `state`. Returns the Changed bits for the
// attribute groups whose value actually changed (CHANGED_NONE for properties
// the profile doesn't map, unchanged values and values out of range). On a
// model without its own air-quality level, a PM2.5 change also flags
// CHANGED_AIR_QUALITY.
uint32_t apply(const Profile &p, State &state, const miot::PropertyUpdate &update);

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

// What the purifier should do, independent of the model:
//   - Auto / Sleep;
//   - a named level 1–3 (a FanMode Low/Medium/High write);
//   - a fine speed 1–15 (a speed/percent write).
struct FanTarget
{
    bool power = false;
    Mode mode = Mode::Auto;      // Auto or Favorite/Manual, see below
    std::optional<int> level;    // 1–3 when a FanMode Low/Medium/High was written
    std::optional<uint8_t> speed; // 1–15 when a speed/percent was written
};

FanTarget fan_target(const FanDrive &drive);

// The set_properties needed to reach `target` from `current` on this model,
// in the order the MCU should apply them (power first). Properties already at
// the target value are skipped; unknown ones are always sent.
//   3H: level → Manual + fan level; speed s → Favorite + favorite level s−1.
//   3C: level → Favorite + preset rpm; speed s → Favorite + rpm_for_speed(s).
std::vector<SetCommand> commands_for(const Profile &p, const FanTarget &target, const State &current);

// The set_properties for a Fan Control WindSetting change: SleepWind on
// enters sleep mode, off returns to Auto (only when currently sleeping).
std::vector<SetCommand> commands_for_sleep(const Profile &p, bool sleep_wind, const State &current);

// 3C favorite rpm ↔ Fan Control speed 1–15 (inverse of each other).
int rpm_for_speed(const Profile &p, uint8_t speed);
uint8_t speed_for_rpm(const Profile &p, int rpm);

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

// Empty until power (and, when on, mode and the speed it needs) are known.
std::optional<FanView> fan_view(const Profile &p, const State &state);

// PercentCurrent / SpeedCurrent. Home Assistant shows PercentCurrent on its
// fan slider, so it must match what was set:
//   - off: 0;
//   - a named level (Low/Medium/High): the cluster's 33 / 66 / 100 %;
//   - a fine speed s: s·100/15 %, the cluster's PercentSetting for s;
//   - Auto and Sleep (the purifier picks the speed): 1–100 % across the
//     model's motor rpm range; 0 until the rpm is known.
uint8_t percent_current(const Profile &p, const State &state);
uint8_t speed_current(uint8_t percent);

// The Matter air-quality level: the purifier's own (3H) or derived from PM2.5
// (3C). Empty while unknown.
std::optional<AirQuality> air_quality(const Profile &p, const State &state);
AirQuality air_quality_from_state(int aqi_state);
AirQuality air_quality_from_pm25(float pm25);

ChangeIndication change_indication(int filter_life_pct);

// Active hardware faults from the purifier's fault code; empty when it
// reports none (0) or hasn't reported yet.
//   3H: 1 motor running fault, 2 motor stuck → Unspecified (Matter has no
//       motor fault); 3 no sensor, 4 humidity sensor, 5 temperature sensor
//       error → Sensor; any other non-zero code → Unspecified.
//   3C: codes not documented yet; any non-zero code → Unspecified.
std::vector<HardwareFault> hardware_faults(const Profile &p, const State &state);

// Matter measurement units: 0.01 °C (int16) and 0.01 % (uint16), clamped to
// the ranges the endpoints are created with.
int16_t temperature_centi(float c);
uint16_t humidity_centi(float pct);

// LastChangedTime (epoch seconds) from the hours the filter has been used:
// now − hours. Empty while the clock is not set (before 2024).
std::optional<uint32_t> filter_last_changed(uint32_t now_epoch_s, int filter_hours);

} // namespace purifier
