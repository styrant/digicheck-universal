#include "ReferenceTrack.h"
#include "../LoudnessMeter.h"
#include "../balance/TonalAnalysis.h"
#include "../fft/FftAnalyzer.h"

//==============================================================================
// the files loaded in this process (weak: a file's audio goes when the last instance using it lets go)
namespace
{
    juce::CriticalSection sharedLock;
    std::map<juce::String, std::weak_ptr<const void>>& sharedMap() { static std::map<juce::String, std::weak_ptr<const void>> m; return m; }

    juce::String cacheKey (const juce::File& f)
    {
        return f.getFullPathName() + "|" + juce::String (f.getSize()) + "|" + juce::String (f.getLastModificationTime().toMilliseconds());
    }
    constexpr float kToFloat = 1.0f / 32767.0f;
}

std::shared_ptr<const ReferenceTrack::Data> ReferenceTrack::findShared (const juce::String& key)
{
    const juce::ScopedLock sl (sharedLock);
    if (auto it = sharedMap().find (key); it != sharedMap().end())
        if (auto p = it->second.lock()) return std::static_pointer_cast<const Data> (p);
    return {};
}

void ReferenceTrack::addShared (const std::shared_ptr<const Data>& d)
{
    const juce::ScopedLock sl (sharedLock);
    for (auto it = sharedMap().begin(); it != sharedMap().end();)
        it = it->second.expired() ? sharedMap().erase (it) : std::next (it);
    sharedMap()[d->key] = d;
}

//==============================================================================
struct ReferenceTrack::Loader : public juce::Thread
{
    Loader (ReferenceTrack& o, const juce::File& f, std::function<void (bool, const juce::String&)> d)
        : juce::Thread ("Reference track"), owner (o), file (f), done (std::move (d)) {}
    ~Loader() override { stopThread (5000); }

    void run() override
    {
        const auto key = cacheKey (file);
        if (auto existing = findShared (key)) { finish (existing, {}); return; }   // another instance has it

        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
        juce::String error;
        if (rd == nullptr) error = "Couldn't read " + file.getFileName();
        else if (rd->sampleRate <= 0 || rd->lengthInSamples <= 0) error = file.getFileName() + " has no audio";
        else if (rd->lengthInSamples > (juce::int64) (rd->sampleRate * 15 * 60)) error = "References up to 15 minutes, please";
        if (error.isNotEmpty()) { finish (nullptr, error); return; }

        auto d = std::make_shared<Data>();
        d->key = key;
        d->rate = rd->sampleRate;
        d->frames = (int) rd->lengthInSamples;
        d->pcm.resize ((size_t) d->frames * 2);
        LoudnessMeter meter; meter.configure (rd->sampleRate, 2);
        tonal::SpectrumAccumulator acc (rd->sampleRate);
        constexpr int kChunk = 1 << 16;
        juce::AudioBuffer<float> buf (2, kChunk);
        std::vector<float> mono ((size_t) kChunk);
        for (int pos = 0; pos < d->frames && ! threadShouldExit(); pos += kChunk)
        {
            const int n = juce::jmin (kChunk, d->frames - pos);
            rd->read (&buf, 0, n, pos, true, true);
            if (rd->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, n);
            const float* l = buf.getReadPointer (0); const float* r = buf.getReadPointer (1);
            meter.process (l, r, n);                 // measured at full precision, kept as 16-bit
            for (int i = 0; i < n; ++i)
            {
                mono[(size_t) i] = 0.5f * (l[i] + r[i]);
                d->pcm[((size_t) pos + i) * 2]     = (int16_t) juce::jlimit (-32767, 32767, (int) std::lrint (l[i] * 32767.0f));
                d->pcm[((size_t) pos + i) * 2 + 1] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lrint (r[i] * 32767.0f));
            }
            acc.process (mono.data(), n);
            owner.progress = (double) (pos + n) / d->frames;
        }
        if (threadShouldExit()) return;
        d->stats.integrated = meter.getIntegrated(); d->stats.lra = meter.getLoudnessRange(); d->stats.truePeak = meter.getTruePeak();
        d->stats.plr = meter.getPLR(); d->stats.maxShortTerm = meter.getShortTermMax();
        if (acc.hasResult()) d->tonal = acc.normalizedDb();
        std::shared_ptr<const Data> shared = d;
        addShared (shared);
        finish (shared, {});
    }

    void finish (std::shared_ptr<const Data> d, const juce::String& error)
    {
        juce::MessageManager::callAsync ([o = &owner, alive = owner.alive, d, error, f = file, cb = done]
        {
            if (! *alive) return;
            o->loading = false;
            if (d != nullptr) o->adopt (d, f);
            if (cb) cb (d != nullptr, error);
        });
    }

    ReferenceTrack& owner;
    juce::File file;
    std::function<void (bool, const juce::String&)> done;
};

//==============================================================================
// the whole file's mean power spectrum at one FFT size, mapped onto the analyzer's grid (background)
struct ReferenceTrack::SpectrumJob : public juce::ThreadPoolJob
{
    SpectrumJob (std::shared_ptr<const Data> d, int o, double hz, int b, std::pair<int, long long> k, ReferenceTrack& t)
        : juce::ThreadPoolJob ("Reference spectrum"), data (std::move (d)), order (o), binHz (hz), bins (b), key (k), owner (&t), alive (t.alive) {}

    JobStatus runJob() override
    {
        const int N = 1 << order, hop = N / 4, len = data->frames;
        fft::Radix2 f; f.init (order);
        std::vector<float> win ((size_t) N); double sum = 0;
        for (int i = 0; i < N; ++i) { win[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * i / N)); sum += win[(size_t) i]; }
        const double norm = 1.0 / ((sum / 2) * (sum / 2));
        std::vector<double> acc ((size_t) N / 2 + 1, 0.0);
        std::vector<std::complex<float>> buf ((size_t) N);
        int frames = 0;
        for (int start = 0; ; start += hop)
        {
            if (shouldExit()) return jobHasFinished;
            for (int c = 0; c < 2; ++c)
            {
                for (int i = 0; i < N; ++i)
                    buf[(size_t) i] = { start + i < len ? data->pcm[((size_t) start + i) * 2 + (size_t) c] * kToFloat * win[(size_t) i] : 0.0f, 0.0f };
                f.forward (buf.data());
                for (int k = 0; k <= N / 2; ++k) acc[(size_t) k] += 0.5 * std::norm (buf[(size_t) k]) * norm;
            }
            ++frames;
            if (start + N >= len) break;
        }
        // onto the analyzer's grid (linear interpolation in frequency)
        std::vector<float> out ((size_t) bins, 0.0f);
        const double refBinHz = data->rate / N;
        for (int k = 0; k < bins; ++k)
        {
            const double x = k * binHz / refBinHz;
            const int i = (int) x;
            if (i >= N / 2) { out[(size_t) k] = (float) (acc[(size_t) N / 2] / frames); continue; }
            const double fr = x - i;
            out[(size_t) k] = (float) ((acc[(size_t) i] * (1 - fr) + acc[(size_t) i + 1] * fr) / frames);
        }
        juce::MessageManager::callAsync ([o = owner, alive = alive, k = key, d = data, out = std::move (out)]() mutable
        {
            if (! *alive || o->data != d) return;          // cleared or replaced meanwhile
            o->fftPending.erase (k);
            o->fftCache[k] = std::move (out);
        });
        return jobHasFinished;
    }

    std::shared_ptr<const Data> data;
    int order; double binHz; int bins;
    std::pair<int, long long> key;
    ReferenceTrack* owner;
    std::shared_ptr<bool> alive;
};

//==============================================================================
ReferenceTrack::ReferenceTrack() : alive (std::make_shared<bool> (true)) {}
ReferenceTrack::~ReferenceTrack()
{
    *alive = false;
    loader.reset();
    if (spectrumPool != nullptr) spectrumPool->removeAllJobs (true, 5000);
}

void ReferenceTrack::load (const juce::File& f, std::function<void (bool, const juce::String&)> done)
{
    loader.reset();
    clear();
    loading = true; progress = 0;
    loader = std::make_unique<Loader> (*this, f, std::move (done));
    loader->startThread (juce::Thread::Priority::normal);
}

void ReferenceTrack::adopt (std::shared_ptr<const Data> d, const juce::File& f)
{
    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        data = std::move (d);
        isReady = true;
    }
    fileName = f.getFileName(); path = f;
    fftCache.clear(); fftPending.clear();
    posSeconds = 0;
}

void ReferenceTrack::clear()
{
    listen = false; playing = false;
    std::shared_ptr<const Data> old;
    {
        const juce::SpinLock::ScopedLockType sl (audioLock);
        isReady = false;
        old = std::move (data);
    }
    if (spectrumPool != nullptr) spectrumPool->removeAllJobs (true, 2000);
    fftCache.clear(); fftPending.clear();
    fileName = {}; path = juce::File();
    old.reset();                                     // freed here, never on the audio thread
}

float ReferenceTrack::matchGainDb() const noexcept
{
    const float mix = mixLufs.load();
    const auto& st = stats();
    if (! matchLoudness.load() || ! std::isfinite (mix) || ! std::isfinite (st.integrated)) return 0.0f;
    return juce::jlimit (-40.0f, 24.0f, mix - (float) st.integrated);
}

void ReferenceTrack::sampleAt (const Data& d, double t, float& l, float& r) const noexcept
{
    const int n = d.frames;
    const double x = t * d.rate;
    const int i = (int) std::floor (x);
    if (i < 0 || i >= n) { l = r = 0; return; }
    const float f = (float) (x - i);
    const int i0 = juce::jmax (0, i - 1), i2 = juce::jmin (n - 1, i + 1), i3 = juce::jmin (n - 1, i + 2);
    for (int c = 0; c < 2; ++c)
    {
        const auto at = [&] (int k) { return d.pcm[(size_t) k * 2 + (size_t) c] * kToFloat; };
        const float y0 = at (i0), y1 = at (i), y2 = at (i2), y3 = at (i3);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        (c == 0 ? l : r) = ((c3 * f + c2) * f + c1) * f + y1;
    }
}

void ReferenceTrack::renderSynced (float* l, float* r, int n, double sampleRate, double t0, bool hostPlaying) noexcept
{
    const juce::SpinLock::ScopedTryLockType sl (audioLock);
    if (! sl.isLocked()) return;
    const Data* d = isReady.load() ? data.get() : nullptr;
    const bool want = listen.load() && d != nullptr;
    if (! want && fade <= 0.0f) return;
    const float g = juce::Decibels::decibelsToGain (matchGainDb());
    const float step = (float) (1.0 / (0.010 * sampleRate));
    for (int i = 0; i < n; ++i)
    {
        fade = juce::jlimit (0.0f, 1.0f, fade + (want ? step : -step));
        float a = 0, b = 0;
        if (d != nullptr && hostPlaying) sampleAt (*d, t0 + i / sampleRate, a, b);
        if (l == r) { l[i] = l[i] * (1.0f - fade) + 0.5f * (a + b) * g * fade; continue; }   // a mono track
        l[i] = l[i] * (1.0f - fade) + a * g * fade;
        r[i] = r[i] * (1.0f - fade) + b * g * fade;
    }
}

void ReferenceTrack::getNextAudioBlock (const juce::AudioSourceChannelInfo& info)
{
    info.clearActiveBufferRegion();
    const juce::SpinLock::ScopedTryLockType sl (audioLock);
    const Data* d = sl.isLocked() && isReady.load() ? data.get() : nullptr;
    if (d == nullptr || ! playing.load() || info.buffer->getNumChannels() == 0) return;
    const float g = juce::Decibels::decibelsToGain (matchGainDb());
    float* l = info.buffer->getWritePointer (0, info.startSample);
    float* r = info.buffer->getNumChannels() > 1 ? info.buffer->getWritePointer (1, info.startSample) : nullptr;
    double t = posSeconds.load();
    const double len = (double) d->frames / d->rate;
    for (int i = 0; i < info.numSamples; ++i)
    {
        float a, b;
        sampleAt (*d, t, a, b);
        l[i] = a * g; if (r != nullptr) r[i] = b * g;
        t += 1.0 / outRate;
        if (t >= len) t = 0;                         // loops
    }
    posSeconds = t;
}

const std::vector<float>& ReferenceTrack::fftPower (int order, double binHz, int bins)
{
    static const std::vector<float> none;
    if (! ready() || data == nullptr) return none;
    const auto key = std::make_pair (order, (long long) std::llround (binHz * 1000.0) * 100000 + bins);
    if (auto it = fftCache.find (key); it != fftCache.end()) return it->second;
    if (fftPending.insert (key).second)
    {
        if (spectrumPool == nullptr) spectrumPool = std::make_unique<juce::ThreadPool> (juce::ThreadPoolOptions().withNumberOfThreads (1).withThreadName ("Reference spectrum"));
        spectrumPool->addJob (new SpectrumJob (data, order, binHz, bins, key, *this), true);
    }
    return none;
}
