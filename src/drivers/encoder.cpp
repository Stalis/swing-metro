#include "encoder.h"

Encoder::Encoder(EncoderSettings& settings) 
    : pinA(settings.pinA), pinB(settings.pinB), pinSwitch(settings.pinSwitch)
{}