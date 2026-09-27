#include "test_serial_run_controller.h"
#include "input/serial_run_controller.h"
#include <fstream>
#include <sstream>
#include <string>
#include <unity.h>
#include <vector>

namespace {
using namespace SwingMetro;

class TestConsole final : public DiagnosticConsole {
  public:
    std::string input;
    std::string output;
    std::size_t cursor = 0;
    std::uint32_t timeMs = 0;
    void print(const char* text) override { output += text; }
    void print(std::uint32_t value) override { output += std::to_string(value); }
    void print(char character) override { output += character; }
    int read() override { return cursor < input.size() ? input[cursor++] : -1; }
    void flush() override {}
    std::uint32_t nowMs() override { return timeMs; }
    std::uint32_t nowUs() override { return timeMs * 1000U; }
};

class AcceptingSink final : public MidiMessageSink {
  public:
    SendResult send(const MidiDeliveryAttempt&) override { return SendResult::Accepted; }
};

struct RunFixture {
    TestConsole console;
    Sequencer sequencer;
    MidiClockSettings settings;
    AcceptingSink sink;
    FaultMidiMessageSink fault{sink};
    TransportController transport{sequencer, settings, sink};
    InternalTickSource ticks;
    EncoderSampleDiagnostics encoder;
    RuntimeTimingDiagnostics runtime;
    DiagnosticsSerializer serializer{console};
    DiagnosticsCapture capture{transport, ticks, encoder, runtime, serializer};
    Counter<std::uint8_t> tempo{{.step = 1,
                                 .value = 120,
                                 .minValue = 40,
                                 .maxValue = 240,
                                 .overflowBehavior = CounterOverflowBehavior::Clamp}};
    Counter<std::uint8_t> swing{{.step = 1,
                                 .value = 50,
                                 .minValue = 50,
                                 .maxValue = 95,
                                 .overflowBehavior = CounterOverflowBehavior::Clamp}};
    SerialRunController controller{console, sequencer, settings, transport, tempo,
                                   swing,   runtime,   encoder,  capture,   &fault};
};

std::vector<std::string> splitFields(const std::string& row) {
    std::vector<std::string> fields;
    std::istringstream input(row);
    std::string field;
    while (std::getline(input, field, ',')) {
        fields.push_back(field);
    }
    return fields;
}

void testSerializerPreservesFrozenHeadersAndValues() {
    TestConsole console;
    DiagnosticsSerializer serializer(console);
    TickPipelineDiagnostics pipeline;
    TransportDiagnostics transport;
    EncoderSampleDiagnostics encoder;
    pipeline.producer.failedPublications = 11;
    pipeline.producer.tickQueueOverflows = 22;
    transport.delivery[0].attempts = 31;
    transport.delivery[0].accepted = 29;
    serializer.exportInternalTimingDiagnostics(pipeline, transport, {}, {}, encoder);
    std::istringstream output(console.output);
    std::string headers[3];
    const char* fixtureNames[] = {"diagnostics.csv", "input-diagnostics.csv",
                                  "runtime-diagnostics.csv"};
    for (std::size_t i = 0; i < 3; ++i) {
        std::getline(output, headers[i]);
        TEST_ASSERT_EQUAL_CHAR('\r', headers[i].back());
        headers[i].pop_back();
        std::ifstream fixture(std::string("scripts/tests/fixtures/refactor_baseline_v4/") +
                              fixtureNames[i]);
        TEST_ASSERT_TRUE(fixture.good());
        std::string reference;
        std::getline(fixture, reference);
        TEST_ASSERT_EQUAL_STRING(reference.c_str(), headers[i].c_str());
    }
    std::string row;
    std::getline(output, row);
    row.pop_back();
    const auto names = splitFields(headers[0]);
    const auto values = splitFields(row);
    TEST_ASSERT_EQUAL(names.size(), values.size());
    const auto field = [&](const char* name) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (names[i] == name) {
                return values[i];
            }
        }
        return std::string("missing");
    };
    TEST_ASSERT_EQUAL_STRING("11", field("failed_publications").c_str());
    // Preserve the historical alias; the dedicated v4 tail holds actual overflows.
    TEST_ASSERT_EQUAL_STRING("11", field("tick_queue_overflows").c_str());
    TEST_ASSERT_EQUAL_STRING("22", field("internal_tick_queue_overflows").c_str());
    TEST_ASSERT_EQUAL_STRING("31", field("delivery_clock_attempts").c_str());
    TEST_ASSERT_EQUAL_STRING("29", field("delivery_clock_accepted").c_str());
    console.output.clear();
    serializer.exportInternalTimingDiagnostics(pipeline, transport, {}, {}, encoder);
    TEST_ASSERT_EQUAL(std::string::npos, console.output.find("alarm_callback_invocations"));
}

void testInternalRunCompletesOnceAfterStoppedSnapshotAndAlarm() {
    RunFixture f;
    f.console.input = "RUN 1000 68 75\n";
    f.controller.pollSerialRunCommand();
    TEST_ASSERT_EQUAL_STRING("swing_metro_control_v1,run_started,1000,68,75\r\n",
                             f.console.output.c_str());
    TEST_ASSERT_TRUE(f.sequencer.isRunning());
    TEST_ASSERT_EQUAL_UINT8(68, f.tempo.getValue());
    f.console.timeMs = 999;
    f.controller.updateSerialRun();
    TEST_ASSERT_TRUE(f.sequencer.isRunning());
    f.console.timeMs = 1000;
    f.controller.updateSerialRun();
    TEST_ASSERT_FALSE(f.sequencer.isRunning());
    f.capture.requestExport(); // The alarm-stop branch owns this in main.
    f.controller.completeIfReady(false);
    TEST_ASSERT_EQUAL(std::string::npos, f.console.output.find("run_complete"));
    f.capture.exportIfReady();
    TEST_ASSERT_TRUE(f.capture.pending());
    f.runtime.publishRequestedSnapshot();
    f.capture.exportIfReady();
    TEST_ASSERT_FALSE(f.capture.pending());
    f.controller.completeIfReady(true);
    TEST_ASSERT_EQUAL(std::string::npos, f.console.output.find("run_complete"));
    f.controller.completeIfReady(false);
    const auto output = f.console.output;
    TEST_ASSERT_TRUE(output.find("swing_metro_runtime_diagnostics_v1,0") <
                     output.find("run_complete"));
    f.controller.completeIfReady(false);
    TEST_ASSERT_EQUAL_STRING(output.c_str(), f.console.output.c_str());
}

void testExternalRunWaitsForCoreOneAndDoesNotStartTransport() {
    RunFixture f;
    f.transport.applyMode(MidiClockMode::External, 0);
    f.console.input = "EXTERNAL_RUN 1000 120 50\n";
    f.controller.pollSerialRunCommand();
    f.controller.updateSerialRun();
    TEST_ASSERT_TRUE(f.console.output.empty());
    f.runtime.publishRequestedSnapshot();
    f.controller.updateSerialRun();
    TEST_ASSERT_EQUAL_STRING("swing_metro_control_v1,run_started,1000,120,50\r\n",
                             f.console.output.c_str());
    TEST_ASSERT_FALSE(f.sequencer.isRunning());
    f.console.timeMs = 1000;
    f.controller.updateSerialRun();
    TEST_ASSERT_TRUE(f.capture.pending());
    f.controller.completeIfReady(false);
    TEST_ASSERT_EQUAL(std::string::npos, f.console.output.find("run_complete"));
    f.runtime.publishRequestedSnapshot();
    f.capture.exportIfReady();
    f.controller.completeIfReady(false);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, f.console.output.find("run_complete"));
}

void testExternalModeErrorsNeverCompleteSuccessfulRun() {
    RunFixture f;
    f.console.input = "EXTERNAL_RUN 1000 120 50\n";
    f.controller.pollSerialRunCommand();
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          f.console.output.find("external_run_requires_external_mode"));
    f.transport.applyMode(MidiClockMode::External, 0);
    f.console.input += "EXTERNAL_RUN 1000 120 50\n";
    f.controller.pollSerialRunCommand();
    f.runtime.publishRequestedSnapshot();
    f.controller.updateSerialRun();
    f.transport.applyMode(MidiClockMode::Internal, 0);
    f.console.timeMs = 1000;
    f.controller.updateSerialRun();
    f.controller.completeIfReady(false);
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          f.console.output.find("external_mode_changed_during_run"));
    TEST_ASSERT_EQUAL(std::string::npos, f.console.output.find("run_complete"));
}

void testExportDefersWhilePlayingAndBlocksNewRun() {
    RunFixture f;
    f.transport.toggle(0);
    f.capture.requestExport();
    f.runtime.publishRequestedSnapshot();
    f.capture.exportIfReady();
    TEST_ASSERT_TRUE(f.capture.pending());
    TEST_ASSERT_TRUE(f.console.output.empty());
    f.transport.toggle(1);
    f.console.input = "RUN 1000 120 50\n";
    f.controller.pollSerialRunCommand();
    TEST_ASSERT_EQUAL_UINT32(0, f.console.cursor);
    f.capture.exportIfReady();
    f.controller.pollSerialRunCommand();
    TEST_ASSERT_TRUE(f.sequencer.isRunning());
}

void testFaultSelectionPrecedesRunAndMillisWrapIsSafe() {
    RunFixture f;
    f.console.timeMs = UINT32_MAX - 499U;
    f.console.input = "FAULT retry_first_clock\nRUN 1000 120 50\n";
    f.controller.pollSerialRunCommand();
    TEST_ASSERT_EQUAL(FaultScenario::RetryFirstClock, f.fault.scenario());
    TEST_ASSERT_TRUE(f.console.output.find("swing_metro_fault_v1,selected,retry_first_clock") <
                     f.console.output.find("run_started"));
    f.console.timeMs += 1000;
    f.controller.updateSerialRun();
    TEST_ASSERT_FALSE(f.sequencer.isRunning());
}
} // namespace

void testSerialRunControllerMain() {
    RUN_TEST(testSerializerPreservesFrozenHeadersAndValues);
    RUN_TEST(testInternalRunCompletesOnceAfterStoppedSnapshotAndAlarm);
    RUN_TEST(testExternalRunWaitsForCoreOneAndDoesNotStartTransport);
    RUN_TEST(testExternalModeErrorsNeverCompleteSuccessfulRun);
    RUN_TEST(testExportDefersWhilePlayingAndBlocksNewRun);
    RUN_TEST(testFaultSelectionPrecedesRunAndMillisWrapIsSafe);
}
