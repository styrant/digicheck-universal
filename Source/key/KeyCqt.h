// Streaming constant-Q front end for the key finder. Pure C++17 (no JUCE), unit-tested against librosa.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// librosa.cqt (hop 0.2 s, 105 bins, 24/oct, fmin 65 Hz, hann, norm=1, scale=True) computed directly
// at any sample rate; magnitudes are scaled as if computed at 44.1 kHz, as the model was trained.
class KeyCqt
{
public:
    static constexpr int kBins = 105;
    static constexpr double kFmin = 65.0, kHopSeconds = 0.2;

    void configure (double sampleRate);
    void reset();
    // Appends samples; calls onFrame (const float* log1pMagnitudes[kBins], float rmsDb) for each finished frame.
    template <typename Fn> void process (const float* x, int n, Fn&& onFrame);
    double getSampleRate() const noexcept { return sr; }
    int flushSamples() const noexcept { return maxHalfAfter; }   // zeros to append at the end of a file (= librosa's padding)

private:
    struct Kernel { std::vector<float> re, im; int half = 0; float scale = 1; };
    std::vector<Kernel> kernels;
    std::vector<float> buf;               // samples [base, base + buf.size())
    long long base = 0, total = 0, nextFrame = 0;
    double sr = 0, hop = 0;
    int maxHalfAfter = 0, maxHalfBefore = 0;
    float frame[kBins] {};
};

//==============================================================================
template <typename Fn>
void KeyCqt::process (const float* x, int n, Fn&& onFrame)
{
    if (kernels.empty()) return;
    buf.insert (buf.end(), x, x + n);
    total += n;
    for (;;)
    {
        const long long centre = (long long) std::llround ((double) nextFrame * hop);
        if (centre + maxHalfAfter > total) break;
        double e = 0; int cnt = 0;
        const int h = (int) (hop / 2);
        for (long long i = std::max (base, centre - h); i < std::min (total, centre + h); ++i) { const float v = buf[(size_t) (i - base)]; e += v * v; ++cnt; }
        const float rmsDb = cnt > 0 ? (float) (10.0 * std::log10 (e / cnt + 1e-20)) : -200.0f;
        for (int k = 0; k < kBins; ++k)
        {
            const auto& K = kernels[(size_t) k];
            const long long start = centre - K.half;
            float re = 0, im = 0;
            const int len = (int) K.re.size();
            for (int j = 0; j < len; ++j)
            {
                const long long idx = start + j;
                if (idx < base) continue;                      // before the stream: zeros (as librosa pads)
                const float v = buf[(size_t) (idx - base)];
                re += v * K.re[(size_t) j]; im += v * K.im[(size_t) j];
            }
            frame[k] = std::log1p (std::sqrt (re * re + im * im) * K.scale);
        }
        onFrame (frame, rmsDb);
        ++nextFrame;
    }
    // drop samples no future frame needs
    const long long keepFrom = (long long) std::llround ((double) nextFrame * hop) - maxHalfBefore - 1;
    if (keepFrom > base + (long long) (hop * 4))
    {
        const long long drop = std::min<long long> (keepFrom - base, (long long) buf.size());
        buf.erase (buf.begin(), buf.begin() + (ptrdiff_t) drop);
        base += drop;
    }
}
