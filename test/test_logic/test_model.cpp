#include <unity.h>

#include "purifier_model.h"

using namespace purifier;
using miot::PropertyUpdate;
using miot::Value;

static const Profile &H = profile(Model::Mb3); // 3H
static const Profile &C = profile(Model::Mb4); // 3C

static PropertyUpdate upd(PropertyId id, Value v)
{
    return { id, v };
}

static State running(Mode mode)
{
    State s;
    s.power = true;
    s.mode = mode;
    s.fan_level = 1;
    s.favorite_level = 0;
    s.favorite_rpm = 300;
    return s;
}

static bool contains(const std::vector<PropertyId> &ids, PropertyId id)
{
    for (const auto &x : ids)
        if (x == id)
            return true;
    return false;
}

// ── Models ───────────────────────────────────────────────────────────────────

static void test_model_detection(void)
{
    TEST_ASSERT_TRUE(model_from_string("zhimi.airpurifier.mb3") == Model::Mb3);
    TEST_ASSERT_TRUE(model_from_string("zhimi.airp.mb4a") == Model::Mb4);
    TEST_ASSERT_TRUE(model_from_string("zhimi.airpurifier.mb4") == Model::Mb4);
    TEST_ASSERT_TRUE(model_from_string("zhimi.airpurifier.ma4") == Model::Unknown);
    TEST_ASSERT_EQUAL_STRING("3H", model_name(Model::Mb3));
    TEST_ASSERT_EQUAL_STRING("3C", model_name(Model::Mb4));

    TEST_ASSERT_TRUE(model_from_probe({ upd(PROBE_MB3, Value::of_int(700)) }) == Model::Mb3);
    TEST_ASSERT_TRUE(model_from_probe({ upd(PROBE_MB4, Value::of_int(700)) }) == Model::Mb4);
    TEST_ASSERT_TRUE(model_from_probe({ upd({ 2, 2 }, Value::of_bool(true)) }) == Model::Unknown);
    TEST_ASSERT_TRUE(model_from_probe({}) == Model::Unknown);
    TEST_ASSERT_EQUAL(2, probe_properties().size());
}

static void test_model_decisions(void)
{
    const Model U = Model::Unknown, M3 = Model::Mb3, M4 = Model::Mb4;
    TEST_ASSERT_TRUE(boot_model(U, U) == U);   // detect now
    TEST_ASSERT_TRUE(boot_model(U, M4) == M4); // remembered
    TEST_ASSERT_TRUE(boot_model(M3, M4) == M3); // forced wins

    // First boot: detected what we started with (after waiting).
    TEST_ASSERT_TRUE(on_model_detected(U, U, M4, M4) == DetectAction::Remember);
    // Normal boot: nothing new.
    TEST_ASSERT_TRUE(on_model_detected(U, M4, M4, M4) == DetectAction::None);
    // Fell back to the 3H, but it's a 3C — or the module moved purifiers.
    TEST_ASSERT_TRUE(on_model_detected(U, U, M3, M4) == DetectAction::RememberAndRestart);
    TEST_ASSERT_TRUE(on_model_detected(U, M3, M3, M4) == DetectAction::RememberAndRestart);
    // Forced model: never restart, warn on mismatch.
    TEST_ASSERT_TRUE(on_model_detected(M3, U, M3, M4) == DetectAction::WarnForced);
    TEST_ASSERT_TRUE(on_model_detected(M3, U, M3, M3) == DetectAction::None);
    // Nothing detected.
    TEST_ASSERT_TRUE(on_model_detected(U, M3, M3, U) == DetectAction::None);
}

static void test_profiles_differ_where_expected(void)
{
    TEST_ASSERT_TRUE(H.power == (PropertyId{ 2, 2 }));
    TEST_ASSERT_TRUE(C.power == (PropertyId{ 2, 1 }));
    TEST_ASSERT_TRUE(H.temperature.has_value());
    TEST_ASSERT_FALSE(C.temperature.has_value());
    TEST_ASSERT_FALSE(C.humidity.has_value());
    TEST_ASSERT_FALSE(C.aqi_state.has_value());
    TEST_ASSERT_FALSE(C.mode_manual.has_value());
    TEST_ASSERT_TRUE(C.favorite_rpm.has_value());
    TEST_ASSERT_TRUE(profile(Model::Unknown).model == Model::Mb3);
}

static void test_polled_properties(void)
{
    auto h = polled_properties(H);
    TEST_ASSERT_TRUE(contains(h, { 2, 2 }));
    TEST_ASSERT_TRUE(contains(h, { 10, 10 }));
    TEST_ASSERT_TRUE(contains(h, { 3, 8 }));
    TEST_ASSERT_FALSE(contains(h, { 7, 1 })); // child lock: not exposed (D2)

    auto c = polled_properties(C);
    TEST_ASSERT_TRUE(contains(c, { 2, 1 }));
    TEST_ASSERT_TRUE(contains(c, { 9, 3 }));
    TEST_ASSERT_TRUE(contains(c, { 3, 4 }));
    TEST_ASSERT_FALSE(contains(c, { 3, 8 }));
    TEST_ASSERT_FALSE(contains(c, { 8, 1 })); // child lock
}

// ── apply ────────────────────────────────────────────────────────────────────

static void test_apply_3h_reports_changed_groups_once(void)
{
    State s;
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAN, apply(H, s, upd({ 2, 2 }, Value::of_bool(true))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(H, s, upd({ 2, 2 }, Value::of_bool(true))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_PM25, apply(H, s, upd({ 3, 6 }, Value::of_int(12))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_TEMPERATURE, apply(H, s, upd({ 3, 8 }, Value::of_float(21.5))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_HUMIDITY, apply(H, s, upd({ 3, 7 }, Value::of_int(45))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_AIR_QUALITY, apply(H, s, upd({ 13, 8 }, Value::of_int(2))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FILTER, apply(H, s, upd({ 4, 3 }, Value::of_int(80))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FILTER, apply(H, s, upd({ 4, 5 }, Value::of_int(700))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAULT, apply(H, s, upd({ 2, 1 }, Value::of_int(0))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FILTER_DOOR, apply(H, s, upd({ 15, 5 }, Value::of_bool(false))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAN, apply(H, s, upd({ 2, 5 }, Value::of_int(3))));
    TEST_ASSERT_TRUE(*s.mode == Mode::Manual);
    TEST_ASSERT_EQUAL_FLOAT(21.5f, *s.temperature_c);
}

static void test_apply_rejects_out_of_range_and_unknown(void)
{
    State s;
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(H, s, upd({ 2, 5 }, Value::of_int(7))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(H, s, upd({ 2, 4 }, Value::of_int(0))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(H, s, upd({ 10, 10 }, Value::of_int(15))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(H, s, upd({ 4, 3 }, Value::of_int(101))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(H, s, upd({ 2, 2 }, Value::of_string("on"))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(H, s, upd({ 99, 1 }, Value::of_int(1))));
    TEST_ASSERT_FALSE(s.mode.has_value());
}

static void test_apply_3c(void)
{
    State s;
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAN, apply(C, s, upd({ 2, 1 }, Value::of_bool(true))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAN, apply(C, s, upd({ 2, 4 }, Value::of_int(2))));
    TEST_ASSERT_TRUE(*s.mode == Mode::Favorite);
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(C, s, upd({ 2, 4 }, Value::of_int(3)))); // no manual mode
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAN, apply(C, s, upd({ 9, 3 }, Value::of_int(1250))));
    TEST_ASSERT_EQUAL_INT(1250, *s.favorite_rpm);
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAN, apply(C, s, upd({ 9, 1 }, Value::of_int(1240))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FILTER, apply(C, s, upd({ 4, 1 }, Value::of_int(60))));
    // 3H IDs mean nothing on the 3C.
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(C, s, upd({ 3, 8 }, Value::of_float(21.5))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(C, s, upd({ 13, 8 }, Value::of_int(2))));
}

static void test_apply_3c_derives_air_quality_from_pm25(void)
{
    State s;
    TEST_ASSERT_EQUAL_UINT32(CHANGED_PM25 | CHANGED_AIR_QUALITY, apply(C, s, upd({ 3, 4 }, Value::of_int(8))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_PM25, apply(C, s, upd({ 3, 4 }, Value::of_int(10)))); // still Good
    TEST_ASSERT_EQUAL_UINT32(CHANGED_PM25 | CHANGED_AIR_QUALITY, apply(C, s, upd({ 3, 4 }, Value::of_int(40))));
    TEST_ASSERT_TRUE(*air_quality(C, s) == AirQuality::Moderate);
}

// ── Matter → purifier ────────────────────────────────────────────────────────

static void test_fan_target(void)
{
    TEST_ASSERT_FALSE(fan_target(FanDrive{ FanMode::Off, 0, 0 }).power);
    TEST_ASSERT_TRUE(fan_target(FanDrive{ FanMode::Auto, std::nullopt, std::nullopt }).mode == Mode::Auto);
    TEST_ASSERT_TRUE(fan_target(FanDrive{ FanMode::Smart, std::nullopt, std::nullopt }).mode == Mode::Auto);

    // The values the Fan Control cluster assigns for FanMode Low/Medium/High.
    TEST_ASSERT_EQUAL_INT(1, *fan_target(FanDrive{ FanMode::Low, 33, 1 }).level);
    TEST_ASSERT_EQUAL_INT(2, *fan_target(FanDrive{ FanMode::Medium, 66, 8 }).level);
    TEST_ASSERT_EQUAL_INT(3, *fan_target(FanDrive{ FanMode::High, 100, 15 }).level);
    TEST_ASSERT_EQUAL_INT(3, *fan_target(FanDrive{ FanMode::On, std::nullopt, std::nullopt }).level);
    TEST_ASSERT_EQUAL_INT(2, *fan_target(FanDrive{ FanMode::Medium, std::nullopt, std::nullopt }).level);

    // Speed writes: SpeedSetting 3 → cluster derives 20 % / Low.
    FanTarget t = fan_target(FanDrive{ FanMode::Low, 20, 3 });
    TEST_ASSERT_TRUE(t.mode == Mode::Favorite);
    TEST_ASSERT_EQUAL_UINT8(3, *t.speed);
    // SpeedSetting 8 → 53 %: not the Medium FanMode values.
    TEST_ASSERT_EQUAL_UINT8(8, *fan_target(FanDrive{ FanMode::Medium, 53, 8 }).speed);
    // PercentSetting 33 → cluster derives speed 5.
    TEST_ASSERT_EQUAL_UINT8(5, *fan_target(FanDrive{ FanMode::Low, 33, 5 }).speed);
    // Percent without speed.
    TEST_ASSERT_EQUAL_UINT8(8, *fan_target(FanDrive{ FanMode::Medium, 50, std::nullopt }).speed);
}

static void test_canonical_settings(void)
{
    TEST_ASSERT_EQUAL_UINT8(33, canonical_settings(FanMode::Low)->first);
    TEST_ASSERT_EQUAL_UINT8(1, canonical_settings(FanMode::Low)->second);
    TEST_ASSERT_EQUAL_UINT8(66, canonical_settings(FanMode::Medium)->first);
    TEST_ASSERT_EQUAL_UINT8(8, canonical_settings(FanMode::Medium)->second);
    TEST_ASSERT_EQUAL_UINT8(100, canonical_settings(FanMode::High)->first);
    TEST_ASSERT_EQUAL_UINT8(15, canonical_settings(FanMode::High)->second);
    TEST_ASSERT_FALSE(canonical_settings(FanMode::Auto).has_value());
    TEST_ASSERT_FALSE(canonical_settings(FanMode::Off).has_value());
}

static void test_commands_3h(void)
{
    State s;
    s.power = false;
    s.mode = Mode::Auto;

    FanTarget level;
    level.power = true;
    level.mode = Mode::Manual;
    level.level = 2;
    auto cmds = commands_for(H, level, s);
    TEST_ASSERT_EQUAL(3, cmds.size());
    TEST_ASSERT_TRUE(cmds[0].id == (PropertyId{ 2, 2 }));
    TEST_ASSERT_TRUE(*cmds[0].value.as_bool());
    TEST_ASSERT_TRUE(cmds[1].id == (PropertyId{ 2, 5 }));
    TEST_ASSERT_EQUAL_INT64(3, *cmds[1].value.as_int());
    TEST_ASSERT_TRUE(cmds[2].id == (PropertyId{ 2, 4 }));
    TEST_ASSERT_EQUAL_INT64(2, *cmds[2].value.as_int());

    s.power = true;
    s.mode = Mode::Manual;
    s.fan_level = 2;
    TEST_ASSERT_EQUAL(0, commands_for(H, level, s).size());

    FanTarget speed;
    speed.power = true;
    speed.mode = Mode::Favorite;
    speed.speed = 10;
    cmds = commands_for(H, speed, s);
    TEST_ASSERT_EQUAL(2, cmds.size());
    TEST_ASSERT_EQUAL_INT64(2, *cmds[0].value.as_int());
    TEST_ASSERT_TRUE(cmds[1].id == (PropertyId{ 10, 10 }));
    TEST_ASSERT_EQUAL_INT64(9, *cmds[1].value.as_int());
}

static void test_commands_3c(void)
{
    State s;
    s.power = true;
    s.mode = Mode::Auto;

    // A named level becomes favorite mode at the preset rpm.
    FanTarget level;
    level.power = true;
    level.mode = Mode::Manual;
    level.level = 2;
    auto cmds = commands_for(C, level, s);
    TEST_ASSERT_EQUAL(2, cmds.size());
    TEST_ASSERT_TRUE(cmds[0].id == (PropertyId{ 2, 4 }));
    TEST_ASSERT_EQUAL_INT64(2, *cmds[0].value.as_int());
    TEST_ASSERT_TRUE(cmds[1].id == (PropertyId{ 9, 3 }));
    TEST_ASSERT_EQUAL_INT64(C.preset_rpm[1], *cmds[1].value.as_int());

    // A fine speed becomes favorite rpm.
    FanTarget speed;
    speed.power = true;
    speed.mode = Mode::Favorite;
    speed.speed = 1;
    cmds = commands_for(C, speed, s);
    TEST_ASSERT_EQUAL_INT64(300, *cmds[1].value.as_int());

    // Off uses the 3C's power property.
    FanTarget off;
    cmds = commands_for(C, off, s);
    TEST_ASSERT_EQUAL(1, cmds.size());
    TEST_ASSERT_TRUE(cmds[0].id == (PropertyId{ 2, 1 }));
}

static void test_commands_unknown_state_sends_everything(void)
{
    State s;
    FanTarget t;
    t.power = true;
    t.mode = Mode::Favorite;
    t.speed = 10;
    TEST_ASSERT_EQUAL(3, commands_for(H, t, s).size());
    TEST_ASSERT_EQUAL(3, commands_for(C, t, s).size());
}

static void test_commands_sleep(void)
{
    State s = running(Mode::Auto);
    auto on = commands_for_sleep(H, true, s);
    TEST_ASSERT_EQUAL(1, on.size());
    TEST_ASSERT_EQUAL_INT64(1, *on[0].value.as_int());
    TEST_ASSERT_TRUE(commands_for_sleep(C, true, s)[0].id == (PropertyId{ 2, 4 }));

    TEST_ASSERT_EQUAL(0, commands_for_sleep(H, false, s).size()); // not sleeping
    s.mode = Mode::Sleep;
    auto off = commands_for_sleep(H, false, s);
    TEST_ASSERT_EQUAL(1, off.size());
    TEST_ASSERT_EQUAL_INT64(0, *off[0].value.as_int());

    State off_state;
    off_state.power = false;
    TEST_ASSERT_EQUAL(2, commands_for_sleep(H, true, off_state).size());
}

static void test_rpm_speed_mapping_3c(void)
{
    TEST_ASSERT_EQUAL_INT(300, rpm_for_speed(C, 1));
    TEST_ASSERT_EQUAL_INT(2200, rpm_for_speed(C, 15));
    for (uint8_t s = 1; s <= SPEED_MAX; s++)
    {
        int rpm = rpm_for_speed(C, s);
        TEST_ASSERT_EQUAL_INT(0, rpm % 10);
        TEST_ASSERT_EQUAL_UINT8(s, speed_for_rpm(C, rpm));
    }
    TEST_ASSERT_EQUAL_UINT8(1, speed_for_rpm(C, 0));
    TEST_ASSERT_EQUAL_UINT8(15, speed_for_rpm(C, 5000));
}

// ── Purifier → Matter ────────────────────────────────────────────────────────

static void test_fan_view_3h(void)
{
    State s;
    TEST_ASSERT_FALSE(fan_view(H, s).has_value());
    s.power = false;
    TEST_ASSERT_TRUE(fan_view(H, s)->mode == FanMode::Off);

    s = running(Mode::Auto);
    TEST_ASSERT_TRUE(fan_view(H, s)->mode == FanMode::Auto);
    s = running(Mode::Sleep);
    TEST_ASSERT_TRUE(fan_view(H, s)->mode == FanMode::Low);
    TEST_ASSERT_TRUE(fan_view(H, s)->sleep_wind);

    s = running(Mode::Manual);
    s.fan_level = 2;
    TEST_ASSERT_TRUE(fan_view(H, s)->kind == FanView::Kind::Mode);
    TEST_ASSERT_TRUE(fan_view(H, s)->mode == FanMode::Medium);

    s = running(Mode::Favorite);
    s.favorite_level = 7;
    auto v = fan_view(H, s);
    TEST_ASSERT_TRUE(v->kind == FanView::Kind::Speed);
    TEST_ASSERT_EQUAL_UINT8(8, v->speed);
    TEST_ASSERT_TRUE(v->mode == FanMode::Medium);

    State unknown_mode;
    unknown_mode.power = true;
    TEST_ASSERT_FALSE(fan_view(H, unknown_mode).has_value());
}

static void test_fan_view_3c(void)
{
    State s = running(Mode::Favorite);
    s.favorite_rpm = C.preset_rpm[0];
    TEST_ASSERT_TRUE(fan_view(C, s)->kind == FanView::Kind::Mode);
    TEST_ASSERT_TRUE(fan_view(C, s)->mode == FanMode::Low);
    s.favorite_rpm = C.preset_rpm[2];
    TEST_ASSERT_TRUE(fan_view(C, s)->mode == FanMode::High);

    s.favorite_rpm = 1250;
    auto v = fan_view(C, s);
    TEST_ASSERT_TRUE(v->kind == FanView::Kind::Speed);
    TEST_ASSERT_EQUAL_UINT8(speed_for_rpm(C, 1250), v->speed);

    s.favorite_rpm.reset();
    TEST_ASSERT_FALSE(fan_view(C, s).has_value());
}

// A controller write, carried out by the purifier and reported back, must
// present the same way again — on both models.
static void round_trip(const Profile &p, FanDrive drive, FanView::Kind kind, FanMode mode, uint8_t speed)
{
    State s = running(Mode::Auto);
    for (const auto &c : commands_for(p, fan_target(drive), s))
        apply(p, s, { c.id, c.value });
    auto v = fan_view(p, s);
    TEST_ASSERT_TRUE(v.has_value());
    TEST_ASSERT_TRUE(v->kind == kind);
    if (kind == FanView::Kind::Mode)
        TEST_ASSERT_TRUE(v->mode == mode);
    else
        TEST_ASSERT_EQUAL_UINT8(speed, v->speed);
}

static void test_round_trips(void)
{
    for (const Profile *p : { &H, &C })
    {
        round_trip(*p, FanDrive{ FanMode::Low, 33, 1 }, FanView::Kind::Mode, FanMode::Low, 0);
        round_trip(*p, FanDrive{ FanMode::Medium, 66, 8 }, FanView::Kind::Mode, FanMode::Medium, 0);
        round_trip(*p, FanDrive{ FanMode::High, 100, 15 }, FanView::Kind::Mode, FanMode::High, 0);
        for (uint8_t sp = 1; sp < SPEED_MAX; sp++)
        {
            uint8_t pct = static_cast<uint8_t>(sp * 100 / SPEED_MAX);
            FanMode m = pct <= 33 ? FanMode::Low : (pct <= 66 ? FanMode::Medium : FanMode::High);
            round_trip(*p, FanDrive{ m, pct, sp }, FanView::Kind::Speed, m, sp);
        }
    }
}

static void test_percent_current_follows_setting(void)
{
    // The bug from the first 3H test: a slider write must read back as the
    // same percentage, whatever the motor reports.
    State s = running(Mode::Favorite);
    s.motor_rpm = 380;
    s.favorite_level = 11; // speed 12, written as 80 %
    TEST_ASSERT_EQUAL_UINT8(80, percent_current(H, s));
    TEST_ASSERT_EQUAL_UINT8(12, speed_current(percent_current(H, s)));

    s = running(Mode::Manual);
    s.fan_level = 2;
    TEST_ASSERT_EQUAL_UINT8(66, percent_current(H, s));

    s = running(Mode::Favorite);
    s.favorite_rpm = C.preset_rpm[2];
    TEST_ASSERT_EQUAL_UINT8(100, percent_current(C, s));
    s.favorite_rpm = rpm_for_speed(C, 4);
    TEST_ASSERT_EQUAL_UINT8(4 * 100 / SPEED_MAX, percent_current(C, s));

    // Every slider position reads back as the PercentSetting the cluster
    // derives from it, on both models.
    for (const Profile *p : { &H, &C })
    {
        for (uint8_t pct = 1; pct <= 100; pct++)
        {
            uint8_t speed = static_cast<uint8_t>((SPEED_MAX * pct + 99) / 100);
            FanMode m = pct <= 33 ? FanMode::Low : (pct <= 66 ? FanMode::Medium : FanMode::High);
            State st = running(Mode::Auto);
            for (const auto &c : commands_for(*p, fan_target(FanDrive{ m, pct, speed }), st))
                apply(*p, st, { c.id, c.value });
            auto c = canonical_settings(m);
            uint8_t expected = (c && pct == c->first && speed == c->second) ? c->first
                                                                            : static_cast<uint8_t>(speed * 100 / SPEED_MAX);
            TEST_ASSERT_EQUAL_UINT8(expected, percent_current(*p, st));
        }
    }
}

static void test_percent_current(void)
{
    State s = running(Mode::Auto);
    TEST_ASSERT_EQUAL_UINT8(0, percent_current(H, s)); // rpm unknown
    s.motor_rpm = 300;
    TEST_ASSERT_EQUAL_UINT8(1, percent_current(H, s)); // running: never 0
    s.motor_rpm = 1300;
    TEST_ASSERT_EQUAL_UINT8(50, percent_current(H, s));
    s.motor_rpm = 2300;
    TEST_ASSERT_EQUAL_UINT8(100, percent_current(H, s));
    TEST_ASSERT_EQUAL_UINT8(100, percent_current(C, s)); // 3C tops out at 2200
    s.motor_rpm = 1250;
    TEST_ASSERT_EQUAL_UINT8(50, percent_current(C, s));
    s.power = false;
    TEST_ASSERT_EQUAL_UINT8(0, percent_current(H, s));

    TEST_ASSERT_EQUAL_UINT8(0, speed_current(0));
    TEST_ASSERT_EQUAL_UINT8(1, speed_current(1));
    TEST_ASSERT_EQUAL_UINT8(8, speed_current(50));
    TEST_ASSERT_EQUAL_UINT8(15, speed_current(100));
}

static void test_air_quality(void)
{
    TEST_ASSERT_TRUE(air_quality_from_state(0) == AirQuality::Good);
    TEST_ASSERT_TRUE(air_quality_from_state(5) == AirQuality::ExtremelyPoor);
    TEST_ASSERT_TRUE(air_quality_from_state(6) == AirQuality::Unknown);

    TEST_ASSERT_TRUE(air_quality_from_pm25(0.0f) == AirQuality::Good);
    TEST_ASSERT_TRUE(air_quality_from_pm25(12.0f) == AirQuality::Good);
    TEST_ASSERT_TRUE(air_quality_from_pm25(13.0f) == AirQuality::Fair);
    TEST_ASSERT_TRUE(air_quality_from_pm25(55.0f) == AirQuality::Moderate);
    TEST_ASSERT_TRUE(air_quality_from_pm25(100.0f) == AirQuality::Poor);
    TEST_ASSERT_TRUE(air_quality_from_pm25(200.0f) == AirQuality::VeryPoor);
    TEST_ASSERT_TRUE(air_quality_from_pm25(400.0f) == AirQuality::ExtremelyPoor);

    State s;
    TEST_ASSERT_FALSE(air_quality(H, s).has_value());
    TEST_ASSERT_FALSE(air_quality(C, s).has_value());
    s.pm25 = 100.0f;
    s.aqi_state = 0;
    TEST_ASSERT_TRUE(*air_quality(H, s) == AirQuality::Good); // the 3H's own level wins
    TEST_ASSERT_TRUE(*air_quality(C, s) == AirQuality::Poor);
}

static void test_filter_and_units(void)
{
    TEST_ASSERT_TRUE(change_indication(80) == ChangeIndication::Ok);
    TEST_ASSERT_TRUE(change_indication(11) == ChangeIndication::Ok);
    TEST_ASSERT_TRUE(change_indication(10) == ChangeIndication::Warning);
    TEST_ASSERT_TRUE(change_indication(0) == ChangeIndication::Critical);

    TEST_ASSERT_EQUAL_INT16(2150, temperature_centi(21.5f));
    TEST_ASSERT_EQUAL_INT16(-4000, temperature_centi(-60.0f));
    TEST_ASSERT_EQUAL_INT16(8500, temperature_centi(120.0f));
    TEST_ASSERT_EQUAL_UINT16(4500, humidity_centi(45.0f));
    TEST_ASSERT_EQUAL_UINT16(0, humidity_centi(-3.0f));
    TEST_ASSERT_EQUAL_UINT16(10000, humidity_centi(101.0f));

    const uint32_t now = 1794326400u; // 2026-11-10
    TEST_ASSERT_EQUAL_UINT32(now - 700u * 3600u, *filter_last_changed(now, 700));
    TEST_ASSERT_FALSE(filter_last_changed(1000u, 10).has_value()); // clock not set
    TEST_ASSERT_FALSE(filter_last_changed(now, -1).has_value());
}

static void test_hardware_faults(void)
{
    State s;
    TEST_ASSERT_EQUAL(0, hardware_faults(H, s).size()); // not reported yet
    s.fault = 0;
    TEST_ASSERT_EQUAL(0, hardware_faults(H, s).size());
    s.fault = 2; // motor stuck
    TEST_ASSERT_TRUE(hardware_faults(H, s) == std::vector<HardwareFault>{ HardwareFault::Unspecified });
    for (int code = 3; code <= 5; code++) // sensor errors
    {
        s.fault = code;
        TEST_ASSERT_TRUE(hardware_faults(H, s) == std::vector<HardwareFault>{ HardwareFault::Sensor });
    }
    s.fault = 9;
    TEST_ASSERT_TRUE(hardware_faults(H, s) == std::vector<HardwareFault>{ HardwareFault::Unspecified });
    s.fault = 4; // the 3C's codes aren't known: never guessed as Sensor
    TEST_ASSERT_TRUE(hardware_faults(C, s) == std::vector<HardwareFault>{ HardwareFault::Unspecified });

    // The fault property feeds it on both models.
    State c;
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAULT, apply(C, c, upd({ 2, 2 }, Value::of_int(1))));
    TEST_ASSERT_EQUAL(1, hardware_faults(C, c).size());
}

void run_model_tests(void)
{
    RUN_TEST(test_hardware_faults);
    RUN_TEST(test_model_detection);
    RUN_TEST(test_model_decisions);
    RUN_TEST(test_profiles_differ_where_expected);
    RUN_TEST(test_polled_properties);
    RUN_TEST(test_apply_3h_reports_changed_groups_once);
    RUN_TEST(test_apply_rejects_out_of_range_and_unknown);
    RUN_TEST(test_apply_3c);
    RUN_TEST(test_apply_3c_derives_air_quality_from_pm25);
    RUN_TEST(test_fan_target);
    RUN_TEST(test_canonical_settings);
    RUN_TEST(test_commands_3h);
    RUN_TEST(test_commands_3c);
    RUN_TEST(test_commands_unknown_state_sends_everything);
    RUN_TEST(test_commands_sleep);
    RUN_TEST(test_rpm_speed_mapping_3c);
    RUN_TEST(test_fan_view_3h);
    RUN_TEST(test_fan_view_3c);
    RUN_TEST(test_round_trips);
    RUN_TEST(test_percent_current_follows_setting);
    RUN_TEST(test_percent_current);
    RUN_TEST(test_air_quality);
    RUN_TEST(test_filter_and_units);
}
