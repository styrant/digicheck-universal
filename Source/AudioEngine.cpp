#include "AudioEngine.h"
#include <chrono>

AudioEngine::AudioEngine()
{
    deviceManager.getAvailableDeviceTypes();       // creates WASAPI / ASIO / ALSA ... types
}

AudioEngine::~AudioEngine()
{
    stop();
}

std::vector<AudioEngine::Source> AudioEngine::scanSources()
{
    std::vector<Source> out;

    for (auto* type : deviceManager.getAvailableDeviceTypes())
    {
        const auto tn = type->getTypeName();
        if (tn.containsIgnoreCase ("Low Latency") || tn == "DirectSound") continue;
        type->scanForDevices();
        juce::String group = tn;
        if (tn == "Windows Audio")                              group = "Windows Audio (shared - works next to your DAW)";
        else if (tn.containsIgnoreCase ("Exclusive"))           group = "Windows Audio (exclusive)";
        else if (tn == "ASIO")                                  group = "ASIO (bit-exact; may be busy while your DAW runs)";
        for (auto& name : type->getDeviceNames (true))
        {
            Source s; s.kind = Source::Device; s.typeName = tn; s.deviceName = name; s.group = group;
            s.label = (tn == "ASIO" ? "ASIO: " : "") + name;
            s.key = tn + "|" + name;
            out.push_back (s);
        }
    }

    for (auto& e : LoopbackCapture::listOutputDevices())
    {
        Source s; s.kind = Source::Loopback; s.endpointId = e.id;
        s.deviceName = juce::String (e.name.c_str());
        s.group = "Windows playback (loopback capture)";
        s.label = "Playback: " + s.deviceName + (e.isDefault ? " (default)" : "");
        s.key = "loopback|" + juce::String (e.id.c_str());
        out.push_back (s);
    }

    Source t; t.kind = Source::Test; t.group = "Other"; t.label = "Test signal"; t.key = "test";
    out.push_back (t);
    return out;
}

bool AudioEngine::start (const Source& s, juce::String& error)
{
    stop();
    channelNames.clear();
    double rate = 0;

    if (s.kind == Source::Device)
    {
        deviceManager.setCurrentAudioDeviceType (s.typeName, true);
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        deviceManager.getAudioDeviceSetup (setup);
        setup.inputDeviceName  = s.deviceName;
        setup.outputDeviceName = s.typeName == "ASIO" ? s.deviceName : juce::String();   // ASIO needs one driver for both
        setup.useDefaultInputChannels = false;  setup.inputChannels.clear();  setup.inputChannels.setRange (0, 64, true);
        setup.useDefaultOutputChannels = false; setup.outputChannels.clear();
        setup.sampleRate = 0; setup.bufferSize = 0;
        error = deviceManager.setAudioDeviceSetup (setup, true);
        auto* dev = deviceManager.getCurrentAudioDevice();
        if (error.isNotEmpty() || dev == nullptr)
        {
            if (error.isEmpty()) error = "Could not open " + s.deviceName;
            deviceManager.closeAudioDevice();
            return false;
        }
        const auto names = dev->getInputChannelNames();
        const auto active = dev->getActiveInputChannels();
        for (int i = 0; i < names.size(); ++i) if (active[i]) channelNames.add (names[i]);
        if (channelNames.isEmpty()) { error = s.deviceName + " has no active inputs"; deviceManager.closeAudioDevice(); return false; }
        rate = dev->getCurrentSampleRate();
        prepare (rate);
        deviceManager.addAudioCallback (this);
    }
    else if (s.kind == Source::Loopback)
    {
        std::string err;
        if (! loopback.start (s.endpointId, [this] (const float* const* ch, int nch, int n) { push (ch, nch, n); }, err))
        {
            error = err;
            return false;
        }
        rate = loopback.getSampleRate();
        const int n = loopback.getNumChannels();
        for (int i = 0; i < n; ++i)
            channelNames.add (n == 2 ? (i == 0 ? "Left" : "Right") : "Channel " + juce::String (i + 1));
    }
    else
    {
        rate = 48000.0;
        channelNames.add ("Left"); channelNames.add ("Right");
        prepare (rate);
        startTestThread();
    }

    if (s.kind == Source::Loopback) prepare (rate);
    current = s;
    running = true;
    return true;
}

void AudioEngine::stop()
{
    if (! running) return;
    deviceManager.removeAudioCallback (this);
    deviceManager.closeAudioDevice();
    loopback.stop();
    testQuit = true;
    if (testThread.joinable()) testThread.join();
    running = false;
    release();
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int i = 0; i < numOut; ++i) if (out[i] != nullptr) juce::FloatVectorOperations::clear (out[i], numSamples);
    push (in, numIn, numSamples);
}

void AudioEngine::startTestThread()
{
    testQuit = false;
    testThread = std::thread ([this]
    {
        // 12 s loop: -12 dBFS log sweep 20 Hz-20 kHz (8 s), then a chord; -30 dBFS 1 kHz throughout,
        // a click every second, low noise, and a 3 kHz tone on the left channel only.
        const double sr = 48000.0, tw = juce::MathConstants<double>::twoPi;
        const int block = 480, loopLen = (int) sr * 12;
        std::vector<float> L ((size_t) block), R ((size_t) block);
        const float* ch[2] = { L.data(), R.data() };
        juce::Random rng;
        double ph = 0; long long pos = 0;
        auto next = std::chrono::steady_clock::now();
        while (! testQuit)
        {
            for (int i = 0; i < block; ++i, ++pos)
            {
                const long long p = pos % loopLen;
                const double t = p / sr;
                double s = 0;
                if (t < 8) { ph += tw * 20.0 * std::pow (1000.0, t / 8) / sr; s += 0.25 * std::sin (ph); }
                else s += 0.06 * (std::sin (tw * 220 * t) + std::sin (tw * 277.18 * t) + std::sin (tw * 329.63 * t));
                if (p == 0) ph = 0;
                s += 0.0316 * std::sin (tw * 1000 * t);
                if (p % (long long) sr == (long long) sr / 2) s += 0.5;
                s += (rng.nextFloat() * 2 - 1) * 0.0015;
                L[(size_t) i] = (float) (s + 0.01 * std::sin (tw * 3000 * t));
                R[(size_t) i] = (float) s;
            }
            push (ch, 2, block);
            next += std::chrono::milliseconds (10);
            std::this_thread::sleep_until (next);
        }
    });
}
