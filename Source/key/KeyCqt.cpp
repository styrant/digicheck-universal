#include "KeyCqt.h"

void KeyCqt::configure (double sampleRate)
{
    sr = sampleRate;
    hop = kHopSeconds * sr;
    kernels.assign (kBins, {});
    const double Q = 1.0 / (std::pow (2.0, 1.0 / 24.0) - 1.0), twoPi = 6.283185307179586476925;
    maxHalfAfter = maxHalfBefore = 0;
    for (int k = 0; k < kBins; ++k)
    {
        const double f = kFmin * std::pow (2.0, k / 24.0);
        const double L = Q * sr / f, L44 = Q * 44100.0 / f;
        const int n = (int) std::floor (L);
        auto& K = kernels[(size_t) k];
        K.re.resize ((size_t) n); K.im.resize ((size_t) n);
        K.half = n / 2;
        double wsum = 0;
        for (int j = 0; j < n; ++j)
        {
            const double w = 0.5 - 0.5 * std::cos (twoPi * j / n);       // periodic Hann, as librosa
            const double ph = -twoPi * f * (j - K.half) / sr;
            K.re[(size_t) j] = (float) (w * std::cos (ph));
            K.im[(size_t) j] = (float) (w * std::sin (ph));
            wsum += w;
        }
        K.scale = (float) (std::sqrt (L44) / wsum);
        maxHalfBefore = std::max (maxHalfBefore, K.half);
        maxHalfAfter = std::max (maxHalfAfter, n - K.half);
    }
    reset();
}

void KeyCqt::reset()
{
    buf.clear(); base = total = nextFrame = 0;
}

