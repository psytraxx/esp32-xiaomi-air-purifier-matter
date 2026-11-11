#include <unity.h>

#include "miot_protocol.h"

using namespace miot;

static void test_tokenize_plain(void)
{
    auto t = tokenize("properties_changed 2 2 true");
    TEST_ASSERT_EQUAL(4, t.size());
    TEST_ASSERT_EQUAL_STRING("properties_changed", t[0].text.c_str());
    TEST_ASSERT_EQUAL_STRING("true", t[3].text.c_str());
    TEST_ASSERT_FALSE(t[3].quoted);
}

static void test_tokenize_collapses_spaces(void)
{
    auto t = tokenize("  get_down   ");
    TEST_ASSERT_EQUAL(1, t.size());
    TEST_ASSERT_EQUAL_STRING("get_down", t[0].text.c_str());
    TEST_ASSERT_EQUAL(0, tokenize("").size());
    TEST_ASSERT_EQUAL(0, tokenize("    ").size());
}

static void test_tokenize_quoted_string_with_spaces(void)
{
    auto t = tokenize("error \"bad thing happened\" -5001");
    TEST_ASSERT_EQUAL(3, t.size());
    TEST_ASSERT_TRUE(t[1].quoted);
    TEST_ASSERT_EQUAL_STRING("bad thing happened", t[1].text.c_str());
    TEST_ASSERT_EQUAL_STRING("-5001", t[2].text.c_str());
}

static void test_tokenize_escaped_quote_and_unterminated(void)
{
    auto t = tokenize("x \"a\\\"b\" \"open end");
    TEST_ASSERT_EQUAL(3, t.size());
    TEST_ASSERT_EQUAL_STRING("a\"b", t[1].text.c_str());
    TEST_ASSERT_EQUAL_STRING("open end", t[2].text.c_str());
    TEST_ASSERT_TRUE(t[2].quoted);
}

static void test_tokenize_empty_quoted(void)
{
    auto t = tokenize("model \"\"");
    TEST_ASSERT_EQUAL(2, t.size());
    TEST_ASSERT_TRUE(t[1].quoted);
    TEST_ASSERT_EQUAL_STRING("", t[1].text.c_str());
}

static void test_value_parse_types(void)
{
    TEST_ASSERT_TRUE(Value::parse("true", false).type() == Value::Type::Bool);
    TEST_ASSERT_TRUE(*Value::parse("true", false).as_bool());
    TEST_ASSERT_FALSE(*Value::parse("false", false).as_bool());

    Value i = Value::parse("-12", false);
    TEST_ASSERT_TRUE(i.type() == Value::Type::Int);
    TEST_ASSERT_EQUAL_INT64(-12, *i.as_int());

    Value f = Value::parse("21.5", false);
    TEST_ASSERT_TRUE(f.type() == Value::Type::Float);
    TEST_ASSERT_EQUAL_DOUBLE(21.5, *f.as_float());
    TEST_ASSERT_EQUAL_INT64(22, *f.as_int()); // rounds

    Value s = Value::parse("12", true);
    TEST_ASSERT_TRUE(s.type() == Value::Type::String);
    TEST_ASSERT_FALSE(s.as_int().has_value());

    Value bare = Value::parse("abc", false);
    TEST_ASSERT_TRUE(bare.type() == Value::Type::String);
}

static void test_value_rejects_nan_inf_hex(void)
{
    TEST_ASSERT_TRUE(Value::parse("nan", false).type() == Value::Type::String);
    TEST_ASSERT_TRUE(Value::parse("inf", false).type() == Value::Type::String);
    TEST_ASSERT_TRUE(Value::parse("0x10", false).type() == Value::Type::String);
}

static void test_value_to_wire(void)
{
    TEST_ASSERT_EQUAL_STRING("true", Value::of_bool(true).to_wire().c_str());
    TEST_ASSERT_EQUAL_STRING("false", Value::of_bool(false).to_wire().c_str());
    TEST_ASSERT_EQUAL_STRING("14", Value::of_int(14).to_wire().c_str());
    TEST_ASSERT_EQUAL_STRING("-3", Value::of_int(-3).to_wire().c_str());
    TEST_ASSERT_EQUAL_STRING("21.5", Value::of_float(21.5).to_wire().c_str());
    TEST_ASSERT_EQUAL_STRING("\"a \\\"q\\\"\"", Value::of_string("a \"q\"").to_wire().c_str());
}

static void test_value_bool_reads_numbers(void)
{
    TEST_ASSERT_TRUE(*Value::of_int(1).as_bool());
    TEST_ASSERT_FALSE(*Value::of_int(0).as_bool());
    TEST_ASSERT_EQUAL_INT64(1, *Value::of_bool(true).as_int());
}

static void test_parse_id(void)
{
    TEST_ASSERT_EQUAL_UINT16(13, *parse_id("13"));
    TEST_ASSERT_FALSE(parse_id("-1").has_value());
    TEST_ASSERT_FALSE(parse_id("70000").has_value());
    TEST_ASSERT_FALSE(parse_id("1a").has_value());
    TEST_ASSERT_FALSE(parse_id("").has_value());
    TEST_ASSERT_FALSE(parse_id("+2").has_value());
}

void run_protocol_tests(void)
{
    RUN_TEST(test_tokenize_plain);
    RUN_TEST(test_tokenize_collapses_spaces);
    RUN_TEST(test_tokenize_quoted_string_with_spaces);
    RUN_TEST(test_tokenize_escaped_quote_and_unterminated);
    RUN_TEST(test_tokenize_empty_quoted);
    RUN_TEST(test_value_parse_types);
    RUN_TEST(test_value_rejects_nan_inf_hex);
    RUN_TEST(test_value_to_wire);
    RUN_TEST(test_value_bool_reads_numbers);
    RUN_TEST(test_parse_id);
}
