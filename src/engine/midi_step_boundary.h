#pragma once

#include "external_midi_clock.h"
#include "midi_clock_transmitter.h"
#include "sequencer.h"

#include <cstdint>

namespace SwingMetro {

template <typename SendNoteOff, typename SendNoteOn>
auto processMidiStepBoundary(Sequencer& sequencer, std::uint32_t nowUs, MidiClockMode mode,
                             MidiClockTransmitter& clock, MidiRealTimeSink& clockSink,
                             bool& noteSent, std::uint8_t& lastNoteSent, SendNoteOff sendNoteOff,
                             SendNoteOn sendNoteOn) -> MidiClockTickResult {
    const auto result =
        clock.transition(nowUs, sequencer.getBpm(), mode, sequencer.isRunning(), clockSink);
    if (result.transportStopped) {
        if (noteSent) {
            sendNoteOff(lastNoteSent);
            noteSent = false;
        }
        return result;
    }

    if (mode != MidiClockMode::External && sequencer.update(nowUs)) {
        if (noteSent) {
            sendNoteOff(lastNoteSent);
            noteSent = false;
        }

        if (sequencer.isCurrentStepEnabled()) {
            lastNoteSent = sequencer.currentStepMidiNote();
            noteSent = true;
            sendNoteOn(lastNoteSent, sequencer.currentStepVelocity());
        }
    }

    clock.emitDueClocks(nowUs, clockSink);
    return result;
}

template <typename SendNoteOff, typename SendNoteOn>
auto processExternalMidiClock(Sequencer& sequencer, const ExternalMidiClockResult& result,
                              bool& noteSent, std::uint8_t& lastNoteSent, SendNoteOff sendNoteOff,
                              SendNoteOn sendNoteOn) -> void {
    if (result.stopped && noteSent) {
        sendNoteOff(lastNoteSent);
        noteSent = false;
    }
    if (result.reset) {
        sequencer.externalStart();
    } else if (result.started) {
        sequencer.externalContinue();
    } else if (result.stopped) {
        sequencer.externalStop();
    }
    if (!result.advanceStep || !sequencer.advanceExternal()) {
        return;
    }
    if (noteSent) {
        sendNoteOff(lastNoteSent);
        noteSent = false;
    }
    if (sequencer.isCurrentStepEnabled()) {
        lastNoteSent = sequencer.currentStepMidiNote();
        noteSent = true;
        sendNoteOn(lastNoteSent, sequencer.currentStepVelocity());
    }
}

} // namespace SwingMetro
