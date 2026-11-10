#include <unity.h>

#include "miot_queue.h"

using namespace miot;

static std::string pop_render(CommandQueue &q)
{
    auto c = q.pop();
    return c ? c->render() : std::string("<empty>");
}

static void test_fifo_order_and_render(void)
{
    CommandQueue q;
    q.push_set({ 2, 2 }, Value::of_bool(true));
    q.push_get({ { 2, 2 }, { 3, 6 } });
    q.push_action({ 4, 1 }, "3 0");
    q.push_action({ 8, 1 }, "");
    TEST_ASSERT_EQUAL_STRING("set_properties 2 2 true", pop_render(q).c_str());
    TEST_ASSERT_EQUAL_STRING("get_properties 2 2 3 6", pop_render(q).c_str());
    TEST_ASSERT_EQUAL_STRING("action 4 1 3 0", pop_render(q).c_str());
    TEST_ASSERT_EQUAL_STRING("action 8 1", pop_render(q).c_str());
    TEST_ASSERT_FALSE(q.pop().has_value());
}

static void test_set_replaces_waiting_value_in_place(void)
{
    CommandQueue q;
    q.push_set({ 2, 2 }, Value::of_bool(true));
    q.push_set({ 2, 5 }, Value::of_int(2));
    q.push_set({ 2, 2 }, Value::of_bool(false));
    TEST_ASSERT_EQUAL(2, q.size());
    TEST_ASSERT_EQUAL_STRING("set_properties 2 2 false", pop_render(q).c_str());
    TEST_ASSERT_EQUAL_STRING("set_properties 2 5 2", pop_render(q).c_str());
}

static void test_duplicate_get_not_queued(void)
{
    CommandQueue q;
    q.push_get({ { 2, 2 } });
    q.push_get({ { 2, 2 } });
    q.push_get({ { 3, 6 } });
    TEST_ASSERT_EQUAL(2, q.size());
    q.push_get({});
    TEST_ASSERT_EQUAL(2, q.size());
}

static void test_raw_front(void)
{
    CommandQueue q;
    q.push_set({ 2, 2 }, Value::of_bool(true));
    q.push_raw("MIIO_net_change cloud", true);
    TEST_ASSERT_EQUAL_STRING("MIIO_net_change cloud", pop_render(q).c_str());
}

static void test_capacity_drops_gets_first(void)
{
    CommandQueue q(3);
    q.push_set({ 2, 2 }, Value::of_bool(true));
    q.push_get({ { 3, 6 } });
    q.push_set({ 2, 5 }, Value::of_int(1));
    q.push_set({ 2, 4 }, Value::of_int(3)); // full: the get goes
    TEST_ASSERT_EQUAL(3, q.size());
    TEST_ASSERT_EQUAL(1, q.dropped());
    TEST_ASSERT_EQUAL_STRING("set_properties 2 2 true", pop_render(q).c_str());
    TEST_ASSERT_EQUAL_STRING("set_properties 2 5 1", pop_render(q).c_str());
    TEST_ASSERT_EQUAL_STRING("set_properties 2 4 3", pop_render(q).c_str());
}

static void test_capacity_drops_oldest_without_gets(void)
{
    CommandQueue q(2);
    q.push_set({ 1, 1 }, Value::of_int(1));
    q.push_set({ 1, 2 }, Value::of_int(2));
    q.push_set({ 1, 3 }, Value::of_int(3));
    TEST_ASSERT_EQUAL(2, q.size());
    TEST_ASSERT_EQUAL_STRING("set_properties 1 2 2", pop_render(q).c_str());
}

static void test_chunk_gets_by_count(void)
{
    std::vector<PropertyId> ids;
    for (uint16_t i = 0; i < 70; i++)
        ids.push_back({ 2, i });
    auto chunks = chunk_gets(ids, 32);
    TEST_ASSERT_EQUAL(3, chunks.size());
    TEST_ASSERT_EQUAL(32, chunks[0].size());
    TEST_ASSERT_EQUAL(32, chunks[1].size());
    TEST_ASSERT_EQUAL(6, chunks[2].size());
}

static void test_chunk_gets_by_length(void)
{
    std::vector<PropertyId> ids;
    for (int i = 0; i < 100; i++)
        ids.push_back({ 65535, 65535 });
    for (const auto &chunk : chunk_gets(ids, 1000))
    {
        Command c;
        c.kind = Command::Kind::Get;
        c.gets = chunk;
        TEST_ASSERT_TRUE(("down " + c.render()).size() <= MAX_LINE_LENGTH);
    }
}

void run_queue_tests(void)
{
    RUN_TEST(test_fifo_order_and_render);
    RUN_TEST(test_set_replaces_waiting_value_in_place);
    RUN_TEST(test_duplicate_get_not_queued);
    RUN_TEST(test_raw_front);
    RUN_TEST(test_capacity_drops_gets_first);
    RUN_TEST(test_capacity_drops_oldest_without_gets);
    RUN_TEST(test_chunk_gets_by_count);
    RUN_TEST(test_chunk_gets_by_length);
}
