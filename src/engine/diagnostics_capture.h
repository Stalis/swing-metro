#pragma once

#include "diagnostics_serializer.h"
#include "transport_controller.h"

namespace SwingMetro {

// Main-core owner of the asynchronous core-1 snapshot/export handshake.
class DiagnosticsCapture {
  public:
    DiagnosticsCapture(TransportController& transport, InternalTickSource& ticks,
                       EncoderSampleDiagnostics& encoder, RuntimeTimingDiagnostics& runtime,
                       DiagnosticsSerializer& serializer)
        : _transport(transport), _ticks(ticks), _encoder(encoder), _runtime(runtime),
          _serializer(serializer) {}

    void beginWindow() {
        (void)_runtime.requestSnapshot();
        (void)_encoder.snapshotAndResetWindow();
    }
    void requestExport() {
        _encoderWindow = _encoder.snapshotAndResetWindow();
        _generation = _runtime.requestSnapshot();
        _pending = true;
    }
    [[nodiscard]] bool pending() const { return _pending; }
    void exportIfReady() {
        RuntimeTimingSnapshot snapshot;
        if (!_pending || _transport.isRunning() || _transport.usesInternalTiming() ||
            !_runtime.readSnapshot(_generation, snapshot)) {
            return;
        }
        _serializer.exportInternalTimingDiagnostics(_transport.pipelineDiagnostics(_ticks),
                                                    _transport.diagnostics(), snapshot,
                                                    _encoderWindow, _encoder);
        _pending = false;
    }

  private:
    TransportController& _transport;
    InternalTickSource& _ticks;
    EncoderSampleDiagnostics& _encoder;
    RuntimeTimingDiagnostics& _runtime;
    DiagnosticsSerializer& _serializer;
    EncoderSampleWindowDiagnostics _encoderWindow;
    std::uint32_t _generation = 0;
    bool _pending = false;
};

} // namespace SwingMetro
