#include <unity.h>

#include <string>
#include <vector>

#include "miot_session.h"

using namespace miot;

namespace
{
struct Recorder
{
    std::vector<PropertyUpdate> props;
    std::vector<std::pair<PropertyId, int>> set_failures;
    std::vector<std::pair<uint16_t, uint16_t>> events;
    std::vector<std::string> logs;
    int restores = 0;
    NetState net = NetState::Unprovisioned;
    std::string time_str = "0";
    bool last_time_posix = false;
};

Session make_session(Recorder &r)
{
    Session::Hooks h;
    h.on_properties = [&r](const std::vector<PropertyUpdate> &u) {
        r.props.insert(r.props.end(), u.begin(), u.end());
    };
    h.on_set_failed = [&r](PropertyId id, int code) { r.set_failures.push_back({ id, code }); };
    h.on_event = [&r](uint16_t s, uint16_t e, const std::vector<PropertyUpdate> &) { r.events.push_back({ s, e }); };
    h.on_restore = [&r]() { r.restores++; };
    h.net_state = [&r]() { return r.net; };
    h.time_reply = [&r](bool posix) {
        r.last_time_posix = posix;
        return r.time_str;
    };
    h.mac_reply = []() { return std::string("aa:bb:cc:dd:ee:ff"); };
    h.log = [&r](const std::string &m) { r.logs.push_back(m); };
    return Session(h);
}

// A session past its start-up handshake (network announcement and version
// request already handed to the MCU), so a test sees only its own commands.
Session started_session(Recorder &r)
{
    Session s = make_session(r);
    s.start();
    s.handle_line("get_down", 0);
    s.handle_line("get_down", 0);
    return s;
}
} // namespace

static void test_start_announces_net_then_version_request(void)
{
    Recorder r;
    Session s = make_session(r);
    s.start();
    TEST_ASSERT_EQUAL_STRING("down MIIO_net_change unprov", s.handle_line("get_down", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("down MIIO_mcu_version_req", s.handle_line("get_down", 2).c_str());
    TEST_ASSERT_EQUAL_STRING("down none", s.handle_line("get_down", 3).c_str());
}

static void test_net_change_jumps_queue_once(void)
{
    Recorder r;
    Session s = make_session(r);
    s.start();
    s.handle_line("get_down", 1); // net_change unprov
    s.queue_set({ 2, 2 }, Value::of_bool(true));
    r.net = NetState::Cloud;
    TEST_ASSERT_EQUAL_STRING("down MIIO_net_change cloud", s.handle_line("get_down", 2).c_str());
    TEST_ASSERT_EQUAL_STRING("down MIIO_mcu_version_req", s.handle_line("get_down", 3).c_str());
    TEST_ASSERT_EQUAL_STRING("down set_properties 2 2 true", s.handle_line("get_down", 4).c_str());
    TEST_ASSERT_EQUAL_STRING("down none", s.handle_line("get_down", 5).c_str());
}

static void test_net_reply(void)
{
    Recorder r;
    Session s = make_session(r);
    r.net = NetState::Offline;
    TEST_ASSERT_EQUAL_STRING("offline", s.handle_line("net", 1).c_str());
    r.net = NetState::Cloud;
    TEST_ASSERT_EQUAL_STRING("cloud", s.handle_line("net", 1).c_str());
}

static void test_net_state_for(void)
{
    TEST_ASSERT_TRUE(net_state_for(false, false) == NetState::Unprovisioned);
    TEST_ASSERT_TRUE(net_state_for(false, true) == NetState::Unprovisioned);
    TEST_ASSERT_TRUE(net_state_for(true, false) == NetState::Offline);
    TEST_ASSERT_TRUE(net_state_for(true, true) == NetState::Cloud);
}

static void test_properties_changed(void)
{
    Recorder r;
    Session s = make_session(r);
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("properties_changed 3 6 12 3 8 21.5 2 2 true", 1).c_str());
    TEST_ASSERT_EQUAL(3, r.props.size());
    TEST_ASSERT_TRUE(r.props[0].id == (PropertyId{ 3, 6 }));
    TEST_ASSERT_EQUAL_INT64(12, *r.props[0].value.as_int());
    TEST_ASSERT_EQUAL_DOUBLE(21.5, *r.props[1].value.as_float());
    TEST_ASSERT_TRUE(*r.props[2].value.as_bool());
}

static void test_properties_changed_ignores_trailing_partial(void)
{
    Recorder r;
    Session s = make_session(r);
    s.handle_line("properties_changed 3 6 12 3 8", 1);
    TEST_ASSERT_EQUAL(1, r.props.size());
}

static void test_get_result_parses_values_and_skips_failures(void)
{
    Recorder r;
    Session s = started_session(r);
    s.queue_get({ { 2, 2 }, { 3, 6 }, { 9, 9 }, { 2, 5 } });
    TEST_ASSERT_EQUAL_STRING("down get_properties 2 2 3 6 9 9 2 5", s.handle_line("get_down", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("result 2 2 0 true 3 6 0 7 9 9 -4001 2 5 0 1", 2).c_str());
    TEST_ASSERT_EQUAL(3, r.props.size());
    TEST_ASSERT_TRUE(r.props[2].id == (PropertyId{ 2, 5 }));
    TEST_ASSERT_EQUAL_INT64(1, *r.props[2].value.as_int());
    TEST_ASSERT_EQUAL(0, r.set_failures.size());
}

static void test_set_result_reports_failures(void)
{
    Recorder r;
    Session s = started_session(r);
    s.queue_set({ 2, 5 }, Value::of_int(9));
    s.handle_line("get_down", 1);
    s.handle_line("result 2 5 -4005", 2);
    TEST_ASSERT_EQUAL(1, r.set_failures.size());
    TEST_ASSERT_EQUAL_INT(-4005, r.set_failures[0].second);
    TEST_ASSERT_EQUAL(0, r.props.size());

    s.queue_set({ 2, 2 }, Value::of_bool(true));
    s.handle_line("get_down", 3);
    s.handle_line("result 2 2 0", 4);
    TEST_ASSERT_EQUAL(1, r.set_failures.size());
    TEST_ASSERT_EQUAL(0, r.props.size());
}

static void test_action_result(void)
{
    Recorder r;
    Session s = started_session(r);
    s.queue_action({ 4, 1 }, "3 0");
    TEST_ASSERT_EQUAL_STRING("down action 4 1 3 0", s.handle_line("get_down", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("result 4 1 0", 2).c_str());
    TEST_ASSERT_EQUAL(0, r.props.size());
    TEST_ASSERT_EQUAL(0, r.set_failures.size());
}

static void test_event(void)
{
    Recorder r;
    Session s = make_session(r);
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("event_occured 4 1 3 5", 1).c_str());
    TEST_ASSERT_EQUAL(1, r.events.size());
    TEST_ASSERT_EQUAL_UINT16(4, r.events[0].first);
}

static void test_time_mac_model_version(void)
{
    Recorder r;
    Session s = make_session(r);
    r.time_str = "2026-11-10 18:00:00";
    TEST_ASSERT_EQUAL_STRING("2026-11-10 18:00:00", s.handle_line("time", 1).c_str());
    TEST_ASSERT_FALSE(r.last_time_posix);
    s.handle_line("time posix", 1);
    TEST_ASSERT_TRUE(r.last_time_posix);
    TEST_ASSERT_EQUAL_STRING("aa:bb:cc:dd:ee:ff", s.handle_line("mac", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("model zhimi.airpurifier.mb3", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("zhimi.airpurifier.mb3", s.model().c_str());
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("mcu_version 0042", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("0042", s.mcu_version().c_str());
}

static void test_restore_and_error(void)
{
    Recorder r;
    Session s = make_session(r);
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("restore", 1).c_str());
    TEST_ASSERT_EQUAL(1, r.restores);
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("error \"motor stuck\" -5", 1).c_str());
    TEST_ASSERT_TRUE(r.logs.back().find("motor stuck") != std::string::npos);
}

static void test_unknown_and_garbage(void)
{
    Recorder r;
    Session s = make_session(r);
    TEST_ASSERT_EQUAL_STRING("ok", s.handle_line("somethingnew 1 2", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("error", s.handle_line("\x01\x02garbage", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("error", s.handle_line("", 1).c_str());
    TEST_ASSERT_EQUAL_STRING("error", s.handle_line("AB", 1).c_str());
}

static void test_last_rx_tracking(void)
{
    Recorder r;
    Session s = make_session(r);
    TEST_ASSERT_FALSE(s.mcu_seen());
    s.handle_line("get_down", 1234);
    TEST_ASSERT_TRUE(s.mcu_seen());
    TEST_ASSERT_EQUAL_UINT32(1234, s.last_rx_ms());
}

void run_session_tests(void)
{
    RUN_TEST(test_start_announces_net_then_version_request);
    RUN_TEST(test_net_change_jumps_queue_once);
    RUN_TEST(test_net_reply);
    RUN_TEST(test_net_state_for);
    RUN_TEST(test_properties_changed);
    RUN_TEST(test_properties_changed_ignores_trailing_partial);
    RUN_TEST(test_get_result_parses_values_and_skips_failures);
    RUN_TEST(test_set_result_reports_failures);
    RUN_TEST(test_action_result);
    RUN_TEST(test_event);
    RUN_TEST(test_time_mac_model_version);
    RUN_TEST(test_restore_and_error);
    RUN_TEST(test_unknown_and_garbage);
    RUN_TEST(test_last_rx_tracking);
}
