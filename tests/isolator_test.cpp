// Band listening (VST3): bypass is bit-exact, all bands sum flat, each band isolates its range,
// Mid / Side / Left / Right, the Scope band-pass, and switching without clicks.
#include "../Source/listen/BandIsolator.h"
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void expect (const std::string& what, double got, double want, double tol)
{
    const bool ok = std::abs (got - want) <= tol;
    std::printf ("%s %-66s %9.3f (expected %8.3f +/- %.3f)\n", ok ? "PASS" : "FAIL", what.c_str(), got, want, tol);
    if (! ok) ++failures;
}
static const double tw = 2 * 3.14159265358979323846, sr = 48000;

// level (dB) of the output's last half second for a sine at f in both channels (or L only / R only)
static double run (BandIsolatorParams& p, double f, double al = 1, double ar = 1, int ch = 0, double seconds = 1.0)
{
    BandIsolator iso; iso.prepare (sr);
    const int n = (int) (sr * seconds); std::vector<float> L ((size_t) n), R ((size_t) n);
    for (int i = 0; i < n; ++i) { const double v = 0.5 * std::sin (tw * f * i / sr); L[(size_t) i] = (float) (al * v); R[(size_t) i] = (float) (ar * v); }
    for (int i = 0; i < n; i += 256) iso.process (L.data() + i, R.data() + i, std::min (256, n - i), p);
    double e = 0; for (int i = n / 2; i < n; ++i) { const double v = ch == 0 ? L[(size_t) i] : R[(size_t) i]; e += v * v; }
    return 10 * std::log10 (e / (n / 2) / 0.125 + 1e-30);    // relative to a 0.5-amplitude sine
}

int main()
{
    {
        BandIsolatorParams p; BandIsolator iso; iso.prepare (sr);
        std::vector<float> L (4096), R (4096), L0, R0;
        for (int i = 0; i < 4096; ++i) { L[(size_t) i] = (float) std::sin (i * 0.37) * 0.3f; R[(size_t) i] = (float) std::cos (i * 0.11) * 0.2f; }
        L0 = L; R0 = R;
        iso.process (L.data(), R.data(), 4096, p);
        expect ("nothing active: output identical to the input (bit-exact)", L == L0 && R == R0 ? 1 : 0, 1, 0);
    }
    for (double f : { 30.0, 100.0, 440.0, 1000.0, 3000.0, 9000.0, 15000.0 })
    {
        BandIsolatorParams p; p.mode = BandIsolatorParams::Stereo; p.mask = 31;
        p.bandpass = false;
        // all bands but forced active (Mid mode with identical channels = the same signal)
        p.mode = BandIsolatorParams::Mid;
        expect ("all five bands sum flat at " + std::to_string ((int) f) + " Hz (dB)", run (p, f), 0.0, 0.02);
    }
    {
        BandIsolatorParams p;
        const double centre[5] = { 35, 125, 700, 3500, 12000 }, far[5] = { 4000, 8000, 40, 120, 300 };
        for (int b = 0; b < 5; ++b)
        {
            p.mask = 1 << b;
            // a narrow band (2-6 kHz is 1.6 octaves) dips a little in the middle: the two 24 dB/oct skirts overlap
            expect ("band " + std::to_string (b + 1) + " alone: its centre (" + std::to_string ((int) centre[b]) + " Hz) passes, dB", run (p, centre[b]), -1.0, 1.0);
            expect ("band " + std::to_string (b + 1) + " alone: " + std::to_string ((int) far[b]) + " Hz is gone (below -40 dB)", run (p, far[b]) < -40 ? 1 : 0, 1, 0);
        }
        p.mask = 0b00110;   // bands 2 and 3 together: flat across their joint (250 Hz)
        expect ("two neighbouring bands together: flat across their split (250 Hz)", run (p, 250), 0.0, 0.1);
    }
    {
        BandIsolatorParams p;
        p.mode = BandIsolatorParams::Side;
        expect ("Side of a mono signal: silent", run (p, 1000) < -100 ? 1 : 0, 1, 0);
        expect ("Side of an inverted pair: full level", run (p, 1000, 1, -1), 0.0, 0.05);
        p.mode = BandIsolatorParams::Left;
        expect ("Left only, heard in the right channel too", run (p, 1000, 1, 0, 1), 0.0, 0.05);
        p.mode = BandIsolatorParams::Right;
        expect ("Right only: left-only input is silent", run (p, 1000, 1, 0, 0) < -100 ? 1 : 0, 1, 0);
        p.mode = BandIsolatorParams::Mid;
        expect ("Mid of an inverted pair: silent", run (p, 1000, 1, -1) < -100 ? 1 : 0, 1, 0);
    }
    {
        BandIsolatorParams p; p.bandpass = true; p.bandpassHz = 100;
        expect ("Scope band-pass at 100 Hz: 100 Hz passes", run (p, 100), 0.0, 0.1);
        expect ("Scope band-pass at 100 Hz: 1 kHz is down more than 30 dB", run (p, 1000) < -30 ? 1 : 0, 1, 0);
    }
    {
        // switching a band off mid-stream: the biggest sample-to-sample jump stays small (10 ms fade)
        BandIsolatorParams p; p.mode = BandIsolatorParams::Mid; BandIsolator iso; iso.prepare (sr);
        const int n = 48000; std::vector<float> L (n), R (n);
        for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = (float) (0.5 * std::sin (tw * 100 * i / sr));
        double maxStep = 0; float prev = 0;
        for (int i = 0; i < n; i += 128)
        {
            if (i == 24000) p.mask = 31 & ~2;            // band 2 (60-250 Hz, where the tone is) off
            iso.process (L.data() + i, R.data() + i, 128, p);
            for (int k = i; k < i + 128; ++k) { maxStep = std::max (maxStep, (double) std::abs (L[(size_t) k] - prev)); prev = L[(size_t) k]; }
        }
        const double sineStep = 0.5 * tw * 100 / sr;   // the largest step of the tone itself
        expect ("muting a band fades (no click): largest step vs the tone's own", maxStep / sineStep, 1.0, 0.1);
    }
    std::printf (failures ? "%d FAILURES\n" : "all isolator checks passed\n", failures);
    return failures ? 1 : 0;
}
