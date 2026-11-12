#include <unity.h>

#include "device_identity.h"

using namespace identity;

static void test_passcodes(void)
{
    TEST_ASSERT_TRUE(is_valid_passcode(79983961));
    TEST_ASSERT_TRUE(is_valid_passcode(1));
    TEST_ASSERT_TRUE(is_valid_passcode(99999998));
    TEST_ASSERT_FALSE(is_valid_passcode(0));
    TEST_ASSERT_FALSE(is_valid_passcode(99999999));
    TEST_ASSERT_FALSE(is_valid_passcode(100000000));
    TEST_ASSERT_FALSE(is_valid_passcode(12345678));
    TEST_ASSERT_FALSE(is_valid_passcode(87654321));
    TEST_ASSERT_FALSE(is_valid_passcode(33333333));
    static_assert(is_valid_passcode(20202021), "constexpr usable at compile time");
}

static void test_discriminators(void)
{
    TEST_ASSERT_TRUE(is_valid_discriminator(0));
    TEST_ASSERT_TRUE(is_valid_discriminator(0x8CA));
    TEST_ASSERT_TRUE(is_valid_discriminator(0xFFF));
    TEST_ASSERT_FALSE(is_valid_discriminator(0x1000));
}

static void test_node_label_default_per_model(void)
{
    TEST_ASSERT_EQUAL_STRING("Air Purifier 3H", node_label("", "Air Purifier", purifier::Model::Mb3).c_str());
    TEST_ASSERT_EQUAL_STRING("Air Purifier 3C", node_label("", "Air Purifier", purifier::Model::Mb4).c_str());
    TEST_ASSERT_EQUAL_STRING("Air Purifier 3H", node_label("   ", "Air Purifier", purifier::Model::Mb3).c_str());
}

static void test_node_label_configured(void)
{
    TEST_ASSERT_EQUAL_STRING("Bedroom purifier", node_label("  Bedroom purifier ", "Air Purifier", purifier::Model::Mb3).c_str());
    // Cut to 32 bytes.
    std::string long_name(40, 'x');
    TEST_ASSERT_EQUAL(32, node_label(long_name, "Air Purifier", purifier::Model::Mb3).size());
    // Never split a multi-byte character: 31 ASCII + "ü" (2 bytes) → 31 bytes.
    std::string umlaut = std::string(31, 'a') + "\xC3\xBC";
    TEST_ASSERT_EQUAL(31, node_label(umlaut, "Air Purifier", purifier::Model::Mb3).size());
}

static void test_should_write_label(void)
{
    const char *AUTO = "Air Purifier 3H";
    // First boot: nothing set yet.
    TEST_ASSERT_TRUE(should_write_label("", AUTO, "", AUTO));
    // Unchanged.
    TEST_ASSERT_FALSE(should_write_label(AUTO, AUTO, AUTO, AUTO));
    // Renamed in a controller: kept, also when the menuconfig name changes.
    TEST_ASSERT_FALSE(should_write_label("Kitchen", AUTO, AUTO, AUTO));
    TEST_ASSERT_FALSE(should_write_label("Kitchen", "Bedroom purifier", AUTO, AUTO));
    // Our own label, new menuconfig name: updated.
    TEST_ASSERT_TRUE(should_write_label("Bedroom purifier", "Living room", "Bedroom purifier", AUTO));
    // Upgrade from firmware that wrote the automatic label but didn't record it.
    TEST_ASSERT_TRUE(should_write_label(AUTO, "Bedroom purifier", "", AUTO));
    // Back to the automatic label when the menuconfig name is cleared.
    TEST_ASSERT_TRUE(should_write_label("Bedroom purifier", AUTO, "Bedroom purifier", AUTO));
}

void run_identity_tests(void)
{
    RUN_TEST(test_should_write_label);
    RUN_TEST(test_passcodes);
    RUN_TEST(test_discriminators);
    RUN_TEST(test_node_label_default_per_model);
    RUN_TEST(test_node_label_configured);
}
