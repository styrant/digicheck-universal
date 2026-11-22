// Offline resampling to the transcription model's 16 kHz: a Kaiser-windowed sinc low-pass at 7.5 kHz
// (94 % of the new Nyquist: flat to 6.5 kHz, more than 60 dB down from 8.5 kHz), read from a finely tabulated kernel. For whole files and recordings,
// not streams. Pure C++.
#pragma once
#include <cstddef>
#include <vector>

namespace transcribe
{
    // mono in at inRate, mono out at outRate (any ratio; downsampling filters, upsampling interpolates)
    std::vector<float> resample (const float* in, std::size_t n, double inRate, double outRate = 16000.0);
}
