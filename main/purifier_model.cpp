#include "purifier_model.h"

#include <cmath>

namespace purifier
{

// ── Models and profiles ───────────────────────────────────────────────────────

Model model_from_string(std::string_view s)
{
    if (s == "zhimi.airpurifier.mb3")
        return Model::Mb3;
    if (s == "zhimi.airp.mb4a" || s == "zhimi.airpurifier.mb4")
        return Model::Mb4;
    return Model::Unknown;
}

const char *model_name(Model m)
{
    switch (m)
    {
    case Model::Mb3:
        return "3H";
    case Model::Mb4:
        return "3C";
    case Model::Unknown:
        break;
    }
    return "unknown";
}

std::vector<PropertyId> probe_properties()
{
    return { PROBE_MB3, PROBE_MB4 };
}

Model model_from_probe(const std::vector<miot::PropertyUpdate> &updates)
{
    for (const auto &u : updates)
    {
        if (u.id == PROBE_MB3)
            return Model::Mb3;
        if (u.id == PROBE_MB4)
            return Model::Mb4;
    }
    return Model::Unknown;
}

Model boot_model(Model forced, Model remembered)
{
    return forced != Model::Unknown ? forced : remembered;
}

DetectAction on_model_detected(Model forced, Model remembered, Model active, Model detected)
{
    if (detected == Model::Unknown)
        return DetectAction::None;
    if (forced != Model::Unknown)
        return detected == forced ? DetectAction::None : DetectAction::WarnForced;
    if (detected != active)
        return DetectAction::RememberAndRestart;
    return detected == remembered ? DetectAction::None : DetectAction::Remember;
}

// Mi Air Purifier 3/3H — https://home.miot-spec.com/spec/zhimi.airpurifier.mb3
static const Profile k_mb3 = {
    /*model*/ Model::Mb3,
    /*fan_scheme*/ FanScheme::LevelsAndFavoriteLevel,
    /*power*/ { 2, 2 },
    /*mode*/ { 2, 5 },
    /*motor_rpm*/ { 10, 8 },
    /*pm25*/ { 3, 6 },
    /*filter_life*/ { 4, 3 },
    /*filter_hours*/ { 4, 5 },
    /*fault*/ { 2, 1 },
    /*heartbeat*/ { 13, 9 },
    /*fan_level*/ PropertyId{ 2, 4 },
    /*favorite_level*/ PropertyId{ 10, 10 },
    /*favorite_rpm*/ std::nullopt,
    /*aqi_state*/ PropertyId{ 13, 8 },
    /*temperature*/ PropertyId{ 3, 8 },
    /*humidity*/ PropertyId{ 3, 7 },
    /*filter_door*/ PropertyId{ 15, 5 },
    /*reset_filter*/ { 4, 1 },
    /*reset_filter_args*/ "3 0",
    /*mode_auto*/ 0,
    /*mode_sleep*/ 1,
    /*mode_favorite*/ 2,
    /*mode_manual*/ 3,
    /*rpm_min*/ 300,
    /*rpm_max*/ 2300,
    /*favorite_rpm_min*/ 0,
    /*favorite_rpm_max*/ 0,
    /*favorite_rpm_step*/ 0,
    /*preset_rpm*/ { 0, 0, 0 },
};

// Mi Air Purifier 3C — https://home.miot-spec.com/spec/zhimi.airp.mb4a
// Favorite rpm 300–2200 in steps of 10 (some units reject values above 2200).
// Low/Medium/High presets are a first guess, to tune on a real unit.
static const Profile k_mb4 = {
    /*model*/ Model::Mb4,
    /*fan_scheme*/ FanScheme::FavoriteRpm,
    /*power*/ { 2, 1 },
    /*mode*/ { 2, 4 },
    /*motor_rpm*/ { 9, 1 },
    /*pm25*/ { 3, 4 },
    /*filter_life*/ { 4, 1 },
    /*filter_hours*/ { 4, 3 },
    /*fault*/ { 2, 2 },
    /*heartbeat*/ { 9, 4 },
    /*fan_level*/ std::nullopt,
    /*favorite_level*/ std::nullopt,
    /*favorite_rpm*/ PropertyId{ 9, 3 },
    /*aqi_state*/ std::nullopt,
    /*temperature*/ std::nullopt,
    /*humidity*/ std::nullopt,
    /*filter_door*/ std::nullopt,
    /*reset_filter*/ { 4, 1 },
    /*reset_filter_args*/ "3 0",
    /*mode_auto*/ 0,
    /*mode_sleep*/ 1,
    /*mode_favorite*/ 2,
    /*mode_manual*/ std::nullopt,
    /*rpm_min*/ 300,
    /*rpm_max*/ 2200,
    /*favorite_rpm_min*/ 300,
    /*favorite_rpm_max*/ 2200,
    /*favorite_rpm_step*/ 10,
    /*preset_rpm*/ { 800, 1400, 2200 },
};

const Profile &profile(Model m)
{
    return m == Model::Mb4 ? k_mb4 : k_mb3;
}

std::vector<PropertyId> polled_properties(const Profile &p)
{
    std::vector<PropertyId> ids = { p.power, p.mode, p.motor_rpm, p.pm25, p.filter_life, p.filter_hours, p.fault };
    for (const auto &opt : { p.fan_level, p.favorite_level, p.favorite_rpm, p.aqi_state, p.temperature, p.humidity, p.filter_door })
    {
        if (opt)
            ids.push_back(*opt);
    }
    return ids;
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

static std::optional<Mode> mode_from_wire(const Profile &p, int wire)
{
    if (wire == p.mode_auto)
        return Mode::Auto;
    if (wire == p.mode_sleep)
        return Mode::Sleep;
    if (wire == p.mode_favorite)
        return Mode::Favorite;
    if (p.mode_manual && wire == *p.mode_manual)
        return Mode::Manual;
    return std::nullopt;
}

static int mode_to_wire(const Profile &p, Mode m)
{
    switch (m)
    {
    case Mode::Auto:
        return p.mode_auto;
    case Mode::Sleep:
        return p.mode_sleep;
    case Mode::Favorite:
        return p.mode_favorite;
    case Mode::Manual:
        return p.mode_manual.value_or(p.mode_favorite);
    }
    return p.mode_auto;
}

static bool is(const std::optional<PropertyId> &opt, PropertyId id)
{
    return opt && *opt == id;
}

uint32_t apply(const Profile &p, State &s, const miot::PropertyUpdate &u)
{
    const PropertyId id = u.id;
    const miot::Value &v = u.value;

    if (id == p.power)
    {
        auto b = v.as_bool();
        return (b && assign(s.power, *b)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (id == p.mode)
    {
        auto w = v.as_int();
        auto m = w ? mode_from_wire(p, static_cast<int>(*w)) : std::nullopt;
        return (m && assign(s.mode, *m)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (is(p.fan_level, id))
    {
        auto l = int_in_range(v, FAN_LEVEL_MIN, FAN_LEVEL_MAX);
        return (l && assign(s.fan_level, *l)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (is(p.favorite_level, id))
    {
        auto l = int_in_range(v, FAVORITE_LEVEL_MIN, FAVORITE_LEVEL_MAX);
        return (l && assign(s.favorite_level, *l)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (is(p.favorite_rpm, id))
    {
        auto r = int_in_range(v, 0, 10000);
        return (r && assign(s.favorite_rpm, *r)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (id == p.motor_rpm)
    {
        auto r = int_in_range(v, 0, 10000);
        return (r && assign(s.motor_rpm, *r)) ? CHANGED_FAN : CHANGED_NONE;
    }
    if (id == p.pm25)
    {
        auto f = float_in_range(v, 0.0f, 1000.0f);
        if (!f)
            return CHANGED_NONE;
        auto before = p.aqi_state ? std::nullopt : air_quality(p, s);
        if (!assign(s.pm25, *f))
            return CHANGED_NONE;
        uint32_t changed = CHANGED_PM25;
        if (!p.aqi_state && air_quality(p, s) != before)
            changed |= CHANGED_AIR_QUALITY;
        return changed;
    }
    if (is(p.aqi_state, id))
    {
        auto a = int_in_range(v, 0, 5);
        return (a && assign(s.aqi_state, *a)) ? CHANGED_AIR_QUALITY : CHANGED_NONE;
    }
    if (is(p.temperature, id))
    {
        auto f = float_in_range(v, -40.0f, 85.0f);
        return (f && assign(s.temperature_c, *f)) ? CHANGED_TEMPERATURE : CHANGED_NONE;
    }
    if (is(p.humidity, id))
    {
        auto f = float_in_range(v, 0.0f, 100.0f);
        return (f && assign(s.humidity_pct, *f)) ? CHANGED_HUMIDITY : CHANGED_NONE;
    }
    if (id == p.filter_life)
    {
        auto l = int_in_range(v, 0, 100);
        return (l && assign(s.filter_life_pct, *l)) ? CHANGED_FILTER : CHANGED_NONE;
    }
    if (id == p.filter_hours)
    {
        auto h = int_in_range(v, 0, 1000000);
        return (h && assign(s.filter_hours, *h)) ? CHANGED_FILTER : CHANGED_NONE;
    }
    if (id == p.fault)
    {
        auto f = int_in_range(v, 0, 255);
        return (f && assign(s.fault, *f)) ? CHANGED_FAULT : CHANGED_NONE;
    }
    if (is(p.filter_door, id))
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

std::optional<std::pair<uint8_t, uint8_t>> canonical_settings(FanMode mode)
{
    switch (mode)
    {
    case FanMode::Low:
        return std::make_pair(PERCENT_LOW, SPEED_LOW);
    case FanMode::Medium:
        return std::make_pair(PERCENT_MEDIUM, SPEED_MEDIUM);
    case FanMode::High:
        return std::make_pair(PERCENT_HIGH, SPEED_HIGH);
    default:
        return std::nullopt;
    }
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

    // Values the cluster assigns for a FanMode write → named level. A FanMode
    // without any settings (no MultiSpeed info) also names its level.
    const bool no_settings = !d.percent_setting && !d.speed_setting;
    const FanMode named = d.mode == FanMode::On ? FanMode::High : d.mode;
    for (int level = 1; level <= 3; level++)
    {
        const FanMode m = level == 1 ? FanMode::Low : (level == 2 ? FanMode::Medium : FanMode::High);
        auto c = canonical_settings(m);
        if ((c && pct == c->first && spd == c->second) || (no_settings && named == m))
        {
            t.mode = Mode::Manual;
            t.level = level;
            return t;
        }
    }

    // A speed or percent write → fine speed.
    int speed = spd;
    if (speed == 0 && pct > 0)
        speed = (SPEED_MAX * pct + 99) / 100;
    if (speed == 0)
    {
        // Running mode without any speed information: fall back to the level
        // the mode names.
        t.mode = Mode::Manual;
        t.level = d.mode == FanMode::Medium ? 2 : (d.mode == FanMode::Low ? 1 : 3);
        return t;
    }
    t.mode = Mode::Favorite;
    t.speed = static_cast<uint8_t>(clamp_int(speed, 1, SPEED_MAX));
    return t;
}

int rpm_for_speed(const Profile &p, uint8_t speed)
{
    const int s = clamp_int(speed, 1, SPEED_MAX);
    const int step = p.favorite_rpm_step > 0 ? p.favorite_rpm_step : 1;
    const double raw = p.favorite_rpm_min + (s - 1) * double(p.favorite_rpm_max - p.favorite_rpm_min) / (SPEED_MAX - 1);
    const int rpm = static_cast<int>(std::lround(raw / step)) * step;
    return clamp_int(rpm, p.favorite_rpm_min, p.favorite_rpm_max);
}

uint8_t speed_for_rpm(const Profile &p, int rpm)
{
    const int span = p.favorite_rpm_max - p.favorite_rpm_min;
    if (span <= 0)
        return 1;
    const long s = 1 + std::lround((rpm - p.favorite_rpm_min) * double(SPEED_MAX - 1) / span);
    return static_cast<uint8_t>(clamp_int(static_cast<int>(s), 1, SPEED_MAX));
}

static void push_if_different(std::vector<SetCommand> &out, PropertyId id, const std::optional<int> &current, int target)
{
    if (current != std::optional<int>(target))
        out.push_back({ id, miot::Value::of_int(target) });
}

std::vector<SetCommand> commands_for(const Profile &p, const FanTarget &t, const State &cur)
{
    std::vector<SetCommand> out;
    if (!t.power)
    {
        if (cur.power != std::optional<bool>(false))
            out.push_back({ p.power, miot::Value::of_bool(false) });
        return out;
    }

    if (cur.power != std::optional<bool>(true))
        out.push_back({ p.power, miot::Value::of_bool(true) });

    // The model-specific mode and speed property for the target.
    Mode mode = t.mode;
    std::optional<std::pair<PropertyId, int>> speed_prop;
    std::optional<int> speed_current;

    if (p.fan_scheme == FanScheme::LevelsAndFavoriteLevel)
    {
        if (t.level && p.fan_level)
        {
            mode = Mode::Manual;
            speed_prop = std::make_pair(*p.fan_level, clamp_int(*t.level, FAN_LEVEL_MIN, FAN_LEVEL_MAX));
            speed_current = cur.fan_level;
        }
        else if (t.speed && p.favorite_level)
        {
            mode = Mode::Favorite;
            speed_prop = std::make_pair(*p.favorite_level, clamp_int(*t.speed - 1, FAVORITE_LEVEL_MIN, FAVORITE_LEVEL_MAX));
            speed_current = cur.favorite_level;
        }
    }
    else if (p.favorite_rpm && (t.level || t.speed))
    {
        mode = Mode::Favorite;
        int rpm = t.level ? p.preset_rpm[clamp_int(*t.level, 1, 3) - 1] : rpm_for_speed(p, *t.speed);
        speed_prop = std::make_pair(*p.favorite_rpm, rpm);
        speed_current = cur.favorite_rpm;
    }

    if (cur.mode != std::optional<Mode>(mode))
        out.push_back({ p.mode, miot::Value::of_int(mode_to_wire(p, mode)) });
    if (speed_prop)
        push_if_different(out, speed_prop->first, speed_current, speed_prop->second);
    return out;
}

std::vector<SetCommand> commands_for_sleep(const Profile &p, bool sleep_wind, const State &cur)
{
    std::vector<SetCommand> out;
    if (sleep_wind)
    {
        if (cur.power != std::optional<bool>(true))
            out.push_back({ p.power, miot::Value::of_bool(true) });
        if (cur.mode != std::optional<Mode>(Mode::Sleep))
            out.push_back({ p.mode, miot::Value::of_int(p.mode_sleep) });
    }
    else if (cur.mode == std::optional<Mode>(Mode::Sleep))
    {
        out.push_back({ p.mode, miot::Value::of_int(p.mode_auto) });
    }
    return out;
}

// ── Purifier → Matter ─────────────────────────────────────────────────────────

static FanMode mode_for_speed(uint8_t speed)
{
    // FanMode by thirds of the speed range, as the cluster derives it from
    // the percentage (≤33 % Low, ≤66 % Medium).
    const int pct = speed * 100 / SPEED_MAX;
    return pct <= 33 ? FanMode::Low : (pct <= 66 ? FanMode::Medium : FanMode::High);
}

static FanMode named_mode(int level)
{
    return level >= 3 ? FanMode::High : (level == 2 ? FanMode::Medium : FanMode::Low);
}

std::optional<FanView> fan_view(const Profile &p, const State &s)
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
        return v;
    case Mode::Sleep:
        v.mode = FanMode::Low;
        v.sleep_wind = true;
        return v;
    case Mode::Manual:
        v.mode = named_mode(s.fan_level.value_or(1));
        return v;
    case Mode::Favorite:
        break;
    }

    if (p.fan_scheme == FanScheme::LevelsAndFavoriteLevel)
    {
        if (!s.favorite_level)
            return std::nullopt;
        v.kind = FanView::Kind::Speed;
        v.speed = static_cast<uint8_t>(*s.favorite_level + 1);
        v.mode = mode_for_speed(v.speed);
        return v;
    }

    if (!s.favorite_rpm)
        return std::nullopt;
    for (int level = 1; level <= 3; level++)
    {
        if (*s.favorite_rpm == p.preset_rpm[level - 1])
        {
            v.mode = named_mode(level);
            return v;
        }
    }
    v.kind = FanView::Kind::Speed;
    v.speed = speed_for_rpm(p, *s.favorite_rpm);
    v.mode = mode_for_speed(v.speed);
    return v;
}

uint8_t percent_current(const Profile &p, const State &s)
{
    if (!s.power || !*s.power || !s.motor_rpm || *s.motor_rpm <= 0)
        return 0;
    const int span = p.rpm_max - p.rpm_min;
    if (span <= 0)
        return 100;
    int pct = static_cast<int>(std::lround((*s.motor_rpm - p.rpm_min) * 100.0 / span));
    return static_cast<uint8_t>(clamp_int(pct, 1, 100));
}

uint8_t speed_current(uint8_t percent)
{
    return static_cast<uint8_t>((SPEED_MAX * percent + 99) / 100);
}

AirQuality air_quality_from_state(int aqi_state)
{
    if (aqi_state < 0 || aqi_state > 5)
        return AirQuality::Unknown;
    return static_cast<AirQuality>(aqi_state + 1);
}

AirQuality air_quality_from_pm25(float pm25)
{
    // US EPA PM2.5 breakpoints (µg/m³), one Matter level per AQI category.
    if (pm25 < 0.0f)
        return AirQuality::Unknown;
    if (pm25 <= 12.0f)
        return AirQuality::Good;
    if (pm25 <= 35.4f)
        return AirQuality::Fair;
    if (pm25 <= 55.4f)
        return AirQuality::Moderate;
    if (pm25 <= 150.4f)
        return AirQuality::Poor;
    if (pm25 <= 250.4f)
        return AirQuality::VeryPoor;
    return AirQuality::ExtremelyPoor;
}

std::optional<AirQuality> air_quality(const Profile &p, const State &s)
{
    if (p.aqi_state)
    {
        if (!s.aqi_state)
            return std::nullopt;
        return air_quality_from_state(*s.aqi_state);
    }
    if (!s.pm25)
        return std::nullopt;
    return air_quality_from_pm25(*s.pm25);
}

ChangeIndication change_indication(int filter_life_pct)
{
    if (filter_life_pct <= 0)
        return ChangeIndication::Critical;
    if (filter_life_pct <= FILTER_WARNING_PERCENT)
        return ChangeIndication::Warning;
    return ChangeIndication::Ok;
}

std::vector<HardwareFault> hardware_faults(const Profile &p, const State &s)
{
    if (!s.fault || *s.fault == 0)
        return {};
    if (p.model == Model::Mb3 && *s.fault >= 3 && *s.fault <= 5)
        return { HardwareFault::Sensor };
    return { HardwareFault::Unspecified };
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
