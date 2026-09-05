#include <unity.h>

#include "utils/round_buffer.h"

void test_round_buffer_add_and_get() {
    RoundBuffer<int> buffer(3);

    buffer.add(1);
    buffer.add(2);
    buffer.add(3);

    TEST_ASSERT_EQUAL(1, buffer.get(0));
    TEST_ASSERT_EQUAL(2, buffer.get(1));
    TEST_ASSERT_EQUAL(3, buffer.get(2));

    // Adding more elements should overwrite the oldest ones
    buffer.add(4);
    TEST_ASSERT_EQUAL(2, buffer.get(0));
    TEST_ASSERT_EQUAL(3, buffer.get(1));
    TEST_ASSERT_EQUAL(4, buffer.get(2));
}

void test_round_buffer_peek() {
    RoundBuffer<int> buffer(3);

    buffer.add(1);
    buffer.add(2);

    TEST_ASSERT_EQUAL(1, buffer.peek());

    buffer.add(3);
    TEST_ASSERT_EQUAL(1, buffer.peek());

    buffer.add(4);
    TEST_ASSERT_EQUAL(2, buffer.peek());
}

void test_round_buffer_count() {
    RoundBuffer<int> buffer(3);

    TEST_ASSERT_EQUAL(0, buffer.count());

    buffer.add(1);
    TEST_ASSERT_EQUAL(1, buffer.count());

    buffer.add(2);
    TEST_ASSERT_EQUAL(2, buffer.count());

    buffer.add(3);
    TEST_ASSERT_EQUAL(3, buffer.count());

    // Adding more elements should not increase the count beyond the size
    buffer.add(4);
    TEST_ASSERT_EQUAL(3, buffer.count());
}

void test_round_buffer_get_negative_index() {
    RoundBuffer<int> buffer(3);

    buffer.add(1);
    buffer.add(2);
    buffer.add(3);

    TEST_ASSERT_EQUAL(3, buffer.get(-1));
    TEST_ASSERT_EQUAL(2, buffer.get(-2));
    TEST_ASSERT_EQUAL(1, buffer.get(-3));
}

void test_round_buffer_get_from_tail_and_head() {
    RoundBuffer<int> buffer(3);

    buffer.add(1);
    buffer.add(2);
    buffer.add(3);

    TEST_ASSERT_EQUAL(1, buffer.getFromTail(0));
    TEST_ASSERT_EQUAL(2, buffer.getFromTail(1));
    TEST_ASSERT_EQUAL(3, buffer.getFromTail(2));
    TEST_ASSERT_EQUAL(3, buffer.getFromHead(0));
    TEST_ASSERT_EQUAL(2, buffer.getFromHead(1));
    TEST_ASSERT_EQUAL(1, buffer.getFromHead(2));
}

void test_round_buffer_main() {
    // UNITY_BEGIN();

    RUN_TEST(test_round_buffer_add_and_get);
    RUN_TEST(test_round_buffer_peek);
    RUN_TEST(test_round_buffer_count);
    RUN_TEST(test_round_buffer_get_negative_index);
    RUN_TEST(test_round_buffer_get_from_tail_and_head);

    // return UNITY_END();
}
