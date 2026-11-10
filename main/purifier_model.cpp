#include "purifier_model.h"

#include <cmath>

namespace purifier
{

const std::vector<PropertyId> &polled_properties()
{
    static const std::vector<PropertyId> k_props = {
        prop::POWER,
        prop::MODE,
        prop::FAN_LEVEL,
        prop::FAVORITE_LEVEL,
        prop::MOTOR_RPM,
        prop::PM25,
        prop::AQI_STATE,
        prop::TEMPERATURE,
        prop::HUMIDITY,
        prop::FILTER_LIFE,
        prop::FILTER_HOURS,
        prop::FAULT,
        prop::FILTER_DOOR,
    };
    return k_props;
}

// ── State tracking ────────────────────────────────────────────────────────────

template <typename T>
static bool assign(std::optional<T> &field, T value)
{
    if (field && *field == value)
        return false;
    field = value;
    return true;
}

static std::optional<int> int_in_range(const miot::Value &v, int lo, int hi)
{
    auto i = v.as_int();
    if (!i || *i < lo || *i > hi)
        return std::nullopt;
    return static_cast<int>(*i);
}

static std::optional<float> float_in_range(const miot::Value &v, float lo, float hi)
{
    auto f = v.as_float();
    if (!f || *f < lo || *f > hi)
        return std::nullopt;
    return static_cast<float>(*f);
}

uint32_t apply(State &s, const miot::PropertyUpdate &u)
{
    const PropertyId id = u.id;
    const miot::Value &v = u.value;

    if (id == prop::POWER)
    {
        auto b = v.as_bool();
        return (b && assign(s.power, *b)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (id == prop::MODE)
    {
        auto m = int_in_range(v, 0, 3);
        return (m && assign(s.mode, static_cast<Mode>(*m))) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (id == prop::FAN_LEVEL)
    {
        auto l = int_in_range(v, FAN_LEVEL_MIN, FAN_LEVEL_MAX);
        return (l && assign(s.fan_level, *l)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (id == prop::FAVORITE_LEVEL)
    {
        auto l = int_in_range(v, FAVORITE_LEVEL_MIN, FAVORITE_LEVEL_MAX);
        return (l && assign(s.favorite_level, *l)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (id == prop::MOTOR_RPM)
    {
        auto r = int_in_range(v, 0, 10000);
        return (r && assign(s.motor_rpm, *r)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (id == prop::PM25)
    {
        auto f = float_in_range(v, 0.0f, 1000.0f);
        return (f && assign(s.pm25, *f)) ? CHANGED_PM25 : CHANGED_NONE;
    }
    if (id == prop::AQI_STATE)
    {
        auto a = int_in_range(v, 0, 5);
        return (a && assign(s.aqi_state, *a)) ? CHANGED_AIR_QUALITY : CHANGED_NONE;
    }
    if (id == prop::TEMPERATURE)
    {
        auto f = float_in_range(v, -40.0f, 85.0f);
        return (f && assign(s.temperature_c, *f)) ? CHANGED_TEMPERATURE : CHANGED_NONE;
    }
    if (id == prop::HUMIDITY)
    {
        auto f = float_in_range(v, 0.0f, 100.0f);
        return (f && assign(s.humidity_pct, *f)) ? CHANGED_HUMIDITY : CHANGED_NONE;
    }
    if (id == prop::FILTER_LIFE)
    {
        auto l = int_in_range(v, 0, 100);
        return (l && assign(s.filter_life_pct, *l)) ? CHANGED_FILTER : CHANGED_NONE;
    }
    if (id == prop::FILTER_HOURS)
    {
        auto h = int_in_range(v, 0, 1000000);
        return (h && assign(s.filter_hours, *h)) ? CHANGED_FILTER : CHANGED_NONE;
    }
    if (id == prop::FAULT)
    {
        auto f = int_in_range(v, 0, 255);
        return (f && assign(s.fault, *f)) ? CHANGED_FAULT : CHANGED_NONE;
    }
    if (id == prop::FILTER_DOOR)
    {
        auto b = v.as_bool();
        return (b && assign(s.filter_door_open, *b)) ? CHANGED_FILTER_DOOR : CHANGED_NONE;
    }
    return CHANGED_NONE;
}

// ── Matter → purifier ─────────────────────────────────────────────────────────

static int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

FanTarget fan_target(const FanDrive &d)
{
    FanTarget t;
    switch (d.mode)
    {
    case FanMode::Off:
        t.power = false;
        return t;
    case FanMode::Auto:
    case FanMode::Smart:
        t.power = true;
        t.mode = Mode::Auto;
        return t;
    default:
        break;
    }

    t.power = true;
    const uint8_t pct = d.percent_setting.value_or(0);
    const uint8_t spd = d.speed_setting.value_or(0);

    // Values the cluster assigns for a FanMode write → manual levels.
    if ((pct == PERCENT_LOW && spd == SPEED_LOW) || (!d.percent_setting && !d.speed_setting && d.mode == FanMode::Low))
    {
        t.mode = Mode::Manual;
        t.fan_level = 1;
        return t;
    }
    if ((pct == PERCENT_MEDIUM && spd == SPEED_MEDIUM) || (!d.percent_setting && !d.speed_setting && d.mode == FanMode::Medium))
    {
        t.mode = Mode::Manual;
        t.fan_level = 2;
        return t;
    }
    if ((pct == PERCENT_HIGH && spd == SPEED_HIGH) || (!d.percent_setting && !d.speed_setting && (d.mode == FanMode::High || d.mode == FanMode::On)))
    {
        t.mode = Mode::Manual;
        t.fan_level = 3;
        return t;
    }

    // A speed or percent write → favorite mode, speed s = favorite level s−1.
    int speed = spd;
    if (speed == 0 && pct > 0)
        speed = (SPEED_MAX * pct + 99) / 100;
    if (speed == 0)
    {
        // Running mode without any speed information: fall back to the level
        // the mode names.
        t.mode = Mode::Manual;
        t.fan_level = d.mode == FanMode::Medium ? 2 : (d.mode == FanMode::Low ? 1 : 3);
        return t;
    }
    t.mode = Mode::Favorite;
    t.favorite_level = clamp_int(speed - 1, FAVORITE_LEVEL_MIN, FAVORITE_LEVEL_MAX);
    return t;
}

std::vector<SetCommand> commands_for(const FanTarget &t, const State &cur)
{
    std::vector<SetCommand> out;
    if (!t.power)
    {
        if (cur.power != std::optional<bool>(false))
            out.push_back({ prop::POWER, miot::Value::of_bool(false) });
        return out;
    }

    if (cur.power != std::optional<bool>(true))
        out.push_back({ prop::POWER, miot::Value::of_bool(true) });
    if (cur.mode != std::optional<Mode>(t.mode))
        out.push_back({ prop::MODE, miot::Value::of_int(static_cast<int>(t.mode)) });
    if (t.fan_level && cur.fan_level != t.fan_level)
        out.push_back({ prop::FAN_LEVEL, miot::Value::of_int(*t.fan_level) });
    if (t.favorite_level && cur.favorite_level != t.favorite_level)
        out.push_back({ prop::FAVORITE_LEVEL, miot::Value::of_int(*t.favorite_level) });
    return out;
}

std::vector<SetCommand> commands_for_sleep(bool sleep_wind, const State &cur)
{
    std::vector<SetCommand> out;
    if (sleep_wind)
    {
        if (cur.power != std::optional<bool>(true))
            out.push_back({ prop::POWER, miot::Value::of_bool(true) });
        if (cur.mode != std::optional<Mode>(Mode::Sleep))
            out.push_back({ prop::MODE, miot::Value::of_int(static_cast<int>(Mode::Sleep)) });
    }
    else if (cur.mode == std::optional<Mode>(Mode::Sleep))
    {
        out.push_back({ prop::MODE, miot::Value::of_int(static_cast<int>(Mode::Auto)) });
    }
    return out;
}

// ── Purifier → Matter ─────────────────────────────────────────────────────────

std::optional<FanView> fan_view(const State &s)
{
    if (!s.power)
        return std::nullopt;
    FanView v;
    if (!*s.power)
    {
        v.mode = FanMode::Off;
        return v;
    }
    if (!s.mode)
        return std::nullopt;

    switch (*s.mode)
    {
    case Mode::Auto:
        v.mode = FanMode::Auto;
        break;
    case Mode::Sleep:
        v.mode = FanMode::Low;
        v.sleep_wind = true;
        break;
    case Mode::Manual:
    {
        int level = s.fan_level.value_or(1);
        v.mode = level >= 3 ? FanMode::High : (level == 2 ? FanMode::Medium : FanMode::Low);
        break;
    }
    case Mode::Favorite:
        if (!s.favorite_level)
            return std::nullopt;
        v.kind = FanView::Kind::Speed;
        v.speed = static_cast<uint8_t>(*s.favorite_level + 1);
        // FanMode by thirds of the speed range, as the cluster derives it.
        v.mode = v.speed <= 5 ? FanMode::Low : (v.speed <= 10 ? FanMode::Medium : FanMode::High);
        break;
    }
    return v;
}

uint8_t percent_current(const State &s)
{
    if (!s.power || !*s.power || !s.motor_rpm || *s.motor_rpm <= 0)
        return 0;
    int pct = static_cast<int>(std::lround((*s.motor_rpm - MOTOR_RPM_MIN) * 100.0 / (MOTOR_RPM_MAX - MOTOR_RPM_MIN)));
    return static_cast<uint8_t>(clamp_int(pct, 1, 100));
}

uint8_t speed_current(uint8_t percent)
{
    return static_cast<uint8_t>((SPEED_MAX * percent + 99) / 100);
}

AirQuality air_quality(int aqi_state)
{
    if (aqi_state < 0 || aqi_state > 5)
        return AirQuality::Unknown;
    return static_cast<AirQuality>(aqi_state + 1);
}

ChangeIndication change_indication(int filter_life_pct)
{
    if (filter_life_pct <= 0)
        return ChangeIndication::Critical;
    if (filter_life_pct <= FILTER_WARNING_PERCENT)
        return ChangeIndication::Warning;
    return ChangeIndication::Ok;
}

int16_t temperature_centi(float c)
{
    long v = std::lround(c * 100.0f);
    return static_cast<int16_t>(v < -4000 ? -4000 : (v > 8500 ? 8500 : v));
}

uint16_t humidity_centi(float pct)
{
    long v = std::lround(pct * 100.0f);
    return static_cast<uint16_t>(v < 0 ? 0 : (v > 10000 ? 10000 : v));
}

std::optional<uint32_t> filter_last_changed(uint32_t now_epoch_s, int filter_hours)
{
    static constexpr uint32_t EPOCH_2024 = 1704067200u;
    if (now_epoch_s < EPOCH_2024 || filter_hours < 0)
        return std::nullopt;
    uint64_t used_s = static_cast<uint64_t>(filter_hours) * 3600u;
    if (used_s > now_epoch_s)
        return std::nullopt;
    return static_cast<uint32_t>(now_epoch_s - used_s);
}

} // namespace purifier
