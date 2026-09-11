#include "test_event_batch.h"

#include <context_input.h>
#include <cstdint>
#include <type_traits>
#include <unity.h>

namespace {

struct NonDefaultEvent {
    explicit NonDefaultEvent(std::int32_t newValue) : value{newValue} {}

    std::int32_t value;
};

using TestBatch = ContextInput::EventBatch<NonDefaultEvent, 2>;

static_assert(TestBatch::capacity() == 2);
static_assert(std::is_default_constructible_v<TestBatch>);

void test_empty_batch_has_zero_size() {
    const TestBatch batch;

    TEST_ASSERT_TRUE(batch.empty());
    TEST_ASSERT_EQUAL_UINT32(0, batch.size());
    TEST_ASSERT_EQUAL_UINT32(2, batch.capacity());
}

void test_batch_preserves_event_order() {
    const TestBatch batch{NonDefaultEvent{11}, NonDefaultEvent{22}};

    TEST_ASSERT_FALSE(batch.empty());
    TEST_ASSERT_EQUAL_UINT32(2, batch.size());
    TEST_ASSERT_EQUAL_INT32(11, batch[0].value);
    TEST_ASSERT_EQUAL_INT32(22, batch[1].value);
}

void test_batch_supports_fewer_events_than_capacity() {
    const ContextInput::EventBatch<NonDefaultEvent, 3> batch{NonDefaultEvent{7}};

    TEST_ASSERT_EQUAL_UINT32(1, batch.size());
    TEST_ASSERT_EQUAL_UINT32(3, batch.capacity());
    TEST_ASSERT_EQUAL_INT32(7, batch[0].value);
}

void test_batch_copy_preserves_occupied_events() {
    TestBatch original{NonDefaultEvent{31}, NonDefaultEvent{32}};

    const auto copy = original;

    TEST_ASSERT_EQUAL_UINT32(2, copy.size());
    TEST_ASSERT_EQUAL_INT32(31, copy[0].value);
    TEST_ASSERT_EQUAL_INT32(32, copy[1].value);
}

} // namespace

void test_event_batch_main() {
    RUN_TEST(test_empty_batch_has_zero_size);
    RUN_TEST(test_batch_preserves_event_order);
    RUN_TEST(test_batch_supports_fewer_events_than_capacity);
    RUN_TEST(test_batch_copy_preserves_occupied_events);
}
