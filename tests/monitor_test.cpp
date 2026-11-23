// The Monitor page's measurements: levels, correlation (wide band and per band), balance, width,
// sound-field folding, PSR / PLR history and extremes.
#include "../Source/monitor/MonitorModel.h"
#include <cstdio>
#include <functional>
#include <utility>

static int failures = 0;
static void expect (const char* what, double got, double want, double tol)
{
    const bool ok = std::abs (got - want) <= tol;
    std::printf ("%s %-62s %8.3f (expected %7.3f +/- %.3f)\n", ok ? "PASS" : "FAIL", what, got, want, tol);
    if (! ok) ++failures;
}

static const double tw = 2 * 3.14159265358979323846, sr = 48000;
using Gen = std::function<std::pair<double, double> (double t, long long i)>;

static void run (monitor::MonitorModel& m, double seconds, Gen gen, double t0 = 0)
{
    std::vector<float> L (777), R (777);                       // odd block size on purpose
    long long i = (long long) (t0 * sr);
    for (long long done = 0; done < (long long) (seconds * sr); done += 777)
    {
        for (int k = 0; k < 777; ++k, ++i) { auto s = gen (i / sr, i); L[(size_t) k] = (float) s.first; R[(size_t) k] = (float) s.second; }
        m.push (L.data(), R.data(), 777);
    }
}

static unsigned rng = 12345;
static double noise() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) / 8388608.0) - 1.0; }

int main()
{
    using M = monitor::MonitorModel;
    {
        M m; m.configure (sr);
        run (m, 2, [] (double t, long long) { const double v = 0.5 * std::sin (tw * 1000 * t); return std::make_pair (v, v); });
        expect ("mono sine 0.5: peak L (dB)", M::toDb (m.level (M::L).peak), -6.02, 0.05);
        expect ("mono sine 0.5: RMS L (dB, sine = peak - 3)", M::toDb (m.level (M::L).rms), -9.03, 0.1);
        expect ("mono sine: side level is silent (dB below -100)", M::toDb (m.level (M::S).peak) < -100 ? 1 : 0, 1, 0);
        expect ("mono sine: correlation +1", m.correlation(), 1.0, 0.001);
        expect ("mono sine: width 0", m.width(), 0.0, 0.001);
        expect ("mono sine: balance 0 dB", m.balanceDb(), 0.0, 0.01);
        expect ("mono sine: mid band active, low band not", (m.bandActive (M::Mid) && ! m.bandActive (M::Low)) ? 1 : 0, 1, 0);
        // the polar level of a mono signal points straight up (middle bin)
        int best = 0; for (int b = 0; b < M::kPolarBins; ++b) if (m.polarLevel (b) > m.polarLevel (best)) best = b;
        expect ("mono sine: polar level peaks in the centre bin", best, M::kPolarBins / 2, 0);
    }
    {
        M m; m.configure (sr);
        run (m, 2, [] (double t, long long) { const double v = 0.5 * std::sin (tw * 440 * t); return std::make_pair (v, -v); });
        expect ("inverted polarity: correlation -1", m.correlation(), -1.0, 0.001);
        expect ("inverted polarity: width 2", m.width(), 2.0, 0.001);
        int best = 0; for (int b = 0; b < M::kPolarBins; ++b) if (m.polarLevel (b) > m.polarLevel (best)) best = b;
        expect ("inverted polarity: polar level at an edge", (best == 0 || best == M::kPolarBins - 1) ? 1 : 0, 1, 0);
        expect ("inverted polarity: minimum correlation recorded", m.correlationMin(), -1.0, 0.01);
    }
    {
        M m; m.configure (sr);
        run (m, 4, [] (double, long long) { return std::make_pair (0.3 * noise(), 0.3 * noise()); });
        expect ("independent noise L/R: correlation about 0", m.correlation(), 0.0, 0.08);
        expect ("independent noise L/R: width about 1", m.width(), 1.0, 0.08);
    }
    {
        M m; m.configure (sr);
        // left only, 6 dB louder than right (same signal): balance -6 dB, correlation +1
        run (m, 2, [] (double t, long long) { const double v = std::sin (tw * 500 * t); return std::make_pair (0.5 * v, 0.25 * v); });
        expect ("left 6 dB louder: balance -6 dB", m.balanceDb(), -6.02, 0.05);
        expect ("left 6 dB louder: still fully correlated", m.correlation(), 1.0, 0.001);
    }
    {
        // bands: a mono 1 kHz tone plus a 60 Hz tone in opposite polarity -> lows -1, mids +1
        M m; m.configure (sr);
        run (m, 3, [] (double t, long long) { const double a = 0.3 * std::sin (tw * 1000 * t), b = 0.3 * std::sin (tw * 60 * t);
                                              return std::make_pair (a + b, a - b); });
        expect ("60 Hz inverted + 1 kHz mono: low band correlation", m.bandCorrelation (M::Low), -1.0, 0.05);
        expect ("60 Hz inverted + 1 kHz mono: mid band correlation", m.bandCorrelation (M::Mid), 1.0, 0.05);
        expect ("... wide band is in between", m.correlation(), 0.0, 0.2);
    }
    {
        // dynamics: a -20 dBFS sine with a short burst to -2 dBFS every second -> PSR ~ 18 dB at all times
        M m; m.configure (sr);
        auto gen = [] (double t, long long) { const double frac = t - std::floor (t);
                                              const double a = frac < 0.005 ? 0.79 : 0.1; const double v = a * std::sin (tw * 1000 * t); return std::make_pair (v, v); };
        run (m, 10, gen);
        const double psr = m.loudness().getPSR();
        expect ("bursts every second over a -20 dBFS sine: PSR about 17 dB", psr, 17.0, 1.5);
        expect ("... PLR about the same", m.loudness().getPLR(), 17.0, 1.5);
        expect ("... history has one entry per 100 ms", (double) m.historyCount(), 100, 1);
        expect ("... the newest history PSR matches", m.history (0).psr, psr, 0.01);
        expect ("... PSR min/max recorded while loud", (m.psrMin() > 15 && m.psrMax() < 20) ? 1 : 0, 1, 0);
        // now squash it: a steady -2 dBFS sine -> PSR falls to ~0 (and PLR follows integrated up)
        run (m, 6, [] (double t, long long) { const double v = 0.79 * std::sin (tw * 1000 * t); return std::make_pair (v, v); }, 10);
        expect ("then a steady -2 dBFS sine: PSR ~ 0 dB", m.loudness().getPSR(), 0.0, 0.3);
        expect ("... psrMin catches the squashed part", m.psrMin(), 0.0, 0.5);
        m.reset();
        expect ("reset clears the history", (double) m.historyCount(), 0, 0);
        expect ("reset clears PLR", m.loudness().getPLR() == M::kNone ? 1 : 0, 1, 0);
    }
    {
        // the sound-field ring keeps the newest samples in order
        M m; m.configure (sr);
        run (m, 1, [] (double, long long i) { return std::make_pair ((double) (i % 1000) / 1000.0, 0.0); });
        const long long w = m.pointsWritten();
        expect ("points: all samples counted", (double) w, 48174, 1);     // 62 blocks of 777
        expect ("points: newest sample in the ring", m.pointsL()[(size_t) ((w - 1) & (M::kPoints - 1))], ((w - 1) % 1000) / 1000.0, 1e-6);
    }
    std::printf (failures ? "%d FAILURES\n" : "all monitor checks passed\n", failures);
    return failures ? 1 : 0;
}
