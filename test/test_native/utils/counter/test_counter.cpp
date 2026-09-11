#include <unity.h>

#include "utils/counter.h"

void test_counter_stepUp() {
    Counter<int> counter({.step = 1,
                          .value = 0,
                          .minValue = 0,
                          .maxValue = 5,
                          .overflowBehavior = CounterOverflowBehavior::Clamp});

    counter.stepUp();
    TEST_ASSERT_EQUAL(1, counter.getValue());

    counter.stepUp();
    TEST_ASSERT_EQUAL(2, counter.getValue());

    counter.stepUp();
    TEST_ASSERT_EQUAL(3, counter.getValue());

    counter.stepUp();
    TEST_ASSERT_EQUAL(4, counter.getValue());

    counter.stepUp();
    TEST_ASSERT_EQUAL(5, counter.getValue());

    // stepUping beyond max should clamp to max
    counter.stepUp();
    TEST_ASSERT_EQUAL(5, counter.getValue());
}

void test_counter_stepDown() {
    Counter<int> counter({.step = 1,
                          .value = 5,
                          .minValue = 0,
                          .maxValue = 5,
                          .overflowBehavior = CounterOverflowBehavior::Clamp});

    counter.stepDown();
    TEST_ASSERT_EQUAL(4, counter.getValue());

    counter.stepDown();
    TEST_ASSERT_EQUAL(3, counter.getValue());

    counter.stepDown();
    TEST_ASSERT_EQUAL(2, counter.getValue());

    counter.stepDown();
    TEST_ASSERT_EQUAL(1, counter.getValue());

    counter.stepDown();
    TEST_ASSERT_EQUAL(0, counter.getValue());

    // stepDowning below min should clamp to min
    counter.stepDown();
    TEST_ASSERT_EQUAL(0, counter.getValue());
}

void test_counter_wrap_around() {
    Counter<int> counter({.step = 1,
                          .value = 5,
                          .minValue = 0,
                          .maxValue = 5,
                          .overflowBehavior = CounterOverflowBehavior::WrapAround});

    counter.stepUp();
    TEST_ASSERT_EQUAL(0, counter.getValue());

    counter.stepDown();
    TEST_ASSERT_EQUAL(5, counter.getValue());

    counter.stepUp();
    TEST_ASSERT_EQUAL(0, counter.getValue());
}

void test_counter_step() {
    Counter<int> counter({.step = 2,
                          .value = 0,
                          .minValue = 0,
                          .maxValue = 5,
                          .overflowBehavior = CounterOverflowBehavior::Clamp});

    counter.stepUp();
    TEST_ASSERT_EQUAL(2, counter.getValue());

    counter.stepUp();
    TEST_ASSERT_EQUAL(4, counter.getValue());

    // stepUping beyond max should clamp to max
    counter.stepUp();
    TEST_ASSERT_EQUAL(5, counter.getValue());
}

void test_counter_main() {
    // UNITY_BEGIN();

    RUN_TEST(test_counter_stepUp);
    RUN_TEST(test_counter_stepDown);
    RUN_TEST(test_counter_wrap_around);
    RUN_TEST(test_counter_step);

    // return UNITY_END();
}
