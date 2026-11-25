// The analysis side of the app, with no audio device: a lock-free sample ring, the DSP thread and
// everything it feeds (the reassigned spectrogram's column queue, the loudness meter, the spectrum
// analyzer and the AudioTaps: key finder, tonal balance, recorder).
//
//   push() (audio thread, real-time safe) --(sample ring)--> DSP thread --(column queue)--> UI
//
// The standalone app's AudioEngine owns the audio devices and calls prepare() / push() / release().
// A plugin's processor does the same from prepareToPlay / processBlock / releaseResources, so all the
// analysis code is shared unchanged between the two.
#pragma once
#include <JuceHeader.h>
#include "Reassigner.h"
#include "LoudnessMeter.h"
#include "analyzer/PluginProcessor.h"
#include "AudioTap.h"
#include <array>
#include <atomic>

class AnalysisEngine : private juce::Thread
{
public:
    static constexpr int kMaxRows = 4096;

    AnalysisEngine();
    ~AnalysisEngine() override;

    // stream control: prepare before the first push (and on every sample-rate change), release after the last
    void prepare (double sampleRate);
    void release();
    bool isAnalysing() const noexcept             { return analysing.load(); }

    // audio thread: the input channels (the chosen L / R pair is taken from them). Never blocks or allocates.
    void push (const float* const* channels, int numChannels, int numSamples) noexcept;
    void pushLR (const float* L, const float* R, int numSamples) noexcept;   // already the chosen L / R
    int channelL() const noexcept { return chL.load(); }
    int channelR() const noexcept { return chR.load(); }

    double getSampleRate() const noexcept         { return sampleRate.load(); }
    float getLoad() const noexcept                { return load.load(); }
    double getLatencySeconds() const noexcept     { return latency.load(); }

    void setChannels (int left, int right)        { if (left != chL || right != chR) loudReset = true; chL = left; chR = right; }
    void resetLoudness()                          { loudReset = true; }
    void setAnalyzer (SpectrumAnalyzerProcessor* p) { analyzer = p; }   // fed from the DSP thread
    // Analysis taps (key finder, tonal balance, recorder) fed from the DSP thread. Register them before
    // the first prepare(); they must outlive this engine's DSP thread.
    void addTap (AudioTap* t)                     { for (auto& slot : taps) if (slot.load() == nullptr) { slot = t; return; } jassertfalse; }
    // The spectrogram's (expensive) analysis only runs while its view is showing
    void setSpectrogramActive (bool a)            { if (a && ! specActive.load()) specResume = true; specActive = a; }
    double getShortTermLufs() const noexcept      { return lufsS.load(); }
    double getIntegratedLufs() const noexcept     { return lufsI.load(); }
    double getTruePeakDb() const noexcept         { return truePeak.load(); }
    void setConfig (const Reassigner::Config& c); // sampleRate is filled in by the engine
    void setFloorDb (double db)                   { pendingFloor = db; floorDirty = true; }

    // UI thread: hands every queued column of the current configuration to fn (const float* dB, int rows).
    template <typename Fn>
    int popColumns (Fn&& fn)
    {
        const int ready = colFifo.getNumReady();
        if (ready <= 0) return 0;
        int s1, n1, s2, n2, used = 0;
        colFifo.prepareToRead (ready, s1, n1, s2, n2);
        const int gen = generation.load();
        auto take = [&] (int start, int n)
        {
            for (int i = start; i < start + n; ++i)
                if (colGen[(size_t) i] == gen) { fn (colData.data() + (size_t) i * kMaxRows, colRows[(size_t) i]); ++used; }
        };
        take (s1, n1); take (s2, n2);
        colFifo.finishedRead (n1 + n2);
        return used;
    }

private:
    void pushColumn (const float* col, int rows) noexcept;
    void run() override;

    static constexpr int kRing = 1 << 18;
    juce::AbstractFifo fifo { kRing };
    juce::AudioBuffer<float> ring { 2, kRing };
    std::atomic<int> chL { 0 }, chR { 1 }, dropped { 0 };

    juce::CriticalSection cfgLock;
    Reassigner::Config pendingCfg;
    int pendingGen = 0;
    std::atomic<bool> cfgDirty { false }, floorDirty { false };
    std::atomic<double> pendingFloor { -100.0 };
    std::atomic<int> generation { 0 };

    Reassigner dsp;
    int dspGen = -1;

    static constexpr int kSlots = 512;
    juce::AbstractFifo colFifo { kSlots };
    std::vector<float> colData;
    std::vector<int> colGen, colRows;

    SpectrumAnalyzerProcessor* analyzer = nullptr;
    std::array<std::atomic<AudioTap*>, 8> taps {};
    std::atomic<bool> specActive { true }, specResume { false };
    LoudnessMeter loud;
    std::atomic<bool> loudReset { true };
    std::atomic<double> lufsS { LoudnessMeter::kNone }, lufsI { LoudnessMeter::kNone }, truePeak { LoudnessMeter::kNone };

    std::atomic<float> load { 0 };
    std::atomic<double> latency { 0 }, sampleRate { 0 };
    std::atomic<bool> analysing { false };

};
