// AnalyzerModel.h - UI-side meter ballistics (peak, peak-hold, average).
//
// Port of sp::Meter and sp::Averager. The original assumed a fixed 48 ms
// poll; this version takes the real elapsed time so it works at any refresh
// rate (vsync-driven repaint). Averaging uses fixed 20 ms bins in a ring so
// the "average time" window is exact regardless of frame rate.
//
// No JUCE dependency.

#pragma once

#include "FilterBank.h"

#include <cmath>
#include <vector>

namespace sa
{

struct MeterOptions
{
    double peakDecayDbPerSec  = 15.0;
    bool   holdInfinite       = false;
    double holdTimeMs         = 2000.0;
    double holdDecayDbPerSec  = 3.0;
    double avgTimeMs          = 3000.0;
    double avgSlopeDbPerOct   = 0.0;   // tilt, pivot at 1 kHz
};

class AnalyzerModel
{
public:
    static constexpr double kFloorDb   = -200.0;
    static constexpr double kBinMs     = 20.0;
    static constexpr int    kMaxBins   = 1024;   // > 20 s at 20 ms
    static constexpr double kRmsToPeak = 3.0102999566398; // sine RMS -> peak (as original "+6 / 2")

    AnalyzerModel()
        : binSums ((size_t) kMaxBins * kMaxBands, 0.0),
          binCounts ((size_t) kMaxBins, 0)
    {
        reset();
    }

    // Returns true if the layout changed (caller may want to re-layout).
    bool setLayout (const BandLayout& L)
    {
        if (L.key == key && L.numBands == numBands)
            return false;

        key = L.key;
        numBands = L.numBands;
        for (int b = 0; b < numBands; ++b)
        {
            centre[(size_t) b] = L.centre[(size_t) b];
            lower [(size_t) b] = L.lower [(size_t) b];
            upper [(size_t) b] = L.upper [(size_t) b];
        }
        reset();
        return true;
    }

    const LayoutKey& getKey() const noexcept { return key; }

    // Merge one analysis block. Snapshots for a different layout are ignored.
    void push (const Snapshot& s) noexcept
    {
        if (s.key != key || s.numBands != numBands)
            return;

        for (int b = 0; b < numBands; ++b)
        {
            pendingPeak[(size_t) b] = std::max (pendingPeak[(size_t) b], (double) s.peak[(size_t) b]);
            curBinSum  [(size_t) b] += (double) s.sum[(size_t) b];
        }
        curBinCount += s.count;
        havePending = true;
    }

    // Advance the ballistics by dtMs using whatever was pushed since last tick.
    void tick (double dtMs, const MeterOptions& o) noexcept
    {
        dtMs = std::clamp (dtMs, 0.0, 250.0);

        // --- peak + hold (sp::Meter) ---
        for (int b = 0; b < numBands; ++b)
        {
            const double v = havePending ? toDb (pendingPeak[(size_t) b]) : kFloorDb;

            double& p = peak[(size_t) b];
            p -= o.peakDecayDbPerSec * dtMs * 0.001;
            if (p < v) p = v;
            if (p < kFloorDb) p = kFloorDb;

            double& h  = hold[(size_t) b];
            double& ht = holdTimer[(size_t) b];
            if (ht > 0.0)
                ht -= dtMs;
            else if (! o.holdInfinite)
                h -= o.holdDecayDbPerSec * dtMs * 0.001;

            if (h < v)
            {
                h  = v;
                ht = o.holdTimeMs;
            }
            if (h < kFloorDb) h = kFloorDb;

            pendingPeak[(size_t) b] = 0.0;
        }
        havePending = false;

        // --- average (rectangular window of 20 ms bins) ---
        const int window = std::clamp ((int) std::lround (o.avgTimeMs / kBinMs), 1, kMaxBins - 1);
        if (window != windowBins)
        {
            windowBins = window;
            recomputeWindowSums();
        }

        binElapsedMs += dtMs;
        while (binElapsedMs >= kBinMs)
        {
            binElapsedMs -= kBinMs;
            commitBin();
        }

        const int64_t totalCount = windowCount + curBinCount;
        for (int b = 0; b < numBands; ++b)
        {
            double a = kFloorDb;
            if (totalCount > 0)
            {
                const double power = (windowSum[(size_t) b] + curBinSum[(size_t) b]) / (double) totalCount;
                if (power > 1e-20)
                    a = 10.0 * std::log10 (power) + kRmsToPeak
                        + o.avgSlopeDbPerOct * std::log2 (centre[(size_t) b] / 1000.0);
            }
            avg[(size_t) b] = a;
        }
    }

    void reset() noexcept
    {
        peak.fill (kFloorDb);
        hold.fill (kFloorDb);
        avg.fill (kFloorDb);
        holdTimer.fill (0.0);
        pendingPeak.fill (0.0);
        havePending = false;
        std::fill (binSums.begin(), binSums.end(), 0.0);
        std::fill (binCounts.begin(), binCounts.end(), 0);
        curBinSum.fill (0.0);
        curBinCount = 0;
        windowSum.fill (0.0);
        windowCount = 0;
        binHead = 0;
        binElapsedMs = 0.0;
    }

    int numBands = 0;
    std::array<double, kMaxBands> centre {}, lower {}, upper {};
    std::array<double, kMaxBands> peak {}, hold {}, avg {};   // dB

private:
    static double toDb (double power) noexcept
    {
        return power > 1e-20 ? 10.0 * std::log10 (power) : kFloorDb;
    }

    double* binRow (int i) noexcept { return binSums.data() + (size_t) i * kMaxBands; }

    void commitBin() noexcept
    {
        // drop the bin leaving the window
        const int leaving = (binHead - windowBins + kMaxBins) % kMaxBins;
        // ring holds bins [binHead - windowBins, binHead); leaving one is replaced
        double* out = binRow (leaving);
        for (int b = 0; b < numBands; ++b)
            windowSum[(size_t) b] -= out[b];
        windowCount -= binCounts[(size_t) leaving];

        // store new bin at binHead
        double* in = binRow (binHead);
        for (int b = 0; b < numBands; ++b)
        {
            in[b] = curBinSum[(size_t) b];
            windowSum[(size_t) b] += in[b];
            curBinSum[(size_t) b] = 0.0;
        }
        binCounts[(size_t) binHead] = curBinCount;
        windowCount += curBinCount;
        curBinCount = 0;

        binHead = (binHead + 1) % kMaxBins;

        // clear the slot that will leave next time, keeping invariants simple
        // (bins older than the window are always zero)
        const int stale = (binHead - windowBins - 1 + 2 * kMaxBins) % kMaxBins;
        if (stale != binHead)
        {
            double* s = binRow (stale);
            for (int b = 0; b < numBands; ++b) s[b] = 0.0;
            binCounts[(size_t) stale] = 0;
        }

        // guard against floating point drift
        for (int b = 0; b < numBands; ++b)
            if (windowSum[(size_t) b] < 0.0) windowSum[(size_t) b] = 0.0;
        if (windowCount < 0) windowCount = 0;
    }

    void recomputeWindowSums() noexcept
    {
        windowSum.fill (0.0);
        windowCount = 0;
        for (int i = 0; i < kMaxBins; ++i)
        {
            const int age = (binHead - 1 - i + kMaxBins) % kMaxBins; // newest first
            double* row = binRow (age);
            if (i < windowBins)
            {
                for (int b = 0; b < numBands; ++b) windowSum[(size_t) b] += row[b];
                windowCount += binCounts[(size_t) age];
            }
            else
            {
                for (int b = 0; b < numBands; ++b) row[b] = 0.0;
                binCounts[(size_t) age] = 0;
            }
        }
    }

    LayoutKey key;

    std::array<double, kMaxBands> holdTimer {}, pendingPeak {};
    bool havePending = false;

    std::vector<double>  binSums;    // kMaxBins x kMaxBands
    std::vector<int64_t> binCounts;  // kMaxBins
    std::array<double, kMaxBands> curBinSum {}, windowSum {};
    int64_t curBinCount = 0, windowCount = 0;
    int binHead = 0, windowBins = 150;
    double binElapsedMs = 0.0;
};

} // namespace sa
