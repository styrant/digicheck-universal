#pragma once
#include <atomic>
#include <algorithm>
#include <cmath>

// One frame clock shared by everything that animates (spectrogram, analyzer, GPU frame triggers),
// so a frame-rate cap applies to the whole window: e.g. 141 fps keeps a 144 Hz G-Sync / FreeSync
// monitor inside its variable-refresh range. All callers run on the message thread from their
// VBlankAttachment; the first caller in a given vblank decides, the others get the same answer.
namespace frameclock
{
    inline std::atomic<int> capHz { 0 };          // 0 = no cap (follow the monitor's refresh rate)

    struct Clock
    {
        bool due (double t) noexcept
        {
            if (std::abs (t - lastT) < 1.0e-6) return lastDue;    // same vblank as the previous caller
            const double gap = lastT > 0 ? t - lastT : 0.0;
            lastT = t;
            // the monitor's refresh interval: per-vblank gaps (a longer gap counts as several vblanks), outliers
            // ignored, slowly averaged, so whatever is paced by it moves at an even speed
            if (gap > 0.002 && gap < 0.1)
            {
                const double k = std::max (1.0, std::round (gap / refresh)), g = gap / k;
                if (std::abs (g - refresh) < 0.25 * refresh || refreshSamples < 30) { refresh += (refreshSamples < 30 ? 0.2 : 0.01) * (g - refresh); ++refreshSamples; }
            }
            const int cap = capHz.load();
            if (cap <= 0) return lastDue = true;
            const double interval = 1.0 / cap;
            // Three cases, by how the cap compares with the monitor's refresh rate:
            //  - at or above it (60 on a 60 Hz monitor): every vblank. Deciding by timestamps here dropped a
            //    frame whenever a vblank came a fraction of a millisecond early: a visible stutter in anything
            //    that moves (the Scope's Scroll)
            //  - just below it (141 on 144 Hz, to stay in a G-Sync / FreeSync range): the exact cap, by time
            //  - well below it: every k-th vblank, so frames are evenly spaced (60 on 60 / 120 / 240 Hz, an
            //    even 72 on 144 Hz, 75 on 75 Hz) instead of alternating gaps
            const double ratio = interval / refresh;
            if (ratio < 1.005) { skip = 0; next = t + interval; return lastDue = true; }
            if (ratio < 1.15)
            {
                if (t < next - 0.25 * refresh) return lastDue = false;
                next = (t - next > 2.0 * interval) ? t + interval : next + interval;
                return lastDue = true;
            }
            const int k = atLeastOne ((int) std::lround (ratio));
            if (gap > 1.5 * refresh * k) skip = 0;                   // after a stall: draw now
            if (skip > 0) { --skip; return lastDue = false; }
            skip = k - 1;
            next = t + interval;
            return lastDue = true;
        }
        static int atLeastOne (int v) noexcept { return v < 1 ? 1 : v; }
        double lastT = -1.0, next = 0.0, refresh = 1.0 / 60.0;
        int skip = 0, refreshSamples = 0;
        bool lastDue = true;
    };

    inline Clock& clock() noexcept { static Clock c; return c; }
}
