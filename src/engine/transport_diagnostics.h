#pragma once

#include "internal_tick_source.h"
#include "midi_pending_delivery_queue.h"
#include "stage5_instrumentation.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace SwingMetro {

using DeliveryMessageClass = MidiMessageClass;
static constexpr std::size_t DELIVERY_MESSAGE_CLASS_COUNT = MIDI_MESSAGE_CLASS_COUNT;

struct LatenessDistribution {
    static constexpr std::size_t POSITIVE_BUCKET_COUNT = 10;
    std::uint32_t early = 0;
    std::uint32_t onTime = 0;
    std::uint32_t unordered = 0;
    std::array<std::uint32_t, POSITIVE_BUCKET_COUNT> positive{};
};

struct DeliveryClassDiagnostics {
    std::uint32_t attempts = 0;
    std::uint32_t accepted = 0;
    std::uint32_t retryLater = 0;
    std::uint32_t disconnected = 0;
    std::uint32_t retryRecovered = 0;
    std::uint32_t maxFirstAttemptLatenessUs = 0;
    std::uint32_t maxAcceptanceLatenessUs = 0;
};

struct TransportDiagnostics {
    // Maxima are microseconds from local observations, not USB acceptance or host delivery.
    std::uint32_t maxServiceIntervalUs = 0;
    std::uint32_t maxInternalTickProcessingLatenessUs = 0;
    std::uint32_t maxExternalTickProcessingLatenessUs = 0;
    std::uint32_t maxClockAttemptLatenessUs = 0;
    std::uint32_t maxQueuedEventAttemptLatenessUs = 0;
    // Historical count of internal tick records discarded after the per-pass processing budget.
    std::uint32_t droppedTicks = 0;
    std::uint32_t successfulInternalTickPops = 0;
    std::uint32_t outgoingInternalClockAttempts = 0;
    std::uint32_t maxInternalTicksPoppedPerProcessPass = 0;
    std::uint32_t internalTickBudgetReachedPasses = 0;
    std::uint32_t maxRemainingInternalTicksAfterBudgetPass = 0;
    std::uint32_t maxProcessDurationUs = 0;
    std::uint32_t clockCoalescedCount = 0;
    std::uint32_t clockExpiredCount = 0;
    std::uint32_t noteOnExpiredCount = 0;
    std::uint32_t staleGateOffCount = 0;
    std::uint32_t terminalNoteOffAbandonedCount = 0;
    std::uint32_t terminalStopAbandonedCount = 0;
    std::array<DeliveryClassDiagnostics, DELIVERY_MESSAGE_CLASS_COUNT> delivery{};
    std::array<std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT>,
               DELIVERY_REMOVAL_REASON_COUNT>
        pendingRemovals{};
    std::array<std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT>,
               DELIVERY_REMOVAL_REASON_COUNT>
        scheduledRemovals{};
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> scheduledCreated{};
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> scheduledTransferred{};
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> outboxInserted{};
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> currentScheduledDepth{};
#if SWING_METRO_STAGE5_INSTRUMENTATION
    std::uint32_t currentScheduledDepthTotal = 0;
    std::uint32_t maxScheduledDepth = 0;
#endif
    std::array<std::uint32_t, DELIVERY_MESSAGE_CLASS_COUNT> currentOutboxDepthByClass{};
    std::uint32_t currentOutboxDepth = 0;
    std::uint32_t maxOutboxDepth = 0;
    std::uint32_t maxSendAttemptsPerPublicPass = 0;
    std::uint32_t outboxCapacityFailures = 0;
    std::uint32_t deliveryCapacitySafetyStops = 0;
    std::uint32_t retryWindowSafetyStops = 0;
    std::uint32_t sessionGenerationAdvances = 0;
    std::uint32_t explicitCleanStarts = 0;
    std::array<std::uint32_t, static_cast<std::size_t>(InvalidationReason::SupersededStart) + 1>
        sessionEnds{};
    std::uint32_t currentSessionGeneration = 1;
    InvalidationReason lastSessionEndReason = InvalidationReason::Stop;
#if SWING_METRO_STAGE5_INSTRUMENTATION
    LatenessDistribution clockAttemptLateness{};
    LatenessDistribution clockAcceptedLateness{};
    LatenessDistribution noteAttemptLateness{};
    LatenessDistribution noteAcceptedLateness{};
#endif
};

enum class DeliveryLifecycleOutcome : std::uint8_t {
    None,
    Disconnected,
    RetryWindowExceeded,
};

struct TickPipelineDiagnostics {
    // This is a coherent producer snapshot followed by core-0-owned consumer fields.
    InternalTickDiagnostics producer;
    std::uint32_t successfulConsumerPops = 0;
    std::uint32_t budgetDiscards = 0;
    std::uint32_t outgoingInternalClockAttempts = 0;
};

} // namespace SwingMetro
