#include <unity.h>

#include <cstring>

#include "wifi_diag.h"

using namespace wifi_diag;

static void test_reason_names(void)
{
    TEST_ASSERT_EQUAL_STRING("IE_IN_4WAY_DIFFERS", reason_name(17));
    TEST_ASSERT_EQUAL_STRING("4WAY_HANDSHAKE_TIMEOUT", reason_name(15));
    TEST_ASSERT_EQUAL_STRING("NO_AP_FOUND", reason_name(201));
    TEST_ASSERT_EQUAL_STRING("NO_AP_FOUND_IN_RSSI_THRESHOLD", reason_name(212));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", reason_name(0));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", reason_name(42));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", reason_name(255));
}

static void test_reason_hints(void)
{
    TEST_ASSERT_NOT_NULL(strstr(reason_hint(17), "PMF"));
    TEST_ASSERT_NOT_NULL(strstr(reason_hint(15), "password"));
    TEST_ASSERT_NOT_NULL(strstr(reason_hint(201), "2.4 GHz"));
    TEST_ASSERT_NOT_NULL(strstr(reason_hint(210), "WPA3"));
    TEST_ASSERT_NULL(reason_hint(8));
}

static void test_authmode_names(void)
{
    TEST_ASSERT_EQUAL_STRING("WPA2_PSK", authmode_name(3));
    TEST_ASSERT_EQUAL_STRING("WPA3_PSK", authmode_name(6));
    TEST_ASSERT_EQUAL_STRING("WPA2_WPA3_PSK", authmode_name(7));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", authmode_name(11));
    TEST_ASSERT_EQUAL_STRING("UNKNOWN", authmode_name(-1));
}

static void test_filter_logs_first_and_every_nth(void)
{
    DisconnectFilter f(5);
    for (uint32_t i = 1; i <= 11; i++)
    {
        auto d = f.on_disconnect(17);
        TEST_ASSERT_EQUAL_UINT32(i, d.repeats);
        TEST_ASSERT_EQUAL(i == 1 || i == 5 || i == 10, d.log);
        TEST_ASSERT_EQUAL_UINT32(0, d.previous_count);
    }
}

static void test_filter_reason_change_ends_run(void)
{
    DisconnectFilter f(50);
    f.on_disconnect(17);
    f.on_disconnect(17);
    f.on_disconnect(17);
    auto d = f.on_disconnect(201);
    TEST_ASSERT_TRUE(d.log);
    TEST_ASSERT_EQUAL_UINT32(1, d.repeats);
    TEST_ASSERT_EQUAL_UINT8(17, d.previous_reason);
    TEST_ASSERT_EQUAL_UINT32(3, d.previous_count);
}

static void test_filter_connected_ends_run(void)
{
    DisconnectFilter f;
    uint8_t reason = 0;
    TEST_ASSERT_EQUAL_UINT32(0, f.on_connected(reason));
    f.on_disconnect(15);
    f.on_disconnect(15);
    TEST_ASSERT_EQUAL_UINT32(2, f.on_connected(reason));
    TEST_ASSERT_EQUAL_UINT8(15, reason);
    TEST_ASSERT_TRUE(f.on_disconnect(15).log); // a new run starts fresh
}

void run_wifi_diag_tests(void)
{
    RUN_TEST(test_reason_names);
    RUN_TEST(test_reason_hints);
    RUN_TEST(test_authmode_names);
    RUN_TEST(test_filter_logs_first_and_every_nth);
    RUN_TEST(test_filter_reason_change_ends_run);
    RUN_TEST(test_filter_connected_ends_run);
}
