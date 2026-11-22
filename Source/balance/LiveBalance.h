// Live tonal balance of the input: the same measurement as TargetBuilder (TonalAnalysis), over a
// rolling window (Realtime 0.3 s, 1 / 3 / 10 / 30 s) or everything since Reset. Frames every 23 ms. Runs on its own low-priority thread,
// fed from the DSP thread, only while the Balance view is shown.
#pragma once
#include <JuceHeader.h>
#include "TonalAnalysis.h"
#include <array>
#include <atomic>
#include <deque>

class LiveBalance : private juce::Thread
{
public:
    LiveBalance();
    ~LiveBalance() override;

    void setActive (bool a);
    bool isActive() const noexcept { return active.load(); }
    void setWindow (int index);                   // 0: realtime, 1: 1 s, 2: 3 s, 3: 10 s, 4: 30 s, 5: since reset
    void reset() { resetRequest = true; }
    static juce::StringArray windowNames() { return { "Realtime", "1 s", "3 s", "10 s", "30 s", "Since reset" }; }
    static constexpr int kDefaultWindow = 3;
    static bool isFast (int index) { return index <= 1; }

    // DSP thread
    void push (const float* left, const float* right, int n, double sampleRate) noexcept;

    struct Snapshot
    {
        bool valid = false;
        std::vector<double> raw, smooth;           // normalised spectrum (kBins) and its 1/3-octave smoothing
        std::array<double, 4> shares {};           // band shares
        double seconds = 0;                        // audio (above the gate) in the result
        int window = 0;                            // the window it was measured over
        int serial = 0;                            // increments with every new result
    };
    Snapshot getSnapshot() const;

private:
    void run() override;
    void publish();

    static constexpr int kRing = 1 << 18;
    juce::AbstractFifo fifo { kRing };
    std::vector<float> ring;
    std::atomic<double> pushRate { 0 };
    std::atomic<bool> active { false }, resetRequest { true };
    std::atomic<int> windowIndex { kDefaultWindow };
    std::atomic<bool> republish { false };

    // balance thread only
    std::unique_ptr<tonal::FrameAnalyzer> frames;
    std::unique_ptr<tonal::SpectrumAccumulator> sinceReset;
    struct Frame { std::vector<float> power; double ms; };
    std::deque<Frame> recent;                      // the last 30 s of frames (silent ones have no spectrum)
    int framesSincePublish = 0;
    double frameSeconds = 0.0232;

    mutable juce::SpinLock lock;
    Snapshot snap;
};
