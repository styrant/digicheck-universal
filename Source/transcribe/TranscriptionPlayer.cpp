#include "TranscriptionPlayer.h"

namespace
{
    constexpr double kTwoPi = 6.283185307179586;

    // how each layer sounds: level, attack and release (s), decay time constant (s, 0 = held), harmonics
    struct Timbre { float level, attack, release, decay; float h[4]; float pan; };
    const Timbre kTimbre[TranscriptionPlayer::kLayers] = {
        { 0.10f, 0.004f, 0.12f, 1.2f, { 1.0f, 0.35f, 0.12f, 0.00f }, 0.5f },   // notes: soft piano
        { 0.17f, 0.008f, 0.08f, 3.0f, { 1.0f, 0.50f, 0.25f, 0.12f }, 0.5f },   // melody: brighter, held
        { 0.05f, 0.040f, 0.25f, 0.0f, { 1.0f, 0.50f, 0.33f, 0.25f }, 0.5f },   // chords: pad
        { 0.14f, 0.005f, 0.10f, 2.5f, { 1.0f, 0.30f, 0.00f, 0.00f }, 0.5f },   // chord bass
        { 0.25f, 0.001f, 0.01f, 0.0f, { 0, 0, 0, 0 }, 0.5f },                 // drums (one-shots, below)
    };

    bool isKick (int p)   { return p == 35 || p == 36; }
    bool isSnare (int p)  { return p >= 37 && p <= 40; }
    bool isTom (int p)    { return p == 41 || p == 43 || p == 45 || p == 47 || p == 48 || p == 50; }
    float noise (juce::uint32& s) { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 8388608.0f - 1.0f; }
}

TranscriptionPlayer::TranscriptionPlayer()
{
    readAhead.startThread (juce::Thread::Priority::normal);
}

TranscriptionPlayer::~TranscriptionPlayer()
{
    transport.setSource (nullptr);
    readAhead.stopThread (2000);
}

void TranscriptionPlayer::unload()
{
    transport.stop();
    transport.setSource (nullptr);
    source.reset();
    recordingBuffer.setSize (0, 0);
    loaded = false;
}

void TranscriptionPlayer::loadFile (const juce::File& f, juce::AudioFormatManager& formats)
{
    unload();
    auto* reader = formats.createReaderFor (f);
    if (reader == nullptr) return;
    const double rate = reader->sampleRate;
    source = std::make_unique<juce::AudioFormatReaderSource> (reader, true);
    transport.setSource (source.get(), 1 << 16, &readAhead, rate, 2);
    loaded = true;
}

void TranscriptionPlayer::loadRecording (std::shared_ptr<const std::vector<float>> mono, double rate)
{
    unload();
    if (mono == nullptr || mono->empty() || rate <= 0) return;
    // two channels (the same signal), so both speakers play it
    recordingBuffer.setSize (2, (int) mono->size());
    for (int c = 0; c < 2; ++c) recordingBuffer.copyFrom (c, 0, mono->data(), (int) mono->size());
    source = std::make_unique<juce::MemoryAudioSource> (recordingBuffer, false, false);
    transport.setSource (source.get(), 0, nullptr, rate, 2);
    loaded = true;
}

void TranscriptionPlayer::setEvents (std::vector<Event> e)
{
    // note-offs before note-ons at the same moment, so repeated notes restart cleanly
    std::stable_sort (e.begin(), e.end(), [] (const Event& a, const Event& b) { return a.time < b.time || (a.time == b.time && ! a.on && b.on); });
    const juce::SpinLock::ScopedLockType sl (eventLock);
    events.swap (e);
    eventsChanged = true;
}                                                   // the old list is freed here, on the message thread

void TranscriptionPlayer::play()
{
    if (! loaded) return;
    if (transport.getCurrentPosition() >= transport.getLengthInSeconds() - 0.05) transport.setPosition (0);
    transport.start();
}

void TranscriptionPlayer::pause()          { transport.stop(); }
void TranscriptionPlayer::seek (double s)  { transport.setPosition (juce::jlimit (0.0, juce::jmax (0.0, length()), s)); }

//==============================================================================
void TranscriptionPlayer::prepareToPlay (int block, double rate)
{
    sr = rate > 0 ? rate : 48000;
    transport.prepareToPlay (block, sr);
    scratch.setSize (2, juce::jmax (block, 4096));
    for (auto& v : voices) v.active = false;
    expected = -1;
}

void TranscriptionPlayer::releaseResources() { transport.releaseResources(); }

void TranscriptionPlayer::startVoice (const Event& e)
{
    Voice* v = nullptr;
    for (auto& c : voices) if (! c.active) { v = &c; break; }
    if (v == nullptr)                                // steal: the oldest releasing voice, else the oldest
    {
        v = &voices[0];
        for (auto& c : voices)
            if ((c.releasing && ! v->releasing) || (c.releasing == v->releasing && c.age > v->age)) v = &c;
    }
    *v = {};
    v->active = true;
    v->pitch = e.pitch;
    v->layer = e.layer;
    v->level = kTimbre[e.layer].level * e.velocity;
    v->inc = kTwoPi * 440.0 * std::pow (2.0, (e.pitch - 69) / 12.0) / sr;
    v->noise = 0x9e3779b9u ^ (juce::uint32) (e.pitch * 7919 + (int) (e.time * 1000));
}

void TranscriptionPlayer::stopVoice (const Event& e)
{
    if (e.layer == Drums) return;                    // one-shots
    for (auto& v : voices)
        if (v.active && ! v.releasing && v.pitch == e.pitch && v.layer == e.layer) { v.releasing = true; v.rel = v.env; break; }
}

void TranscriptionPlayer::renderVoices (float* l, float* r, int from, int to)
{
    for (auto& v : voices)
    {
        if (! v.active) continue;
        const auto& t = kTimbre[v.layer];
        const double attackS = t.attack * sr, releaseS = t.release * sr;
        const float decay = t.decay > 0 ? (float) std::exp (-1.0 / (t.decay * sr)) : 1.0f;
        const float relCoef = (float) std::exp (-6.9 / juce::jmax (1.0, releaseS));
        for (int i = from; i < to && v.active; ++i)
        {
            float s;
            if (v.layer == Drums)
            {
                const double age = v.age / sr;
                if (isKick (v.pitch))
                {
                    const double f = 45.0 + 65.0 * std::exp (-age / 0.03);
                    v.phase += kTwoPi * f / sr;
                    s = (float) (std::sin (v.phase) * std::exp (-age / 0.22));
                }
                else if (isTom (v.pitch))
                {
                    const double f = 70.0 + (v.pitch - 41) * 15.0;
                    v.phase += kTwoPi * f / sr;
                    s = (float) (std::sin (v.phase) * std::exp (-age / 0.2)) * 0.8f;
                }
                else if (isSnare (v.pitch))
                {
                    v.phase += kTwoPi * 185.0 / sr;
                    s = (float) ((0.7 * noise (v.noise) + 0.4 * std::sin (v.phase)) * std::exp (-age / 0.09)) * 0.8f;
                }
                else                                     // hats, cymbals, the rest: bright noise
                {
                    const float n = noise (v.noise), d = n - v.env;
                    v.env = n;
                    const double tau = (v.pitch == 49 || v.pitch == 57 || v.pitch == 52 || v.pitch == 55) ? 0.45 : 0.045;
                    s = (float) (d * std::exp (-age / tau)) * 0.35f;
                }
                if (v.age > sr * 0.8) v.active = false;
                s *= v.level;
            }
            else
            {
                float env;
                if (v.releasing)
                {
                    v.rel *= relCoef;                    // to -60 dB over the release time
                    env = v.rel;
                    if (env <= 0.0005f) { v.active = false; env = 0; }
                }
                else
                {
                    v.env = v.age < attackS ? (float) (v.age / attackS) : (v.age < attackS + 1 ? 1.0f : v.env * decay);
                    if (t.decay > 0 && v.age > attackS && v.env < 0.001f) v.active = false;    // decayed away (not at the start of the attack)
                    env = v.env;
                }
                v.phase += v.inc;
                if (v.phase > kTwoPi * 64) v.phase -= kTwoPi * 64;
                const double p = v.phase;
                s = (float) (t.h[0] * std::sin (p) + t.h[1] * std::sin (2 * p) + t.h[2] * std::sin (3 * p) + t.h[3] * std::sin (4 * p)) * env * v.level;
            }
            l[i] += s; r[i] += s;
            v.age += 1;
        }
    }
}

void TranscriptionPlayer::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    auto& out = *info.buffer;
    const int n = info.numSamples, start = info.startSample;
    const bool playing = transport.isPlaying();
    const double t0 = transport.getCurrentPosition();

    transport.getNextAudioBlock (info);
    const int m = mode.load();
    const float vol = volume.load();
    const float originalGain = (m == Original ? 1.0f : m == Mix ? 0.6f : 0.0f) * vol;
    out.applyGain (start, n, originalGain);

    if (! playing)
    {
        for (auto& v : voices) v.active = false;
        expected = -1;
        return;
    }

    scratch.clear (0, n);
    float* l = scratch.getWritePointer (0);
    float* r = scratch.getWritePointer (1);
    {
        const juce::SpinLock::ScopedTryLockType sl (eventLock);
        if (! sl.isLocked()) { expected = -1; }        // the list is being swapped: resynchronise next block
        else
        {
            if (eventsChanged || expected < 0 || std::abs (t0 - expected) > 0.03)
            {
                // a seek or a new event list: silence the voices and find the first event from here on
                for (auto& v : voices) if (v.active && v.layer != Drums) { v.releasing = true; v.rel = v.env; }
                cursor = (size_t) (std::lower_bound (events.begin(), events.end(), t0, [] (const Event& e, double t) { return e.time < t; }) - events.begin());
                eventsChanged = false;
            }
            const double t1 = t0 + n / sr;
            int done = 0;
            while (cursor < events.size() && events[cursor].time < t1)
            {
                const int at = juce::jlimit (0, n, (int) ((events[cursor].time - t0) * sr));
                renderVoices (l, r, done, at);
                done = at;
                if (events[cursor].on) startVoice (events[cursor]); else stopVoice (events[cursor]);
                ++cursor;
            }
            renderVoices (l, r, done, n);
            expected = t1;
        }
    }

    const float synthGain = (m == Original ? 0.0f : 1.0f) * vol;
    if (synthGain > 0 && out.getNumChannels() > 0)
    {
        out.addFrom (0, start, scratch, 0, 0, n, synthGain);
        if (out.getNumChannels() > 1) out.addFrom (1, start, scratch, 1, 0, n, synthGain);
    }
}

//==============================================================================
PlaybackOutput::PlaybackOutput (juce::AudioSource& s) : source (s)
{
    player.setSource (&source);
}

PlaybackOutput::~PlaybackOutput()
{
    close();
    player.setSource (nullptr);
}

juce::StringArray PlaybackOutput::outputNames()
{
    auto& types = manager.getAvailableDeviceTypes();
    for (auto* t : types)
        if (t->getTypeName() == "Windows Audio" || types.size() == 1)
        {
            t->scanForDevices();
            return t->getDeviceNames (false);
        }
    return {};
}

juce::String PlaybackOutput::current() const
{
    auto* d = manager.getCurrentAudioDevice();
    return d != nullptr ? d->getName() : juce::String();
}

bool PlaybackOutput::open (const juce::String& name, juce::String& error)
{
    close();
    for (auto* t : manager.getAvailableDeviceTypes())
        if (t->getTypeName() == "Windows Audio") { manager.setCurrentAudioDeviceType (t->getTypeName(), false); break; }
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    manager.initialiseWithDefaultDevices (0, 2);
    setup = manager.getAudioDeviceSetup();
    if (name.isNotEmpty()) setup.outputDeviceName = name;
    setup.inputDeviceName = {};
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    setup.useDefaultOutputChannels = true;
    error = manager.setAudioDeviceSetup (setup, true);
    if (error.isEmpty() && manager.getCurrentAudioDevice() == nullptr) error = "No audio output found";
    if (error.isNotEmpty()) return false;
    manager.addAudioCallback (&player);
    return true;
}

void PlaybackOutput::close()
{
    manager.removeAudioCallback (&player);
    manager.closeAudioDevice();
}

bool PlaybackOutput::isOpen() const { return manager.getCurrentAudioDevice() != nullptr; }
