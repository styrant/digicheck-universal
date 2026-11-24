#pragma once
#include <JuceHeader.h>
#include "AnalysisEngine.h"
#include "LoopbackCapture.h"
#include <thread>

// The standalone app's audio input: any JUCE input device (Windows Audio shared/exclusive, ASIO),
// WASAPI loopback of any output device (Windows only), the DAW bridge (the DigiCheck VST3 sending what
// it sees, see bridge/Bridge.h), or a built-in test signal, feeding the AnalysisEngine it extends. (A plugin build has no AudioEngine: its processor feeds an AnalysisEngine.)
class AudioEngine : public AnalysisEngine, private juce::AudioIODeviceCallback, private juce::Timer
{
public:
    struct Source
    {
        enum Kind { Device, Loopback, Test, Bridge } kind = Test;
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
    juce::String bridgeStatus() const;               // "From DAW": which track is heard, or what's missing

    juce::AudioDeviceManager deviceManager;

private:
    void startTestThread();
    void startBridgeThread (const juce::String& track);
    void timerCallback() override;                   // the bridge: a sender's sample rate changed, restart
    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}

    LoopbackCapture loopback;
    std::thread testThread;
    std::atomic<bool> testQuit { false };
    std::atomic<int> bridgeSlot { -1 };              // the slot being read (-1: none sending)
    std::atomic<double> bridgeRateChange { 0 };      // set by the reader when the sender's rate differs
    double bridgeRate = 48000;

    Source current;
    bool running = false;
    juce::StringArray channelNames;
};
