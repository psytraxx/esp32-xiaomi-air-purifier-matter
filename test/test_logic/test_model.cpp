#include <unity.h>

#include "purifier_model.h"

using namespace purifier;
using miot::PropertyUpdate;
using miot::Value;

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
    return s;
}

// ── apply ────────────────────────────────────────────────────────────────────

static void test_apply_reports_changed_groups_once(void)
{
    State s;
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAN, apply(s, upd(prop::POWER, Value::of_bool(true))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(s, upd(prop::POWER, Value::of_bool(true))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_PM25, apply(s, upd(prop::PM25, Value::of_int(12))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_TEMPERATURE, apply(s, upd(prop::TEMPERATURE, Value::of_float(21.5))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_HUMIDITY, apply(s, upd(prop::HUMIDITY, Value::of_int(45))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_AIR_QUALITY, apply(s, upd(prop::AQI_STATE, Value::of_int(2))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FILTER, apply(s, upd(prop::FILTER_LIFE, Value::of_int(80))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FILTER, apply(s, upd(prop::FILTER_HOURS, Value::of_int(700))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FAULT, apply(s, upd(prop::FAULT, Value::of_int(0))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_FILTER_DOOR, apply(s, upd(prop::FILTER_DOOR, Value::of_bool(false))));
    TEST_ASSERT_EQUAL_FLOAT(21.5f, *s.temperature_c);
    TEST_ASSERT_EQUAL_FLOAT(45.0f, *s.humidity_pct);
}

static void test_apply_rejects_out_of_range_and_unknown(void)
{
    State s;
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(s, upd(prop::MODE, Value::of_int(7))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(s, upd(prop::FAN_LEVEL, Value::of_int(0))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(s, upd(prop::FAVORITE_LEVEL, Value::of_int(15))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(s, upd(prop::FILTER_LIFE, Value::of_int(101))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(s, upd(prop::POWER, Value::of_string("on"))));
    TEST_ASSERT_EQUAL_UINT32(CHANGED_NONE, apply(s, upd(PropertyId{ 99, 1 }, Value::of_int(1))));
    TEST_ASSERT_FALSE(s.mode.has_value());
}

// ── Matter → purifier ────────────────────────────────────────────────────────

static void test_fan_target_off_and_auto(void)
{
    FanDrive d;
    d.mode = FanMode::Off;
    TEST_ASSERT_FALSE(fan_target(d).power);

    d.mode = FanMode::Auto;
    FanTarget t = fan_target(d);
    TEST_ASSERT_TRUE(t.power);
    TEST_ASSERT_TRUE(t.mode == Mode::Auto);
}

static void test_fan_target_fanmode_writes_map_to_manual_levels(void)
{
    // The values the Fan Control cluster assigns for FanMode Low/Medium/High.
    FanDrive d{ FanMode::Low, 33, 1 };
    FanTarget t = fan_target(d);
    TEST_ASSERT_TRUE(t.mode == Mode::Manual);
    TEST_ASSERT_EQUAL_INT(1, *t.fan_level);

    t = fan_target(FanDrive{ FanMode::Medium, 66, 8 });
    TEST_ASSERT_TRUE(t.mode == Mode::Manual);
    TEST_ASSERT_EQUAL_INT(2, *t.fan_level);

    t = fan_target(FanDrive{ FanMode::High, 100, 15 });
    TEST_ASSERT_TRUE(t.mode == Mode::Manual);
    TEST_ASSERT_EQUAL_INT(3, *t.fan_level);
}

static void test_fan_target_speed_writes_map_to_favorite(void)
{
    // SpeedSetting 3 → cluster derives 20 % / Low.
    FanTarget t = fan_target(FanDrive{ FanMode::Low, 20, 3 });
    TEST_ASSERT_TRUE(t.mode == Mode::Favorite);
    TEST_ASSERT_EQUAL_INT(2, *t.favorite_level);

    // SpeedSetting 8 → 53 % / Medium: not the Medium FanMode values.
    t = fan_target(FanDrive{ FanMode::Medium, 53, 8 });
    TEST_ASSERT_TRUE(t.mode == Mode::Favorite);
    TEST_ASSERT_EQUAL_INT(7, *t.favorite_level);

    // SpeedSetting 1 → 6 %.
    t = fan_target(FanDrive{ FanMode::Low, 6, 1 });
    TEST_ASSERT_TRUE(t.mode == Mode::Favorite);
    TEST_ASSERT_EQUAL_INT(0, *t.favorite_level);
}

static void test_fan_target_percent_writes_map_to_favorite(void)
{
    // PercentSetting 33 → cluster derives speed ⌈15·33/100⌉ = 5.
    FanTarget t = fan_target(FanDrive{ FanMode::Low, 33, 5 });
    TEST_ASSERT_TRUE(t.mode == Mode::Favorite);
    TEST_ASSERT_EQUAL_INT(4, *t.favorite_level);

    // Percent without a speed (no MultiSpeed info) still maps sensibly.
    t = fan_target(FanDrive{ FanMode::Medium, 50, std::nullopt });
    TEST_ASSERT_TRUE(t.mode == Mode::Favorite);
    TEST_ASSERT_EQUAL_INT(7, *t.favorite_level);
}

static void test_fan_target_on_and_bare_modes(void)
{
    TEST_ASSERT_EQUAL_INT(3, *fan_target(FanDrive{ FanMode::On, std::nullopt, std::nullopt }).fan_level);
    TEST_ASSERT_EQUAL_INT(2, *fan_target(FanDrive{ FanMode::Medium, std::nullopt, std::nullopt }).fan_level);
    TEST_ASSERT_TRUE(fan_target(FanDrive{ FanMode::Smart, std::nullopt, std::nullopt }).mode == Mode::Auto);
}

static void test_commands_power_first_and_skip_unchanged(void)
{
    State s;
    s.power = false;
    s.mode = Mode::Auto;
    FanTarget t;
    t.power = true;
    t.mode = Mode::Manual;
    t.fan_level = 2;
    auto cmds = commands_for(t, s);
    TEST_ASSERT_EQUAL(3, cmds.size());
    TEST_ASSERT_TRUE(cmds[0].id == prop::POWER);
    TEST_ASSERT_TRUE(*cmds[0].value.as_bool());
    TEST_ASSERT_TRUE(cmds[1].id == prop::MODE);
    TEST_ASSERT_EQUAL_INT64(3, *cmds[1].value.as_int());
    TEST_ASSERT_TRUE(cmds[2].id == prop::FAN_LEVEL);
    TEST_ASSERT_EQUAL_INT64(2, *cmds[2].value.as_int());

    // Already there: nothing to send.
    s.power = true;
    s.mode = Mode::Manual;
    s.fan_level = 2;
    TEST_ASSERT_EQUAL(0, commands_for(t, s).size());
}

static void test_commands_unknown_state_sends_everything(void)
{
    State s;
    FanTarget t;
    t.power = true;
    t.mode = Mode::Favorite;
    t.favorite_level = 9;
    auto cmds = commands_for(t, s);
    TEST_ASSERT_EQUAL(3, cmds.size());
    TEST_ASSERT_TRUE(cmds[2].id == prop::FAVORITE_LEVEL);
    TEST_ASSERT_EQUAL_INT64(9, *cmds[2].value.as_int());
}

static void test_commands_off(void)
{
    State s = running(Mode::Auto);
    FanTarget t;
    t.power = false;
    auto cmds = commands_for(t, s);
    TEST_ASSERT_EQUAL(1, cmds.size());
    TEST_ASSERT_FALSE(*cmds[0].value.as_bool());
    s.power = false;
    TEST_ASSERT_EQUAL(0, commands_for(t, s).size());
}

static void test_commands_sleep(void)
{
    State s = running(Mode::Auto);
    auto on = commands_for_sleep(true, s);
    TEST_ASSERT_EQUAL(1, on.size());
    TEST_ASSERT_EQUAL_INT64(1, *on[0].value.as_int());

    TEST_ASSERT_EQUAL(0, commands_for_sleep(false, s).size()); // not sleeping
    s.mode = Mode::Sleep;
    auto off = commands_for_sleep(false, s);
    TEST_ASSERT_EQUAL(1, off.size());
    TEST_ASSERT_EQUAL_INT64(0, *off[0].value.as_int());

    State off_state;
    off_state.power = false;
    TEST_ASSERT_EQUAL(2, commands_for_sleep(true, off_state).size());
}

// ── Purifier → Matter ────────────────────────────────────────────────────────

static void test_fan_view(void)
{
    State s;
    TEST_ASSERT_FALSE(fan_view(s).has_value());

    s.power = false;
    TEST_ASSERT_TRUE(fan_view(s)->mode == FanMode::Off);

    s = running(Mode::Auto);
    TEST_ASSERT_TRUE(fan_view(s)->mode == FanMode::Auto);
    TEST_ASSERT_FALSE(fan_view(s)->sleep_wind);

    s = running(Mode::Sleep);
    TEST_ASSERT_TRUE(fan_view(s)->mode == FanMode::Low);
    TEST_ASSERT_TRUE(fan_view(s)->sleep_wind);

    s = running(Mode::Manual);
    s.fan_level = 2;
    TEST_ASSERT_TRUE(fan_view(s)->kind == FanView::Kind::Mode);
    TEST_ASSERT_TRUE(fan_view(s)->mode == FanMode::Medium);
    s.fan_level = 3;
    TEST_ASSERT_TRUE(fan_view(s)->mode == FanMode::High);

    s = running(Mode::Favorite);
    s.favorite_level = 7;
    auto v = fan_view(s);
    TEST_ASSERT_TRUE(v->kind == FanView::Kind::Speed);
    TEST_ASSERT_EQUAL_UINT8(8, v->speed);
    TEST_ASSERT_TRUE(v->mode == FanMode::Medium);

    State unknown_mode;
    unknown_mode.power = true;
    TEST_ASSERT_FALSE(fan_view(unknown_mode).has_value());
}

static void test_round_trip_matter_write_is_stable(void)
{
    // A controller writes SpeedSetting 4 (cluster: 26 %, Low). The purifier
    // then reports favorite level 3, which must present as speed 4 again.
    FanTarget t = fan_target(FanDrive{ FanMode::Low, 26, 4 });
    State s = running(Mode::Auto);
    for (const auto &c : commands_for(t, s))
        apply(s, { c.id, c.value });
    auto v = fan_view(s);
    TEST_ASSERT_TRUE(v->kind == FanView::Kind::Speed);
    TEST_ASSERT_EQUAL_UINT8(4, v->speed);

    // FanMode Medium → manual level 2 → presents as Medium again.
    t = fan_target(FanDrive{ FanMode::Medium, 66, 8 });
    for (const auto &c : commands_for(t, s))
        apply(s, { c.id, c.value });
    v = fan_view(s);
    TEST_ASSERT_TRUE(v->kind == FanView::Kind::Mode);
    TEST_ASSERT_TRUE(v->mode == FanMode::Medium);
}

static void test_percent_current(void)
{
    State s = running(Mode::Auto);
    TEST_ASSERT_EQUAL_UINT8(0, percent_current(s)); // rpm unknown
    s.motor_rpm = 300;
    TEST_ASSERT_EQUAL_UINT8(1, percent_current(s)); // running: never 0
    s.motor_rpm = 1300;
    TEST_ASSERT_EQUAL_UINT8(50, percent_current(s));
    s.motor_rpm = 2300;
    TEST_ASSERT_EQUAL_UINT8(100, percent_current(s));
    s.motor_rpm = 3000;
    TEST_ASSERT_EQUAL_UINT8(100, percent_current(s));
    s.power = false;
    TEST_ASSERT_EQUAL_UINT8(0, percent_current(s));

    TEST_ASSERT_EQUAL_UINT8(0, speed_current(0));
    TEST_ASSERT_EQUAL_UINT8(1, speed_current(1));
    TEST_ASSERT_EQUAL_UINT8(8, speed_current(50));
    TEST_ASSERT_EQUAL_UINT8(15, speed_current(100));
}

static void test_air_quality_and_filter(void)
{
    TEST_ASSERT_TRUE(air_quality(0) == AirQuality::Good);
    TEST_ASSERT_TRUE(air_quality(2) == AirQuality::Moderate);
    TEST_ASSERT_TRUE(air_quality(5) == AirQuality::ExtremelyPoor);
    TEST_ASSERT_TRUE(air_quality(6) == AirQuality::Unknown);
    TEST_ASSERT_TRUE(air_quality(-1) == AirQuality::Unknown);

    TEST_ASSERT_TRUE(change_indication(80) == ChangeIndication::Ok);
    TEST_ASSERT_TRUE(change_indication(11) == ChangeIndication::Ok);
    TEST_ASSERT_TRUE(change_indication(10) == ChangeIndication::Warning);
    TEST_ASSERT_TRUE(change_indication(1) == ChangeIndication::Warning);
    TEST_ASSERT_TRUE(change_indication(0) == ChangeIndication::Critical);
}

static void test_units(void)
{
    TEST_ASSERT_EQUAL_INT16(2150, temperature_centi(21.5f));
    TEST_ASSERT_EQUAL_INT16(-4000, temperature_centi(-60.0f));
    TEST_ASSERT_EQUAL_INT16(8500, temperature_centi(120.0f));
    TEST_ASSERT_EQUAL_UINT16(4500, humidity_centi(45.0f));
    TEST_ASSERT_EQUAL_UINT16(0, humidity_centi(-3.0f));
    TEST_ASSERT_EQUAL_UINT16(10000, humidity_centi(101.0f));
}

static void test_filter_last_changed(void)
{
    const uint32_t now = 1794326400u; // 2026-11-10
    TEST_ASSERT_EQUAL_UINT32(now - 700u * 3600u, *filter_last_changed(now, 700));
    TEST_ASSERT_FALSE(filter_last_changed(1000u, 10).has_value()); // clock not set
    TEST_ASSERT_FALSE(filter_last_changed(now, -1).has_value());
}

static void test_polled_properties_cover_mapped_features(void)
{
    const auto &p = polled_properties();
    auto has = [&p](PropertyId id) {
        for (const auto &x : p)
            if (x == id)
                return true;
        return false;
    };
    TEST_ASSERT_TRUE(has(prop::POWER));
    TEST_ASSERT_TRUE(has(prop::MODE));
    TEST_ASSERT_TRUE(has(prop::FAN_LEVEL));
    TEST_ASSERT_TRUE(has(prop::FAVORITE_LEVEL));
    TEST_ASSERT_TRUE(has(prop::PM25));
    TEST_ASSERT_TRUE(has(prop::TEMPERATURE));
    TEST_ASSERT_TRUE(has(prop::HUMIDITY));
    TEST_ASSERT_TRUE(has(prop::FILTER_LIFE));
    TEST_ASSERT_FALSE(has(prop::CHILD_LOCK)); // D2: not exposed
}

void run_model_tests(void)
{
    RUN_TEST(test_apply_reports_changed_groups_once);
    RUN_TEST(test_apply_rejects_out_of_range_and_unknown);
    RUN_TEST(test_fan_target_off_and_auto);
    RUN_TEST(test_fan_target_fanmode_writes_map_to_manual_levels);
    RUN_TEST(test_fan_target_speed_writes_map_to_favorite);
    RUN_TEST(test_fan_target_percent_writes_map_to_favorite);
    RUN_TEST(test_fan_target_on_and_bare_modes);
    RUN_TEST(test_commands_power_first_and_skip_unchanged);
    RUN_TEST(test_commands_unknown_state_sends_everything);
    RUN_TEST(test_commands_off);
    RUN_TEST(test_commands_sleep);
    RUN_TEST(test_fan_view);
    RUN_TEST(test_round_trip_matter_write_is_stable);
    RUN_TEST(test_percent_current);
    RUN_TEST(test_air_quality_and_filter);
    RUN_TEST(test_units);
    RUN_TEST(test_filter_last_changed);
    RUN_TEST(test_polled_properties_cover_mapped_features);
}
