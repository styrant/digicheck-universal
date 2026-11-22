// Records the live input (mono, (L+R)/2) for transcription. push() is real-time safe: it only writes
// into a lock-free ring; the message thread drains the ring into the growing recording.
#pragma once
#include <JuceHeader.h>
#include "../AudioTap.h"
#include <atomic>

class LiveRecorder : public AudioTap, private juce::Timer
{
public:
    static constexpr double kMaxSeconds = 15 * 60;

    LiveRecorder() : ring ((size_t) kRing) {}
    ~LiveRecorder() override { stopTimer(); }

    void start()
    {
        recording.clear();
        fifo.reset();
        rate = 0;
        armed = true;
        startTimerHz (20);
    }

    // returns the recording; rate() is its sample rate
    std::vector<float> stop()
    {
        armed = false;
        timerCallback();
        stopTimer();
        return std::move (recording);
    }

    bool isRecording() const noexcept   { return armed.load(); }
    double sampleRate() const noexcept  { return rate.load(); }
    double seconds() const noexcept     { const double r = rate.load(); return r > 0 ? (double) recorded.load() / r : 0; }
    bool full() const noexcept          { return seconds() >= kMaxSeconds; }
    float level() const noexcept        { return peak.exchange (0.0f); }      // peak since the last call

    void push (const float* l, const float* r, int n, double sr) noexcept override
    {
        if (! armed.load()) return;
        if (rate.load() != sr) { if (rate.load() == 0) rate = sr; else return; }   // a rate change mid-take: ignore the rest
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        float pk = 0;
        for (int i = 0; i < n1; ++i) { ring[(size_t) (s1 + i)] = 0.5f * (l[i] + r[i]); pk = std::max (pk, std::abs (ring[(size_t) (s1 + i)])); }
        for (int i = 0; i < n2; ++i) { ring[(size_t) (s2 + i)] = 0.5f * (l[n1 + i] + r[n1 + i]); pk = std::max (pk, std::abs (ring[(size_t) (s2 + i)])); }
        fifo.finishedWrite (n1 + n2);
        if (pk > peak.load()) peak = pk;
    }

private:
    void timerCallback() override
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        const double r = rate.load();
        const auto room = (size_t) juce::jmax (0.0, kMaxSeconds * r - (double) recording.size());
        recording.insert (recording.end(), ring.begin() + s1, ring.begin() + s1 + (long) juce::jmin ((size_t) n1, room));
        if ((size_t) n1 < room)
            recording.insert (recording.end(), ring.begin() + s2, ring.begin() + s2 + (long) juce::jmin ((size_t) n2, room - (size_t) n1));
        fifo.finishedRead (n1 + n2);
        recorded = (juce::int64) recording.size();
    }

    static constexpr int kRing = 1 << 20;            // about 20 s at 48 kHz between drains
    std::vector<float> ring;
    juce::AbstractFifo fifo { kRing };
    std::vector<float> recording;
    std::atomic<bool> armed { false };
    std::atomic<double> rate { 0 };
    std::atomic<juce::int64> recorded { 0 };
    mutable std::atomic<float> peak { 0 };
};
