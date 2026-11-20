// FilterBank.h - portable port of the Seven Phases filter-bank analyzer core.
//
// The original (2009-2015) used a bank of resonant two-pole low-pass filters
// placed on band edges; each band is the difference of two adjacent low-pass
// outputs (the "interband" trick), squared to get instantaneous power.
//
// Changes vs. the original:
//  * no SSE intrinsics / MSVC extensions; plain SoA loops that compilers
//    auto-vectorise on x86-64 and ARM64
//  * double precision state and coefficients (needed for the high-Q filters
//    used at 8..24 bands per octave, especially at high sample rates)
//  * bands per octave: 3, 4, 6, 8, 12, 16, 24 (original: 3, 4, 6)
//  * band grid can be anchored to 1 kHz (ISO, as before) or A4 = 440 Hz so
//    that 12/24 bands per octave line up with semitones / quarter tones
//  * per-band gain calibration is computed from the actual transfer function
//    instead of hand-tuned emphasis polynomials
//  * hard bounds on band count (original relied on a disabled debug check)
//  * no locks: results are handed to the UI through a wait-free SPSC ring
//
// This header has no JUCE dependency so it can be unit-tested standalone.

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>

namespace sa
{

constexpr int kMaxFilters = 272;              // 24 bpo * ~10.4 octaves + 1 fits
constexpr int kMaxBands   = kMaxFilters - 1;

constexpr std::array<int, 7> kBandsPerOctaveChoices { 3, 4, 6, 8, 12, 16, 24 };

enum class ChannelMode : int { Left = 0, Right, Both, Mid, Side };
enum class Anchor      : int { Iso1k = 0, A440 };

inline double anchorFrequency (Anchor a) noexcept { return a == Anchor::A440 ? 440.0 : 1000.0; }

//==============================================================================
struct LayoutKey
{
    double sampleRate     = 0.0;
    int    bandsPerOctave = 0;
    Anchor anchor         = Anchor::Iso1k;

    bool operator== (const LayoutKey& o) const noexcept
    {
        // exact comparison intended (sample rates are host-provided constants)
        return ! (sampleRate < o.sampleRate) && ! (o.sampleRate < sampleRate)
            && bandsPerOctave == o.bandsPerOctave && anchor == o.anchor;
    }
    bool operator!= (const LayoutKey& o) const noexcept { return ! (*this == o); }
    bool isValid() const noexcept { return sampleRate > 1000.0 && bandsPerOctave > 0; }
};

struct BandLayout
{
    LayoutKey key;
    int    numBands = 0;
    double freqMin  = 0.0;   // centre of the first band
    double freqMax  = 0.0;   // centre of the last band

    std::array<double, kMaxBands> centre {};    // nominal band centres
    std::array<double, kMaxBands> lower {};     // nominal band edges (centre / k^.5)
    std::array<double, kMaxBands> upper {};     //                    (centre * k^.5)
    std::array<double, kMaxBands> emphasis {};  // power-domain calibration gain

    std::array<double, kMaxFilters> edge {};    // (warped) filter frequencies
    std::array<double, kMaxFilters> k0 {}, k1 {}, k2 {};

    int numFilters() const noexcept { return numBands > 0 ? numBands + 1 : 0; }
};

//==============================================================================
namespace detail
{
    constexpr double pi = 3.14159265358979323846;

    // Same formula as the original sp::twoPoleLPCoeffs (q = 2 * bpo there).
    inline void twoPoleLPCoeffs (double fs, double f, double q, double prewarp,
                                 double& k0, double& k1, double& k2) noexcept
    {
        const double w = 2.0 * pi * f / fs;
        const double y = std::sin (w) / (q * (1.0 + std::cos (w * prewarp)));
        const double a = 1.0 / (1.0 + y);
        k2 = a * (y - 1.0);
        k1 = a * 2.0 * std::cos (w);
        k0 = (0.5 / q) * (1.0 - k1 - k2);
    }

    // Response of the delayed-output two-pole section:
    //   s[n] = k0 x[n] + k1 s[n-1] + k2 s[n-2],  out[n] = s[n-1]
    inline std::complex<double> lpResponse (double k0, double k1, double k2,
                                            double f, double fs) noexcept
    {
        const auto z1 = std::polar (1.0, -2.0 * pi * f / fs); // z^-1
        const auto z2 = z1 * z1;
        return k0 * z1 / (1.0 - k1 * z1 - k2 * z2);
    }

    inline double prewarpFor (int bpo) noexcept
    {
        // empirical values from the original code; >6 bpo uses the 6 bpo value
        switch (bpo)
        {
            case 3:  return 0.904;
            case 4:  return 0.905;
            default: return 0.909;
        }
    }
}

// Magnitude response of band `b` (including the input zero, 1 + z^-1).
inline double bandResponse (const BandLayout& L, int b, double f) noexcept
{
    using namespace detail;
    const double fs = L.key.sampleRate;
    const auto zero = 1.0 + std::polar (1.0, -2.0 * pi * f / fs);
    const auto hi = lpResponse (L.k0[(size_t) b + 1], L.k1[(size_t) b + 1], L.k2[(size_t) b + 1], f, fs);
    const auto lo = lpResponse (L.k0[(size_t) b],     L.k1[(size_t) b],     L.k2[(size_t) b],     f, fs);
    return std::abs (zero * (hi - lo));
}

// Port of Analyzer::update() generalised to any bands-per-octave and anchor.
inline void computeLayout (BandLayout& L, const LayoutKey& key) noexcept
{
    L.key = key;
    L.numBands = 0;
    L.freqMin = L.freqMax = 0.0;

    if (! key.isValid())
        return;

    const double fs   = key.sampleRate;
    const int    bpo  = key.bandsPerOctave;
    const double fmin = 22.0;
    const double fmax = 29000.0;
    const double fa   = anchorFrequency (key.anchor);
    const double k    = std::exp2 (1.0 / bpo);
    const double sk   = std::sqrt (k);

    // first band centre >= fmin, on the anchor grid
    double f = fa * std::pow (k, std::ceil (std::log (fmin / fa) / std::log (k) - 1e-9));

    // Gently warp band edges so the last band's upper edge lands on fedg
    // (just below Nyquist); same scheme as the original.
    const double fedg = 0.94 * 0.5 * fs;
    const int    last = (int) std::floor (0.5 + std::log (fedg / (fa * sk)) / std::log (k));
    const double fli  = 1.0 / (fa * std::pow (k, last));
    const double r    = (1.0 - (fedg / sk) * fli) * fli * fli;

    const double q       = 2.0 * bpo;
    const double prewarp = detail::prewarpFor (bpo);

    int n = 0;
    double ff = (f / sk) * (1.0 - r * f * f);
    for (;;)
    {
        L.edge[(size_t) n] = ff;
        detail::twoPoleLPCoeffs (fs, ff, q, prewarp, L.k0[(size_t) n], L.k1[(size_t) n], L.k2[(size_t) n]);

        ff = f * sk * (1.0 - r * f * f);
        if (ff > fedg + 1.0 || ff > fmax || ff <= L.edge[(size_t) n] || n + 1 >= kMaxFilters)
            break;

        L.centre[(size_t) n] = f;
        L.lower [(size_t) n] = f / sk;
        L.upper [(size_t) n] = f * sk;
        ++n;
        f *= k;
    }

    L.numBands = n;
    if (n == 0)
        return;

    L.freqMin = L.centre[0];
    L.freqMax = L.centre[(size_t) n - 1];

    // Calibrate each band so that a sine at the band centre reads its true
    // peak level (0 dBFS sine -> 0 dB). The original used a fixed +0.21 dB
    // base plus a polynomial tilt near Nyquist; this generalises that.
    for (int b = 0; b < n; ++b)
    {
        const double h = bandResponse (L, b, L.centre[(size_t) b]);
        L.emphasis[(size_t) b] = h > 1e-12 ? 1.0 / (h * h) : 0.0;
    }
}

//==============================================================================
// One block of analysis results, already calibrated (power domain).
struct Snapshot
{
    LayoutKey key;
    int       numBands = 0;
    int64_t   count    = 0;                  // samples (x channels) accumulated
    std::array<float, kMaxBands> peak {};    // max of y^2
    std::array<float, kMaxBands> sum {};     // sum of y^2
};

//==============================================================================
// Wait-free single-producer / single-consumer ring.
template <typename T, size_t Capacity>
class SpscRing
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
public:
    T* beginWrite() noexcept
    {
        const auto h = head.load (std::memory_order_relaxed);
        const auto t = tail.load (std::memory_order_acquire);
        return (h - t) >= Capacity ? nullptr : &buffer[h & (Capacity - 1)];
    }
    void commitWrite() noexcept
    {
        head.store (head.load (std::memory_order_relaxed) + 1, std::memory_order_release);
    }

    const T* beginRead() noexcept
    {
        const auto t = tail.load (std::memory_order_relaxed);
        const auto h = head.load (std::memory_order_acquire);
        return t == h ? nullptr : &buffer[t & (Capacity - 1)];
    }
    void commitRead() noexcept
    {
        tail.store (tail.load (std::memory_order_relaxed) + 1, std::memory_order_release);
    }

private:
    std::array<T, Capacity> buffer {};
    alignas (64) std::atomic<size_t> head { 0 };
    alignas (64) std::atomic<size_t> tail { 0 };
};

//==============================================================================
class FilterBank
{
public:
    // Not real-time safe only in the sense of a few hundred transcendental
    // function calls; it does not allocate or lock, so it is fine to call from
    // the audio thread when a setting changes.
    void configure (const LayoutKey& key) noexcept
    {
        computeLayout (layout, key);
        reset();
    }

    void reset() noexcept
    {
        for (auto& s : state)
        {
            s.z0.fill (0.0);
            s.z1.fill (0.0);
            s.zlp = 0.0;
        }
        clearAccumulators();
    }

    const BandLayout& getLayout() const noexcept { return layout; }

    template <typename Sample>
    void process (const Sample* const* channels, int numChannels, int numSamples, ChannelMode mode) noexcept
    {
        if (layout.numBands <= 0 || numChannels <= 0 || numSamples <= 0)
            return;

        const Sample* l = channels[0];
        const Sample* r = numChannels > 1 ? channels[1] : channels[0];

        if (numChannels == 1)
            mode = ChannelMode::Left;

        switch (mode)
        {
            case ChannelMode::Left:
                for (int i = 0; i < numSamples; ++i) tick (state[0], (double) l[i]);
                accCount += numSamples;
                break;
            case ChannelMode::Right:
                for (int i = 0; i < numSamples; ++i) tick (state[0], (double) r[i]);
                accCount += numSamples;
                break;
            case ChannelMode::Mid:
                for (int i = 0; i < numSamples; ++i) tick (state[0], 0.5 * ((double) l[i] + (double) r[i]));
                accCount += numSamples;
                break;
            case ChannelMode::Side:
                for (int i = 0; i < numSamples; ++i) tick (state[0], 0.5 * ((double) l[i] - (double) r[i]));
                accCount += numSamples;
                break;
            case ChannelMode::Both:
                // as in the original: peak = max over both channels,
                // average = mean power over both channels
                for (int i = 0; i < numSamples; ++i)
                {
                    tick (state[0], (double) l[i]);
                    tick (state[1], (double) r[i]);
                }
                accCount += 2 * (int64_t) numSamples;
                break;
        }
    }

    int64_t accumulatedSamples() const noexcept { return accCount; }

    // Fills `out` with calibrated results and clears the accumulators.
    void takeSnapshot (Snapshot& out) noexcept
    {
        const int n = layout.numBands;
        out.key = layout.key;
        out.numBands = n;
        out.count = accCount;
        for (int b = 0; b < n; ++b)
        {
            const double e = layout.emphasis[(size_t) b];
            out.peak[(size_t) b] = (float) (accPeak[(size_t) b] * e);
            out.sum [(size_t) b] = (float) (accSum [(size_t) b] * e);
        }
        clearAccumulators();
    }

private:
    struct ChannelState
    {
        std::array<double, kMaxFilters> z0 {}, z1 {};
        double zlp = 0.0;
    };

    void clearAccumulators() noexcept
    {
        accPeak.fill (0.0);
        accSum.fill (0.0);
        accCount = 0;
    }

    inline void tick (ChannelState& s, double in) noexcept
    {
        constexpr double antiDenormal = 1e-20; // cancels out (bands have no DC response)
        in += antiDenormal;
        const double x = in + s.zlp;           // zero at Nyquist: 1 + z^-1
        s.zlp = in;

        const int nf = layout.numBands + 1;
        double* __restrict z0 = s.z0.data();
        double* __restrict z1 = s.z1.data();
        double* __restrict out = filterOut.data();
        const double* __restrict k0 = layout.k0.data();
        const double* __restrict k1 = layout.k1.data();
        const double* __restrict k2 = layout.k2.data();

        for (int m = 0; m < nf; ++m)
        {
            const double o = z0[m];
            z0[m] = x * k0[m] + o * k1[m] + z1[m] * k2[m];
            z1[m] = o;
            out[m] = o;
        }

        const int nb = layout.numBands;
        double* __restrict pk = accPeak.data();
        double* __restrict sm = accSum.data();
        for (int b = 0; b < nb; ++b)
        {
            const double y  = out[b + 1] - out[b];
            const double y2 = y * y;
            pk[b] = pk[b] > y2 ? pk[b] : y2;
            sm[b] += y2;
        }
    }

    BandLayout layout;
    std::array<ChannelState, 2> state {};
    std::array<double, kMaxFilters> filterOut {};
    std::array<double, kMaxBands> accPeak {}, accSum {};
    int64_t accCount = 0;
};

} // namespace sa
