// Band listening for the VST3 version (in the manner of Metric AB's band solo or ISOL8): hear only some
// frequency bands, and / or only Mid, Side, Left or Right. Pure C++ (tests/isolator_test.cpp).
//
// Five bands split at four frequencies (default 60, 250, 2000, 6000 Hz) with Linkwitz-Riley 24 dB/oct
// crossovers in a tree; the lower bands get allpass sections for the higher splits, so every band has
// the same phase and any combination sums cleanly (all five: flat). The Scope's band-pass can be heard
// instead (two cascaded band-passes, Q 1.4, as drawn).
//
// Nothing active (all bands, Stereo, no band-pass): the input passes untouched, bit for bit. Changes
// crossfade over 10 ms, so switching never clicks. No latency.
#pragma once
#include <array>
#include <atomic>
#include <cmath>

struct BandIsolatorParams
{
    static constexpr int kBands = 5;
    enum Mode { Stereo = 0, Mid, Side, Left, Right };
    std::atomic<int> mask { 31 };                  // bit per band: heard
    std::atomic<int> mode { Stereo };
    std::array<std::atomic<float>, kBands - 1> splits { 60.0f, 250.0f, 2000.0f, 6000.0f };
    std::atomic<bool> bandpass { false };          // the Scope's BP instead of the bands
    std::atomic<float> bandpassHz { 100.0f };
    bool active() const noexcept { return mask.load() != 31 || mode.load() != Stereo || bandpass.load(); }
};

class BandIsolator
{
public:
    void prepare (double sampleRate);
    void reset();
    void process (float* left, float* right, int n, const BandIsolatorParams& p) noexcept;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x) noexcept { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void set (const Biquad& c) noexcept { b0 = c.b0; b1 = c.b1; b2 = c.b2; a1 = c.a1; a2 = c.a2; }
    };
    struct LR4 { Biquad a, b; double run (double x) noexcept { return b.run (a.run (x)); } void set (const Biquad& c) { a.set (c); b.set (c); } };
    struct AP4 { LR4 lp, hp; double run (double x) noexcept { return lp.run (x) + hp.run (x); } };   // LP4 + HP4 = allpass
    struct Channel
    {
        std::array<LR4, 4> lp, hp;                 // per split
        std::array<AP4, 6> ap;                     // band0: splits 1,2,3; band1: 2,3; band2: 3
        Biquad bp1, bp2;
    };
    void design();
    double bandsOf (Channel& c, double x, const std::array<double, 5>& gain) noexcept;

    double sr = 48000;
    std::array<Channel, 2> ch;
    std::array<float, 4> designed { 0, 0, 0, 0 };
    float designedBp = 0;
    std::array<double, 5> gain { 1, 1, 1, 1, 1 };
    double wet = 0, bpMix = 0, step = 0.002;
    std::array<double, 5> modeGain { 1, 0, 0, 0, 0 };   // crossfade between channel modes
};
