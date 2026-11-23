// A reference song to compare a mix with: loaded once (decoded to memory on a background thread) and
// measured like the live input, so the pages can show it beside the mix:
//   - loudness: integrated, LRA, true peak, PLR, maximum short-term (BS.1770 / EBU R128)
//   - Balance: its tonal balance (the same measurement as the live curve and Target Builder)
//   - Analyzer (FFT): its long-term average spectrum, on the analyzer's own bin grid
// and heard instead of the mix, optionally at matched loudness:
//   - VST3: the plugin's output plays the reference in step with the host's transport (A/B like Metric AB)
//   - app: through Windows Audio (shared), from a position you choose (getNextAudioBlock)
// Switching between mix and reference fades over 10 ms, so it never clicks.
#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <map>

class ReferenceTrack : public juce::AudioSource
{
public:
    ReferenceTrack();
    ~ReferenceTrack() override;

    // loading (message thread); done is called on the message thread
    void load (const juce::File&, std::function<void (bool ok, const juce::String& error)> done);
    void clear();
    bool isLoading() const noexcept          { return loading.load(); }
    double loadProgress() const noexcept     { return progress.load(); }
    bool ready() const noexcept              { return isReady.load(); }
    juce::String name() const                { return fileName; }
    juce::File file() const                  { return path; }
    double lengthSeconds() const noexcept    { return ready() ? (double) audio.getNumSamples() / fileRate : 0.0; }

    struct Stats { double integrated = -INFINITY, lra = 0, truePeak = -INFINITY, plr = -INFINITY, maxShortTerm = -INFINITY; };
    const Stats& stats() const noexcept      { return st; }
    const std::vector<double>& tonalRaw() const noexcept { return tonal; }   // tonal::kBins, normalised dB (empty: too quiet)

    // the average power spectrum (full-scale sine = 1, the FFT analyzer's scaling; stereo = mean of L and R),
    // on a grid of `bins` bins of binHz each (the analyzer's): computed at the file's rate, then mapped
    const std::vector<float>& fftPower (int order, double binHz, int bins);

    // listening
    std::atomic<bool> listen { false };      // hear the reference instead of the mix
    std::atomic<bool> matchLoudness { true };
    std::atomic<float> mixLufs { -INFINITY };// the mix's integrated loudness (set by the UI): matching aims at it
    float matchGainDb() const noexcept;      // the gain applied to the reference when matching (0 when unknown)

    // VST3: replaces (with a fade) the block's audio with the reference at host time t0 (seconds); playing
    // false gives silence from the reference side. Real-time safe.
    void renderSynced (float* l, float* r, int n, double sampleRate, double t0, bool playing) noexcept;

    // app: plays through an AudioSourcePlayer (PlaybackOutput)
    void play()                              { if (ready()) playing = true; }
    void stop()                              { playing = false; }
    bool isPlaying() const noexcept          { return playing.load(); }
    void seek (double seconds)               { posSeconds = juce::jlimit (0.0, juce::jmax (0.0, lengthSeconds()), seconds); }
    double position() const noexcept         { return posSeconds.load(); }
    void prepareToPlay (int, double sampleRate) override { outRate = sampleRate; }
    void releaseResources() override {}
    void getNextAudioBlock (const juce::AudioSourceChannelInfo&) override;

private:
    struct Loader;
    void sampleAt (double t, float& l, float& r) const noexcept;   // 4-point Hermite at t seconds (0 outside)

    std::unique_ptr<Loader> loader;
    std::atomic<bool> loading { false }, isReady { false }, playing { false };
    std::atomic<double> progress { 0 }, posSeconds { 0 };
    juce::AudioBuffer<float> audio;          // stereo, the file's rate (only touched while not ready)
    double fileRate = 48000, outRate = 48000;
    juce::String fileName;
    juce::File path;
    Stats st;
    std::vector<double> tonal;
    std::map<std::pair<int, long long>, std::vector<float>> fftCache;
    float fade = 0;                          // 0 mix .. 1 reference (audio thread)
    std::shared_ptr<bool> alive;
    juce::SpinLock audioLock;                // the audio thread only try-locks it (skips a block rather than wait)             // the loader's results arrive only while this exists
};
