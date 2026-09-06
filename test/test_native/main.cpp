#include <unity.h>
#include "../test/test_native/components/test_ui_view_model.h"
#include "../test/test_native/utils/counter/test_counter.h"
#include "../test/test_native/utils/round_buffer/test_round_buffer.h"

void setUp() {
    // Set up code here
}

void tearDown() {
    // Tear down code here
}

int main() {
    UNITY_BEGIN();

    test_counter_main();
    test_round_buffer_main();
    test_ui_view_model_main();

    UNITY_END();
}
