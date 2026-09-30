#include "../test/test_native/components/test_ui_view_model.h"
#include "../test/test_native/context_input/test_button_input_adapter.h"
#include "../test/test_native/context_input/test_dispatch_result.h"
#include "../test/test_native/context_input/test_encoder_input_adapter.h"
#include "../test/test_native/context_input/test_event_batch.h"
#include "../test/test_native/context_input/test_input_event.h"
#include "../test/test_native/context_input/test_router.h"
#include "../test/test_native/context_input/test_trigger_input_adapter.h"
#include "../test/test_native/drivers/test_midi_usb_encoder.h"
#include "../test/test_native/engine/test_external_midi_clock.h"
#include "../test/test_native/engine/test_internal_tick.h"
#include "../test/test_native/engine/test_midi_clock_mode.h"
#include "../test/test_native/engine/test_midi_clock_transmitter.h"
#include "../test/test_native/engine/test_midi_event_queue.h"
#include "../test/test_native/engine/test_midi_pending_delivery_queue.h"
#include "../test/test_native/engine/test_runtime_timing_diagnostics.h"
#include "../test/test_native/engine/test_sequencer.h"
#include "../test/test_native/engine/test_transport.h"
#include "../test/test_native/engine/test_transport_controller.h"
#include "../test/test_native/input/test_app_event_handler.h"
#include "../test/test_native/input/test_app_input_coordinator.h"
#include "../test/test_native/input/test_app_ui_snapshot_builder.h"
#include "../test/test_native/input/test_encoder_integration.h"
#include "../test/test_native/input/test_encoder_sample_diagnostics.h"
#include "../test/test_native/input/test_main_display_context.h"
#include "../test/test_native/input/test_midi_clock_modal.h"
#include "../test/test_native/input/test_midi_clock_settings_context.h"
#include "../test/test_native/input/test_periodic_scheduler.h"
#include "../test/test_native/input/test_serial_run_command.h"
#include "../test/test_native/input/test_serial_run_controller.h"
#include "../test/test_native/input/test_step_button_integration.h"
#include "../test/test_native/input/test_time_debouncer.h"
#include "../test/test_native/program/test_program.h"
#include "../test/test_native/program/test_program_bank.h"
#include "../test/test_native/program/test_program_codec.h"
#include "../test/test_native/program/test_program_slot_store.h"
#include "../test/test_native/program/test_program_storage_controller.h"
#include "../test/test_native/program/test_program_storage_modal.h"
#include "../test/test_native/program/test_program_storage_request.h"
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
    test_midi_pending_delivery_queue_main();
    test_runtime_timing_diagnostics_main();
    test_sequencer_main();
    test_transport_main();
    test_transport_controller_main();
    test_midi_usb_encoder_main();
    test_input_event_main();
    test_dispatch_result_main();
    test_router_main();
    test_encoder_input_adapter_main();
    test_event_batch_main();
    test_button_input_adapter_main();
    test_trigger_input_adapter_main();
    test_main_display_context_main();
    test_midi_clock_modal_main();
    test_midi_clock_settings_context_main();
    test_periodic_scheduler_main();
    test_serial_run_command_main();
    testSerialRunControllerMain();
    test_app_event_handler_main();
    test_app_input_coordinator_main();
    test_app_ui_snapshot_builder_main();
    test_encoder_integration_main();
    test_encoder_sample_diagnostics_main();
    test_step_button_integration_main();
    test_time_debouncer_main();
    testProgramMain();
    testProgramBankMain();
    testProgramCodecMain();
    testProgramSlotStoreMain();
    testProgramStorageControllerMain();
    testProgramStorageModalMain();
    testProgramStorageRequestMain();

    UNITY_END();
}
