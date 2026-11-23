// One piece of audio and its transcription: decoding / resampling the audio, running the engine on a
// background thread, and holding the notes (filled in chunk by chunk while it runs). No UI: the Key
// page draws from it, and a plugin would own one per instance the same way.
#pragma once
#include <JuceHeader.h>
#include "TranscribeEngine.h"
#include "Harmony.h"
#include <atomic>
#include <mutex>

class TranscriptionSession : private juce::Thread
{
public:
    TranscriptionSession();
    ~TranscriptionSession() override;

    enum class State { Empty, Preparing, Ready, Loading, Running, Done, Cancelled, Failed };

    struct Status
    {
        State state = State::Empty;
        float progress = 0;                    // 0..1 within Preparing / Loading / Running
        juce::String sourceName, message, deviceName;
        double seconds = 0;                    // audio length
        double speed = 0;                      // x real time, once known
        int noteCount = 0;
        int version = 0;                       // changes whenever the audio or the notes change
        bool busy() const { return state == State::Preparing || state == State::Loading || state == State::Running; }
    };

    // sources (each replaces the current audio and clears the notes)
    void loadFile (const juce::File& file);                                           // decoded in the background
    void setAudio (std::vector<float> mono, double sampleRate, const juce::String& name);
    bool canRead (const juce::File& f) const;
    juce::String wildcard() const;

    // gateDbfs: the engine's silence gate (see NoteFilter.h, Sensitivity::gateDbfs)
    void transcribe (const juce::File& model, int device, std::vector<int> instruments, float gateDbfs = -60.0f);
    void cancel();
    void clear();

    Status status() const;
    std::vector<transcribe::Note> notes() const;
    std::vector<float> peaks (int perSecond = 100) const;   // |max| envelope of the audio, for drawing
    std::vector<float> strengths (const std::vector<transcribe::Note>& notes) const;   // NoteFilter, on this audio
    juce::File sourceFile() const;                            // the file the audio came from, if any
    // a recording at its original rate (mono), for playback; null for files
    std::shared_ptr<const std::vector<float>> recording (double& sampleRate) const;

private:
    void run() override;
    void decode (const juce::File& f);
    void runEngine();
    void setState (State s, const juce::String& message = {});

    juce::AudioFormatManager formats;
    mutable std::mutex lock;
    Status st;
    std::vector<float> audio;                // 16 kHz mono
    std::vector<transcribe::Note> noteList;
    juce::File pendingFile, srcFile, modelFile;
    int deviceIndex = -1;
    std::vector<int> instrumentIds;
    float gate = -60.0f;
    enum class Task { None, Decode, Resample, Transcribe } task = Task::None;
    std::vector<float> pendingRaw;
    std::shared_ptr<const std::vector<float>> take;
    double takeRate = 0;
    double pendingRate = 0;
    std::atomic<bool> cancelRequest { false };
};

namespace midiexport
{
    // tracks: one per instrument (named, with its program; drums on channel 10), then "Melody" and
    // "Chords" (block chords with the bass an octave below). 120 BPM, so one beat = 0.5 s.
    juce::MidiFile build (const std::vector<transcribe::Note>& notes, const harmony::Key& key, const harmony::Options& options,
                          bool includeNotes = true, bool includeMelody = true, bool includeChords = true);
}
