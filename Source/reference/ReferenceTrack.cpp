#include "ReferenceTrack.h"
#include "../LoudnessMeter.h"
#include "../balance/TonalAnalysis.h"
#include "../fft/FftAnalyzer.h"

struct ReferenceTrack::Loader : public juce::Thread
{
    Loader (ReferenceTrack& o, const juce::File& f, std::function<void (bool, const juce::String&)> d)
        : juce::Thread ("Reference track"), owner (o), file (f), done (std::move (d)) {}
    ~Loader() override { stopThread (5000); }

    void run() override
    {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
        juce::String error;
        if (rd == nullptr) error = "Couldn't read " + file.getFileName();
        else if (rd->sampleRate <= 0 || rd->lengthInSamples <= 0) error = file.getFileName() + " has no audio";
        else if (rd->lengthInSamples > (juce::int64) (rd->sampleRate * 20 * 60)) error = "References up to 20 minutes, please";
        if (error.isNotEmpty()) { finish (false, error); return; }

        const int len = (int) rd->lengthInSamples;
        juce::AudioBuffer<float> buf (2, len);
        LoudnessMeter meter; meter.configure (rd->sampleRate, 2);
        tonal::SpectrumAccumulator acc (rd->sampleRate);
        std::vector<float> mono;
        constexpr int kChunk = 1 << 16;
        for (int pos = 0; pos < len && ! threadShouldExit(); pos += kChunk)
        {
            const int n = juce::jmin (kChunk, len - pos);
            rd->read (&buf, pos, n, pos, true, true);
            if (rd->numChannels == 1) buf.copyFrom (1, pos, buf, 0, pos, n);
            const float* l = buf.getReadPointer (0, pos); const float* r = buf.getReadPointer (1, pos);
            meter.process (l, r, n);
            mono.resize ((size_t) n);
            for (int i = 0; i < n; ++i) mono[(size_t) i] = 0.5f * (l[i] + r[i]);
            acc.process (mono.data(), n);
            owner.progress = (double) (pos + n) / len;
        }
        if (threadShouldExit()) return;

        Stats s;
        s.integrated = meter.getIntegrated(); s.lra = meter.getLoudnessRange(); s.truePeak = meter.getTruePeak();
        s.plr = meter.getPLR(); s.maxShortTerm = meter.getShortTermMax();
        auto tonalDb = acc.hasResult() ? acc.normalizedDb() : std::vector<double>();
        const double rate = rd->sampleRate;
        auto shared = std::make_shared<juce::AudioBuffer<float>> (std::move (buf));
        juce::MessageManager::callAsync ([o = &owner, alive = owner.alive, shared, s, tonalDb, rate, f = file, d = done]
        {
            if (! *alive) return;
            { const juce::SpinLock::ScopedLockType sl (o->audioLock); o->audio = std::move (*shared); }
            o->fileRate = rate; o->st = s; o->tonal = tonalDb;
            o->fileName = f.getFileName(); o->path = f;
            o->fftCache.clear();
            o->posSeconds = 0;
            o->isReady = true;
            o->loading = false;
            if (d) d (true, {});
        });
    }

    void finish (bool ok, const juce::String& error)
    {
        juce::MessageManager::callAsync ([o = &owner, alive = owner.alive, ok, error, d = done]
        {
            if (! *alive) return;
            o->loading = false;
            if (d) d (ok, error);
        });
    }

    ReferenceTrack& owner;
    juce::File file;
    std::function<void (bool, const juce::String&)> done;
};

ReferenceTrack::ReferenceTrack() : alive (std::make_shared<bool> (true)) {}
ReferenceTrack::~ReferenceTrack() { *alive = false; loader.reset(); }

void ReferenceTrack::load (const juce::File& f, std::function<void (bool, const juce::String&)> done)
{
    loader.reset();
    clear();
    loading = true; progress = 0;
    loader = std::make_unique<Loader> (*this, f, std::move (done));
    loader->startThread (juce::Thread::Priority::normal);
}

void ReferenceTrack::clear()
{
    // the audio thread only reads `audio` while ready: drop readiness first, then give it a block to notice
    listen = false; playing = false;
    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        isReady = false;
        audio.setSize (2, 0);
    }
    tonal.clear(); fftCache.clear(); st = {};
    fileName = {}; path = juce::File();
}

float ReferenceTrack::matchGainDb() const noexcept
{
    const float mix = mixLufs.load();
    if (! matchLoudness.load() || ! std::isfinite (mix) || ! std::isfinite (st.integrated)) return 0.0f;
    return juce::jlimit (-40.0f, 24.0f, mix - (float) st.integrated);
}

void ReferenceTrack::sampleAt (double t, float& l, float& r) const noexcept
{
    const int n = audio.getNumSamples();
    const double x = t * fileRate;
    const int i = (int) std::floor (x);
    if (i < 0 || i >= n) { l = r = 0; return; }
    const float f = (float) (x - i);
    for (int c = 0; c < 2; ++c)
    {
        const float* d = audio.getReadPointer (c);
        const float y0 = d[juce::jmax (0, i - 1)], y1 = d[i], y2 = d[juce::jmin (n - 1, i + 1)], y3 = d[juce::jmin (n - 1, i + 2)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        (c == 0 ? l : r) = ((c3 * f + c2) * f + c1) * f + y1;
    }
}

void ReferenceTrack::renderSynced (float* l, float* r, int n, double sampleRate, double t0, bool hostPlaying) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (audioLock);
    if (! sl.isLocked()) return;
    const bool want = listen.load() && ready();
    if (! want && fade <= 0.0f) return;
    const float g = juce::Decibels::decibelsToGain (matchGainDb());
    const float step = (float) (1.0 / (0.010 * sampleRate));
    const bool haveAudio = ready();
    for (int i = 0; i < n; ++i)
    {
        fade = juce::jlimit (0.0f, 1.0f, fade + (want ? step : -step));
        float a = 0, b = 0;
        if (haveAudio && hostPlaying) sampleAt (t0 + i / sampleRate, a, b);
        if (l == r) { l[i] = l[i] * (1.0f - fade) + 0.5f * (a + b) * g * fade; continue; }   // a mono track
        l[i] = l[i] * (1.0f - fade) + a * g * fade;
        r[i] = r[i] * (1.0f - fade) + b * g * fade;
    }
}

void ReferenceTrack::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    const juce::SpinLock::ScopedTryLockType sl (audioLock);
    if (! sl.isLocked() || ! ready() || ! playing.load() || info.buffer->getNumChannels() == 0) return;
    const float g = juce::Decibels::decibelsToGain (matchGainDb());
    float* l = info.buffer->getWritePointer (0, info.startSample);
    float* r = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;
    double t = posSeconds.load();
    const double len = lengthSeconds();
    for (int i = 0; i < info.numSamples; ++i)
    {
        float a, b;
        sampleAt (t, a, b);
        l[i] = a * g; if (r != nullptr) r[i] = b * g;
        t += 1.0 / outRate;
        if (t >= len) t = 0;                         // loops
    }
    posSeconds = t;
}

const std::vector<float>& ReferenceTrack::fftPower (int order, double binHz, int bins)
{
    static const std::vector<float> none;
    if (! ready()) return none;
    const auto key = std::make_pair (order, (long long) std::llround (binHz * 1000.0) * 100000 + bins);
    if (auto it = fftCache.find (key); it != fftCache.end()) return it->second;

    // the whole file's mean power spectrum at the file's rate (Hann, 75 % overlap, the analyzer's scaling)
    const int N = 1 << order, hop = N / 4, len = audio.getNumSamples();
    fft::Radix2 f; f.init (order);
    std::vector<float> win ((size_t) N); double sum = 0;
    for (int i = 0; i < N; ++i) { win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / N)); sum += win[(size_t) i]; }
    const double norm = 1.0 / ((sum / 2) * (sum / 2));
    std::vector<double> acc ((size_t) N / 2 + 1, 0.0);
    std::vector<std::complex<float>> buf ((size_t) N);
    int frames = 0;
    for (int start = 0; start + N <= len || (frames == 0 && start == 0); start += hop)
    {
        for (int c = 0; c < 2; ++c)
        {
            const float* d = audio.getReadPointer (c);
            for (int i = 0; i < N; ++i) buf[(size_t) i] = { start + i < len ? d[start + i] * win[(size_t) i] : 0.0f, 0.0f };
            f.forward (buf.data());
            for (int k = 0; k <= N / 2; ++k) acc[(size_t) k] += 0.5 * std::norm (buf[(size_t) k]) * norm;
        }
        ++frames;
        if (start + N > len) break;
    }
    // onto the analyzer's grid (linear interpolation in frequency)
    std::vector<float> out ((size_t) bins, 0.0f);
    const double refBinHz = fileRate / N;
    for (int k = 0; k < bins; ++k)
    {
        const double x = k * binHz / refBinHz;
        const int i = (int) x;
        if (i >= N / 2) { out[(size_t) k] = (float) (acc[(size_t) N / 2] / frames); continue; }
        const double fr = x - i;
        out[(size_t) k] = (float) ((acc[(size_t) i] * (1 - fr) + acc[(size_t) i + 1] * fr) / frames);
    }
    return fftCache[key] = std::move (out);
}
