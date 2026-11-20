#pragma once
#include <JuceHeader.h>
#include "Reassigner.h"
#include "LoopbackCapture.h"
#include <atomic>
#include <thread>

// Owns the signal source, a lock-free sample ring, the DSP thread and a lock-free column queue.
//
//   audio/capture thread --(sample ring)--> DSP thread (Reassigner) --(column queue)--> UI timer
//
// Sources: any JUCE input device (Windows Audio shared/exclusive, ASIO), WASAPI loopback of any
// output device (Windows only), or a built-in test signal.
class AudioEngine : private juce::AudioIODeviceCallback, private juce::Thread
{
public:
    struct Source
    {
        enum Kind { Device, Loopback, Test } kind = Test;
        juce::String typeName, deviceName, group, label, key;
        std::wstring endpointId;
    };

    static constexpr int kMaxRows = 4096;

    AudioEngine();
    ~AudioEngine() override;

    std::vector<Source> scanSources();
    bool start (const Source& s, juce::String& error);
    void stop();

    bool isRunning() const noexcept               { return running; }
    double getSampleRate() const noexcept         { return sampleRate.load(); }
    const juce::StringArray& getChannelNames() const noexcept { return channelNames; }
    const Source& getCurrentSource() const noexcept { return current; }
    float getLoad() const noexcept                { return load.load(); }
    double getLatencySeconds() const noexcept     { return latency.load(); }

    void setChannels (int left, int right)        { chL = left; chR = right; }
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

    juce::AudioDeviceManager deviceManager;

private:
    void push (const float* const* ch, int nch, int n) noexcept;
    void pushColumn (const float* col, int rows) noexcept;
    void startTestThread();

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
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

    std::atomic<float> load { 0 };
    std::atomic<double> latency { 0 }, sampleRate { 0 };

    LoopbackCapture loopback;
    std::thread testThread;
    std::atomic<bool> testQuit { false };

    Source current;
    bool running = false;
    juce::StringArray channelNames;
};
