#include "MonitorModel.h"
#include <algorithm>
#include <cstdint>

namespace monitor
{
static constexpr double kPi = 3.14159265358979323846;

Biquad Biquad::lowpass (double sr, double f, double q)
{
    const double w = 2 * kPi * f / sr, c = std::cos (w), a = std::sin (w) / (2 * q), a0 = 1 + a;
    Biquad b; b.b0 = (1 - c) / 2 / a0; b.b1 = (1 - c) / a0; b.b2 = b.b0; b.a1 = -2 * c / a0; b.a2 = (1 - a) / a0;
    return b;
}

Biquad Biquad::highpass (double sr, double f, double q)
{
    const double w = 2 * kPi * f / sr, c = std::cos (w), a = std::sin (w) / (2 * q), a0 = 1 + a;
    Biquad b; b.b0 = (1 + c) / 2 / a0; b.b1 = -(1 + c) / a0; b.b2 = b.b0; b.a1 = -2 * c / a0; b.a2 = (1 - a) / a0;
    return b;
}

void MonitorModel::configure (double sampleRate)
{
    sr = sampleRate > 0 ? sampleRate : 48000;
    loud.configure (sr, 2);
    peakFall = std::pow (10.0, -20.0 / 20.0 / sr);             // 20 dB/s
    polarK = 1.0 / (sr * 0.3);                                 // energy per direction, 300 ms
    rmsK = 1.0 - std::exp (-1.0 / (sr * rmsTime));
    corrFast.setTime (sr, 0.3);
    corrSlow.setTime (sr, 3.0);
    for (auto& b : bands) b.setTime (sr, 0.4);
    // Linkwitz-Riley (two Butterworth stages) splits at 250 Hz and 2.5 kHz
    for (int c = 0; c < 2; ++c)
    {
        lowA[(size_t) c] = lowB[(size_t) c] = Biquad::lowpass (sr, kLowSplit);
        midHpA[(size_t) c] = midHpB[(size_t) c] = Biquad::highpass (sr, kLowSplit);
        midLpA[(size_t) c] = midLpB[(size_t) c] = Biquad::lowpass (sr, kHighSplit);
        highA[(size_t) c] = highB[(size_t) c] = Biquad::highpass (sr, kHighSplit);
    }
    ptL.assign ((size_t) kPoints, 0.0f); ptR.assign ((size_t) kPoints, 0.0f); ptCount = 0;
    polar.fill (0);
    lv = {}; rmsSq = {}; holdLeft = {};
    corrFast.clear(); corrSlow.clear(); for (auto& b : bands) b.clear();
    reset();
}

void MonitorModel::reset()
{
    loud.reset();
    for (auto& l : lv) { l.maxPeak = l.maxRms = 0; l.clips = 0; }
    corrMin = 1.0; corrSeen = 0;
    hist.assign ((size_t) kHistory, Entry { -200, -200, -1, 0, -200, -200 });
    histCount = 0; lastBlocks = 0;
    psrLow = psrHigh = kNone;
}

void MonitorModel::push (const float* l, const float* r, int n)
{
    // in 10 ms pieces, so the 100 ms history follows the loudness meter's blocks closely
    const int piece = std::max (1, (int) (sr * 0.01));
    for (int done = 0; done < n; )
    {
        const int k = std::min (piece, n - done);
        block (l + done, r + done, k);
        done += k;
    }
}

void MonitorModel::block (const float* l, const float* r, int n)
{
    loud.process (l, r, n);
    const int holdSamples = holdTime < 0 ? INT32_MAX : (int) (sr * holdTime);

    for (int i = 0; i < n; ++i)
    {
        const double x[4] = { l[i], r[i], 0.5 * (l[i] + r[i]), 0.5 * (l[i] - r[i]) };
        for (int c = 0; c < 4; ++c)
        {
            auto& m = lv[(size_t) c];
            const double a = std::abs (x[c]);
            m.peak = std::max (a, m.peak * peakFall);
            if (a >= m.hold) { m.hold = a; holdLeft[(size_t) c] = holdSamples; }
            else if (--holdLeft[(size_t) c] < 0) m.hold *= peakFall;
            rmsSq[(size_t) c] += rmsK * (x[c] * x[c] - rmsSq[(size_t) c]);
            if (a > m.maxPeak) m.maxPeak = a;
            if (c < 2 && a >= 0.99999) ++m.clips;
        }
        corrFast.add (x[0], x[1]);
        corrSlow.add (x[0], x[1]);

        double lo[2], mi[2], hi[2];
        for (int c = 0; c < 2; ++c)
        {
            const auto cs = (size_t) c;
            lo[c] = lowB[cs].run (lowA[cs].run (x[c]));
            mi[c] = midLpB[cs].run (midLpA[cs].run (midHpB[cs].run (midHpA[cs].run (x[c]))));
            hi[c] = highB[cs].run (highA[cs].run (x[c]));
        }
        bands[0].add (lo[0], lo[1]); bands[1].add (mi[0], mi[1]); bands[2].add (hi[0], hi[1]);

        // sound field: ring of points, and the loudest magnitude per direction (polar level)
        const auto w = (size_t) (ptCount & (kPoints - 1));
        ptL[w] = l[i]; ptR[w] = r[i]; ++ptCount;
        const double px = (x[1] - x[0]) * 0.70710678, py = (x[0] + x[1]) * 0.70710678;
        const double mag = std::sqrt (px * px + py * py);
        if (mag > 1e-5)
        {
            double ang = std::atan2 (py, px);                         // -pi..pi
            if (ang < 0) ang += kPi;                                  // fold the lower half up (polarity-free)
            const int b = std::clamp ((int) std::lround ((1.0 - ang / kPi) * (kPolarBins - 1)), 0, kPolarBins - 1);
            polar[(size_t) b] += mag * mag * polarK;
        }
    }
    const double fallN = std::exp (-(double) n * polarK);
    for (auto& p : polar) p *= fallN;
    for (int c = 0; c < 4; ++c)
    {
        lv[(size_t) c].rms = std::sqrt (std::max (0.0, rmsSq[(size_t) c]));
        lv[(size_t) c].maxRms = std::max (lv[(size_t) c].maxRms, lv[(size_t) c].rms);
    }
    // the minimum only counts while there's real signal and the slow average has settled
    if (corrSlow.energy() > 1e-6 && ++corrSeen > (long long) (1.0 * sr / std::max (1, n)))
        corrMin = std::min (corrMin, corrFast.value());

    while (lastBlocks < loud.blocksSeen()) { ++lastBlocks; step100ms(); }
}

void MonitorModel::step100ms()
{
    auto f = [] (double v) { return (float) (v > -200 ? v : -200); };
    const double psr = loud.getPSR();
    Entry e { f (loud.getShortTerm()), f (loud.getMomentary()), psr > kNone ? (float) psr : -1.0f,
              (float) corrFast.value(), f (loud.getRecentTruePeak()), f (loud.getIntegrated()) };
    hist[(size_t) (histCount % kHistory)] = e;
    ++histCount;
    // extremes of PSR only while the music is near its loudest (ignores fades, breaks and silence)
    const double st = loud.getShortTerm(), stMax = loud.getShortTermMax();
    if (psr > kNone && st > -60 && st >= stMax - 6.0)
    {
        psrLow  = psrLow  > kNone ? std::min (psrLow, psr)  : psr;
        psrHigh = psrHigh > kNone ? std::max (psrHigh, psr) : psr;
    }
}

bool MonitorModel::bandActive (int b) const noexcept
{
    // about -70 dBFS, and no more than 20 dB below the whole signal (filter leakage doesn't count)
    const double e = bands[(size_t) b].energy();
    return e > 1e-7 && e > 0.01 * corrFast.energy();
}

double MonitorModel::balanceDb() const noexcept
{
    const double l = rmsSq[0], r = rmsSq[1];
    if (l + r < 1e-10) return 0;
    return 10.0 * std::log10 (std::max (r, 1e-12) / std::max (l, 1e-12));
}

double MonitorModel::width() const noexcept
{
    const double m = rmsSq[2], s = rmsSq[3];
    return m + s > 1e-10 ? 2.0 * s / (m + s) : 0.0;
}
} // namespace monitor
