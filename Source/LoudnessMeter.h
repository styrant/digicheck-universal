// ITU-R BS.1770-4 / EBU R128 loudness: short-term (3 s), integrated (gated), and true peak
// (oversampled). Pure C++17, no JUCE dependency.
#pragma once
#include <cmath>
#include <limits>
#include <vector>

class LoudnessMeter
{
public:
    void configure (double sampleRate, int numChannels);   // numChannels 1 (mono) or 2; also resets
    void reset();
    void process (const float* left, const float* right, int numSamples);

    static constexpr double kNone = -std::numeric_limits<double>::infinity();
    double getShortTerm()  const noexcept { return shortTerm; }    // LUFS, kNone until 3 s of audio
    double getIntegrated() const noexcept { return integrated; }   // LUFS, kNone until a block passes the gate
    double getTruePeak()   const noexcept { return truePeakMax > 0 ? 20.0 * std::log10 (truePeakMax) : kNone; } // dBTP, max since reset

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    };
    void endSubBlock();
    void updateIntegrated();
    double truePeakOf (int ch, float x) noexcept;

    double sr = 48000;
    int nch = 2, subLen = 4800, subFill = 0;
    Biquad pre[2], rlb[2];
    double subSum[2] = { 0, 0 };
    std::vector<double> subBlocks;              // ring of the last 30 sub-block energies (100 ms each)
    int subCount = 0;
    std::vector<double> gatedBlocks;            // 400 ms block energies above the absolute gate
    double shortTerm = kNone, integrated = kNone;

    int os = 4, taps = 12;
    std::vector<double> fir;                    // polyphase oversampling filter, os * taps
    std::vector<float> hist[2];
    int histPos = 0;
    double truePeakMax = 0;
};
