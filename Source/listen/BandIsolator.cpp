#include "BandIsolator.h"
#include <algorithm>

namespace
{
    constexpr double kPi = 3.14159265358979323846;
    struct Coefs { double b0, b1, b2, a1, a2; };
    Coefs butter (double sr, double f, bool high, double q = 0.70710678)
    {
        f = std::clamp (f, 10.0, sr * 0.45);
        const double w = 2 * kPi * f / sr, c = std::cos (w), a = std::sin (w) / (2 * q), a0 = 1 + a;
        if (high) return { (1 + c) / 2 / a0, -(1 + c) / a0, (1 + c) / 2 / a0, -2 * c / a0, (1 - a) / a0 };
        return { (1 - c) / 2 / a0, (1 - c) / a0, (1 - c) / 2 / a0, -2 * c / a0, (1 - a) / a0 };
    }
    Coefs bandpass (double sr, double f, double q)
    {
        f = std::clamp (f, 10.0, sr * 0.45);
        const double w = 2 * kPi * f / sr, a = std::sin (w) / (2 * q), a0 = 1 + a;
        return { a / a0, 0, -a / a0, -2 * std::cos (w) / a0, (1 - a) / a0 };
    }
}

void BandIsolator::prepare (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000;
    step = 1.0 / (0.010 * sr);                       // 10 ms ramps
    designed = { 0, 0, 0, 0 }; designedBp = 0;
    reset();
}

void BandIsolator::reset()
{
    ch = {};
    designed = { 0, 0, 0, 0 }; designedBp = 0;
    wet = 0; bpMix = 0; gain = { 1, 1, 1, 1, 1 }; modeGain = { 1, 0, 0, 0, 0 };
}

void BandIsolator::design()
{
    auto set = [] (auto& bq, const Coefs& c) { bq.b0 = c.b0; bq.b1 = c.b1; bq.b2 = c.b2; bq.a1 = c.a1; bq.a2 = c.a2; };
    for (auto& c : ch)
    {
        for (int k = 0; k < 4; ++k)
        {
            Biquad lo, hi;
            set (lo, butter (sr, designed[(size_t) k], false)); set (hi, butter (sr, designed[(size_t) k], true));
            c.lp[(size_t) k].set (lo); c.hp[(size_t) k].set (hi);
        }
        // allpasses: band0 needs splits 1, 2, 3; band1 needs 2, 3; band2 needs 3
        const int apSplit[6] = { 1, 2, 3, 2, 3, 3 };
        for (int i = 0; i < 6; ++i)
        {
            Biquad lo, hi;
            set (lo, butter (sr, designed[(size_t) apSplit[i]], false)); set (hi, butter (sr, designed[(size_t) apSplit[i]], true));
            c.ap[(size_t) i].lp.set (lo); c.ap[(size_t) i].hp.set (hi);
        }
        Biquad b; set (b, bandpass (sr, designedBp, 1.4));
        c.bp1.set (b); c.bp2.set (b);
    }
}

double BandIsolator::bandsOf (Channel& c, double x, const std::array<double, 5>& g) noexcept
{
    const double h0 = c.hp[0].run (x);
    const double b0 = c.ap[2].run (c.ap[1].run (c.ap[0].run (c.lp[0].run (x))));
    const double h1 = c.hp[1].run (h0);
    const double b1 = c.ap[4].run (c.ap[3].run (c.lp[1].run (h0)));
    const double h2 = c.hp[2].run (h1);
    const double b2 = c.ap[5].run (c.lp[2].run (h1));
    const double b3 = c.lp[3].run (h2);
    const double b4 = c.hp[3].run (h2);
    return g[0] * b0 + g[1] * b1 + g[2] * b2 + g[3] * b3 + g[4] * b4;
}

void BandIsolator::process (float* L, float* R, int n, const BandIsolatorParams& p) noexcept
{
    // new split frequencies (kept in order, at least a third of an octave apart) or band-pass centre
    std::array<float, 4> want;
    for (int k = 0; k < 4; ++k) want[(size_t) k] = p.splits[(size_t) k].load();
    for (int k = 1; k < 4; ++k) want[(size_t) k] = std::max (want[(size_t) k], want[(size_t) k - 1] * 1.26f);
    const float bpHz = p.bandpassHz.load();
    if (want != designed || bpHz != designedBp) { designed = want; designedBp = bpHz; design(); }

    const bool on = p.active();
    // fully bypassed and settled: untouched (the filters only restart from silence when switched on,
    // under the 10 ms fade-in)
    if (! on && wet <= 0.0)
    {
        if (ch[0].lp[0].a.z1 != 0 || ch[0].lp[0].a.z2 != 0) { ch = {}; design(); }   // start clean next time
        return;
    }
    const int mask = p.mask.load(), mode = std::clamp (p.mode.load(), 0, 4);
    const bool bp = p.bandpass.load();
    std::array<double, 5> target {};
    for (int k = 0; k < 5; ++k) target[(size_t) k] = (mask >> k) & 1 ? 1.0 : 0.0;

    for (int i = 0; i < n; ++i)
    {
        wet = std::clamp (wet + (on ? step : -step), 0.0, 1.0);
        bpMix = std::clamp (bpMix + (bp ? step : -step), 0.0, 1.0);
        for (int k = 0; k < 5; ++k) gain[(size_t) k] += std::clamp (target[(size_t) k] - gain[(size_t) k], -step, step);
        for (int m = 0; m < 5; ++m) modeGain[(size_t) m] += std::clamp ((m == mode ? 1.0 : 0.0) - modeGain[(size_t) m], -step, step);

        const double l = L[i], r = R[i];
        // channel mode (crossfaded): Stereo keeps L / R, the others send one signal to both sides
        const double mid = 0.5 * (l + r), side = 0.5 * (l - r);
        const double mono = modeGain[1] * mid + modeGain[2] * side + modeGain[3] * l + modeGain[4] * r;
        const double xl = modeGain[0] * l + mono, xr = modeGain[0] * r + mono;

        double yl = bandsOf (ch[0], xl, gain), yr = bandsOf (ch[1], xr, gain);
        if (bpMix > 0)
        {
            const double pl = ch[0].bp2.run (ch[0].bp1.run (xl)), pr = ch[1].bp2.run (ch[1].bp1.run (xr));
            yl += bpMix * (pl - yl); yr += bpMix * (pr - yr);
        }
        else { ch[0].bp2.run (ch[0].bp1.run (xl)); ch[1].bp2.run (ch[1].bp1.run (xr)); }   // keep the band-pass warm
        L[i] = (float) (l + wet * (yl - l));
        R[i] = (float) (r + wet * (yr - r));
    }
}
