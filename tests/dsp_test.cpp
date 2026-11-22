// Calibration test for the reassignment engine: a full-scale sine must land on the right frequency
// row and read close to 0 dBFS, with and without reassignment, at 48 and 96 kHz, on every frequency scale.
// Also: Mid / Side channel selection, and that each scale's warp and unwarp agree.
#include "../Source/Reassigner.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>
#include <algorithm>

static int failures = 0;

static void check (bool reassign, double sr, double f0, int scale = freqscale::Log, double fmin = 20, int fft = 4096)
{
    Reassigner r;
    std::vector<std::vector<float>> cols;
    r.setSink ([&] (const float* c, int n) { cols.emplace_back (c, c + n); });
    Reassigner::Config c; c.sampleRate = sr; c.rows = 800; c.reassign = reassign;
    c.fftSize = fft; c.scale = scale; c.fmin = freqscale::bottom (scale, fmin);
    r.configure (c);
    const freqscale::Axis ax (scale, c.fmin, r.getEffectiveFmax());

    const int block = 1024, total = (int) sr * (fft > 8192 ? 3 : 1);     // one second (three for long FFTs)
    std::vector<float> L (block), R (block);
    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i) L[(size_t) i] = R[(size_t) i] = (float) std::sin (2 * 3.14159265358979 * f0 * (pos + i) / sr);
        r.process (L.data(), R.data(), block);
    }
    const auto& col = cols[cols.size() - 40];
    int best = 0;
    for (int j = 1; j < (int) col.size(); ++j) if (col[(size_t) j] > col[(size_t) best]) best = j;
    const double fRow = ax.freq ((best + 0.5) / c.rows);
    const double centsOff = 1200 * std::log2 (fRow / f0);
    // Reassigned: within 30 cents. Plain FFT: within one bin or one display row (its resolution limit is the point of reassignment).
    const double binHz = r.getEffectiveRate() / c.fftSize;
    const double rowHz = ax.freq ((best + 1.0) / c.rows) - ax.freq ((double) best / c.rows);
    // (on Linear a display row can be wider than 30 cents in the bass: within a row is as good as it gets there)
    const bool freqOk = reassign ? (std::abs (centsOff) < 30 || std::abs (fRow - f0) <= rowHz * 1.01)
                                 : std::abs (fRow - f0) <= std::max (binHz, rowHz);
    const bool ok = freqOk && col[(size_t) best] > -3.5f && col[(size_t) best] < 0.5f;
    std::printf ("%s %-7s reassign=%d sr=%6.0f f0=%7.1f -> peak %6.2f dBFS at %8.1f Hz (%+.0f cents)\n",
                 ok ? "PASS" : "FAIL", freqscale::name (scale), reassign, sr, f0, col[(size_t) best], fRow, centsOff);
    if (! ok) ++failures;
}

// L = sine, R = the same sine inverted: Mid must be empty, Side full scale; Left / Right full scale.
static void checkChannels()
{
    const double sr = 48000, f0 = 1000;
    const char* names[4] = { "Left", "Right", "Mid", "Side" };
    for (int ch = 0; ch < 4; ++ch)
    {
        Reassigner r;
        float peak = -1e9f;
        int cols = 0;
        r.setSink ([&] (const float* c, int n) { if (++cols > 200) for (int j = 0; j < n; ++j) peak = std::max (peak, c[j]); });
        Reassigner::Config c; c.sampleRate = sr; c.rows = 600; c.channel = ch;
        r.configure (c);
        std::vector<float> L (1024), R (1024);
        for (int pos = 0; pos < (int) sr; pos += 1024)
        {
            for (int i = 0; i < 1024; ++i) { L[(size_t) i] = (float) std::sin (2 * 3.14159265358979 * f0 * (pos + i) / sr); R[(size_t) i] = -L[(size_t) i]; }
            r.process (L.data(), R.data(), 1024);
        }
        const bool ok = ch == 2 ? peak < -100 : (peak > -3.5f && peak < 0.5f);
        std::printf ("%s channel %-5s with R = -L: peak %7.1f dBFS\n", ok ? "PASS" : "FAIL", names[ch], peak);
        if (! ok) ++failures;
    }
}

static void checkScales()
{
    for (int s = 0; s < freqscale::kCount; ++s)
    {
        double worst = 0;
        bool monotonic = true;
        double prev = -1e300;
        for (double f = 1; f <= 24000; f *= 1.01)
        {
            const double w = freqscale::warp (s, f);
            worst = std::max (worst, std::abs (freqscale::unwarp (s, w) - f) / f);
            monotonic = monotonic && w > prev;
            prev = w;
        }
        const freqscale::Axis ax (s, freqscale::bottom (s, 20), 20000);
        const bool ends = std::abs (ax.pos (ax.lo)) < 1e-9 && std::abs (ax.pos (20000) - 1) < 1e-9;
        const bool ok = worst < 1e-9 && monotonic && ends && ax.lo == (s == freqscale::Log ? 20 : (s == freqscale::ExtLog ? 1 : 0));
        std::printf ("%s scale %-7s warp/unwarp error %.1e, %s, axis %g Hz .. 20 kHz\n", ok ? "PASS" : "FAIL", freqscale::name (s),
                     worst, monotonic ? "monotonic" : "NOT monotonic", ax.lo);
        if (! ok) ++failures;
    }
}

int main()
{
    checkScales();
    for (bool re : { true, false })
        for (double sr : { 48000.0, 96000.0 })
            for (double f : { 50.0, 1000.0, 12345.0 })
                check (re, sr, f);
    for (int sc = 1; sc < freqscale::kCount; ++sc)
        for (bool re : { true, false })
            for (double f : { 50.0, 1000.0, 12345.0 })
                check (re, 48000.0, f, sc);
    // below 20 Hz: an 8 Hz sine with a 16384 FFT, on Ext Log (from 1 Hz) and on Log with the 5 Hz range
    check (true, 48000.0, 8.0, freqscale::ExtLog, 20, 16384);
    check (true, 48000.0, 8.0, freqscale::Log, 5, 16384);
    check (false, 48000.0, 8.0, freqscale::Log, 5, 16384);
    checkChannels();

    // throughput: 10 s of audio at 4096 / 300 px/s with reassignment
    Reassigner r; long long n = 0;
    r.setSink ([&] (const float*, int) { ++n; });
    Reassigner::Config c; c.rows = 1000; r.configure (c);
    std::vector<float> L (1024), R (1024);
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < 48000 * 10 / 1024; ++b)
    {
        for (int i = 0; i < 1024; ++i) L[(size_t) i] = R[(size_t) i] = (float) (std::sin (0.05 * (b * 1024 + i)) + 0.01 * ((i * 7919 % 1000) / 500.0 - 1));
        r.process (L.data(), R.data(), 1024);
    }
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("throughput: 10 s audio -> %lld columns in %.0f ms (%.1f%% of one core)\n", n, ms, ms / 100.0);

    std::printf (failures ? "%d FAILURES\n" : "all calibration checks passed\n", failures);
    return failures ? 1 : 0;
}
