#include "KeyFinder.h"
#include "BinaryData.h"

//==============================================================================
KeyFinder::KeyFinder() : juce::Thread ("Key finder")
{
    ring.assign ((size_t) kRing, 0.0f);
    std::string err;
    const bool ok = net.load (BinaryData::keynet_bin, (size_t) BinaryData::keynet_binSize, err);
    const juce::SpinLock::ScopedLockType sl (resultLock);
    result.modelOk = ok;
    result.error = err;
    startThread (juce::Thread::Priority::low);       // idles while the Key window is closed
}

KeyFinder::~KeyFinder() { stopThread (4000); }

void KeyFinder::setActive (bool a)
{
    if (a == active.load()) return;
    if (a) restartStream = true;                     // audio has a gap: start a fresh 20 s window
    active = a;
}

void KeyFinder::push (const float* L, const float* R, int n, double sampleRate) noexcept
{
    if (! active.load() || n <= 0) return;
    pushRate = sampleRate;
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) ring[(size_t) (s1 + i)] = 0.5f * (L[i] + R[i]);
    for (int i = 0; i < n2; ++i) ring[(size_t) (s2 + i)] = 0.5f * (L[n1 + i] + R[n1 + i]);
    fifo.finishedWrite (n1 + n2);          // if the key thread falls behind, the rest is dropped
}

KeyFinder::Result KeyFinder::getResult() const
{
    const juce::SpinLock::ScopedLockType sl (resultLock);
    return result;
}

void KeyFinder::run()
{
    std::vector<float> chunk (8192), spec;
    constexpr float kGateDb = -55.0f;             // frames quieter than this count as silence

    while (! threadShouldExit())
    {
        if (! active.load()) { wait (100); continue; }
        const double rate = pushRate.load();
        if (restartStream.exchange (false))
        {
            fifo.finishedRead (fifo.getNumReady());
            if (cqt.getSampleRate() > 0) cqt.reset();
            frames.clear(); frameDb.clear(); framesSinceRun = 0;
        }
        if (resetRequest.exchange (false) || (rate > 0 && std::abs (rate - cqt.getSampleRate()) > 0.5))
        {
            if (rate > 0) cqt.configure (rate);
            frames.clear(); frameDb.clear(); framesSinceRun = 0; sessionSum.fill (0);
            const juce::SpinLock::ScopedLockType sl (resultLock);
            const bool ok = result.modelOk; const auto err = result.error;
            result = {}; result.modelOk = ok; result.error = err;
        }

        const int ready = fifo.getNumReady();
        if (ready < 1024 || rate <= 0) { wait (40); continue; }
        int s1, n1, s2, n2;
        const int n = juce::jmin (ready, (int) chunk.size());
        fifo.prepareToRead (n, s1, n1, s2, n2);
        std::copy_n (ring.data() + s1, n1, chunk.data());
        std::copy_n (ring.data() + s2, n2, chunk.data() + n1);
        fifo.finishedRead (n1 + n2);

        cqt.process (chunk.data(), n1 + n2, [&] (const float* f, float db)
        {
            frames.emplace_back (f, f + KeyCqt::kBins);
            frameDb.push_back (db);
            while ((int) frames.size() > kWindowFrames) { frames.pop_front(); frameDb.pop_front(); }
            ++framesSinceRun;
            if (db > kGateDb) { const juce::SpinLock::ScopedLockType sl (resultLock); result.secondsHeard += KeyCqt::kHopSeconds; }
        });

        const int count = (int) frames.size();
        if (! net.isLoaded() || count < kMinFrames || framesSinceRun < kStepFrames) continue;
        framesSinceRun = 0;

        // only judge windows that are mostly music, not silence
        int loud = 0;
        for (float d : frameDb) loud += d > kGateDb ? 1 : 0;
        if (loud < count / 2)
        {
            const juce::SpinLock::ScopedLockType sl (resultLock);
            result.quiet = true;
            continue;
        }

        spec.assign ((size_t) KeyCqt::kBins * (size_t) count, 0.0f);      // [bins][frames]
        for (int t = 0; t < count; ++t)
            for (int b = 0; b < KeyCqt::kBins; ++b)
                spec[(size_t) b * (size_t) count + (size_t) t] = frames[(size_t) t][(size_t) b];
        const auto probs = KeyNet::softmax (net.run (spec.data(), KeyCqt::kBins, count));
        if (probs.size() != 24 || threadShouldExit()) continue;

        for (int i = 0; i < 24; ++i) sessionSum[(size_t) i] += probs[(size_t) i];
        const juce::SpinLock::ScopedLockType sl (resultLock);
        result.quiet = false;
        result.nowValid = true;
        result.windowSeconds = count * KeyCqt::kHopSeconds;
        ++result.sessionWindows;
        result.sessionValid = true;
        double s = 0;
        for (double v : sessionSum) s += v;
        for (int i = 0; i < 24; ++i)
        {
            result.now[(size_t) i] = probs[(size_t) i];
            result.session[(size_t) i] = (float) (sessionSum[(size_t) i] / juce::jmax (1e-9, s));
        }
    }
}
