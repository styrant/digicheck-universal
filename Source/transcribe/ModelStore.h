// Transcription models: where they live, which are installed, and downloading them.
//
// The models are NeuralNote v2's MuScriptor checkpoints (GGUF, from huggingface.co/DamRsn/muscriptor-gguf),
// pinned to the same revision and SHA-256 digests as NeuralNote. They are never bundled with the app:
// they're downloaded on first use into our own models folder. Models that NeuralNote has already
// downloaded are found and used in place, so nothing is downloaded twice.
//
// The model weights are licensed CC BY-NC 4.0 (non-commercial use only) by Kyutai and Mirelo.
//
// The downloader is adapted from NeuralNote's ModelDownloader (Damien Ronssin, Apache-2.0, see
// licenses/NeuralNote-LICENSE.txt): a .part file resumed with HTTP Range requests, retries with back-off,
// then a SHA-256 check before the file is renamed into place.
//
// One ModelStore per process is enough (juce::SharedResourcePointer if several windows or plugin
// instances need it). start / cancel / status are for the message thread; status is cheap.
#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

class ModelStore
{
public:
    enum Size { Small = 0, Medium, Large, kSizes };

    struct Entry { const char* label; const char* fileName; juce::int64 bytes; const char* sha256; const char* hint; };
    static const Entry& entry (int size);

    static juce::File ownFolder();                 // %APPDATA%\Ultimate Audio Viewer\Models (where downloads go)
    static juce::File neuralNoteFolder();          // %APPDATA%\NeuralNote\models (used when present)
    static juce::File modelFile (int size);        // an installed checkpoint (ours or NeuralNote's), or {}
    static bool isInstalled (int size)             { return modelFile (size) != juce::File(); }
    static juce::File testModelOverride();         // SPECTROSCOPE_TEST_MODEL: a model file used for every size

    ModelStore();
    ~ModelStore();

    enum class Phase { Idle, Downloading, Verifying, Failed };
    struct Status
    {
        Phase phase = Phase::Idle;
        juce::int64 done = 0, total = 0;
        juce::String error;
        bool busy() const { return phase == Phase::Downloading || phase == Phase::Verifying; }
        float progress() const { return total > 0 ? (float) ((double) done / (double) total) : 0.0f; }
    };

    void download (int size);                      // resumes a partial download; nothing if busy or installed
    void cancel (int size);                        // keeps the partial file
    Status status (int size) const;

private:
    class Job;
    std::array<std::unique_ptr<Job>, kSizes> jobs;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelStore)
};
