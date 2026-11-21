// Live key finder: a streaming CQT front end (matching librosa.cqt as used to train OpenKeyScan)
// feeding KeyNet on a background thread.
//
//   DSP thread --push()--> mono sample ring --> key thread: CQT frame every 0.2 s
//                                               every 3 s: KeyNet over the last 20 s -> "now"
//                                               running mean of those results       -> "session"
#pragma once
#include <JuceHeader.h>
#include "KeyNet.h"
#include "KeyCqt.h"
#include <array>
#include <atomic>
#include <deque>

class KeyFinder : private juce::Thread
{
public:
    KeyFinder();
    ~KeyFinder() override;

    // analysis runs only while active (the Key window is open)
    void setActive (bool a);
    bool isActive() const noexcept { return active.load(); }
    void reset();                                   // clears the live results (works while stopped too)
    void clearFile();

    // DSP thread
    void push (const float* left, const float* right, int n, double sampleRate) noexcept;

    struct Result
    {
        bool modelOk = false;
        juce::String error;
        bool nowValid = false, sessionValid = false, quiet = false;
        std::array<float, 24> now {}, session {};
        double secondsHeard = 0;          // loud audio analysed since the last reset
        double windowSeconds = 0;         // length of the "now" window used for the last result
        int sessionWindows = 0;
    };
    Result getResult() const;

    // ---- audio files: the whole file, like the original OpenKeyScan analyzer
    struct FileResult
    {
        enum State { None, Working, Done, Failed } state = None;
        juce::String name, error;
        double progress = 0, seconds = 0;
        std::array<float, 24> probs {};
    };
    void analyzeFile (const juce::File& f);
    FileResult getFileResult() const;
    juce::String getFileWildcard() const;
    bool canReadFile (const juce::File& f) const;

    static constexpr int kWindowFrames = 100;      // 20 s, as in training
    static constexpr int kMinFrames = 25;          // first result after 5 s
    static constexpr int kStepFrames = 15;         // a new estimate every 3 s

private:
    void run() override;

    KeyNet net;
    KeyCqt cqt;

    static constexpr int kRing = 1 << 18;
    juce::AbstractFifo fifo { kRing };
    std::vector<float> ring;
    std::atomic<double> pushRate { 0 };
    std::atomic<bool> active { false }, resetRequest { true }, restartStream { false };

    // key thread only
    std::deque<std::vector<float>> frames;
    std::deque<float> frameDb;
    int framesSinceRun = 0;
    std::array<double, 24> sessionSum {};

    mutable juce::SpinLock resultLock;
    Result result;

    class FileJob : public juce::Thread
    {
    public:
        explicit FileJob (KeyFinder& o) : juce::Thread ("Key file analysis"), owner (o) {}
        ~FileJob() override { stopThread (8000); }
        juce::File file;
    private:
        void run() override;
        KeyFinder& owner;
    };
    juce::AudioFormatManager formats;
    std::unique_ptr<FileJob> fileJob;
    FileResult fileResult;
    void setFileResult (const FileResult& r) { const juce::SpinLock::ScopedLockType sl (resultLock); fileResult = r; }
};

