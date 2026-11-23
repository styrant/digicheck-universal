#pragma once
#include <atomic>
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
            if (gap > 0.002 && gap < 0.05) refresh += 0.05 * (gap - refresh);   // the monitor's refresh interval
            const int cap = capHz.load();
            if (cap <= 0) return lastDue = true;
            const double interval = 1.0 / cap;
            // Well below the refresh rate: every k-th vblank, so frames are evenly spaced (a 60 cap draws
            // 60 fps on 60 / 120 / 240 Hz and an even 72 on 144 Hz, instead of alternating 2 and 3 vblanks).
            // Just below it (the G-Sync / FreeSync case, e.g. 141 on 144 Hz): the exact cap, by time.
            const double ratio = interval / refresh;
            if (ratio >= 1.5)
            {
                const int k = (int) std::lround (ratio);
                if (gap > 1.5 * refresh * k) skip = 0;               // after a stall: draw now
                if (skip > 0) { --skip; return lastDue = false; }
                skip = k - 1;
                next = t + interval;
                return lastDue = true;
            }
            if (t < next - 0.0005) return lastDue = false;         // too early for the next capped frame
            next = (t - next > 2.0 * interval) ? t + interval : next + interval;
            return lastDue = true;
        }
        double lastT = -1.0, next = 0.0, refresh = 1.0 / 60.0;
        int skip = 0;
        bool lastDue = true;
    };

    inline Clock& clock() noexcept { static Clock c; return c; }
}
