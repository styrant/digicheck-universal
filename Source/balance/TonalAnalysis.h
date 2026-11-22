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

    // Long-term average spectrum of one track, at the audio's own sample rate: the analysis window
    // has the same length in time as 4096 samples at 44.1 kHz (93 ms, so the same resolution), is
    // zero-padded to a power of two, and the result is read off at the 44.1 kHz bin frequencies.
    // No resampling, so nothing above 22.05 kHz can alias into the measurement. Quiet passages (below -70 dBFS, or more than 20 dB
    // below the track's average level, like loudness gating) are left out, so intros, outros and
    // silence don't tilt the result.
    class SpectrumAccumulator
    {
    public:
        explicit SpectrumAccumulator (double sampleRate = kSampleRate);
        void process (const float* mono, int n);         // mono samples at the given rate, any block size
        bool hasResult() const;
        std::vector<double> normalizedDb() const;          // kBins values; empty if nothing loud enough
        double analysedSeconds() const;                     // audio that passed the gate
        double getSampleRate() const noexcept { return sr; }

    private:
        void frame();
        static constexpr int kBuckets = 152;               // 0.5 dB steps from -70 to +6 dBFS
        std::vector<std::vector<double>> bucketPower;       // summed power spectra per loudness bucket
        std::vector<double> bucketEnergy;                   // summed frame mean-squares
        std::vector<int> bucketFrames;
        std::vector<float> buf;
        std::vector<double> window, re, im;
        double sr;
        int len, hop, nfft, nbins, fill = 0;
    };

    // fractional-octave smoothing in the power domain (frac = 1/3 -> third-octave)
    std::vector<double> smoothDb (const std::vector<double>& db, double frac = 1.0 / 3.0);

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
    Target combine (const std::vector<std::vector<double>>& tracks, double pLo = 10.0, double pHi = 90.0);

    // distance of one track from a target: RMS dB difference of the smoothed curves, 40 Hz - 16 kHz
    double deviationDb (const std::vector<double>& trackDb, const Target& t);

    std::string toJson (const Target& t, const std::string& name, const std::vector<std::string>& trackNames);
}
