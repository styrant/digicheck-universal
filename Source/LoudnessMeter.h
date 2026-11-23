// ITU-R BS.1770-4 / EBU R128 loudness: momentary (400 ms), short-term (3 s), integrated (gated), loudness
// range (EBU Tech 3342) and true peak (oversampled), plus the peak-to-loudness ratios PSR and PLR. Pure C++17, no JUCE dependency.
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
    double getMomentary()  const noexcept { return momentary; }      // LUFS, 400 ms
    double getMomentaryMax() const noexcept { return momentaryMax; }
    double getShortTermMax() const noexcept { return shortTermMax; }
    double getLoudnessRange() const noexcept { return lra; }         // LU (EBU Tech 3342), 0 until there's enough
    double getRecentTruePeak() const noexcept;                       // dBTP, the highest in the last 3 s
    double getChannelTruePeak (int ch) const noexcept { return tpCh[ch & 1] > 0 ? 20.0 * std::log10 (tpCh[ch & 1]) : kNone; }
    // Peak to Short-term loudness Ratio: true peak of the last 3 s minus the short-term loudness (3 s)
    double getPSR() const noexcept { const double p = getRecentTruePeak(); return (shortTerm > -70 && p > kNone) ? p - shortTerm : kNone; }
    // Peak to Loudness Ratio: true peak since reset minus integrated loudness
    double getPLR() const noexcept { const double p = getTruePeak(); return (integrated > kNone && p > kNone) ? p - integrated : kNone; }
    int blocksSeen() const noexcept { return subCount; }             // 100 ms steps since reset

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
    double shortTerm = kNone, integrated = kNone, momentary = kNone, momentaryMax = kNone, shortTermMax = kNone, lra = 0;

    // loudness range: histogram of short-term values above -70 LUFS in 0.1 LU steps
    static constexpr double kHistLo = -70.0, kHistStep = 0.1;
    static constexpr int kHistBins = 800;
    std::vector<int> stHist;
    double stEnergySum = 0; long long stCount = 0;
    void updateRange();

    std::vector<double> tpBlocks;               // true peak of each of the last 30 sub-blocks
    double tpSub = 0, tpCh[2] = { 0, 0 };

    int os = 4, taps = 12;
    std::vector<double> fir;                    // polyphase oversampling filter, os * taps
    std::vector<float> hist[2];
    int histPos = 0;
    double truePeakMax = 0;
};
