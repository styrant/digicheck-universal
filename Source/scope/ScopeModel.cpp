#include "ScopeModel.h"
#include <algorithm>
#include <cmath>

namespace scope
{
    namespace
    {
        constexpr double kPi = 3.14159265358979323846;
        double toDb (double a) { return 20.0 * std::log10 (std::max (a, 1e-10)); }
    }

    //==========================================================================
    void Biquad::lowpass (double f, double fs, double q)
    {
        const double w = 2 * kPi * std::min (f, fs * 0.45) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void Biquad::highpass (double f, double fs, double q)
    {
        const double w = 2 * kPi * std::min (f, fs * 0.45) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = (1 + c) / 2 / a0; b1 = -(1 + c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    void Biquad::bandpass (double f, double fs, double q)
    {
        const double w = 2 * kPi * std::min (f, fs * 0.45) / fs, c = std::cos (w), al = std::sin (w) / (2 * q), a0 = 1 + al;
        b0 = al / a0; b1 = 0; b2 = -al / a0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }

    void Rings::resize (std::size_t samples)
    {
        n = std::max<std::size_t> (kBlock, samples);
        for (auto* v : { &main, &side, &bp, &l, &r }) v->assign (n, 0.0f);
        blocks.assign ((n + kBlock - 1) / kBlock, {});
        writePos = 0; written = 0;
    }

    void Rings::clear()
    {
        for (auto* v : { &main, &side, &bp, &l, &r }) std::fill (v->begin(), v->end(), 0.0f);
        std::fill (blocks.begin(), blocks.end(), Block {});
        writePos = 0; written = 0;
    }

    //==========================================================================
    ScopeModel::ScopeModel() { configure (48000, 2.0); }

    void ScopeModel::configure (double rate, double seconds)
    {
        rate = rate > 1000 ? rate : 48000;
        seconds = std::clamp (seconds, 0.01, 120.0);
        if (rate == sr && seconds == window && live.n > 0) return;
        sr = rate; window = seconds;
        live.resize ((std::size_t) std::llround (window * sr));
        held = Rings {};
        holding = false;
        // band split: sub | low | mid | high | air
        bandFilters[0].lowpass (kBandEdges[0], sr);
        bandFilters[1].highpass (kBandEdges[0], sr); bandFilters[2].lowpass (kBandEdges[1], sr);
        bandFilters[3].highpass (kBandEdges[1], sr); bandFilters[4].lowpass (kBandEdges[2], sr);
        bandFilters[5].highpass (kBandEdges[2], sr); bandFilters[6].lowpass (kBandEdges[3], sr);
        bandFilters[7].highpass (kBandEdges[3], sr);
        for (auto& b : bandFilters) b.reset();
        setBandpass (bpHz);
        if (rec != Rec::Idle) disarm();
    }

    void ScopeModel::setMode (int m)
    {
        if (m == mode) return;
        mode = m;
        live.clear();
    }

    void ScopeModel::setBandpass (double hz)
    {
        bpHz = std::clamp (hz, 20.0, 20000.0);
        // two cascaded band-passes (Q 1.4): a clear view of one region without ringing for long
        bp1.bandpass (bpHz, sr, 1.4); bp2.bandpass (bpHz, sr, 1.4);
    }

    void ScopeModel::reset()
    {
        live.clear();
        silentRun = 0;
    }

    void ScopeModel::hold (bool on)
    {
        holding = on;
        if (on) held = live; else held = Rings {};
    }

    void ScopeModel::arm()    { rec = Rec::Armed; takeL.clear(); takeR.clear(); }
    void ScopeModel::disarm() { rec = Rec::Idle; takeL.clear(); takeR.clear(); }

    //==========================================================================
    void ScopeModel::push (const float* L, const float* R, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            const bool loud = std::max (std::abs (l), std::abs (r)) > kStartThreshold;

            // recorder (runs even when frozen: it records what comes in)
            if (rec == Rec::Armed && loud) rec = Rec::Recording;
            if (rec == Rec::Recording)
            {
                takeL.push_back (l); takeR.push_back (r);
                if (takeL.size() >= live.n) rec = Rec::Done;
            }

            if (frozen) continue;
            // Free: after a silence, the next sound starts a fresh sweep at the left edge
            if (mode == Free)
            {
                if (! loud) ++silentRun;
                else
                {
                    if (silentRun > (std::size_t) (kSilenceToRetrigger * sr) && live.written > 0) live.clear();
                    silentRun = 0;
                }
            }
            write (l, r);
        }
    }

    void ScopeModel::write (float l, float r)
    {
        float m;
        switch (source)
        {
            case Left:  m = l; break;
            case Right: m = r; break;
            case Peak:  m = std::abs (l) >= std::abs (r) ? l : r; break;
            default:    m = 0.5f * (l + r); break;
        }
        const float s = 0.5f * (l - r);
        const float b = bp2.process (bp1.process (m));
        float e[kBands];
        { const float x = bandFilters[0].process (m); e[0] = x * x; }
        { const float x = bandFilters[2].process (bandFilters[1].process (m)); e[1] = x * x; }
        { const float x = bandFilters[4].process (bandFilters[3].process (m)); e[2] = x * x; }
        { const float x = bandFilters[6].process (bandFilters[5].process (m)); e[3] = x * x; }
        { const float x = bandFilters[7].process (m); e[4] = x * x; }

        auto& g = live;
        const std::size_t p = g.writePos;
        g.main[p] = m; g.side[p] = s; g.bp[p] = b; g.l[p] = l; g.r[p] = r;
        auto& k = g.blocks[p / kBlock];
        if (p % kBlock == 0 || k.count == 0)
        {
            k = {};
            k.mn = k.mx = m; k.bmn = k.bmx = b; k.smn = k.smx = s;
        }
        k.mn = std::min (k.mn, m); k.mx = std::max (k.mx, m);
        k.bmn = std::min (k.bmn, b); k.bmx = std::max (k.bmx, b);
        k.smn = std::min (k.smn, s); k.smx = std::max (k.smx, s);
        k.sq += (double) m * m; k.bsq += (double) b * b;
        for (int i = 0; i < kBands; ++i) k.e[i] += e[i];
        ++k.count;
        g.writePos = (p + 1) % g.n;
        ++g.written;
    }

    //==========================================================================
    std::size_t ScopeModel::positionAt (const Rings& g, double t) const
    {
        const double s = std::clamp (t, 0.0, window) * sr;
        if (mode == Free) return std::min (g.n - 1, (std::size_t) s);
        // Scroll: the right edge is the newest sample
        const double back = window * sr - s;
        const auto newest = (double) g.writePos;
        double pos = newest - back;
        while (pos < 0) pos += (double) g.n;
        return std::min (g.n - 1, (std::size_t) pos);
    }

    double ScopeModel::playhead() const
    {
        return mode == Free ? (double) live.writePos / sr : window;
    }

    void ScopeModel::columns (double t0, double t1, int n, bool bpOn, bool fromHold, std::vector<Column>& out) const
    {
        out.assign ((std::size_t) std::max (0, n), {});
        const Rings& g = fromHold ? held : live;
        if (g.n == 0 || n <= 0 || g.written == 0) return;
        const double dt = (t1 - t0) / n;
        for (int c = 0; c < n; ++c)
        {
            const double a = t0 + c * dt, b = a + dt;
            if (b <= 0 || a >= window) continue;
            std::size_t pa = positionAt (g, a), cnt = (std::size_t) std::max (1.0, std::round ((std::min (b, window) - std::max (a, 0.0)) * sr));
            Column col;
            bool any = false;
            for (std::size_t k = 0; k < cnt;)
            {
                const std::size_t p = (pa + k) % g.n;
                if (! g.valid (p)) { ++k; continue; }
                const auto& blk = g.blocks[p / kBlock];
                // a whole block when it lies inside this column, else single samples
                if (p % kBlock == 0 && k + kBlock <= cnt && blk.count >= kBlock)
                {
                    const float lo = bpOn ? blk.bmn : blk.mn, hi = bpOn ? blk.bmx : blk.mx;
                    if (! any) { col.mn = lo; col.mx = hi; col.smn = blk.smn; col.smx = blk.smx; any = true; }
                    else { col.mn = std::min (col.mn, lo); col.mx = std::max (col.mx, hi); col.smn = std::min (col.smn, blk.smn); col.smx = std::max (col.smx, blk.smx); }
                    for (int i = 0; i < kBands; ++i) col.bands[(size_t) i] += blk.e[i];
                    k += kBlock;
                }
                else
                {
                    const float v = bpOn ? g.bp[p] : g.main[p], s = g.side[p];
                    if (! any) { col.mn = col.mx = v; col.smn = col.smx = s; any = true; }
                    else { col.mn = std::min (col.mn, v); col.mx = std::max (col.mx, v); col.smn = std::min (col.smn, s); col.smx = std::max (col.smx, s); }
                    if (blk.count > 0) for (int i = 0; i < kBands; ++i) col.bands[(size_t) i] += blk.e[i] / (float) blk.count;
                    ++k;
                }
            }
            col.valid = any;
            out[(size_t) c] = col;
        }
    }

    void ScopeModel::samples (double t0, double t1, bool bpOn, bool fromHold, std::vector<float>& m, std::vector<float>& s) const
    {
        m.clear(); s.clear();
        const Rings& g = fromHold ? held : live;
        if (g.n == 0 || g.written == 0) return;
        const double a = std::max (0.0, t0), b = std::min (window, t1);
        if (b <= a) return;
        const std::size_t pa = positionAt (g, a), cnt = (std::size_t) std::ceil ((b - a) * sr) + 1;
        for (std::size_t k = 0; k < cnt; ++k)
        {
            const std::size_t p = (pa + k) % g.n;
            m.push_back (g.valid (p) ? (bpOn ? g.bp[p] : g.main[p]) : NAN);
            s.push_back (g.valid (p) ? g.side[p] : NAN);
        }
    }

    Stats ScopeModel::stats (double t0, double t1, bool bpOn) const
    {
        Stats st;
        const Rings& g = live;
        const double a = std::max (0.0, std::min (t0, t1)), b = std::min (window, std::max (t0, t1));
        if (g.written == 0 || b - a < 1.0 / sr) return st;
        const std::size_t pa = positionAt (g, a), cnt = (std::size_t) std::round ((b - a) * sr);
        double peak = 0, sq = 0;
        std::size_t used = 0;
        for (std::size_t k = 0; k < cnt;)
        {
            const std::size_t p = (pa + k) % g.n;
            if (! g.valid (p)) { ++k; continue; }
            const auto& blk = g.blocks[p / kBlock];
            if (p % kBlock == 0 && k + kBlock <= cnt && blk.count >= kBlock)
            {
                peak = std::max ({ peak, (double) std::abs (bpOn ? blk.bmn : blk.mn), (double) std::abs (bpOn ? blk.bmx : blk.mx) });
                sq += bpOn ? blk.bsq : blk.sq;
                used += kBlock; k += kBlock;
            }
            else
            {
                const double v = bpOn ? g.bp[p] : g.main[p];
                peak = std::max (peak, std::abs (v)); sq += v * v;
                ++used; ++k;
            }
        }
        if (used == 0) return st;
        st.valid = true;
        st.seconds = b - a;
        st.peakDb = toDb (peak);
        st.rmsDb = toDb (std::sqrt (sq / (double) used));
        st.crestDb = st.peakDb - st.rmsDb;
        return st;
    }

    //==========================================================================
    ScopeModel::Take ScopeModel::take (bool normalize, bool trim, bool snap, double grid, double silenceDb) const
    {
        Take t;
        t.sampleRate = sr;
        if (rec == Rec::Done || rec == Rec::Recording) { t.l = takeL; t.r = takeR; }
        else
        {
            // the window's audio, oldest first
            const auto& g = live;
            const std::size_t have = (std::size_t) std::min<std::uint64_t> (g.written, g.n);
            const std::size_t start = g.written >= g.n ? g.writePos : 0;
            for (std::size_t k = 0; k < have; ++k) { t.l.push_back (g.l[(start + k) % g.n]); t.r.push_back (g.r[(start + k) % g.n]); }
        }
        if (trim)
        {
            const float thr = (float) std::pow (10.0, silenceDb / 20.0);
            std::size_t end = t.l.size();
            while (end > 0 && std::abs (t.l[end - 1]) <= thr && std::abs (t.r[end - 1]) <= thr) --end;
            t.l.resize (end); t.r.resize (end);
        }
        if (normalize)
        {
            float pk = 0;
            for (size_t i = 0; i < t.l.size(); ++i) pk = std::max ({ pk, std::abs (t.l[i]), std::abs (t.r[i]) });
            if (pk > 1e-9f) { const float g = 1.0f / pk; for (auto& v : t.l) v *= g; for (auto& v : t.r) v *= g; }
        }
        if (snap && grid > 0 && ! t.l.empty())
        {
            const auto step = (std::size_t) std::llround (grid * sr);
            const std::size_t len = ((t.l.size() + step - 1) / step) * step;
            t.l.resize (len, 0.0f); t.r.resize (len, 0.0f);
        }
        return t;
    }
}
