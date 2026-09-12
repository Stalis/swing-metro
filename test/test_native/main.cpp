#include "../test/test_native/components/test_ui_view_model.h"
#include "../test/test_native/context_input/test_button_input_adapter.h"
#include "../test/test_native/context_input/test_dispatch_result.h"
#include "../test/test_native/context_input/test_encoder_input_adapter.h"
#include "../test/test_native/context_input/test_event_batch.h"
#include "../test/test_native/context_input/test_input_event.h"
#include "../test/test_native/context_input/test_router.h"
#include "../test/test_native/context_input/test_trigger_input_adapter.h"
#include "../test/test_native/input/test_app_event_handler.h"
#include "../test/test_native/input/test_app_input_coordinator.h"
#include "../test/test_native/input/test_encoder_integration.h"
#include "../test/test_native/input/test_main_display_context.h"
#include "../test/test_native/input/test_step_button_integration.h"
#include "../test/test_native/utils/counter/test_counter.h"
#include "../test/test_native/utils/round_buffer/test_round_buffer.h"
#include <unity.h>

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
    test_input_event_main();
    test_dispatch_result_main();
    test_router_main();
    test_encoder_input_adapter_main();
    test_event_batch_main();
    test_button_input_adapter_main();
    test_trigger_input_adapter_main();
    test_main_display_context_main();
    test_app_event_handler_main();
    test_app_input_coordinator_main();
    test_encoder_integration_main();
    test_step_button_integration_main();

    UNITY_END();
}
