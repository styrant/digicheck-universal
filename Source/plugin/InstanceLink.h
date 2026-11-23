// Plugin instances that see each other: every DigiCheck Universal instance in the DAW's process publishes
// its track's recent audio here, under its track name (the host tells VST3 plugins their track's name).
// Another instance can then use it as its comparison signal, exactly like a routed sidechain (Spectrogram
// overlay, Scope SC, the FFT's second curve) without any routing.
//
// The audio is the most recent block the other track has processed: when the DAW processes the other
// track after this one, it's one block behind (a few ms). Instances in other processes (a DAW that runs
// plugins sandboxed) can't be seen.
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <memory>

namespace instancelink
{
    // a lock-free single-writer ring of stereo audio
    struct Ring
    {
        static constexpr int kFrames = 1 << 16;
        std::vector<float> l = std::vector<float> ((size_t) kFrames), r = std::vector<float> ((size_t) kFrames);
        std::atomic<long long> written { 0 };       // frames written in total
        std::atomic<double> lastMs { 0 };           // when it was last written (silence detection by readers)

        void write (const float* L, const float* R, int n) noexcept;
        // the most recent n frames (zeros if fewer have been written); returns false when nothing recent
        bool readLatest (float* L, float* R, int n) const noexcept;
    };

    struct Instance
    {
        int id = 0;
        juce::String name;                           // the track's name (or "Instance n")
        std::shared_ptr<Ring> ring;
    };

    class Hub
    {
    public:
        static Hub& get();
        int add (std::shared_ptr<Ring>);             // returns the new instance's id
        void remove (int id);
        void rename (int id, const juce::String&);
        std::vector<Instance> list() const;
        std::shared_ptr<Ring> find (int id) const;
        juce::String nameOf (int id) const;
    private:
        mutable juce::CriticalSection lock;
        std::vector<Instance> items;
        int next = 1;
    };
}
