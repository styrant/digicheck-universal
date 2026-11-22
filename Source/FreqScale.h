// Frequency axis scales for the spectrogram. Pure C++ (used by the Reassigner and its tests).
//
//   Log      octaves take equal height (the default, like DigiCheck)
//   Linear   Hz take equal height: harmonics are evenly spaced, the bass is a thin strip at the bottom
//   Ext Log  extended log, as in iZotope RX: log above ~100 Hz, compressing smoothly towards the bottom
//            (ln(1 + f/20 Hz), which matches RX's axis within a pixel or two), shown from 1 Hz, so infrasonic
//            rumble and sub-bass are visible without the bottom octaves taking over
//   Mel      perceived pitch (O'Shaughnessy: 2595 log10(1 + f/700)); linear-ish below 1 kHz, log above
//   Bark     critical bands of hearing (Traunmüller 1990)
//   ERB      equivalent rectangular bandwidth of hearing filters (Glasberg & Moore 1990)
//
// warp() is monotonic; a frequency's position on the axis is (warp(f) - warp(lo)) / (warp(hi) - warp(lo)).
#pragma once
#include <algorithm>
#include <cmath>

namespace freqscale
{
    enum Scale { Log = 0, Linear, ExtLog, Mel, Bark, Erb, kCount };

    inline const char* name (int s)
    {
        static const char* n[kCount] = { "Log", "Linear", "Ext Log", "Mel", "Bark", "ERB" };
        return n[std::clamp (s, 0, (int) kCount - 1)];
    }

    inline double warp (int s, double f)
    {
        f = std::max (f, 0.0);
        switch (s)
        {
            case Linear: return f;
            case ExtLog: return std::log1p (f / 20.0);
            case Mel:    return 2595.0 * std::log10 (1.0 + f / 700.0);
            case Bark:   return 26.81 * f / (1960.0 + f) - 0.53;
            case Erb:    return 21.4 * std::log10 (1.0 + 0.00437 * f);
            default:     return std::log (std::max (f, 1e-3));
        }
    }

    inline double unwarp (int s, double w)
    {
        switch (s)
        {
            case Linear: return std::max (w, 0.0);
            case ExtLog: return 20.0 * std::expm1 (w);
            case Mel:    return 700.0 * (std::pow (10.0, w / 2595.0) - 1.0);
            case Bark:   { const double z = std::min (w, 26.0); return std::max (0.0, 1960.0 * (z + 0.53) / (26.28 - z)); }
            case Erb:    return (std::pow (10.0, w / 21.4) - 1.0) / 0.00437;
            default:     return std::exp (w);
        }
    }

    // Lowest frequency shown for a range whose lower end is lo (20 Hz or less): Ext Log starts at 1 Hz like
    // RX; Linear / Mel / Bark / ERB start at 0 Hz; Log keeps lo (it can't reach 0).
    inline double bottom (int s, double lo)
    {
        if (s == Log || lo > 20.0) return lo;
        return s == ExtLog ? 1.0 : 0.0;
    }

    // Maps frequencies to a 0..1 position on the axis and back, for a fixed scale and range (lo as given:
    // pass bottom() to start at 0 Hz).
    struct Axis
    {
        int scale = Log;
        double lo = 20, hi = 20000, w0 = 0, span = 1;

        Axis() { set (Log, 20, 20000); }
        Axis (int s, double l, double h) { set (s, l, h); }
        void set (int s, double l, double h)
        {
            scale = std::clamp (s, 0, (int) kCount - 1);
            lo = scale == Log ? std::max (l, 1.0) : std::max (l, 0.0);
            hi = std::max (h, lo + 1.0);
            w0 = warp (scale, lo);
            span = std::max (1e-12, warp (scale, hi) - w0);
        }
        double pos (double f) const  { return (warp (scale, f) - w0) / span; }
        double freq (double p) const { return unwarp (scale, w0 + p * span); }
    };
}
