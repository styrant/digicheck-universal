// The Monitor page's measurements (pure C++, no JUCE): loudness (BS.1770 / R128 via LoudnessMeter),
// channel levels, dynamics (PSR / PLR), phase correlation (wide band and in three bands), balance,
// stereo width, the sound-field points and a 100 ms history of all of it.
#pragma once
#include "../LoudnessMeter.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace monitor
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
    void clear() noexcept { z1 = z2 = 0; }
    static Biquad lowpass (double sr, double f, double q = 0.70710678);
    static Biquad highpass (double sr, double f, double q = 0.70710678);
};

// one-pole average of L², R², L·R: correlation = <LR> / sqrt(<L²><R²>)
struct Correlator
{
    double ll = 0, rr = 0, lr = 0, k = 0.001;
    void setTime (double sr, double seconds) { k = 1.0 - std::exp (-1.0 / (sr * seconds)); }
    void add (double l, double r) noexcept { ll += k * (l * l - ll); rr += k * (r * r - rr); lr += k * (l * r - lr); }
    void clear() noexcept { ll = rr = lr = 0; }
    double value() const noexcept               // +1 mono, 0 unrelated, -1 polarity inverted; 0 when silent
    {
        const double d = std::sqrt (ll * rr);
        return d > 1e-12 ? std::fmax (-1.0, std::fmin (1.0, lr / d)) : 0.0;
    }
    double energy() const noexcept { return ll + rr; }
};

class MonitorModel
{
public:
    static constexpr double kNone = LoudnessMeter::kNone;
    enum Chan { L = 0, R = 1, M = 2, S = 3 };
    enum Band { Low = 0, Mid = 1, High = 2 };
    static constexpr double kLowSplit = 250.0, kHighSplit = 2500.0;

    void configure (double sampleRate);
    void reset();                                  // the loudness, maxima and history (not the live meters)
    void push (const float* left, const float* right, int n);
    double sampleRate() const noexcept { return sr; }

    const LoudnessMeter& loudness() const noexcept { return loud; }

    // levels (linear amplitude): peak meter (instant attack, held 1.5 s, then falling 20 dB/s), RMS (300 ms)
    struct Level { double peak = 0, hold = 0, rms = 0, maxPeak = 0, maxRms = 0; int clips = 0; };
    const Level& level (int ch) const noexcept { return lv[(size_t) ch]; }
    static double toDb (double a) noexcept { return a > 1e-10 ? 20.0 * std::log10 (a) : kNone; }

    // phase: wide-band correlation (fast 300 ms and slow 3 s), its minimum since reset, the three bands
    double correlation() const noexcept     { return corrFast.value(); }
    double correlationSlow() const noexcept { return corrSlow.value(); }
    double correlationMin() const noexcept  { return corrMin; }
    double bandCorrelation (int b) const noexcept { return bands[(size_t) b].value(); }
    bool   bandActive (int b) const noexcept;                        // enough energy in the band to judge
    double balanceDb() const noexcept;                               // + = right louder
    double width() const noexcept;                                   // side share: 0 mono, 1 unrelated L/R, 2 inverted

    // sound field: the most recent samples (ring) and the polar-level envelope
    static constexpr int kPoints = 1 << 14, kPolarBins = 61;
    const std::vector<float>& pointsL() const noexcept { return ptL; }
    const std::vector<float>& pointsR() const noexcept { return ptR; }
    long long pointsWritten() const noexcept { return ptCount; }
    // the level going in each direction (RMS-like: energy over 300 ms, so the directions the signal spends
    // its energy in stand out, not stray peaks); bin 0 = left edge, the middle bin = centre (mono)
    double polarLevel (int bin) const noexcept { return std::sqrt (polar[(size_t) bin]); }

    // history: one entry every 100 ms
    struct Entry { float shortTerm, momentary, psr, correlation, truePeak; };
    static constexpr int kHistory = 6000;                            // 10 minutes
    int historySize() const noexcept { return (int) std::min<long long> (histCount, kHistory); }
    const Entry& history (int ago) const noexcept { return hist[(size_t) ((histCount - 1 - ago) % kHistory)]; } // 0 = newest
    long long historyCount() const noexcept { return histCount; }

    // dynamics extremes since reset (PSR while the music is near its loudest: short-term within 6 LU of its max)
    double psrMin() const noexcept { return psrLow; }
    double psrMax() const noexcept { return psrHigh; }

private:
    void block (const float* l, const float* r, int n);
    void step100ms();

    double sr = 48000;
    LoudnessMeter loud;
    std::array<Level, 4> lv {};
    std::array<double, 4> rmsSq {}, holdLeft {};
    double peakFall = 0.999, rmsK = 0.001;

    Correlator corrFast, corrSlow;
    std::array<Correlator, 3> bands;
    std::array<Biquad, 2> lowA, lowB, midHpA, midHpB, midLpA, midLpB, highA, highB;
    double corrMin = 1.0;
    long long corrSeen = 0;

    std::vector<float> ptL, ptR;
    long long ptCount = 0;
    std::array<double, kPolarBins> polar {};
    double polarK = 1e-4;

    std::vector<Entry> hist;
    long long histCount = 0;
    int lastBlocks = 0;
    double psrLow = kNone, psrHigh = kNone;
};
} // namespace monitor
