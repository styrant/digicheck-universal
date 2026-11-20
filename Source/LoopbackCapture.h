// WASAPI loopback capture: records exactly what Windows is sending to an output device
// (speakers, the Scarlett's outputs, ...), so normal Windows playback can be analysed without
// any routing. Pure Win32 + std (no JUCE headers) to keep the COM headers isolated.
#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

class LoopbackCapture
{
public:
    struct Endpoint { std::wstring id; std::wstring name; bool isDefault = false; };

    // Called from the capture thread with de-interleaved float channels.
    using Callback = std::function<void (const float* const* channels, int numChannels, int numFrames)>;

    static std::vector<Endpoint> listOutputDevices();   // empty on non-Windows platforms

    ~LoopbackCapture() { stop(); }

    // Opens the endpoint and starts capturing. Returns false and sets error on failure.
    bool start (const std::wstring& endpointId, Callback cb, std::string& error);
    void stop();

    double getSampleRate() const noexcept { return sampleRate; }
    int    getNumChannels() const noexcept { return numChannels; }

private:
    void run (std::wstring id);

    std::thread thread;
    std::atomic<bool> quit { false };
    std::atomic<int>  state { 0 };   // 0 starting, 1 running, -1 failed
    std::string startError;
    Callback callback;
    double sampleRate = 0;
    int numChannels = 0;
};
