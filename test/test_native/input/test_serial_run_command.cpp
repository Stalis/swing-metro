#include "test_serial_run_command.h"

#include "input/fault_command_parser.h"
#include "input/serial_run_command.h"

#include <unity.h>

namespace {

auto pushLine(SwingMetro::SerialRunCommandParser& parser, const char* line)
    -> SwingMetro::SerialRunCommandResult {
    SwingMetro::SerialRunCommandResult result;
    while (*line != '\0') {
        result = parser.push(*line++);
    }
    return parser.push('\n');
}

auto pushFaultLine(SwingMetro::FaultCommandParser& parser, const char* line)
    -> SwingMetro::FaultCommandResult {
    SwingMetro::FaultCommandResult result;
    while (*line != '\0') {
        result = parser.push(*line++);
    }
    return parser.push('\n');
}

void test_run_command_parses_duration_tempo_and_swing() {
    SwingMetro::SerialRunCommandParser parser;
    const auto result = pushLine(parser, "RUN 244000 68 50");

    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::SerialRunCommandStatus::Ready),
                          static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_UINT32(244'000, result.command.durationMs);
    TEST_ASSERT_EQUAL_UINT8(68, result.command.bpm);
    TEST_ASSERT_EQUAL_UINT8(50, result.command.swing);
}

void test_run_command_accepts_crlf_and_documented_bounds() {
    SwingMetro::SerialRunCommandParser parser;
    for (const char* byte = "RUN 1000 40 90\r"; *byte != '\0'; ++byte) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::SerialRunCommandStatus::Pending),
                              static_cast<int>(parser.push(*byte).status));
    }
    const auto result = parser.push('\n');
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::SerialRunCommandStatus::Ready),
                          static_cast<int>(result.status));
}

void test_run_command_rejects_invalid_or_out_of_range_input() {
    SwingMetro::SerialRunCommandParser parser;
    const char* invalid[] = {
        "PING",
        "RUN",
        "RUN 999 68 50",
        "RUN 1000 39 50",
        "RUN 1000 68 49",
        "RUN 1000 68 91",
        "RUN 1000 68 50 extra",
        "RUN 999999999999999999999 68 50",
    };
    for (const auto* line : invalid) {
        TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::SerialRunCommandStatus::Invalid),
                              static_cast<int>(pushLine(parser, line).status));
    }
}

void test_run_command_recovers_after_overflow() {
    SwingMetro::SerialRunCommandParser parser;
    for (std::size_t index = 0; index < 80; ++index) {
        (void)parser.push('X');
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::SerialRunCommandStatus::Invalid),
                          static_cast<int>(parser.push('\n').status));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::SerialRunCommandStatus::Ready),
                          static_cast<int>(pushLine(parser, "RUN 1000 120 50").status));
}

void test_fault_command_parses_known_scenarios_and_resets() {
    SwingMetro::FaultCommandParser parser;
    const auto result = pushFaultLine(parser, "FAULT retry_first_clock");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::FaultCommandStatus::Ready),
                          static_cast<int>(result.status));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::FaultScenario::RetryFirstClock),
                          static_cast<int>(result.scenario));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::FaultCommandStatus::Ready),
                          static_cast<int>(pushFaultLine(parser, "FAULT baseline").status));
}

void test_fault_command_rejects_invalid_and_overflow_input() {
    SwingMetro::FaultCommandParser parser;
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::FaultCommandStatus::Invalid),
                          static_cast<int>(pushFaultLine(parser, "FAULT unknown").status));
    for (std::size_t index = 0; index < 80; ++index) {
        (void)parser.push('X');
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(SwingMetro::FaultCommandStatus::Invalid),
                          static_cast<int>(parser.push('\n').status));
}

} // namespace

void test_serial_run_command_main() {
    RUN_TEST(test_run_command_parses_duration_tempo_and_swing);
    RUN_TEST(test_run_command_accepts_crlf_and_documented_bounds);
    RUN_TEST(test_run_command_rejects_invalid_or_out_of_range_input);
    RUN_TEST(test_run_command_recovers_after_overflow);
    RUN_TEST(test_fault_command_parses_known_scenarios_and_resets);
    RUN_TEST(test_fault_command_rejects_invalid_and_overflow_input);
}
