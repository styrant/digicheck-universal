#include "AudioEngine.h"
#include <chrono>

AudioEngine::AudioEngine() : juce::Thread ("Spectroscope DSP")
{
    colData.assign ((size_t) kSlots * kMaxRows, 0.0f);
    colGen.assign ((size_t) kSlots, -1);
    colRows.assign ((size_t) kSlots, 0);
    dsp.setSink ([this] (const float* c, int rows) { pushColumn (c, rows); });
    deviceManager.getAvailableDeviceTypes();       // creates WASAPI / ASIO / ALSA ... types
    startThread (juce::Thread::Priority::high);
}

AudioEngine::~AudioEngine()
{
    stop();
    stopThread (2000);
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
    dropped = 0;   // the DSP thread flushes the ring itself when it reconfigures
    channelNames.clear();

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
        sampleRate = dev->getCurrentSampleRate();
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
        sampleRate = loopback.getSampleRate();
        const int n = loopback.getNumChannels();
        for (int i = 0; i < n; ++i)
            channelNames.add (n == 2 ? (i == 0 ? "Left" : "Right") : "Channel " + juce::String (i + 1));
    }
    else
    {
        sampleRate = 48000.0;
        channelNames.add ("Left"); channelNames.add ("Right");
        startTestThread();
    }

    current = s;
    if (analyzer != nullptr) analyzer->prepareToPlay (sampleRate.load(), 512);
    loudReset = true;
    running = true;
    {
        const juce::ScopedLock sl (cfgLock);
        pendingGen = ++generation;
    }
    cfgDirty = true;               // reconfigure the DSP for the new sample rate
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
    sampleRate = 0;
}

void AudioEngine::setConfig (const Reassigner::Config& c)
{
    {
        const juce::ScopedLock sl (cfgLock);
        pendingCfg = c;
        pendingGen = ++generation;
    }
    pendingFloor = c.floorDb;
    cfgDirty = true;
}

void AudioEngine::push (const float* const* ch, int nch, int n) noexcept
{
    if (nch <= 0 || n <= 0) return;
    if (fifo.getFreeSpace() < n) { dropped += n; return; }        // DSP fell behind: shown as a white gap
    const float* L = ch[juce::jlimit (0, nch - 1, chL.load())];
    const float* R = ch[juce::jlimit (0, nch - 1, chR.load())];
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 > 0) { ring.copyFrom (0, s1, L, n1); ring.copyFrom (1, s1, R, n1); }
    if (n2 > 0) { ring.copyFrom (0, s2, L + n1, n2); ring.copyFrom (1, s2, R + n1, n2); }
    fifo.finishedWrite (n1 + n2);
}

void AudioEngine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut,
                                                    int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    for (int i = 0; i < numOut; ++i) if (out[i] != nullptr) juce::FloatVectorOperations::clear (out[i], numSamples);
    push (in, numIn, numSamples);
}

void AudioEngine::pushColumn (const float* col, int rows) noexcept
{
    int s1, n1, s2, n2;
    colFifo.prepareToWrite (1, s1, n1, s2, n2);
    const int idx = n1 > 0 ? s1 : (n2 > 0 ? s2 : -1);
    if (idx < 0) return;                                              // UI is not keeping up: drop
    const int r = juce::jmin (rows, kMaxRows);
    std::copy (col, col + r, colData.data() + (size_t) idx * kMaxRows);
    colGen[(size_t) idx] = dspGen;
    colRows[(size_t) idx] = r;
    colFifo.finishedWrite (1);
}

void AudioEngine::run()
{
    bool configured = false;
    double busyMs = 0, audioMs = 0, loudSr = 0;
    std::vector<float> L (2048), R (2048);

    while (! threadShouldExit())
    {
        if (cfgDirty.exchange (false))
        {
            Reassigner::Config c; int g;
            { const juce::ScopedLock sl (cfgLock); c = pendingCfg; g = pendingGen; }
            c.sampleRate = sampleRate.load();
            configured = c.sampleRate > 0 && c.rows > 0;
            if (configured)
            {
                c.rows = juce::jmin (c.rows, kMaxRows);
                dsp.configure (c);
                latency = dsp.getLatencySeconds();
            }
            dspGen = g;
            fifo.finishedRead (fifo.getNumReady());
            dropped = 0;
        }
        if (floorDirty.exchange (false) && configured) dsp.setFloorDb (pendingFloor.load());

        if (! configured || ! running) { wait (10); continue; }

        // Loudness keeps running across display-setting changes; it restarts only on a new
        // source, sample rate or channel pair, or when the user presses Reset.
        if (loudReset.exchange (false) || std::abs (sampleRate.load() - loudSr) > 0.5)
        {
            loudSr = sampleRate.load();
            loud.configure (loudSr, chL.load() == chR.load() ? 1 : 2);
            lufsS = lufsI = truePeak = LoudnessMeter::kNone;
        }

        const bool spec = specActive.load();
        if (spec && specResume.exchange (false)) dsp.resetStream();
        if (const int d = dropped.exchange (0); d > 0 && spec) dsp.emitGap (d);

        const int ready = fifo.getNumReady();
        const double sr = sampleRate.load();
        if (ready > (int) (sr * 0.5))                                 // more than 0.5 s behind: skip ahead
        {
            fifo.finishedRead (ready);
            if (spec) dsp.emitGap (ready);
            continue;
        }
        if (ready < 256) { wait (3); continue; }

        const int n = juce::jmin (ready, (int) L.size());
        int s1, n1, s2, n2;
        fifo.prepareToRead (n, s1, n1, s2, n2);
        if (n1 > 0) { std::copy_n (ring.getReadPointer (0, s1), n1, L.data()); std::copy_n (ring.getReadPointer (1, s1), n1, R.data()); }
        if (n2 > 0) { std::copy_n (ring.getReadPointer (0, s2), n2, L.data() + n1); std::copy_n (ring.getReadPointer (1, s2), n2, R.data() + n1); }
        fifo.finishedRead (n1 + n2);

        const auto t0 = std::chrono::steady_clock::now();
        const int total = n1 + n2;
        if (spec) dsp.process (L.data(), R.data(), total);
        loud.process (L.data(), R.data(), total);
        if (keyFinder != nullptr) keyFinder->push (L.data(), R.data(), total, sr);
        if (analyzer != nullptr)
        {
            // Feed the analyzer in ~10 ms slices so it publishes one update per 10 ms of audio, evenly,
            // however large this chunk is (a single big chunk used to give one update per chunk, so
            // the bars moved in uneven bursts).
            const int slice = juce::jmax (64, (int) (sr / 100.0));
            for (int off = 0; off < total; off += slice)
                analyzer->processStereo (L.data() + off, R.data() + off, juce::jmin (slice, total - off));
        }
        lufsS = loud.getShortTerm(); lufsI = loud.getIntegrated(); truePeak = loud.getTruePeak();
        busyMs += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        audioMs += (n1 + n2) / sr * 1000.0;
        if (audioMs >= 500) { load = (float) (busyMs / audioMs); busyMs = audioMs = 0; }
    }
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
