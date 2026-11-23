#include "LoudnessMeter.h"
#include <algorithm>

static constexpr double kPi = 3.14159265358979323846;

void LoudnessMeter::configure (double sampleRate, int numChannels)
{
    sr = sampleRate;
    nch = numChannels == 1 ? 1 : 2;

    // K-weighting for any sample rate (BS.1770 stage 1 shelf + stage 2 RLB high-pass)
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = (Vh + Vb * K / Q + K * K) / a0; b.b1 = 2.0 * (K * K - Vh) / a0; b.b2 = (Vh - Vb * K / Q + K * K) / a0;
        b.a1 = 2.0 * (K * K - 1.0) / a0;      b.a2 = (1.0 - K / Q + K * K) / a0;
        pre[0] = pre[1] = b;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773, K = std::tan (kPi * f0 / sr);
        const double d = 1.0 + K / Q + K * K;
        Biquad b;
        b.b0 = 1.0; b.b1 = -2.0; b.b2 = 1.0;
        b.a1 = 2.0 * (K * K - 1.0) / d; b.a2 = (1.0 - K / Q + K * K) / d;
        rlb[0] = rlb[1] = b;
    }
    subLen = std::max (1, (int) std::lround (sr * 0.1));

    // True peak: oversample to >= 192 kHz (4x at 44.1/48 k, 2x at 88.2/96 k)
    os = sr < 80000 ? 4 : (sr < 160000 ? 2 : 1);
    taps = 12;                               // per phase; the filter is odd-length (os*taps+1) so the
    const int T = taps + 1;                  // interpolated points fall exactly at k/os between samples
    fir.assign ((size_t) (os * T), 0.0);
    if (os > 1)
    {
        const int L = os * taps + 1;
        const double M = os * taps / 2.0, fc = 0.5 / os * 0.92;
        for (int n = 0; n < L; ++n)
        {
            const double x = n - M;
            const double sinc = std::abs (x) < 1e-9 ? 2 * fc : std::sin (2 * kPi * fc * x) / (kPi * x);
            const double w = 0.42 - 0.5 * std::cos (2 * kPi * n / (L - 1)) + 0.08 * std::cos (4 * kPi * n / (L - 1));
            fir[(size_t) n] = sinc * w * os;
        }
        for (int p = 0; p < os; ++p)       // normalise every phase to unity DC gain
        {
            double s = 0;
            for (int k = 0; k < T; ++k) s += fir[(size_t) (p + k * os)];
            for (int k = 0; k < T; ++k) fir[(size_t) (p + k * os)] /= s;
        }
    }
    for (auto& h : hist) h.assign ((size_t) (taps + 1) * 2, 0.0f);
    reset();
}

void LoudnessMeter::reset()
{
    for (int c = 0; c < 2; ++c) { pre[c].z1 = pre[c].z2 = rlb[c].z1 = rlb[c].z2 = 0; subSum[c] = 0; }
    subFill = 0; subCount = 0;
    subBlocks.assign (30, 0.0);
    gatedBlocks.clear();
    shortTerm = integrated = momentary = momentaryMax = shortTermMax = kNone;
    lra = 0;
    stHist.assign ((size_t) kHistBins, 0);
    stEnergySum = 0; stCount = 0;
    tpBlocks.assign (30, 0.0);
    tpSub = 0; tpCh[0] = tpCh[1] = 0;
    for (auto& h : hist) std::fill (h.begin(), h.end(), 0.0f);
    histPos = 0;
    truePeakMax = 0;
}

double LoudnessMeter::truePeakOf (int ch, float x) noexcept
{
    auto& h = hist[ch];
    const int T = taps + 1;
    h[(size_t) histPos] = x; h[(size_t) (histPos + T)] = x;         // doubled ring: newest at histPos+T
    double peak = std::abs ((double) x);
    if (os > 1)
        for (int p = 0; p < os; ++p)
        {
            double y = 0;
            for (int k = 0; k < T; ++k) y += fir[(size_t) (p + k * os)] * h[(size_t) (histPos + T - k)];
            peak = std::max (peak, std::abs (y));
        }
    return peak;
}

void LoudnessMeter::process (const float* L, const float* R, int n)
{
    for (int i = 0; i < n; ++i)
    {
        const float in[2] = { L[i], R[i] };
        for (int c = 0; c < nch; ++c)
        {
            const double y = rlb[c].run (pre[c].run (in[c]));
            subSum[c] += y * y;
            const double tp = truePeakOf (c, in[c]);
            tpSub = std::max (tpSub, tp);
            tpCh[c] = std::max (tpCh[c], tp);
        }
        histPos = (histPos + 1) % (taps + 1);
        if (++subFill >= subLen) endSubBlock();
    }
}

void LoudnessMeter::endSubBlock()
{
    double e = 0;
    for (int c = 0; c < nch; ++c) { e += subSum[c] / subFill; subSum[c] = 0; }     // channel weights 1.0 (L, R)
    subFill = 0;
    subBlocks[(size_t) (subCount % 30)] = e;
    tpBlocks[(size_t) (subCount % 30)] = tpSub;
    truePeakMax = std::max (truePeakMax, tpSub);
    tpSub = 0;
    ++subCount;

    auto lufs = [] (double z) { return z > 0 ? -0.691 + 10.0 * std::log10 (z) : kNone; };

    if (subCount >= 4)          // 400 ms block, 75 % overlap -> gating block for integrated loudness
    {
        double z = 0;
        for (int k = 0; k < 4; ++k) z += subBlocks[(size_t) ((subCount - 1 - k) % 30)];
        z /= 4;
        momentary = lufs (z);
        momentaryMax = std::max (momentaryMax, momentary);
        if (lufs (z) > -70.0) { gatedBlocks.push_back (z); updateIntegrated(); }
    }
    if (subCount >= 30)
    {
        double z = 0;
        for (double v : subBlocks) z += v;
        shortTerm = lufs (z / 30);
        shortTermMax = std::max (shortTermMax, shortTerm);
        if (shortTerm > kHistLo)                 // loudness range input: one short-term value every 100 ms
        {
            const int b = std::clamp ((int) ((shortTerm - kHistLo) / kHistStep), 0, kHistBins - 1);
            ++stHist[(size_t) b];
            stEnergySum += z / 30; ++stCount;
            if (subCount % 10 == 0) updateRange();     // once a second is plenty
        }
    }
}

void LoudnessMeter::updateIntegrated()
{
    double sum = 0;
    for (double z : gatedBlocks) sum += z;
    const double relGate = -0.691 + 10.0 * std::log10 (sum / (double) gatedBlocks.size()) - 10.0;
    const double zGate = std::pow (10.0, (relGate + 0.691) / 10.0);
    double s2 = 0; size_t n2 = 0;
    for (double z : gatedBlocks) if (z > zGate) { s2 += z; ++n2; }
    integrated = n2 > 0 ? -0.691 + 10.0 * std::log10 (s2 / (double) n2) : kNone;
}

double LoudnessMeter::getRecentTruePeak() const noexcept
{
    double m = tpSub;
    for (double v : tpBlocks) m = std::max (m, v);
    return m > 0 ? 20.0 * std::log10 (m) : kNone;
}

// EBU Tech 3342: short-term values above -70 LUFS, then above (their power mean - 20 LU); LRA is the
// spread between the 10th and the 95th percentile of what's left.
void LoudnessMeter::updateRange()
{
    if (stCount < 2) { lra = 0; return; }
    const double rel = -0.691 + 10.0 * std::log10 (stEnergySum / (double) stCount) - 20.0;
    const int first = std::clamp ((int) std::ceil ((rel - kHistLo) / kHistStep), 0, kHistBins);
    long long n = 0;
    for (int b = first; b < kHistBins; ++b) n += stHist[(size_t) b];
    if (n < 2) { lra = 0; return; }
    auto percentile = [&] (double p)
    {
        const long long want = (long long) std::floor (p * (double) (n - 1));
        long long acc = 0;
        for (int b = first; b < kHistBins; ++b)
        {
            acc += stHist[(size_t) b];
            if (acc > want) return kHistLo + (b + 0.5) * kHistStep;
        }
        return kHistLo + kHistBins * kHistStep;
    };
    lra = std::max (0.0, percentile (0.95) - percentile (0.10));
}
