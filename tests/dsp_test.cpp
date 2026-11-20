// Calibration test for the reassignment engine: a full-scale sine must land on the right frequency
// row and read close to 0 dBFS, with and without reassignment, at 48 and 96 kHz.
#include "../Source/Reassigner.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

static void check (bool reassign, double sr, double f0)
{
    Reassigner r;
    std::vector<std::vector<float>> cols;
    r.setSink ([&] (const float* c, int n) { cols.emplace_back (c, c + n); });
    Reassigner::Config c; c.sampleRate = sr; c.rows = 800; c.reassign = reassign;
    r.configure (c);

    const int block = 1024, total = (int) sr;     // one second
    std::vector<float> L (block), R (block);
    for (int pos = 0; pos < total; pos += block)
    {
        for (int i = 0; i < block; ++i) L[(size_t) i] = R[(size_t) i] = (float) std::sin (2 * 3.14159265358979 * f0 * (pos + i) / sr);
        r.process (L.data(), R.data(), block);
    }
    const auto& col = cols[cols.size() - 40];
    int best = 0;
    for (int j = 1; j < (int) col.size(); ++j) if (col[(size_t) j] > col[(size_t) best]) best = j;
    const double fRow = c.fmin * std::pow (r.getEffectiveFmax() / c.fmin, (best + 0.5) / c.rows);
    const double centsOff = 1200 * std::log2 (fRow / f0);
    // Reassigned: within 30 cents. Plain FFT: within one bin or one display row (its resolution limit is the point of reassignment).
    const double binHz = r.getEffectiveRate() / c.fftSize;
    const double rowHz = fRow * (std::pow (r.getEffectiveFmax() / c.fmin, 1.0 / c.rows) - 1);
    const bool freqOk = reassign ? std::abs (centsOff) < 30 : std::abs (fRow - f0) <= std::max (binHz, rowHz);
    const bool ok = freqOk && col[(size_t) best] > -3.5f && col[(size_t) best] < 0.5f;
    std::printf ("%s reassign=%d sr=%6.0f f0=%7.1f -> peak %6.2f dBFS at %8.1f Hz (%+.0f cents)\n",
                 ok ? "PASS" : "FAIL", reassign, sr, f0, col[(size_t) best], fRow, centsOff);
    if (! ok) ++failures;
}

int main()
{
    for (bool re : { true, false })
        for (double sr : { 48000.0, 96000.0 })
            for (double f : { 50.0, 1000.0, 12345.0 })
                check (re, sr, f);

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
