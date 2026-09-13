#include "../test/test_native/components/test_ui_view_model.h"
#include "../test/test_native/context_input/test_button_input_adapter.h"
#include "../test/test_native/context_input/test_dispatch_result.h"
#include "../test/test_native/context_input/test_encoder_input_adapter.h"
#include "../test/test_native/context_input/test_event_batch.h"
#include "../test/test_native/context_input/test_input_event.h"
#include "../test/test_native/context_input/test_router.h"
#include "../test/test_native/context_input/test_trigger_input_adapter.h"
#include "../test/test_native/engine/test_external_midi_clock.h"
#include "../test/test_native/engine/test_internal_tick.h"
#include "../test/test_native/engine/test_midi_clock_mode.h"
#include "../test/test_native/engine/test_midi_clock_transmitter.h"
#include "../test/test_native/engine/test_midi_event_queue.h"
#include "../test/test_native/engine/test_transport.h"
#include "../test/test_native/input/test_app_event_handler.h"
#include "../test/test_native/input/test_app_input_coordinator.h"
#include "../test/test_native/input/test_encoder_integration.h"
#include "../test/test_native/input/test_main_display_context.h"
#include "../test/test_native/input/test_midi_clock_settings_context.h"
#include "../test/test_native/input/test_step_button_integration.h"
#include "../test/test_native/program/test_program.h"
#include "../test/test_native/program/test_program_codec.h"
#include "../test/test_native/program/test_program_slot_store.h"
#include "../test/test_native/program/test_program_storage_controller.h"
#include "../test/test_native/utils/counter/test_counter.h"
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
    test_ui_view_model_main();
    test_midi_clock_mode_main();
    test_external_midi_clock_main();
    test_internal_tick_main();
    test_midi_clock_transmitter_main();
    test_midi_event_queue_main();
    test_transport_main();
    test_input_event_main();
    test_dispatch_result_main();
    test_router_main();
    test_encoder_input_adapter_main();
    test_event_batch_main();
    test_button_input_adapter_main();
    test_trigger_input_adapter_main();
    test_main_display_context_main();
    test_midi_clock_settings_context_main();
    test_app_event_handler_main();
    test_app_input_coordinator_main();
    test_encoder_integration_main();
    test_step_button_integration_main();
    testProgramMain();
    testProgramCodecMain();
    testProgramSlotStoreMain();
    testProgramStorageControllerMain();

    UNITY_END();
}
