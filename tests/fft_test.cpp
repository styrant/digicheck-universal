// The FFT analyzer: calibration, channels, averaging, hold, curve mapping, slope and smoothing.
#include "../Source/fft/FftAnalyzer.h"
#include <cstdio>
#include <string>

static int failures = 0;
static void expect (const std::string& what, double got, double want, double tol)
{
    const bool ok = std::abs (got - want) <= tol;
    std::printf ("%s %-64s %9.3f (expected %8.3f +/- %.3f)\n", ok ? "PASS" : "FAIL", what.c_str(), got, want, tol);
    if (! ok) ++failures;
}
static const double tw = 2 * 3.14159265358979323846;

template <typename G> static void feed (fft::FftAnalyzer& a, int input, double sr, double seconds, G gen)
{
    std::vector<float> L (500), R (500);
    long long i = 0;
    for (long long done = 0; done < (long long) (sr * seconds); done += 500)
    {
        for (int k = 0; k < 500; ++k, ++i) { auto s = gen (i / sr); L[(size_t) k] = (float) s.first; R[(size_t) k] = (float) s.second; }
        a.push (input, L.data(), R.data(), 500);
    }
}
static double peakDb (const std::vector<float>& p) { float m = 0; for (float v : p) m = std::max (m, v); return 10 * std::log10 (std::max (1e-20f, m)); }

int main()
{
    for (int order : { 11, 13, 15 })
    {
        fft::FftAnalyzer a; a.configure (48000, order);
        a.setSeries (0, true, 0, fft::Stereo); a.setSeries (1, true, 0, fft::Side); a.setSeries (2, true, 0, fft::Left);
        a.setAveraging (0);
        const double f = std::round (1000 / a.binHz()) * a.binHz();             // on a bin: no scalloping
        feed (a, 0, 48000, 2, [f] (double t) { const double v = 0.5 * std::sin (tw * f * t); return std::make_pair (v, v); });
        const auto n = std::to_string (1 << order);
        expect ("N=" + n + ": mono sine -6 dBFS, stereo curve peak", peakDb (a.power (0)), -6.02, 0.1);
        expect ("N=" + n + ": ... left", peakDb (a.power (2)), -6.02, 0.1);
        expect ("N=" + n + ": ... side is silent (below -120)", peakDb (a.power (1)) < -120 ? 1 : 0, 1, 0);
    }
    {
        // left only at -6 dBFS: the stereo curve (mean power of L and R) reads -9 dB, side -12 dB
        fft::FftAnalyzer a; a.configure (48000, 13);
        a.setSeries (0, true, 0, fft::Stereo); a.setSeries (1, true, 0, fft::Side); a.setSeries (2, true, 0, fft::Right);
        a.setAveraging (0);
        feed (a, 0, 48000, 1, [] (double t) { return std::make_pair (0.5 * std::sin (tw * 440 * t), 0.0); });
        expect ("left only -6 dBFS: stereo curve -9 dB", peakDb (a.power (0)), -9.03, 0.1);
        expect ("left only -6 dBFS: side (L-R)/2 -12 dB", peakDb (a.power (1)), -12.04, 0.1);
        expect ("left only: right silent", peakDb (a.power (2)) < -120 ? 1 : 0, 1, 0);
    }
    {
        // the sidechain input is analysed on its own
        fft::FftAnalyzer a; a.configure (48000, 12);
        a.setSeries (0, true, 0, fft::Stereo); a.setSeries (1, true, 1, fft::Stereo);
        a.setAveraging (0);
        feed (a, 0, 48000, 1, [] (double t) { const double v = 0.25 * std::sin (tw * 200 * t); return std::make_pair (v, v); });
        const double f5 = std::round (5000 / a.binHz()) * a.binHz();
        feed (a, 1, 48000, 1, [f5] (double t) { const double v = 1.0 * std::sin (tw * f5 * t); return std::make_pair (v, v); });
        expect ("main input: -12 dB", peakDb (a.power (0)), -12.04, 0.1);
        expect ("sidechain input: 0 dB", peakDb (a.power (1)), 0.0, 0.1);
        const int k = (int) std::lround (5000 / a.binHz());
        expect ("sidechain tone is in its own curve, not the main one (main at 5 kHz below -100)", 10 * std::log10 (a.power (0)[(size_t) k] + 1e-30) < -100 ? 1 : 0, 1, 0);
    }
    {
        // averaging: a tone that stops; with 1 s averaging it is still there 100 ms later, real time it isn't
        fft::FftAnalyzer slow, fast;
        for (auto* a : { &slow, &fast }) { a->configure (48000, 12); a->setSeries (0, true, 0, fft::Left); }
        slow.setAveraging (1000); fast.setAveraging (0);
        const double f1 = std::round (1000 / slow.binHz()) * slow.binHz();
        auto tone = [f1] (double t) { const double v = std::sin (tw * f1 * t); return std::make_pair (v, v); };
        auto none = [] (double) { return std::make_pair (0.0, 0.0); };
        for (auto* a : { &slow, &fast }) { feed (*a, 0, 48000, 3, tone); feed (*a, 0, 48000, 0.2, none); }
        expect ("1 s averaging keeps a tone 200 ms after it stops (within 6 dB)", peakDb (slow.power (0)) > -6 ? 1 : 0, 1, 0);
        expect ("real time: gone after 200 ms (below -60 dB)", peakDb (fast.power (0)) < -60 ? 1 : 0, 1, 0);
        expect ("hold keeps the peak in real time", peakDb (fast.held (0)), 0.0, 0.1);
        fast.decayHold (1.0, 20.0);
        expect ("hold decays at 20 dB/s", peakDb (fast.held (0)), -20.0, 0.1);
    }
    {
        // curve: a 1 kHz tone shows at the column holding 1 kHz; slope 4.5 dB/oct lifts a 4 kHz tone by 9 dB
        fft::FftAnalyzer a; a.configure (48000, 13); a.setSeries (0, true, 0, fft::Left); a.setAveraging (0);
        feed (a, 0, 48000, 1, [] (double t) { const double v = 0.5 * std::sin (tw * 4000 * t); return std::make_pair (v, v); });
        std::vector<float> edges, out;
        const int W = 800;
        for (int x = 0; x <= W; ++x) edges.push_back ((float) (20.0 * std::pow (1000.0, (double) x / W)));
        a.curve (a.power (0), edges, 0.0, 0.0, out);
        int best = 0; for (int x = 0; x < W; ++x) if (out[(size_t) x] > out[(size_t) best]) best = x;
        const double fBest = 20.0 * std::pow (1000.0, (best + 0.5) / W);
        expect ("curve: the tone lands at 4 kHz (column frequency)", fBest, 4000, 4000 * 0.01);
        expect ("curve: its true level although 4 kHz is between bins (scalloping corrected)", out[(size_t) best], -6.02, 0.05);
        a.curve (a.power (0), edges, 4.5, 0.0, out);
        expect ("slope 4.5 dB/oct: two octaves above 1 kHz reads +9 dB", out[(size_t) best], -6.02 + 9.0, 0.3);
        a.curve (a.power (0), edges, 0.0, 1.0 / 3.0, out);
        float mx = -300; for (float v : out) mx = std::max (mx, v);
        expect ("1/3-octave smoothing spreads the tone (peak lower)", mx < -10 ? 1 : 0, 1, 0);
        // low end: columns narrower than a bin are interpolated, never empty
        bool finite = true; for (int x = 0; x < W; ++x) if (! std::isfinite (out[(size_t) x])) finite = false;
        expect ("every column has a value (lows interpolated)", finite ? 1 : 0, 1, 0);
    }
    {
        fft::FftAnalyzer a; a.configure (48000, 12); a.setSeries (0, true, 0, fft::Left); a.setAveraging (0);
        std::vector<float> edges, out;
        for (int x = 0; x <= 600; ++x) edges.push_back ((float) (20.0 * std::pow (1000.0, x / 600.0)));
        double worst = 0;
        for (int o = 0; o < 8; ++o)
        {
            const double f = (100.25 + o / 8.0) * a.binHz();
            a.configure (48000, 12); a.setSeries (0, true, 0, fft::Left); a.setAveraging (0);
            feed (a, 0, 48000, 0.5, [f] (double t) { const double v = 0.5 * std::sin (tw * f * t); return std::make_pair (v, v); });
            a.curve (a.power (0), edges, 0, 0, out);
            float mx = -300; for (float v : out) mx = std::max (mx, v);
            worst = std::max (worst, std::abs (mx + 6.02));
        }
        expect ("tones anywhere between two bins: worst level error (dB)", worst, 0, 0.05);
    }
    std::printf (failures ? "%d FAILURES\n" : "all fft checks passed\n", failures);
    return failures ? 1 : 0;
}
