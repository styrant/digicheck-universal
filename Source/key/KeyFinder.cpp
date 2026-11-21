#include "KeyFinder.h"
#include "BinaryData.h"

//==============================================================================
KeyFinder::KeyFinder() : juce::Thread ("Key finder")
{
    ring.assign ((size_t) kRing, 0.0f);
    formats.registerBasicFormats();
    std::string err;
    const bool ok = net.load (BinaryData::keynet_bin, (size_t) BinaryData::keynet_binSize, err);
    const juce::SpinLock::ScopedLockType sl (resultLock);
    result.modelOk = ok;
    result.error = err;
    startThread (juce::Thread::Priority::low);       // idles while the Key window is closed
}

KeyFinder::~KeyFinder() { fileJob.reset(); stopThread (4000); }

//==============================================================================
juce::String KeyFinder::getFileWildcard() const { return formats.getWildcardForAllFormats(); }

bool KeyFinder::canReadFile (const juce::File& f) const
{
    return f.existsAsFile() && formats.findFormatForFileExtension (f.getFileExtension()) != nullptr;
}

void KeyFinder::reset()
{
    resetRequest = true;                          // the key thread also drops its frames and session sum
    const juce::SpinLock::ScopedLockType sl (resultLock);
    const bool ok = result.modelOk; const auto err = result.error;
    result = {}; result.modelOk = ok; result.error = err;
}

void KeyFinder::clearFile()
{
    fileJob.reset();
    setFileResult ({});
}

KeyFinder::FileResult KeyFinder::getFileResult() const
{
    const juce::SpinLock::ScopedLockType sl (resultLock);
    return fileResult;
}

void KeyFinder::analyzeFile (const juce::File& f)
{
    fileJob.reset();                                     // cancels a file still being analysed
    FileResult r;
    r.state = FileResult::Working;
    r.name = f.getFileName();
    setFileResult (r);
    fileJob = std::make_unique<FileJob> (*this);
    fileJob->file = f;
    fileJob->startThread (juce::Thread::Priority::low);
}

void KeyFinder::FileJob::run()
{
    FileResult r;
    r.state = FileResult::Working;
    r.name = file.getFileName();
    auto fail = [&] (const juce::String& why) { r.state = FileResult::Failed; r.error = why; owner.setFileResult (r); };

    std::unique_ptr<juce::AudioFormatReader> reader (owner.formats.createReaderFor (file));
    if (reader == nullptr) { fail ("This file type can't be read (try WAV, AIFF, FLAC, OGG or MP3)."); return; }
    if (! owner.net.isLoaded()) { fail ("The key model could not be loaded."); return; }
    const double sr = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    const int nch = (int) juce::jmax (1u, reader->numChannels);
    if (sr < 8000 || total < (juce::int64) (sr * 5)) { fail ("The file is shorter than 5 seconds."); return; }
    r.seconds = (double) total / sr;

    // 1. decode (mono = mean of all channels, as librosa) and compute the CQT frames
    KeyCqt cqt;
    cqt.configure (sr);
    std::vector<std::vector<float>> frames;
    frames.reserve ((size_t) (r.seconds / KeyCqt::kHopSeconds) + 8);
    auto keep = [&] (const float* f, float) { frames.emplace_back (f, f + KeyCqt::kBins); };
    constexpr int kBlock = 65536;
    juce::AudioBuffer<float> buf (nch, kBlock);
    std::vector<float> mono (kBlock);
    for (juce::int64 pos = 0; pos < total; pos += kBlock)
    {
        if (threadShouldExit()) return;
        const int n = (int) juce::jmin<juce::int64> (kBlock, total - pos);
        reader->read (&buf, 0, n, pos, true, true);
        for (int i = 0; i < n; ++i)
        {
            float v = 0;
            for (int c = 0; c < nch; ++c) v += buf.getSample (c, i);
            mono[(size_t) i] = v / (float) nch;
        }
        cqt.process (mono.data(), n, keep);
        r.progress = 0.4 * (double) (pos + n) / (double) total;
        owner.setFileResult (r);
    }
    std::vector<float> zeros ((size_t) cqt.flushSamples(), 0.0f);
    cqt.process (zeros.data(), (int) zeros.size(), keep);
    if (frames.size() > 2) frames.resize (frames.size() - 2);    // as the original drops the last 2 frames
    const int T = (int) frames.size();

    // 2. the network over the whole file (in chunks of up to 2 minutes)
    std::vector<float> spec ((size_t) KeyCqt::kBins * (size_t) T);
    for (int t = 0; t < T; ++t)
        for (int b = 0; b < KeyCqt::kBins; ++b) spec[(size_t) b * (size_t) T + (size_t) t] = frames[(size_t) t][(size_t) b];
    frames.clear();
    const auto z = owner.net.runLong (spec.data(), KeyCqt::kBins, T, 600, [&] (double p)
    {
        r.progress = 0.4 + 0.6 * p;
        owner.setFileResult (r);
        return ! threadShouldExit();
    });
    if (z.size() != 24) return;                           // cancelled
    const auto p = KeyNet::softmax (z);
    for (int i = 0; i < 24; ++i) r.probs[(size_t) i] = p[(size_t) i];
    r.progress = 1.0;
    r.state = FileResult::Done;
    owner.setFileResult (r);
}

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
