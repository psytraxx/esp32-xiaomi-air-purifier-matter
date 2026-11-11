#include <unity.h>

#include "ota_policy.h"

using namespace ota_policy;

static void test_verdict(void)
{
    TEST_ASSERT_TRUE(verdict(0, true, true, true) == Verdict::Keep);
    TEST_ASSERT_TRUE(verdict(VERIFY_TIMEOUT_MS + 1, true, true, true) == Verdict::Keep);
    TEST_ASSERT_TRUE(verdict(1000, true, true, false) == Verdict::Wait);
    TEST_ASSERT_TRUE(verdict(1000, false, true, true) == Verdict::Wait);
    TEST_ASSERT_TRUE(verdict(1000, true, false, true) == Verdict::Wait);
    TEST_ASSERT_TRUE(verdict(VERIFY_TIMEOUT_MS - 1, true, true, false) == Verdict::Wait);
    TEST_ASSERT_TRUE(verdict(VERIFY_TIMEOUT_MS, true, true, false) == Verdict::RollBack);
    TEST_ASSERT_TRUE(verdict(VERIFY_TIMEOUT_MS, false, false, false) == Verdict::RollBack);
}

static void test_token(void)
{
    TEST_ASSERT_TRUE(token_matches(nullptr, ""));
    TEST_ASSERT_TRUE(token_matches("anything", ""));
    TEST_ASSERT_TRUE(token_matches("Bearer s3cret", "s3cret"));
    TEST_ASSERT_FALSE(token_matches(nullptr, "s3cret"));
    TEST_ASSERT_FALSE(token_matches("", "s3cret"));
    TEST_ASSERT_FALSE(token_matches("s3cret", "s3cret"));
    TEST_ASSERT_FALSE(token_matches("Bearer s3cre", "s3cret"));
    TEST_ASSERT_FALSE(token_matches("Bearer s3cret2", "s3cret"));
    TEST_ASSERT_FALSE(token_matches("Bearer S3cret", "s3cret"));
    TEST_ASSERT_FALSE(token_matches("bearer s3cret", "s3cret"));
}

void run_ota_policy_tests(void)
{
    RUN_TEST(test_verdict);
    RUN_TEST(test_token);
}
