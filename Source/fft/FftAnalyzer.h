// The FFT analyzer behind the Analyzer view's "FFT" style (a modern, Voxengo SPAN / FabFilter Pro-Q
// style curve). Pure C++, no JUCE (tests/fft_test.cpp).
//
// Two inputs: 0 the main signal and 1 a sidechain (the VST3 version's sidechain bus). Up to kSeries
// curves, each from one input and one channel (stereo = the mean power of L and R, left, right, mid,
// side). Per curve: the averaged power spectrum (exponential, time constant in ms; 0 = real time) and a
// peak-hold spectrum that the display decays. A full-scale sine reads 0 dB (Hann window, amplitude
// normalised), before the slope.
//
// curve() maps a spectrum onto display columns: each column takes the loudest bin in its frequency span
// (high frequencies), or interpolates between bins when a column is narrower than a bin (lows); then the
// slope (dB per octave, pivot 1 kHz) and fractional-octave smoothing are applied.
#pragma once
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace fft
{
enum Channel { Stereo = 0, Left, Right, Mid, Side };

class Radix2
{
public:
    void init (int order);
    int size() const noexcept { return n; }
    void forward (std::complex<float>* data) const noexcept;     // in place, unscaled
private:
    int n = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;
};

class FftAnalyzer
{
public:
    static constexpr int kSeries = 3, kInputs = 2;

    void configure (double sampleRate, int order);              // order 11 (2048) .. 15 (32768); clears
    double sampleRate() const noexcept { return sr; }
    int order() const noexcept { return ord; }
    int bins() const noexcept { return N / 2 + 1; }
    double binHz() const noexcept { return sr / N; }

    void setSeries (int index, bool on, int input, int channel);
    bool seriesOn (int i) const noexcept { return series[(size_t) i].on; }
    void setAveraging (double ms) noexcept { avgMs = ms; }
    void setOverlap (int factor) noexcept { overlap = factor < 1 ? 1 : factor; }   // hops per FFT length (default 4)

    void push (int input, const float* left, const float* right, int n);
    void decayHold (double seconds, double dbPerSecond);         // call per display frame
    void resetHold();
    void clear();                                                // spectra and hold

    const std::vector<float>& power (int i) const noexcept { return series[(size_t) i].avg; }
    const std::vector<float>& held (int i) const noexcept  { return series[(size_t) i].hold; }
    long long hops (int input) const noexcept { return in[(size_t) input].hops; }

    // display columns: edges = n + 1 frequencies (Hz, ascending); out = n values in dB
    void curve (const std::vector<float>& spectrum, const std::vector<float>& edges, double slopeDbPerOct,
                double smoothOct, std::vector<float>& outDb) const;

private:
    void analyse (int input);

    struct Input { std::vector<float> l, r; int pos = 0, sinceHop = 0; long long hops = 0, filled = 0; };
    struct Series { bool on = false; int input = 0, channel = Stereo; std::vector<float> avg, hold; bool fresh = true; };

    double sr = 48000, avgMs = 300;
    int ord = 13, N = 8192, overlap = 4;
    Radix2 fftImpl;
    std::vector<float> window;
    float norm = 1;                                              // power scale: full-scale sine = 1
    std::array<Input, kInputs> in;
    std::array<Series, kSeries> series;
    std::vector<std::complex<float>> bufA, bufB;
    std::vector<float> powL, powR, powM, powS;
};
} // namespace fft
