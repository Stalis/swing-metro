#pragma once

#include "diagnostic_output.h"
#include "input/encoder_sample_diagnostics.h"
#include "runtime_timing_diagnostics.h"
#include "transport_controller.h"

namespace SwingMetro {

class DiagnosticsSerializer {
  public:
    explicit DiagnosticsSerializer(DiagnosticOutput& output) : _output(output) {}
    // Call only with a stopped transport snapshot; capture owns that guard.
    void exportInternalTimingDiagnostics(const TickPipelineDiagnostics& diagnostics,
                                         const TransportDiagnostics& transport,
                                         const RuntimeTimingSnapshot& runtime,
                                         const EncoderSampleWindowDiagnostics& encoderWindow,
                                         const EncoderSampleDiagnostics& encoderSampleDiagnostics);

  private:
    void printDeliveryDiagnosticsHeader();
    void printDeliveryDiagnostics(const TransportDiagnostics& transport,
                                  const InternalTickDiagnostics& producer);
    DiagnosticOutput& _output;
    bool _headerPrinted = false;
};

} // namespace SwingMetro
