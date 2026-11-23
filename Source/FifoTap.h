#pragma once
#include <JuceHeader.h>
#include "AudioTap.h"
#include <atomic>
#include <vector>

// A page's live input: a lock-free stereo ring filled by the audio / DSP thread and drained on the
// message thread (the Scope and the Monitor pages). Per instance, so a plugin gets one per editor.
class FifoTap : public AudioTap
{
public:
    FifoTap() : l ((size_t) kSize), r ((size_t) kSize) {}
    void setActive (bool a) { if (a && ! active.load()) fifo.reset(); active = a; }
    double sampleRate() const { return rate.load(); }

    void push (const float* left, const float* right, int n, double sr) noexcept override
    {
        if (! active.load()) return;
        rate = sr;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        std::copy (left, left + n1, l.begin() + s1);   std::copy (right, right + n1, r.begin() + s1);
        std::copy (left + n1, left + n1 + n2, l.begin() + s2); std::copy (right + n1, right + n1 + n2, r.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }

    template <typename Fn> void drain (Fn&& fn)      // fn (const float* l, const float* r, int n)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        if (n1 > 0) fn (l.data() + s1, r.data() + s1, n1);
        if (n2 > 0) fn (l.data() + s2, r.data() + s2, n2);
        fifo.finishedRead (n1 + n2);
    }

private:
    static constexpr int kSize = 1 << 19;            // ~10 s at 48 kHz between drains
    std::vector<float> l, r;
    juce::AbstractFifo fifo { kSize };
    std::atomic<bool> active { false };
    std::atomic<double> rate { 48000 };
};
