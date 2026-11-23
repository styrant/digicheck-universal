// Tonal balance analysis: long-term average spectrum of a track, and genre targets built from many
// tracks. Pure C++17 (no JUCE) so the target builder tool, the future Balance view and the unit
// tests all share exactly the same measurement.
//
// Target format (JSON, same layout as Tonal Balance Control target files):
//   frequencies_hz          2049 bins, 0 .. 22050 Hz (4096-point FFT at 44.1 kHz)
//   raw_normalized_mag_dB   median of the tracks' spectra, unsmoothed
//   normalized_mag_dB       the target curve: raw, smoothed (1/3 octave, power domain)
//   low_/high_normalized_mag_dB   the "in range" band (percentiles across tracks)
//   region_N_frequencies / region_N_bounds   4 bands (0-250, 250-2k, 2k-8k, 8k-22k Hz) and the
//                                            range of each band's share of the spectrum
// A spectrum is normalised so its linear magnitudes add up to 1, which makes it independent of level.
#pragma once
#include <array>
#include <string>
#include <vector>

namespace tonal
{
    // the target grid: a 4096-point FFT at 44.1 kHz (2049 bins, 10.77 Hz apart)
    constexpr double kSampleRate = 44100.0;
    constexpr int kFftSize = 4096, kHop = 2048, kBins = kFftSize / 2 + 1;
    constexpr double kBandEdges[5] = { 0.0, 250.0, 2000.0, 8000.0, 22050.0 };

    double binFrequency (int bin) noexcept;

    // Splits audio into analysis frames at the audio's own sample rate: the window has the same
    // length in time as 4096 samples at 44.1 kHz (93 ms, so the same resolution), is zero-padded to a
    // power of two, and each frame's power spectrum is read off at the 44.1 kHz bin frequencies.
    // No resampling, so nothing above 22.05 kHz can alias into the measurement.
    class FrameAnalyzer
    {
    public:
        // hopDivisor: frames start every window/hopDivisor (2 = 46 ms, as for targets; 4 = 23 ms for live views)
        explicit FrameAnalyzer (double sampleRate = kSampleRate, int hopDivisor = 2);
        // calls onFrame (const double* power[kBins], double meanSquare) for each frame; frames quieter
        // than -70 dBFS get power == nullptr (no FFT is done for them)
        template <typename Fn> void process (const float* mono, int n, Fn&& onFrame);
        double getSampleRate() const noexcept { return sr; }
        double secondsPerFrame() const noexcept { return hop / sr; }
        void reset() { fill = 0; }

    private:
        bool frame();                                   // true if the frame passed the absolute gate
        std::vector<float> buf;
        std::vector<double> window, re, im, grid;
        double sr, meanSquare = 0;
        int len, hop, nfft, nbins, fill = 0;
    };

    constexpr double kGateAbsDb = -70.0, kGateRelDb = -20.0;

    // Long-term average spectrum of one track. Quiet passages (below -70 dBFS, or more than 20 dB
    // below the track's average level, like loudness gating) are left out, so intros, outros and
    // silence don't tilt the result.
    class SpectrumAccumulator
    {
    public:
        explicit SpectrumAccumulator (double sampleRate = kSampleRate);
        void process (const float* mono, int n);         // mono samples at the given rate, any block size
        void addFrame (const double* power, double meanSquare);   // a frame from a FrameAnalyzer
        bool hasResult() const;
        std::vector<double> normalizedDb() const;          // kBins values; empty if nothing loud enough
        double analysedSeconds() const;                     // audio that passed the gate
        double getSampleRate() const noexcept { return frames.getSampleRate(); }
        void reset();

    private:
        static constexpr int kBuckets = 152;               // 0.5 dB steps from -70 to +6 dBFS
        FrameAnalyzer frames;
        std::vector<std::vector<double>> bucketPower;       // summed power spectra (44.1 kHz grid) per loudness bucket
        std::vector<double> bucketEnergy;                   // summed frame mean-squares
        std::vector<int> bucketFrames;
    };

    // RMS magnitude from summed power over `frames` frames, normalised so magnitudes add up to 1 (dB)
    std::vector<double> normalizeSummedPower (const std::vector<double>& power, int frames);

    // fractional-octave smoothing in the power domain (frac = 1/3 -> third-octave)
    std::vector<double> smoothDb (const std::vector<double>& db, double frac = 1.0 / 3.0);
    // smooth, bell-shaped (near Gaussian) fractional-octave smoothing in the power domain: three passes of a
    // log-symmetric box, so no corners. fwhmOct: the width at half weight in octaves (1 = one octave).
    std::vector<double> smoothDbBell (const std::vector<double>& db, double fwhmOct);
    // the display smoothing choices: Smooth (1 octave, the default), Medium (1/2), Detailed (1/3), Fine (1/6)
    inline double smoothingOctaves (int index) { static const double o[4] = { 1.0, 0.5, 1.0 / 3.0, 1.0 / 6.0 }; return o[index < 0 ? 0 : (index > 3 ? 3 : index)]; }

    // each band's share of the (linear magnitude) spectrum; the 4 values add up to 1
    std::array<double, 4> bandShares (const std::vector<double>& normalizedDb);

    struct Target
    {
        std::vector<double> raw, mean, low, high;          // kBins each, dB
        std::array<std::array<double, 2>, 4> regions {};
        int tracks = 0;
    };

    // Combines the tracks' normalised spectra: median curve, and the pLo..pHi percentile range
    // across tracks (of their smoothed curves). With fewer than 5 tracks the range is widened to at
    // least +/-2 dB and the band ranges to +/-15 % so a target from 1-4 songs is still usable.
    // wide: the range is also never narrower than Tonal Balance Control's typical range around its
    // curve (kWideUp / kWideDown: about 11-17 dB in total, with more room below the curve than above).
    Target combine (const std::vector<std::vector<double>>& tracks, double pLo = 10.0, double pHi = 90.0, bool wide = false);

    // the "wide" range around a target curve at frequency f (dB above / below the curve), and the
    // band ranges (dB below / above each band's share)
    double wideUpDb (double hz);
    double wideDownDb (double hz);
    extern const double kWideBandBelowDb[4], kWideBandAboveDb[4];

    // distance of one track from a target: RMS dB difference of the smoothed curves, 40 Hz - 16 kHz
    double deviationDb (const std::vector<double>& trackDb, const Target& t);

    std::string toJson (const Target& t, const std::string& name, const std::vector<std::string>& trackNames);

    //==============================================================================
    template <typename Fn>
    void FrameAnalyzer::process (const float* x, int n, Fn&& onFrame)
    {
        for (int i = 0; i < n; ++i)
        {
            buf[(size_t) fill++] = x[i];
            if (fill == len)
            {
                onFrame (frame() ? grid.data() : nullptr, meanSquare);
                std::copy (buf.begin() + hop, buf.end(), buf.begin());
                fill = len - hop;
            }
        }
    }
}
