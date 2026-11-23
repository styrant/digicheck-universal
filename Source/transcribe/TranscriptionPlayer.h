// Plays the Key page's audio and what was transcribed from it, so you can hear how well they match.
//
//   Listen: Original (the song or recording), Mix (both), or Transcription (the synth alone)
//   Synth:  which parts the synth plays: melody and chords, all notes, melody, chords
//
// The original streams from disk (or from memory for a recording) through an AudioTransportSource,
// which also converts the sample rate. The synth is a small built-in polyphonic one driven by an event
// list (note on / off times in seconds), so it follows seeks and stays sample-locked to the original.
//
// It's a juce::AudioSource: the standalone app plays it on an output device (PlaybackOutput); a plugin
// could mix it into its own output from processBlock. Control methods are for the message thread;
// getNextAudioBlock is real-time safe (no allocation; a try-lock around the event list).
#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

class TranscriptionPlayer : public juce::AudioSource
{
public:
    enum Layer { Notes = 0, Melody, Chords, Bass, Drums, kLayers };
    enum Listen { Original = 0, Mix, Transcription };

    struct Event
    {
        double time = 0;
        int pitch = 60;
        int layer = Notes;
        bool on = true;
        float velocity = 0.8f;
    };

    TranscriptionPlayer();
    ~TranscriptionPlayer() override;

    // what to play (each clears the position)
    void loadFile (const juce::File& f, juce::AudioFormatManager& formats);
    void loadRecording (std::shared_ptr<const std::vector<float>> mono, double sampleRate);
    void unload();
    bool hasAudio() const                       { return loaded.load(); }

    void setEvents (std::vector<Event> events); // sorted here; may be called while playing
    void setListen (int listen)                 { mode = juce::jlimit (0, 2, listen); }
    void setVolume (float v)                    { volume = juce::jlimit (0.0f, 1.0f, v); }

    void play();
    void pause();
    void toggle()                               { if (isPlaying()) pause(); else play(); }
    bool isPlaying() const                      { return transport.isPlaying(); }
    void seek (double seconds);
    double position() const                     { return transport.getCurrentPosition(); }
    double length() const                       { return transport.getLengthInSeconds(); }

    // AudioSource
    void prepareToPlay (int samplesPerBlock, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

private:
    struct Voice
    {
        bool active = false, releasing = false;
        int pitch = 0, layer = 0;
        double phase = 0, inc = 0, age = 0;     // age in samples
        float level = 0, env = 0, rel = 1;
        juce::uint32 noise = 22222;
    };
    void startVoice (const Event&);
    void stopVoice (const Event&);
    void renderVoices (float* l, float* r, int from, int to);

    juce::TimeSliceThread readAhead { "Key page playback" };
    juce::AudioTransportSource transport;
    std::unique_ptr<juce::PositionableAudioSource> source;
    juce::AudioBuffer<float> recordingBuffer;
    std::atomic<bool> loaded { false };

    juce::SpinLock eventLock;
    std::vector<Event> events;                  // guarded by eventLock
    bool eventsChanged = false;                 // guarded by eventLock
    size_t cursor = 0;
    double expected = -1;                       // where the next block should start (to notice seeks)

    std::array<Voice, 96> voices {};
    double sr = 48000;
    juce::AudioBuffer<float> scratch;
    std::atomic<int> mode { Mix };
    std::atomic<float> volume { 0.8f };
};

// The standalone app's playback device: the system's default output (or a chosen one), in shared mode.
class PlaybackOutput
{
public:
    explicit PlaybackOutput (juce::AudioSource& source);
    ~PlaybackOutput();

    juce::StringArray outputNames();            // the Windows Audio (shared) outputs
    juce::String current() const;
    bool open (const juce::String& name, juce::String& error);   // empty name = system default
    void close();
    bool isOpen() const;

private:
    juce::AudioDeviceManager manager;
    juce::AudioSourcePlayer player;
    juce::AudioSource& source;
};
