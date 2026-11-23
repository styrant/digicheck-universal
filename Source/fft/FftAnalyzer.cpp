#include "FftAnalyzer.h"
#include <algorithm>

namespace fft
{
static constexpr double kPi = 3.14159265358979323846;

void Radix2::init (int order)
{
    n = 1 << order;
    rev.assign ((size_t) n, 0);
    for (int i = 0; i < n; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b) if (i & (1 << b)) r |= 1 << (order - 1 - b);
        rev[(size_t) i] = r;
    }
    tw.resize ((size_t) (n / 2));
    for (int k = 0; k < n / 2; ++k) tw[(size_t) k] = std::polar (1.0f, (float) (-2.0 * kPi * k / n));
}

void Radix2::forward (std::complex<float>* x) const noexcept
{
    for (int i = 0; i < n; ++i) if (i < rev[(size_t) i]) std::swap (x[i], x[rev[(size_t) i]]);
    for (int len = 2; len <= n; len <<= 1)
    {
        const int half = len / 2, step = n / len;
        for (int s = 0; s < n; s += len)
            for (int k = 0; k < half; ++k)
            {
                const auto t = tw[(size_t) (k * step)] * x[s + k + half];
                x[s + k + half] = x[s + k] - t;
                x[s + k] += t;
            }
    }
}

void FftAnalyzer::configure (double sampleRate, int order)
{
    sr = sampleRate > 0 ? sampleRate : 48000;
    ord = std::clamp (order, 11, 15);
    N = 1 << ord;
    fftImpl.init (ord);
    window.resize ((size_t) N);
    double sum = 0;
    for (int i = 0; i < N; ++i) { window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * kPi * i / N)); sum += window[(size_t) i]; }
    norm = (float) (1.0 / ((sum / 2) * (sum / 2)));            // |X|² of a full-scale sine -> 1
    for (auto& i : in) { i.l.assign ((size_t) N, 0.0f); i.r.assign ((size_t) N, 0.0f); i.pos = 0; i.sinceHop = 0; i.hops = 0; i.filled = 0; }
    bufA.assign ((size_t) N, {}); bufB.assign ((size_t) N, {});
    for (auto* p : { &powL, &powR, &powM, &powS }) p->assign ((size_t) bins(), 0.0f);
    clear();
}

void FftAnalyzer::clear()
{
    for (auto& s : series) { s.avg.assign ((size_t) bins(), 0.0f); s.hold.assign ((size_t) bins(), 0.0f); s.fresh = true; }
}

void FftAnalyzer::resetHold() { for (auto& s : series) std::fill (s.hold.begin(), s.hold.end(), 0.0f); }

void FftAnalyzer::setSeries (int i, bool on, int input, int channel)
{
    auto& s = series[(size_t) i];
    if (s.on == on && s.input == input && s.channel == channel) return;
    s.on = on; s.input = input; s.channel = channel; s.fresh = true;
    std::fill (s.avg.begin(), s.avg.end(), 0.0f);
    std::fill (s.hold.begin(), s.hold.end(), 0.0f);
}

void FftAnalyzer::push (int input, const float* L, const float* R, int n)
{
    auto& I = in[(size_t) input];
    const int hop = std::max (1, N / overlap);
    for (int i = 0; i < n; ++i)
    {
        I.l[(size_t) I.pos] = L[i]; I.r[(size_t) I.pos] = R[i];
        I.pos = (I.pos + 1) & (N - 1);
        ++I.filled;
        if (++I.sinceHop >= hop) { I.sinceHop = 0; analyse (input); }
    }
}

void FftAnalyzer::analyse (int input)
{
    auto& I = in[(size_t) input];
    ++I.hops;
    bool need[5] = {};
    for (auto& s : series) if (s.on && s.input == input) { if (s.channel == Stereo) need[Left] = need[Right] = true; else need[s.channel] = true; }
    if (! (need[Left] || need[Right] || need[Mid] || need[Side])) return;

    const int B = bins();
    // two real signals per complex FFT: x = a + i·b  ->  A[k] = (X[k] + conj X[N-k]) / 2, B[k] = (X[k] - conj X[N-k]) / 2i
    auto pair = [&] (float (*fa) (float, float), float (*fb) (float, float), std::vector<float>& pa, std::vector<float>& pb)
    {
        for (int k = 0; k < N; ++k)
        {
            const auto idx = (size_t) ((I.pos + k) & (N - 1));
            const float w = window[(size_t) k];
            bufA[(size_t) k] = { fa (I.l[idx], I.r[idx]) * w, fb (I.l[idx], I.r[idx]) * w };
        }
        fftImpl.forward (bufA.data());
        for (int k = 0; k < B; ++k)
        {
            const auto X = bufA[(size_t) k], Y = std::conj (bufA[(size_t) ((N - k) & (N - 1))]);
            const auto a = (X + Y) * 0.5f, b = (X - Y) * std::complex<float> (0, -0.5f);
            pa[(size_t) k] = std::norm (a) * norm;
            pb[(size_t) k] = std::norm (b) * norm;
        }
    };
    if (need[Left] || need[Right]) pair ([] (float l, float) { return l; }, [] (float, float r) { return r; }, powL, powR);
    if (need[Mid] || need[Side])   pair ([] (float l, float r) { return 0.5f * (l + r); }, [] (float l, float r) { return 0.5f * (l - r); }, powM, powS);

    const double hopSec = (double) (N / overlap) / sr;
    const float a = avgMs <= 0 ? 1.0f : (float) (1.0 - std::exp (-hopSec / (avgMs * 0.001)));
    for (auto& s : series)
    {
        if (! s.on || s.input != input) continue;
        const float aa = s.fresh || I.filled < N ? 1.0f : a;      // the first full frame starts the average
        s.fresh = I.filled < N;
        for (int k = 0; k < B; ++k)
        {
            float p;
            switch (s.channel)
            {
                case Left:  p = powL[(size_t) k]; break;
                case Right: p = powR[(size_t) k]; break;
                case Mid:   p = powM[(size_t) k]; break;
                case Side:  p = powS[(size_t) k]; break;
                default:    p = 0.5f * (powL[(size_t) k] + powR[(size_t) k]); break;
            }
            auto& v = s.avg[(size_t) k];
            v += aa * (p - v);
            s.hold[(size_t) k] = std::max (s.hold[(size_t) k], v);
        }
    }
}

void FftAnalyzer::decayHold (double seconds, double dbPerSecond)
{
    const float f = (float) std::pow (10.0, -dbPerSecond * seconds / 10.0);
    for (auto& s : series)
        for (size_t k = 0; k < s.hold.size(); ++k) s.hold[k] = std::max (s.avg[k], s.hold[k] * f);
}

void FftAnalyzer::curve (const std::vector<float>& spec, const std::vector<float>& edges, double slope, double smoothOct, std::vector<float>& out) const
{
    const int n = (int) edges.size() - 1;
    out.assign ((size_t) std::max (0, n), -200.0f);
    if (n <= 0 || spec.empty()) return;
    const double bw = binHz();
    const int B = (int) spec.size();
    std::vector<float> pw ((size_t) n);
    for (int x = 0; x < n; ++x)
    {
        const double f0 = edges[(size_t) x], f1 = edges[(size_t) x + 1];
        const double b0 = f0 / bw, b1 = f1 / bw;
        float p;
        if (b1 - b0 >= 1.0)                                          // several bins: the loudest
        {
            const int k0 = std::clamp ((int) std::ceil (b0), 1, B - 1), k1 = std::clamp ((int) std::floor (b1), 1, B - 1);
            p = 0; int kb = k0;
            for (int k = k0; k <= k1; ++k) if (spec[(size_t) k] > p) { p = spec[(size_t) k]; kb = k; }
            // a local maximum: the tone's true level. The exact Hann two-bin estimate (from the ratio to the
            // larger neighbour) undoes the window's scalloping, up to 1.4 dB for a tone between two bins.
            if (kb > 1 && kb < B - 1 && spec[(size_t) kb - 1] < p && spec[(size_t) kb + 1] <= p)
            {
                const double nb = std::max (spec[(size_t) kb - 1], spec[(size_t) kb + 1]);
                const double alpha = std::sqrt (nb / p);                      // amplitude ratio, 0..1
                const double d = (2 * alpha - 1) / (alpha + 1);               // offset from the bin, 0..0.5
                if (d > 1e-6)
                {
                    const double w = std::sin (kPi * d) / (kPi * d * (1 - d * d));   // Hann main-lobe response
                    if (w > 0.1) p = (float) (p / (w * w));
                }
            }
        }
        else                                                         // less than a bin: interpolate (in dB) at the centre
        {
            const double bc = std::clamp (0.5 * (b0 + b1), 1.0, (double) B - 1.001);
            const int k = (int) bc; const double t = bc - k;
            const double d0 = 10 * std::log10 (std::max (1e-20f, spec[(size_t) k])), d1 = 10 * std::log10 (std::max (1e-20f, spec[(size_t) k + 1]));
            p = (float) std::pow (10.0, (d0 + (d1 - d0) * (0.5 - 0.5 * std::cos (t * kPi))) / 10.0);
        }
        pw[(size_t) x] = p;
    }
    // fractional-octave smoothing: a box average of power over the columns in +-smoothOct/2 octaves
    if (smoothOct > 0)
    {
        std::vector<double> cum ((size_t) n + 1, 0.0);
        for (int x = 0; x < n; ++x) cum[(size_t) x + 1] = cum[(size_t) x] + pw[(size_t) x];
        const double ratio = std::pow (2.0, smoothOct / 2);
        std::vector<float> sm ((size_t) n);
        int lo = 0, hi = 0;
        for (int x = 0; x < n; ++x)
        {
            const double fc = std::sqrt ((double) edges[(size_t) x] * edges[(size_t) x + 1]);
            while (lo < x && edges[(size_t) lo + 1] < fc / ratio) ++lo;
            if (hi < x) hi = x;
            while (hi + 1 < n && edges[(size_t) hi + 1] < fc * ratio) ++hi;
            sm[(size_t) x] = (float) ((cum[(size_t) hi + 1] - cum[(size_t) lo]) / (hi - lo + 1));
        }
        pw.swap (sm);
    }
    for (int x = 0; x < n; ++x)
    {
        const double fc = std::sqrt ((double) std::max (1.0f, edges[(size_t) x]) * edges[(size_t) x + 1]);
        out[(size_t) x] = (float) (10.0 * std::log10 (std::max (1e-20f, pw[(size_t) x])) + slope * std::log2 (fc / 1000.0));
    }
}
} // namespace fft
