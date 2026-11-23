#include "LiveBalance.h"

LiveBalance::LiveBalance() : juce::Thread ("Tonal balance")
{
    ring.assign ((size_t) kRing, 0.0f);
    startThread (juce::Thread::Priority::low);             // idles while the Balance view is hidden
}

LiveBalance::~LiveBalance() { stopThread (4000); }

void LiveBalance::setActive (bool a)
{
    if (a == active.load()) return;
    active = a;
}

void LiveBalance::setWindow (int index)
{
    windowIndex = juce::jlimit (0, 5, index);
    republish = true;                                      // a new result right away
}

void LiveBalance::push (const float* L, const float* R, int n, double sampleRate) noexcept
{
    if (! active.load() || n <= 0) return;
    pushRate = sampleRate;
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) ring[(size_t) (s1 + i)] = 0.5f * (L[i] + R[i]);
    for (int i = 0; i < n2; ++i) ring[(size_t) (s2 + i)] = 0.5f * (L[n1 + i] + R[n1 + i]);
    fifo.finishedWrite (n1 + n2);
}

LiveBalance::Snapshot LiveBalance::getSnapshot() const
{
    const juce::SpinLock::ScopedLockType sl (lock);
    return snap;
}

void LiveBalance::publish()
{
    Snapshot s;
    const int w = windowIndex.load();
    s.window = w;
    if (w == 5)
    {
        if (sinceReset != nullptr && sinceReset->hasResult())
        {
            s.raw = sinceReset->normalizedDb();
            s.seconds = sinceReset->analysedSeconds();
        }
    }
    else
    {
        // the window's frames, gated like a whole song: drop frames more than 20 dB under the average
        static constexpr double kSeconds[5] = { 0.3, 1.0, 3.0, 10.0, 30.0 };
        const double seconds = kSeconds[juce::jlimit (0, 4, w)];
        const size_t want = (size_t) std::ceil (seconds / frameSeconds);
        const size_t first = recent.size() > want ? recent.size() - want : 0;
        double e = 0;
        size_t count = 0;
        for (size_t i = first; i < recent.size(); ++i) if (! recent[i].power.empty()) { e += recent[i].ms; ++count; }
        if (count > 0)
        {
            const double threshold = e / (double) count * std::pow (10.0, tonal::kGateRelDb / 10.0);
            std::vector<double> power ((size_t) tonal::kBins, 0.0);
            int used = 0;
            for (size_t i = first; i < recent.size(); ++i)
            {
                if (recent[i].power.empty() || recent[i].ms < threshold) continue;
                const float* p = recent[i].power.data();
                for (int k = 0; k < tonal::kBins; ++k) power[(size_t) k] += p[k];
                ++used;
            }
            s.raw = tonal::normalizeSummedPower (power, used);
            s.seconds = used * frameSeconds;
        }
    }
    if (! s.raw.empty())
    {
        s.valid = true;
        s.smooth = tonal::smoothDbBell (s.raw, tonal::smoothingOctaves (smoothing.load()));
        s.shares = tonal::bandShares (s.raw);
    }
    else if (isFast (w) && snap.valid && snap.window == w)
        return;                                    // a short gap in fast modes: keep showing the last sound
    const juce::SpinLock::ScopedLockType sl (lock);
    s.serial = snap.serial + 1;
    snap = std::move (s);
}

void LiveBalance::run()
{
    std::vector<float> chunk (2048);                       // small chunks, so fast modes publish every frame
    while (! threadShouldExit())
    {
        if (! active.load()) { wait (100); continue; }
        const double rate = pushRate.load();
        if (rate <= 0) { wait (50); continue; }

        if (resetRequest.exchange (false) || frames == nullptr || std::abs (frames->getSampleRate() - rate) > 0.5)
        {
            frames = std::make_unique<tonal::FrameAnalyzer> (rate, 4);          // a frame every 23 ms
            sinceReset = std::make_unique<tonal::SpectrumAccumulator> (rate);
            frameSeconds = frames->secondsPerFrame();
            recent.clear();
            framesSincePublish = 0;
            publish();
        }

        const int ready = fifo.getNumReady();
        if (ready < 256)
        {
            if (republish.exchange (false)) publish();
            wait (20);
            continue;
        }
        int s1, n1, s2, n2;
        const int n = juce::jmin (ready, (int) chunk.size());
        fifo.prepareToRead (n, s1, n1, s2, n2);
        std::copy_n (ring.data() + s1, n1, chunk.data());
        std::copy_n (ring.data() + s2, n2, chunk.data() + n1);
        fifo.finishedRead (n1 + n2);

        const size_t maxFrames = (size_t) std::ceil (30.0 / frameSeconds) + 2;
        frames->process (chunk.data(), n1 + n2, [&] (const double* p, double ms)
        {
            sinceReset->addFrame (p, ms);
            Frame f;
            if (recent.size() >= maxFrames) { f = std::move (recent.front()); recent.pop_front(); }
            if (p != nullptr)
            {
                f.power.resize ((size_t) tonal::kBins);
                for (int k = 0; k < tonal::kBins; ++k) f.power[(size_t) k] = (float) p[k];
            }
            else f.power.clear();                       // silence: keeps the window's timing
            f.ms = ms;
            recent.push_back (std::move (f));
            ++framesSincePublish;
        });
        // a new result with every frame in the fast modes, about 10 times a second otherwise
        const double every = isFast (windowIndex.load()) ? 0.0 : 0.1;
        if (republish.exchange (false) || (framesSincePublish > 0 && framesSincePublish * frameSeconds >= every)) { framesSincePublish = 0; publish(); }
    }
}
