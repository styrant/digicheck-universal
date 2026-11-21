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
            lastT = t;
            const int cap = capHz.load();
            if (cap <= 0) return lastDue = true;
            const double interval = 1.0 / cap;
            if (t < next - 0.0005) return lastDue = false;         // too early for the next capped frame
            next = (t - next > 2.0 * interval) ? t + interval : next + interval;
            return lastDue = true;
        }
        double lastT = -1.0, next = 0.0;
        bool lastDue = true;
    };

    inline Clock& clock() noexcept { static Clock c; return c; }
}
