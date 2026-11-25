// The Key page: every key-related tool on one page, sharing one audio source.
//
//   header   Record (live input) | Browse... (or drop a file anywhere) | the source | model, device,
//            instruments | Transcribe
//   left     KEY: the key finder (OpenKeyScan's KeyNet): the key of the file, or of the live input
//            now and over the session, with Camelot / Open Key and keys that mix well, over the key chart
//   right    CHORDS & MELODY (NeuralNote v2's MuScriptor engine): chord lane with Roman numerals, the
//            waveform, a piano roll with the melody highlighted; copy the chords, save or drag the MIDI
//   player   Play the original, the transcription (built-in synth) or both; click the timeline to seek.
//            Sensitivity drops weak / excess notes (instantly) and sets how quiet a passage the next
//            transcription listens to
//
// Nothing here talks to the audio device directly: the live input arrives through AudioTap objects
// (KeyFinder, LiveRecorder) and files through TranscriptionSession, so the page can sit in a plugin
// editor unchanged.
#pragma once
#include <JuceHeader.h>
#include "KeyFinder.h"
#include "../Settings.h"
#include "../transcribe/TranscriptionSession.h"
#include "../transcribe/LiveRecorder.h"
#include "../transcribe/ModelStore.h"
#include "../transcribe/TranscriptionPlayer.h"
#include "../transcribe/NoteFilter.h"
#include <thread>

class KeyPage : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    // output: the standalone app's playback device (nullptr: the player is mixed in elsewhere, e.g. a plugin)
    KeyPage (KeyFinder&, TranscriptionSession&, LiveRecorder&, ModelStore&, TranscriptionPlayer&, PlaybackOutput* output,
             Settings&, std::function<void()> settingsChanged);
    ~KeyPage() override;

    void setActive (bool shown);                  // the page is showing (analysis keeps running when hidden)
    void setInputRunning (bool r)                 { inputRunning = r; }
    void loadSource (const juce::File& f);        // Browse / drop / --test-screenshot
    void toggleRecording();
    void clearAll();                              // back to an empty page: no audio, notes or key
    void startTranscription();
    void togglePlay();
    // renders the player offline (no output device) to a WAV file: for the self-test
    void renderPreview (const juce::File& wav, double seconds, int listen);
    juce::File writeMidi (const juce::File& dest) const;   // the transcription as a MIDI file

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

    struct Sidebar;   struct Roll;   struct ModelCard;   struct DragMidi;

private:
    void timerCallback() override;
    void refreshData (bool force);
    void updateHeader();
    void showInstrumentMenu();
    void browse();
    harmony::Key chordKey (juce::String* from = nullptr) const;
    harmony::Options harmonyOptions() const;
    int selectedDevice() const;
    void saveMidi();
    void copyChords();
    void rebuildEvents();
    void filterNotes();
    void seekTo (double seconds);

    KeyFinder& finder;
    TranscriptionSession& session;
    LiveRecorder& recorder;
    ModelStore& models;
    TranscriptionPlayer& player;
    PlaybackOutput* output;
    juce::AudioFormatManager formats;
    juce::String playError;
    Settings& s;
    std::function<void()> changed;

    enum class Source { None, File, Recording, Recorded } source = Source::None;
    bool active = false, inputRunning = false, dragOver = false, autoPending = false;
    int lastVersion = -1;
    std::array<bool, ModelStore::kSizes> wasBusy {};   // per model: a download that just finished
    juce::File lastFile;

    std::vector<transcribe::Note> rawNotes, notes;  // as transcribed, and after the sensitivity filter
    std::vector<float> strengths;
    std::vector<harmony::MelodyNote> melodyNotes;
    std::vector<harmony::Chord> chordList;
    harmony::Key key;
    juce::String keySource;
    std::vector<float> peaks;

    std::vector<transcribe::Device> devices;      // filled once on a background thread (GPU start-up is slow)
    std::atomic<bool> devicesReady { false };
    std::thread deviceThread;

    juce::TextButton recordBtn, browseBtn { "Browse..." }, clearBtn { "Clear" }, instrumentsBtn, transcribeBtn { "Transcribe" };
    juce::ComboBox modelBox, deviceBox;
    juce::ToggleButton autoToggle { "Auto" }, seventhsToggle { "7th chords" }, notesToggle { "All notes" };
    juce::TextButton copyBtn { "Copy chords" }, saveBtn { "Save MIDI..." };
    juce::TextButton playBtn;
    juce::ComboBox listenBox, synthBox, outputBox;
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox },
                 sensSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    std::unique_ptr<Sidebar> sidebar;
    std::unique_ptr<Roll> roll;
    std::unique_ptr<ModelCard> card;
    std::unique_ptr<DragMidi> dragMidi;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> header, playerRow, timeArea, sensLabel, footer, statusArea;
    juce::Array<int> dividersTop, dividersPlayer;        // x of the thin lines between control groups
};
