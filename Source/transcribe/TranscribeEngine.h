// Audio-to-notes transcription: a thin, C++20-clean front for muscriptor.cpp, the engine of
// NeuralNote v2 (MuScriptor model, Kyutai / Mirelo; https://github.com/DamRsn/muscriptor.cpp).
//
// muscriptor.cpp's own headers need C++23 (std::expected), so they are included only by
// TranscribeEngine.cpp, which is built as its own C++23 static library (dcu_transcribe). Everything
// else in the app sees just this header: no JUCE, no muscriptor types, so it can be shared as is by
// the standalone app, a future VST3 and command-line tools.
//
// Threading: run() blocks for seconds to minutes; call it from a background thread, never the
// message or audio thread. One run at a time per call site (each run loads its own model and frees it
// before returning, so concurrent runs from different threads each hold their own copy).
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace transcribe
{
    constexpr int kSampleRate = 16000;            // the model's input: 16 kHz mono float
    constexpr int kDrumProgram = 128;

    struct Note
    {
        double onset = 0, offset = 0;             // seconds from the start of the audio
        int pitch = 0;                            // MIDI note; for drums the GM percussion note
        int program = 0;                          // MIDI program the model decoded, kDrumProgram for drums
        bool drum = false;
    };

    struct Device
    {
        std::string name, backend;                // e.g. "NVIDIA GeForce RTX 4070", "Vulkan"; the CPU is last
        bool integrated = false;
        std::uint64_t memory = 0;                 // bytes, 0 when unknown
    };

    // The devices the engine can run on: GPUs first, the CPU always last. The first call initialises
    // the GPU backends and can take a few seconds; call it off the message thread. Fixed for the life
    // of the process.
    std::vector<Device> devices();
    int autoDevice();                             // index the "Auto" choice resolves to
    std::string cpuVariant();                     // empty when the CPU is too old for the engine
    bool gpuBuild();                              // built with a GPU backend (Vulkan / Metal)

    // Instrument groups the model knows (for restricting what it listens for)
    struct Instrument { int id = 0; std::string name; };
    std::vector<Instrument> instruments();
    std::string instrumentName (int program);     // "Acoustic piano", "Drums", ...
    // GM 32-39 basses, 43 contrabass, 58 tuba, 70 bassoon
    inline bool isBassProgram (int program) { return (program >= 32 && program <= 39) || program == 43 || program == 58 || program == 70; }

    struct Request
    {
        std::string modelPath;                    // a muscriptor GGUF checkpoint
        int device = -1;                          // index into devices(), -1 = Auto
        std::vector<int> instruments;             // Instrument ids; empty = let the model decide
        int threads = 0;                          // 0 = all cores
        std::function<bool()> cancel;             // polled often; return true to stop
        std::function<void (float)> loadProgress; // 0..1 while the weights load
        // Called after every 5 s chunk with the notes that became final (never repeated) and the
        // overall progress 0..1. Runs on the calling thread.
        std::function<void (const std::vector<Note>& newNotes, double finalizedThrough, float progress)> chunk;
    };

    struct Outcome
    {
        enum Status { Ok, Cancelled, Failed } status = Failed;
        std::string error;                        // for Failed: short enough to show as is
        std::vector<Note> notes;                  // sorted by onset; the complete result for Ok
        std::string deviceName;                   // what it actually ran on
        double loadSeconds = 0, runSeconds = 0;
    };

    // mono16k: the whole signal at kSampleRate. Blocking.
    Outcome run (const float* mono16k, std::size_t numSamples, const Request& request);

    // Reads a checkpoint's header only: true when it looks like a model this build can load.
    bool looksLikeModel (const std::string& path, std::string* whyNot = nullptr);
}
