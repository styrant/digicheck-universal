#pragma once
#include <JuceHeader.h>
#include "AudioTap.h"
#include <atomic>
#include <tuple>
#include <vector>

// A page's live input: a lock-free ring filled by the audio / DSP thread and drained on the message
// thread (the Scope, Monitor and FFT pages). Per instance, so a plugin gets one per editor.
//
// With extras (the VST3 version), each sample also carries the sidechain's L / R and a sync phase
// (0..1 through the Scope's window, from the host's musical position; -1 while the transport is
// stopped), written together with the main signal so they stay sample-aligned.
class FifoTap : public AudioTap
{
public:
    explicit FifoTap (bool withExtras = false) : l ((size_t) kSize), r ((size_t) kSize)
    {
        if (withExtras) { scl.resize ((size_t) kSize); scr.resize ((size_t) kSize); ph.resize ((size_t) kSize); }
    }
    void setActive (bool a) { if (a && ! active.load()) fifo.reset(); active = a; }
    bool isActive() const   { return active.load(); }
    double sampleRate() const { return rate.load(); }
    bool hasExtras() const  { return ! scl.empty(); }
    bool sidechainPresent() const { return scOn.load(); }      // the last chunk carried a sidechain
    bool phasePresent() const     { return phOn.load(); }

    void push (const float* left, const float* right, int n, double sr) noexcept override
    {
        pushAll (left, right, nullptr, nullptr, nullptr, n, sr);
    }

    // main, sidechain (nullptr: none) and sync phase (nullptr: none) together
    void pushAll (const float* left, const float* right, const float* scLeft, const float* scRight, const float* phase, int n, double sr) noexcept
    {
        if (! active.load()) return;
        rate = sr;
        const bool sc = scLeft != nullptr && hasExtras(), p = phase != nullptr && hasExtras();
        scOn = sc; phOn = p;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        auto put = [&] (std::vector<float>& dst, const float* src, int start, int from, int count)
        {
            if (src != nullptr) std::copy (src + from, src + from + count, dst.begin() + start);
            else std::fill (dst.begin() + start, dst.begin() + start + count, 0.0f);
        };
        for (auto [start, from, count] : { std::tuple<int, int, int> { s1, 0, n1 }, { s2, n1, n2 } })
        {
            if (count <= 0) continue;
            put (l, left, start, from, count); put (r, right, start, from, count);
            if (hasExtras()) { put (scl, sc ? scLeft : nullptr, start, from, count); put (scr, sc ? scRight : nullptr, start, from, count);
                               if (p) put (ph, phase, start, from, count); else std::fill (ph.begin() + start, ph.begin() + start + count, -1.0f); }
        }
        fifo.finishedWrite (n1 + n2);
    }

    template <typename Fn> void drain (Fn&& fn)      // fn (const float* l, const float* r, int n)
    {
        drainAll ([&] (const float* a, const float* b, const float*, const float*, const float*, int n) { fn (a, b, n); });
    }

    // fn (l, r, scl, scr, phase, n): scl / scr nullptr without a sidechain, phase nullptr without sync
    template <typename Fn> void drainAll (Fn&& fn)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        const bool sc = scOn.load(), p = phOn.load();
        auto call = [&] (int s, int n)
        {
            if (n <= 0) return;
            fn (l.data() + s, r.data() + s, sc ? scl.data() + s : nullptr, sc ? scr.data() + s : nullptr, p ? ph.data() + s : nullptr, n);
        };
        call (s1, n1); call (s2, n2);
        fifo.finishedRead (n1 + n2);
    }

private:
    static constexpr int kSize = 1 << 19;            // ~10 s at 48 kHz between drains
    std::vector<float> l, r, scl, scr, ph;
    juce::AbstractFifo fifo { kSize };
    std::atomic<bool> active { false }, scOn { false }, phOn { false };
    std::atomic<double> rate { 48000 };
};
