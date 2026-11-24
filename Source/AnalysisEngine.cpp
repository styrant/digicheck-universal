#include "AnalysisEngine.h"
#include <chrono>

AnalysisEngine::AnalysisEngine() : juce::Thread ("UAV DSP")
{
    colData.assign ((size_t) kSlots * kMaxRows, 0.0f);
    colGen.assign ((size_t) kSlots, -1);
    colRows.assign ((size_t) kSlots, 0);
    dsp.setSink ([this] (const float* c, int rows) { pushColumn (c, rows); });
    startThread (juce::Thread::Priority::high);
}

AnalysisEngine::~AnalysisEngine()
{
    analysing = false;
    stopThread (2000);
}

void AnalysisEngine::prepare (double sr)
{
    dropped = 0;   // the DSP thread flushes the ring itself when it reconfigures
    sampleRate = sr;
    if (analyzer != nullptr) analyzer->prepareToPlay (sr, 512);
    loudReset = true;
    {
        const juce::ScopedLock sl (cfgLock);
        pendingGen = ++generation;
    }
    cfgDirty = true;               // reconfigure the DSP for the new sample rate
    analysing = true;
}

void AnalysisEngine::release()
{
    analysing = false;
    sampleRate = 0;
}

void AnalysisEngine::setConfig (const Reassigner::Config& c)
{
    {
        const juce::ScopedLock sl (cfgLock);
        pendingCfg = c;
        pendingGen = ++generation;
    }
    pendingFloor = c.floorDb;
    cfgDirty = true;
}

void AnalysisEngine::push (const float* const* ch, int nch, int n) noexcept
{
    if (nch <= 0 || n <= 0) return;
    if (fifo.getFreeSpace() < n) { dropped += n; return; }        // DSP fell behind: shown as a white gap
    const float* L = ch[juce::jlimit (0, nch - 1, chL.load())];
    const float* R = ch[juce::jlimit (0, nch - 1, chR.load())];
    int s1, n1, s2, n2;
    fifo.prepareToWrite (n, s1, n1, s2, n2);
    if (n1 > 0) { ring.copyFrom (0, s1, L, n1); ring.copyFrom (1, s1, R, n1); }
    if (n2 > 0) { ring.copyFrom (0, s2, L + n1, n2); ring.copyFrom (1, s2, R + n1, n2); }
    fifo.finishedWrite (n1 + n2);
}

void AnalysisEngine::pushColumn (const float* col, int rows) noexcept
{
    int s1, n1, s2, n2;
    colFifo.prepareToWrite (1, s1, n1, s2, n2);
    const int idx = n1 > 0 ? s1 : (n2 > 0 ? s2 : -1);
    if (idx < 0) return;                                              // UI is not keeping up: drop
    const int r = juce::jmin (rows, kMaxRows);
    std::copy (col, col + r, colData.data() + (size_t) idx * kMaxRows);
    colGen[(size_t) idx] = dspGen;
    colRows[(size_t) idx] = r;
    colFifo.finishedWrite (1);
}

void AnalysisEngine::run()
{
    bool configured = false;
    double busyMs = 0, audioMs = 0, loudSr = 0;
    std::vector<float> L (2048), R (2048);

    while (! threadShouldExit())
    {
        if (cfgDirty.exchange (false))
        {
            Reassigner::Config c; int g;
            { const juce::ScopedLock sl (cfgLock); c = pendingCfg; g = pendingGen; }
            c.sampleRate = sampleRate.load();
            configured = c.sampleRate > 0 && c.rows > 0;
            if (configured)
            {
                c.rows = juce::jmin (c.rows, kMaxRows);
                dsp.configure (c);
                latency = dsp.getLatencySeconds();
            }
            dspGen = g;
            fifo.finishedRead (fifo.getNumReady());
            dropped = 0;
        }
        if (floorDirty.exchange (false) && configured) dsp.setFloorDb (pendingFloor.load());

        if (! configured || ! analysing.load()) { wait (10); continue; }

        // Loudness keeps running across display-setting changes; it restarts only on a new
        // source, sample rate or channel pair, or when the user presses Reset.
        if (loudReset.exchange (false) || std::abs (sampleRate.load() - loudSr) > 0.5)
        {
            loudSr = sampleRate.load();
            loud.configure (loudSr, chL.load() == chR.load() ? 1 : 2);
            lufsS = lufsI = truePeak = LoudnessMeter::kNone;
        }

        const bool spec = specActive.load();
        if (spec && specResume.exchange (false)) dsp.resetStream();
        if (const int d = dropped.exchange (0); d > 0 && spec) dsp.emitGap (d);

        const int ready = fifo.getNumReady();
        const double sr = sampleRate.load();
        if (ready > (int) (sr * 0.5))                                 // more than 0.5 s behind: skip ahead
        {
            fifo.finishedRead (ready);
            if (spec) dsp.emitGap (ready);
            continue;
        }
        if (ready < 256) { wait (3); continue; }

        const int n = juce::jmin (ready, (int) L.size());
        int s1, n1, s2, n2;
        fifo.prepareToRead (n, s1, n1, s2, n2);
        if (n1 > 0) { std::copy_n (ring.getReadPointer (0, s1), n1, L.data()); std::copy_n (ring.getReadPointer (1, s1), n1, R.data()); }
        if (n2 > 0) { std::copy_n (ring.getReadPointer (0, s2), n2, L.data() + n1); std::copy_n (ring.getReadPointer (1, s2), n2, R.data() + n1); }
        fifo.finishedRead (n1 + n2);

        const auto t0 = std::chrono::steady_clock::now();
        const int total = n1 + n2;
        if (spec) dsp.process (L.data(), R.data(), total);
        loud.process (L.data(), R.data(), total);
        for (auto& slot : taps) if (auto* t = slot.load()) t->push (L.data(), R.data(), total, sr);
        if (analyzer != nullptr)
        {
            // Feed the analyzer in ~10 ms slices so it publishes one update per 10 ms of audio, evenly,
            // however large this chunk is (a single big chunk used to give one update per chunk, so
            // the bars moved in uneven bursts).
            const int slice = juce::jmax (64, (int) (sr / 100.0));
            for (int off = 0; off < total; off += slice)
                analyzer->processStereo (L.data() + off, R.data() + off, juce::jmin (slice, total - off));
        }
        lufsS = loud.getShortTerm(); lufsI = loud.getIntegrated(); truePeak = loud.getTruePeak();
        busyMs += std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        audioMs += (n1 + n2) / sr * 1000.0;
        if (audioMs >= 500) { load = (float) (busyMs / audioMs); busyMs = audioMs = 0; }
    }
}

