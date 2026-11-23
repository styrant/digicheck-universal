// EBU Tech 3341 / 3342-style checks for the loudness meter.
#include "../Source/LoudnessMeter.h"
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;
static void expect (const char* what, double got, double want, double tol)
{
    const bool ok = std::abs (got - want) <= tol;
    std::printf ("%s %-58s %8.2f (expected %6.2f +/- %.1f)\n", ok ? "PASS" : "FAIL", what, got, want, tol);
    if (! ok) ++failures;
}

template <typename Gen>
static LoudnessMeter run (double sr, int ch, double seconds, Gen gen)
{
    LoudnessMeter m; m.configure (sr, ch);
    const int block = 1000; std::vector<float> L (block), R (block);
    long long pos = 0;
    for (long long done = 0; done < (long long) (sr * seconds); done += block)
    {
        for (int i = 0; i < block; ++i, ++pos) { const auto s = gen (pos / sr); L[(size_t) i] = s.first; R[(size_t) i] = s.second; }
        m.process (L.data(), R.data(), block);
    }
    return m;
}

int main()
{
    const double tw = 2 * 3.14159265358979323846;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // Tech 3341 #1: stereo 1 kHz sine at -23 dBFS -> -23 LUFS (short-term and integrated)
        const double a = std::pow (10.0, -23.0 / 20.0);
        auto m = run (sr, 2, 20, [&] (double t) { const float v = (float) (a * std::sin (tw * 1000 * t)); return std::make_pair (v, v); });
        char buf[96];
        std::snprintf (buf, sizeof buf, "%.1f kHz: 1 kHz -23 dBFS stereo, integrated", sr / 1000); expect (buf, m.getIntegrated(), -23.0, 0.1);
        std::snprintf (buf, sizeof buf, "%.1f kHz: 1 kHz -23 dBFS stereo, short-term", sr / 1000); expect (buf, m.getShortTerm(), -23.0, 0.1);

        // Relative gate: 10 s at -36 dBFS then 10 s at -16 dBFS (both 1 kHz) -> quiet part gated out -> about -16
        const double q = std::pow (10.0, -36.0 / 20.0), l = std::pow (10.0, -16.0 / 20.0);
        auto g = run (sr, 2, 20, [&] (double t) { const float v = (float) ((t < 10 ? q : l) * std::sin (tw * 1000 * t)); return std::make_pair (v, v); });
        std::snprintf (buf, sizeof buf, "%.1f kHz: -36 then -16 dBFS, relative gate", sr / 1000); expect (buf, g.getIntegrated(), -16.0, 0.2);

        // True peak: 0 dBFS sine at fs/4 with 45 deg phase -> samples at -3 dB, true peak 0 dBTP
        auto tp = run (sr, 1, 2, [&] (double t) { const float v = (float) std::sin (tw * (sr / 4) * t + tw / 8); return std::make_pair (v, v); });
        std::snprintf (buf, sizeof buf, "%.1f kHz: fs/4 sine, sample peak -3 dB, true peak", sr / 1000); expect (buf, tp.getTruePeak(), 0.0, 0.2);

        // True peak on a plain 997 Hz sine at -6 dBFS
        const double h = std::pow (10.0, -6.0 / 20.0);
        auto tp2 = run (sr, 2, 2, [&] (double t) { const float v = (float) (h * std::sin (tw * 997 * t)); return std::make_pair (v, v); });
        std::snprintf (buf, sizeof buf, "%.1f kHz: 997 Hz -6 dBFS, true peak", sr / 1000); expect (buf, tp2.getTruePeak(), -6.0, 0.1);
    }
    // Mono: one channel at -23 dBFS reads -26 LUFS (half the energy of the stereo case)
    const double a = std::pow (10.0, -23.0 / 20.0);
    auto mono = run (48000, 1, 10, [&] (double t) { const float v = (float) (a * std::sin (tw * 1000 * t)); return std::make_pair (v, v); });
    expect ("48 kHz: mono 1 kHz -23 dBFS, integrated", mono.getIntegrated(), -26.0, 0.1);

    // EBU Tech 3342 loudness range: 1 kHz stereo, 20 s at one level then 20 s at another
    auto lraCase = [&] (double a1, double a2, double want, const char* what)
    {
        // a1 / a2 are LUFS: a 1 kHz sine at -x dBFS on both channels reads -x LUFS
        const double s1 = std::pow (10.0, a1 / 20.0), s2 = std::pow (10.0, a2 / 20.0);
        auto m = run (48000, 2, 40, [&] (double t) { const float v = (float) ((t < 20 ? s1 : s2) * std::sin (tw * 1000 * t)); return std::make_pair (v, v); });
        expect (what, m.getLoudnessRange(), want, 1.0);
    };
    lraCase (-20, -30, 10, "Tech 3342 #1: -20 then -30 LUFS, LRA");
    lraCase (-20, -15, 5,  "Tech 3342 #2: -20 then -15 LUFS, LRA");
    lraCase (-40, -20, 20, "Tech 3342 #3: -40 then -20 LUFS, LRA");

    // momentary, PSR and PLR on a steady sine: peak -6 dBTP, loudness -6 LUFS (1 kHz on both channels
    // reads its dBFS level), so PSR = PLR = 0; a 1 kHz sine at -20 dBFS with one 0 dBFS click: PSR 20 near it
    {
        const double h = std::pow (10.0, -6.0 / 20.0);
        auto m = run (48000, 2, 6, [&] (double t) { const float v = (float) (h * std::sin (tw * 1000 * t)); return std::make_pair (v, v); });
        expect ("steady sine -6 dBFS: momentary", m.getMomentary(), -6.0, 0.1);
        expect ("steady sine -6 dBFS: PSR", m.getPSR(), 0.0, 0.2);
        expect ("steady sine -6 dBFS: PLR", m.getPLR(), 0.0, 0.2);
        const double q = std::pow (10.0, -20.0 / 20.0);
        auto c = run (48000, 2, 6, [&] (double t) { const float v = (float) (q * std::sin (tw * 1000 * t)) + ((t > 4.0 && t < 4.0 + 1.5 / 48000) ? 0.9f : 0.0f); return std::make_pair (v, v); });
        expect ("sine -20 dBFS + one click to ~0 dBFS: PSR", c.getPSR(), 20.0, 1.0);
        auto later = run (48000, 2, 9, [&] (double t) { const float v = (float) (q * std::sin (tw * 1000 * t)) + ((t > 4.0 && t < 4.0 + 1.5 / 48000) ? 0.9f : 0.0f); return std::make_pair (v, v); });
        expect ("... 5 s later the click has left the 3 s window: PSR", later.getPSR(), 0.0, 0.3);
        expect ("... but PLR keeps the click (true peak since reset)", later.getPLR(), 20.0, 1.0);
    }

    {
        // gate: 10 s at -36 then 10 s at -16 dBFS. Program-gated (default) about -16; ungated (BS.1770-1) the
        // plain mean: 10*log10((10^-3.6 + 10^-1.6)/2) = -18.97 LUFS. LRA ends: the two levels
        const double q = std::pow (10.0, -36.0 / 20.0), l = std::pow (10.0, -16.0 / 20.0);
        auto g = run (48000, 2, 20, [&] (double t) { const float v = (float) ((t < 10 ? q : l) * std::sin (tw * 1000 * t)); return std::make_pair (v, v); });
        expect ("program gate: integrated", g.getIntegrated(), -16.0, 0.2);
        g.setProgramGate (false);
        expect ("gate off (BS.1770-1): integrated is the plain mean", g.getIntegrated(), -18.97, 0.2);
        expect ("LRA low end (10th percentile) near the quiet part", g.getRangeLow(), -36.0, 1.5);
        expect ("LRA high end (95th percentile) at the loud part", g.getRangeHigh(), -16.0, 0.5);
    }
    std::printf (failures ? "%d FAILURES\n" : "all loudness checks passed\n", failures);
    return failures ? 1 : 0;
}
