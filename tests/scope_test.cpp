// The oscilloscope engine (Source/scope/ScopeModel.*): sweep and scroll, restart after silence, spectral
// bands, the display band-pass, selection statistics, sources, Hold, Freeze and the recorder.
#include "../Source/scope/ScopeModel.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

static int failures = 0;
static void expect (bool ok, const std::string& what) { std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.c_str()); if (! ok) ++failures; }
static std::string f2 (double v) { char b[32]; std::snprintf (b, sizeof b, "%.2f", v); return b; }

constexpr double sr = 48000, kPi = 3.14159265358979323846;

// pushes `seconds` of a stereo signal: l(t), r(t)
template <typename L, typename R>
static void feed (scope::ScopeModel& m, double seconds, L l, R r, double& clock)
{
    std::vector<float> a (512), b (512);
    for (int done = 0; done < (int) (seconds * sr); done += 512)
    {
        const int n = std::min (512, (int) (seconds * sr) - done);
        for (int i = 0; i < n; ++i) { const double t = clock + (done + i) / sr; a[(size_t) i] = (float) l (t); b[(size_t) i] = (float) r (t); }
        m.push (a.data(), b.data(), n);
    }
    clock += seconds;
}

int main()
{
    auto sine = [] (double f, double a) { return [f, a] (double t) { return a * std::sin (2 * kPi * f * t); }; };
    auto zero = [] (double) { return 0.0; };
    std::vector<scope::Column> cols;

    // ---- Free: the sweep fills from the left; the playhead follows
    {
        scope::ScopeModel m; m.configure (sr, 1.0); double clk = 0;
        feed (m, 0.5, sine (1000, 0.5), sine (1000, 0.5), clk);
        m.columns (0, 1, 100, false, false, cols);
        int valid = 0; float top = 0;
        for (auto& c : cols) if (c.valid) { ++valid; top = std::max (top, c.mx); }
        expect (valid == 50 && std::abs (top - 0.5f) < 0.01f && std::abs (m.playhead() - 0.5) < 0.01,
                "Free sweep: " + std::to_string (valid) + " of 100 columns drawn, peak " + f2 (top) + ", playhead " + f2 (m.playhead()) + " s");
        // silence, then a hit: a new sweep starts at the left edge
        feed (m, 0.3, zero, zero, clk);
        feed (m, 0.1, sine (200, 0.8), sine (200, 0.8), clk);
        m.columns (0, 1, 100, false, false, cols);
        int after = 0; for (auto& c : cols) after += c.valid;
        expect (m.playhead() < 0.11 && after == 10 && cols[0].mx > 0.7f, "restarts at the left after silence: playhead " + f2 (m.playhead()) + " s, " + std::to_string (after) + " columns");
    }

    // ---- Scroll: the newest audio at the right edge
    {
        scope::ScopeModel m; m.configure (sr, 1.0); m.setMode (scope::Scroll); double clk = 0;
        feed (m, 0.3, sine (500, 0.5), sine (500, 0.5), clk);
        m.columns (0, 1, 100, false, false, cols);
        int first = -1, valid = 0;
        for (int i = 0; i < 100; ++i) if (cols[(size_t) i].valid) { ++valid; if (first < 0) first = i; }
        expect (valid == 30 && first == 70, "Scroll: the last 30 columns, from column " + std::to_string (first));
        feed (m, 1.5, sine (500, 0.25), sine (500, 0.25), clk);         // wraps around
        m.columns (0, 1, 100, false, false, cols);
        valid = 0; float top = 0; for (auto& c : cols) if (c.valid) { ++valid; top = std::max (top, c.mx); }
        expect (valid == 100 && std::abs (top - 0.25f) < 0.01f, "Scroll after wrapping: full and only the newest audio (" + f2 (top) + ")");
    }

    // ---- spectral bands: a 50 Hz tone is "sub", a 10 kHz tone is "air", 1 kHz "mid"
    for (auto [f, band] : { std::pair { 50.0, 0 }, std::pair { 1000.0, 2 }, std::pair { 10000.0, 4 } })
    {
        scope::ScopeModel m; m.configure (sr, 1.0); double clk = 0;
        feed (m, 1.0, sine (f, 0.5), sine (f, 0.5), clk);
        m.columns (0.5, 0.9, 4, false, false, cols);
        int best = 0;
        for (int i = 1; i < scope::kBands; ++i) if (cols[2].bands[(size_t) i] > cols[2].bands[(size_t) best]) best = i;
        const char* names[] = { "sub", "low", "mid", "high", "air" };
        expect (best == band, "spectral band of " + std::to_string ((int) f) + " Hz: " + names[best]);
    }

    // ---- the display band-pass: around 1 kHz, a 50 Hz tone almost vanishes, a 1 kHz tone stays
    {
        scope::ScopeModel m; m.configure (sr, 1.0); m.setBandpass (1000); double clk = 0;
        feed (m, 0.5, sine (50, 0.5), sine (50, 0.5), clk);
        feed (m, 0.5, sine (1000, 0.5), sine (1000, 0.5), clk);
        m.columns (0.2, 0.4, 1, true, false, cols); const float low = cols[0].mx;
        m.columns (0.7, 0.9, 1, true, false, cols); const float mid = cols[0].mx;
        expect (20 * std::log10 (low / 0.5) < -30 && std::abs (20 * std::log10 (mid / 0.5)) < 1,
                "BP at 1 kHz: 50 Hz " + f2 (20 * std::log10 (low / 0.5)) + " dB, 1 kHz " + f2 (20 * std::log10 (mid / 0.5)) + " dB");
    }

    // ---- selection statistics: a 0.5 sine is -6.02 dB peak, -9.03 dB RMS, crest 3.01 dB
    {
        scope::ScopeModel m; m.configure (sr, 1.0); double clk = 0;
        feed (m, 1.0, sine (440, 0.5), sine (440, 0.5), clk);
        const auto st = m.stats (0.25, 0.75, false);
        expect (st.valid && std::abs (st.peakDb + 6.02) < 0.05 && std::abs (st.rmsDb + 9.03) < 0.05 && std::abs (st.crestDb - 3.01) < 0.06 && std::abs (st.seconds - 0.5) < 1e-6,
                "stats: peak " + f2 (st.peakDb) + " dB, RMS " + f2 (st.rmsDb) + " dB, crest " + f2 (st.crestDb) + " dB over " + f2 (st.seconds * 1000) + " ms");
    }

    // ---- sources and Side2SC: R = -L makes Mid silent and Side full; Peak takes the larger channel
    {
        scope::ScopeModel m; m.configure (sr, 1.0); double clk = 0;
        feed (m, 0.5, sine (300, 0.6), sine (300, -0.6), clk);
        m.columns (0, 0.5, 1, false, false, cols);
        expect (std::abs (cols[0].mx) < 1e-6f && std::abs (cols[0].smx - 0.6f) < 0.01f, "Mid of R = -L is silent, Side is " + f2 (cols[0].smx));
        scope::ScopeModel p; p.configure (sr, 1.0); p.setSource (scope::Peak); clk = 0;
        feed (p, 0.5, sine (300, 0.2), sine (300, 0.8), clk);
        p.columns (0, 0.5, 1, false, false, cols);
        expect (std::abs (cols[0].mx - 0.8f) < 0.01f, "Peak source follows the louder channel: " + f2 (cols[0].mx));
    }

    // ---- Hold keeps a snapshot through Reset; Freeze stops writing
    {
        scope::ScopeModel m; m.configure (sr, 1.0); double clk = 0;
        feed (m, 1.0, sine (100, 0.7), sine (100, 0.7), clk);
        m.hold (true);
        m.reset();
        m.columns (0, 1, 10, false, true, cols); const bool heldOk = cols[5].valid && cols[5].mx > 0.65f;
        m.columns (0, 1, 10, false, false, cols); const bool liveCleared = ! cols[5].valid;
        expect (heldOk && liveCleared, "Hold survives Reset; the live trace is cleared");
        m.setFrozen (true);
        feed (m, 0.5, sine (100, 0.7), sine (100, 0.7), clk);
        expect (! m.hasSignal(), "Freeze: nothing is written");
    }

    // ---- recorder: waits for audio, records one window, then trims, normalises and snaps to the grid
    {
        scope::ScopeModel m; m.configure (sr, 1.0); double clk = 0;
        m.arm();
        feed (m, 0.3, zero, zero, clk);
        const bool waited = m.recState() == scope::ScopeModel::Rec::Armed;
        feed (m, 0.6, sine (220, 0.25), sine (220, 0.25), clk);
        feed (m, 0.6, zero, zero, clk);
        const bool done = m.recState() == scope::ScopeModel::Rec::Done;
        const auto raw = m.take (false, false, false, 0);
        const auto t = m.take (true, true, true, 0.25);
        float pk = 0; for (float v : t.l) pk = std::max (pk, std::abs (v));
        expect (waited && done && raw.l.size() == 48000 && t.l.size() == 36000 && std::abs (pk - 1.0f) < 1e-4f,
                "recorder: starts on audio, one window (" + std::to_string (raw.l.size()) + " samples); trim + snap to 0.25 s: "
                    + std::to_string (t.l.size()) + " samples, normalised peak " + f2 (pk));
    }

    {
        // Scroll, smooth: one column further on, the columns are the same ones shifted by exactly one
        scope::ScopeModel m; m.configure (48000, 1.0); m.setMode (scope::Scroll);
        std::vector<float> L (48000 * 2), R (48000 * 2);
        for (size_t i = 0; i < L.size(); ++i) L[i] = R[i] = (float) (std::sin (i * 0.013) * (0.2 + 0.7 * ((i / 997) % 3) / 2.0));
        m.push (L.data(), R.data(), (int) L.size());
        const double spc = 48000.0 / 800;                            // 1 s across 800 px
        std::vector<scope::Column> a, b; double fa = 0, fb = 0;
        const double end = (double) m.totalWritten() - 1234.5;
        m.columnsScroll (end, spc, 800, false, a, fa);
        m.columnsScroll (end + spc, spc, 800, false, b, fb);
        bool same = true;
        for (int c = 1; c <= 800; ++c) if (a[(size_t) c].mn != b[(size_t) c - 1].mn || a[(size_t) c].mx != b[(size_t) c - 1].mx) same = false;
        m.columnsScroll (end + spc * 0.25, spc, 800, false, b, fb);
        expect (same && a.size() == 801 && std::abs (fb - std::fmod (fa + 0.25, 1.0)) < 1e-6,
                "Scroll grid: +1 column shifts the columns by exactly one; +1/4 column moves the offset by 0.25 (" + f2 (fa) + " -> " + f2 (fb) + ")");
    }
    {
        // Sync (VST3): the phase decides where each sample lands; stopped (-1) writes nothing
        scope::ScopeModel m; m.configure (48000, 1.0); m.setMode (scope::Sync);
        std::vector<float> L (12000, 0.5f), R (12000, 0.5f), ph (12000);
        for (int i = 0; i < 12000; ++i) ph[(size_t) i] = 0.5f + i / 48000.0f;        // the host is half-way through the window
        m.push (L.data(), R.data(), nullptr, nullptr, ph.data(), 12000);
        std::vector<scope::Column> cols;
        m.columns (0, 1.0, 100, false, false, cols);
        int first = -1, valid = 0; for (int c = 0; c < 100; ++c) if (cols[(size_t) c].valid) { ++valid; if (first < 0) first = c; }
        std::vector<float> stop (4800, -1.0f), L2 (4800, 0.9f);
        m.push (L2.data(), L2.data(), nullptr, nullptr, stop.data(), 4800);
        m.columns (0, 1.0, 100, false, false, cols);
        float top = 0; for (auto& c : cols) if (c.valid) top = std::max (top, c.mx);
        expect (first == 50 && (valid == 25 || valid == 26) && std::abs (m.playhead() - 0.75) < 0.01 && top < 0.6f,
                "Sync: written from the host's position (column " + std::to_string (first) + ", " + std::to_string (valid) + " columns), nothing while stopped");
    }
    {
        // sidechain overlay: the side ring carries the sidechain (mono sum) instead of L-R
        scope::ScopeModel m; m.configure (48000, 0.5); m.setOverlayFromSidechain (true);
        std::vector<float> L (24000, 0.3f), R (24000, 0.3f), SL (24000, 0.8f), SR (24000, 0.4f);
        m.push (L.data(), R.data(), SL.data(), SR.data(), nullptr, 24000);
        std::vector<scope::Column> cols;
        m.columns (0, 0.5, 10, false, false, cols);
        expect (std::abs (cols[5].smx - 0.6f) < 1e-5f && std::abs (cols[5].mx - 0.3f) < 1e-5f, "sidechain overlay: side ring = (scL + scR) / 2 = " + f2 (cols[5].smx));
    }
    std::printf (failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
