#pragma once
#include <JuceHeader.h>
#include "AnalysisEngine.h"
#include "LoopbackCapture.h"
#include <thread>

// The standalone app's audio input: any JUCE input device (Windows Audio shared/exclusive, ASIO),
// WASAPI loopback of any output device (Windows only), or a built-in test signal, feeding the
// AnalysisEngine it extends. (A plugin build has no AudioEngine: its processor feeds an AnalysisEngine.)
class AudioEngine : public AnalysisEngine, private juce::AudioIODeviceCallback
{
public:
    struct Source
    {
        enum Kind { Device, Loopback, Test } kind = Test;
        juce::String typeName, deviceName, group, label, key;
        std::wstring endpointId;
    };

    AudioEngine();
    ~AudioEngine() override;

    std::vector<Source> scanSources();
    bool start (const Source& s, juce::String& error);
    void stop();

    bool isRunning() const noexcept               { return running; }
    const juce::StringArray& getChannelNames() const noexcept { return channelNames; }
    const Source& getCurrentSource() const noexcept { return current; }

    juce::AudioDeviceManager deviceManager;

private:
    void startTestThread();
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    LoopbackCapture loopback;
    std::thread testThread;
    std::atomic<bool> testQuit { false };

    Source current;
    bool running = false;
    juce::StringArray channelNames;
};
