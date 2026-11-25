// The DAW bridge: a VST3 (UAV Bridge, or the full UAV plugin) sends the audio it sees to the app through shared memory,
// so the app can monitor a DAW that uses ASIO (which Windows apps can't otherwise hear), with any
// interface and no routing: put the plugin on the master track and choose "From DAW" in the app.
//
//   shared memory  "UltimateAudioViewer.Bridge.1" (Windows: a named file mapping in the session's Local
//                  namespace; elsewhere POSIX shm), 16 slots, each a lock-free single-writer ring of
//                  stereo audio (65536 frames) with the sender's track name, sample rate and a heartbeat
//   senders        UAV Bridge (bridgeplugin/BridgePlugin.cpp) and UAV plugin instances (Send to the app, on by default); a slot is claimed on the first
//                  block and freed when the instance goes; a slot whose heartbeat stops for 3 s (a
//                  crashed DAW) can be claimed again
//   reader         the app's "From DAW" source (AudioEngine) reads one slot: a chosen track, or
//                  automatically the master (a track named like Master / Stereo Out / Main), else the
//                  sender that started most recently
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <cstdint>

namespace bridge
{
    constexpr int kSlots = 16, kFrames = 1 << 16, kNameLen = 64;
    constexpr uint32_t kMagic = 0x44435542u, kVersion = 1;   // "DCUB"

    struct Slot
    {
        std::atomic<uint32_t> owner;             // 0 free, else the sender's token
        std::atomic<uint32_t> nameSeq;           // seqlock for name (odd while writing)
        std::atomic<int64_t> heartbeatMs;        // wall clock ms of the last block
        std::atomic<int64_t> startedMs;          // when this sender claimed the slot
        std::atomic<int64_t> written;            // frames written in total
        std::atomic<int64_t> rateMilliHz;        // sample rate x 1000
        char name[kNameLen];
        float l[kFrames], r[kFrames];
    };

    struct Header
    {
        std::atomic<uint32_t> magic, version;
        Slot slots[kSlots];
    };

    // the mapping, opened (or created) in this process; nullptr when shared memory isn't available
    Header* shared();

    inline int64_t nowMs() { return (int64_t) juce::Time::currentTimeMillis(); }

    // a sender (one per plugin instance)
    class Sender
    {
    public:
        Sender();
        ~Sender();
        void setName (const juce::String&);                  // message thread
        void setEnabled (bool on)                            { enabled = on; if (! on) release(); }
        // audio thread: claims a slot if needed, then writes; real-time safe (atomics only)
        void write (const float* L, const float* R, int n, double sampleRate) noexcept;
        bool isSending() const noexcept                      { return slot.load() >= 0; }
    private:
        void release();
        void publishName (Slot&);
        Header* h = nullptr;
        std::atomic<int> slot { -1 };
        std::atomic<bool> enabled { true };
        uint32_t token = 0;
        juce::SpinLock nameLock;
        char pendingName[kNameLen] {};
        std::atomic<bool> nameDirty { true };
    };

    // a reader's view of the senders
    struct SenderInfo { int slot; juce::String name; double sampleRate; int64_t startedMs; bool alive; };
    std::vector<SenderInfo> listSenders();
    bool looksLikeMaster (const juce::String& trackName);
    // the slot to read for `want` ("" = automatic: the master, else the newest), or -1
    int chooseSlot (const juce::String& want);

// Adds a sender's audio to another stream (the app's "Also hear your DAW"): reads on the consumer's own
// thread (real-time safe), resamples linearly to its rate, and stays ~40 ms behind the writer, nudging the
// read speed by up to 0.2 % to follow clock drift; resyncs after a stall. Silence while the DAW is late.
struct Mixer
{
    double pos = -1;                                  // read position in the sender's frames (-1: resync)
    void process (int slot, double dstRate, const float* inL, const float* inR, float* outL, float* outR, int n, bool reset) noexcept;
};
}
