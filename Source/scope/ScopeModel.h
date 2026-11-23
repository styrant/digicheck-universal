// The oscilloscope's engine (the Scope view, after Scyllascope's design). Pure C++, no JUCE: shared by
// the standalone app and a future plugin, and unit-tested on its own (tests/scope_test.cpp).
//
// It keeps one window of audio (the division: 1/4 to 8 bars at the tempo, or triplets) in ring buffers:
//   main   the chosen source: Left, Right, Mid ((L+R)/2) or Peak (whichever channel is larger, per sample)
//   side   (L-R)/2, for the Side2SC overlay
//   bp     main through the display band-pass (BP): only what is drawn changes, never any audio
//   l, r   the input as it came, for exporting audio
// and, every 64 samples, a summary block (min / max / sum of squares of main, bp and side, and the
// energy in five spectral bands: sub < 80 Hz, low < 250, mid < 2 k, high < 6 k, air), so a column of the
// display costs a handful of blocks however far out it's zoomed.
//
// Modes:
//   Free    the trace sweeps left to right across the window and wraps; after silence it restarts at
//           the left edge as soon as audio comes in, so a hit always starts at the same place
//   Scroll  the newest audio enters at the right edge and moves left
//   frozen  (Freeze) nothing is written
//
// The window time t (0 .. window seconds, left to right) maps to a ring position; Free: t x rate;
// Scroll: the newest sample at the right edge.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace scope
{
    enum Source { Left = 0, Right, Mid, Peak };
    enum Mode { Free = 0, Scroll };
    constexpr int kBands = 5;
    constexpr int kBlock = 64;
    constexpr double kBandEdges[kBands - 1] = { 80.0, 250.0, 2000.0, 6000.0 };

    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void lowpass (double f, double sr, double q = 0.7071);
        void highpass (double f, double sr, double q = 0.7071);
        void bandpass (double f, double sr, double q);
        float process (float x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return (float) y; }
        void reset() { z1 = z2 = 0; }
    };

    struct Column
    {
        bool valid = false;
        float mn = 0, mx = 0;               // main (or band-passed main) range
        float smn = 0, smx = 0;             // side range
        std::array<float, kBands> bands {}; // energy per spectral band (relative)
    };

    struct Stats { bool valid = false; double seconds = 0, peakDb = -200, rmsDb = -200, crestDb = 0; };

    // the ring buffers and their summaries; the live data and the Hold snapshot are both one of these
    struct Rings
    {
        struct Block { float mn = 0, mx = 0, bmn = 0, bmx = 0, smn = 0, smx = 0; double sq = 0, bsq = 0; float e[kBands] {}; int count = 0; };
        std::size_t n = 0;                  // samples in the window
        std::vector<float> main, side, bp, l, r;
        std::vector<Block> blocks;
        std::size_t writePos = 0;           // next position to write
        std::uint64_t written = 0;          // samples written since the last clear
        bool valid (std::size_t pos) const  { return written >= n || (pos < writePos); }
        void resize (std::size_t samples);
        void clear();
    };

    class ScopeModel
    {
    public:
        ScopeModel();

        // window = seconds of audio shown; changing it or the rate clears the buffers
        void configure (double sampleRate, double windowSeconds);
        double sampleRate() const           { return sr; }
        double windowSeconds() const        { return window; }

        void setSource (int s)              { source = s; }
        void setMode (int m);
        int  getMode() const                { return mode; }
        void setFrozen (bool f)             { frozen = f; }
        bool isFrozen() const               { return frozen; }
        void setBandpass (double hz);       // the display band-pass's centre (always computed; shown when BP is on)
        double bandpassHz() const           { return bpHz; }

        void push (const float* left, const float* right, int n);
        void reset();                       // clears the live trace (not the Hold snapshot)

        // display: n columns over window time [t0, t1]
        void columns (double t0, double t1, int n, bool bandpassed, bool held, std::vector<Column>& out) const;
        // the raw samples over [t0, t1] (for drawing when zoomed in to the samples)
        void samples (double t0, double t1, bool bandpassed, bool held, std::vector<float>& main, std::vector<float>& side) const;
        Stats stats (double t0, double t1, bool bandpassed) const;
        double playhead() const;            // window time of the write position (Free); the right edge (Scroll)
        bool hasSignal() const              { return live.written > 0; }

        // Hold: a snapshot drawn behind the live trace until released
        void hold (bool on);
        bool isHolding() const              { return holding; }

        // Recorder: armed, it starts on the first audio and stops after one window
        enum class Rec { Idle, Armed, Recording, Done };
        void arm();
        void disarm();
        Rec recState() const                { return rec; }
        double recProgress() const          { return rec == Rec::Recording ? (double) takeL.size() / (double) live.n : rec == Rec::Done ? 1.0 : 0.0; }

        struct Take { std::vector<float> l, r; double sampleRate = 0; };
        // the recording (or, without one, the window's audio, oldest first), processed:
        // normalise to 0 dBFS, trim trailing silence (below silenceDb), pad to a whole number of grid steps
        Take take (bool normalize, bool trim, bool snap, double gridSeconds, double silenceDb = -96.0) const;

        // levels for starting the recorder and the Free sweep
        static constexpr float kStartThreshold = 0.000316f;   // -70 dBFS
        static constexpr double kSilenceToRetrigger = 0.25;    // seconds of silence before Free restarts at the left

    private:
        std::size_t positionAt (const Rings& g, double t) const;
        void write (float l, float r);

        double sr = 48000, window = 2.0, bpHz = 100.0;
        int source = Mid, mode = Free;
        bool frozen = false, holding = false;
        Rings live, held;
        std::array<Biquad, 8> bandFilters;  // sub LP | low HP+LP | mid HP+LP | high HP+LP | air HP
        Biquad bp1, bp2;
        std::size_t silentRun = 0;
        Rec rec = Rec::Idle;
        std::vector<float> takeL, takeR;
    };
}
