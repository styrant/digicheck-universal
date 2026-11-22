#include "Resample.h"
#include <algorithm>
#include <cmath>

namespace transcribe
{
    namespace
    {
        double besselI0 (double x)
        {
            double sum = 1, term = 1;
            for (int k = 1; k < 40; ++k) { term *= (x / (2 * k)) * (x / (2 * k)); sum += term; if (term < 1e-12 * sum) break; }
            return sum;
        }
    }

    std::vector<float> resample (const float* in, std::size_t n, double inRate, double outRate)
    {
        if (n == 0 || inRate <= 0 || outRate <= 0) return {};
        if (std::abs (inRate - outRate) < 1e-9) return std::vector<float> (in, in + n);

        const double ratio = inRate / outRate;                  // input samples per output sample
        const double cutoff = 0.47 * std::min (inRate, outRate) / inRate * 2.0;   // in cycles per input sample x2 (normalised to Nyquist)
        constexpr double kPi = 3.14159265358979323846;
        constexpr int kZeros = 32;                              // sinc zero crossings each side (at the cutoff)
        const double halfWidth = kZeros / cutoff;               // kernel half width in input samples
        constexpr int kPhases = 256;                            // table points per input sample
        constexpr double kBeta = 8.0;                           // Kaiser: ~80 dB stop band

        // tabulated kernel over [0, halfWidth]
        const int tableLen = (int) std::ceil (halfWidth * kPhases) + 2;
        std::vector<float> table ((size_t) tableLen);
        const double i0b = besselI0 (kBeta);
        for (int i = 0; i < tableLen; ++i)
        {
            const double t = (double) i / kPhases;
            const double x = t / halfWidth;
            if (x >= 1.0) { table[(size_t) i] = 0; continue; }
            const double s = t == 0 ? 1.0 : std::sin (kPi * cutoff * t) / (kPi * cutoff * t);
            table[(size_t) i] = (float) (cutoff * s * besselI0 (kBeta * std::sqrt (1 - x * x)) / i0b);
        }
        auto kernel = [&] (double t)
        {
            const double p = std::abs (t) * kPhases;
            const int i = (int) p;
            if (i + 1 >= tableLen) return 0.0f;
            const float f = (float) (p - i);
            return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * f;
        };

        const std::size_t outN = (std::size_t) std::floor ((double) n / ratio);
        std::vector<float> out (outN);
        const int reach = (int) std::ceil (halfWidth);
        for (std::size_t j = 0; j < outN; ++j)
        {
            const double centre = (double) j * ratio;
            const long lo = std::max (0L, (long) std::floor (centre) - reach), hi = std::min ((long) n - 1, (long) std::ceil (centre) + reach);
            double acc = 0;
            for (long i = lo; i <= hi; ++i) acc += in[i] * kernel ((double) i - centre);
            out[j] = (float) acc;
        }
        return out;
    }
}
