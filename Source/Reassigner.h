// Reassigned spectrogram engine (Auger / Flandrin / Chassande-Mottin time-frequency reassignment).
// Pure C++17, no JUCE dependency, so it can be unit-tested on its own.
//
//   X_h  = STFT with Hann window h
//   X_th = STFT with window t*h   (t = sample offset from the frame centre)
//   X_dh = STFT with window dh/dt
//   t^ = t + Re(X_th * conj X_h) / |X_h|^2            (samples)
//   k^ = k - Im(X_dh * conj X_h) / |X_h|^2 * N / 2pi   (bins)
//
// Every bin's energy is moved to (t^, k^) and accumulated into display pixels: one output column per hop,
// rows on a logarithmic frequency axis. Column values are dBFS (a full-scale sine reads ~0 dB).
#pragma once
#include <cmath>
#include <functional>
#include <vector>

class Reassigner
{
public:
    enum Channel { Left = 0, Right = 1, Sum = 2, Diff = 3 };

    struct Config
    {
        double sampleRate = 48000.0;
        int    fftSize = 4096;
        double pixelsPerSecond = 300.0;
        int    rows = 512;
        double fmin = 20.0, fmax = 20000.0;
        bool   reassign = true;
        bool   shortRange = true;          // "short" halves the reassignment latency
        int    channel = Sum;
        double floorDb = -100.0;           // bins below this (bin-normalised dB) are not reassigned
    };

    // Called once per finished display column: rows values in dBFS, row 0 = lowest frequency.
    // A column of NaN means "data missing" (overload) and is drawn white.
    using ColumnSink = std::function<void (const float* column, int rows)>;

    void setSink (ColumnSink s) { sink = std::move (s); }
    void configure (const Config& c);
    void setFloorDb (double db);
    void process (const float* left, const float* right, int numSamples);
    void emitGap (int numInputSamples);          // overload: emit white columns covering this many samples, reset

    const Config& getConfig() const noexcept { return cfg; }
    int    getDecimation() const noexcept   { return dec; }
    double getEffectiveRate() const noexcept { return srEff; }
    double getEffectiveFmax() const noexcept { return fmaxEff; }
    int    getHop() const noexcept          { return hop; }
    double getLatencySeconds() const noexcept;

    static int decimationFor (double sr) noexcept { return sr > 100000.0 ? 4 : (sr > 50000.0 ? 2 : 1); }

private:
    void processFrame (int offset);
    void fft (std::vector<double>& re, std::vector<double>& im) const;

    Config cfg;
    ColumnSink sink;
    int N = 0, hop = 1, H = 1, S = 0, C = 1, dec = 1, kmin = 1, kmax = 1;
    double srEff = 48000.0, fmaxEff = 20000.0, maxShift = 0, normB = 1, normR = 1, thrP = 0, lnMin = 0, lnSpan = 1;
    long long frame = 0;

    std::vector<double> win, wt, wd, re, im, re2, im2, binP, cosT, sinT;
    std::vector<unsigned> rev;
    std::vector<std::vector<double>> acc;
    std::vector<int> rowA, rowB;
    std::vector<double> rowF;
    std::vector<float> buf, column;
    int bufLen = 0, framePos = 0;

    std::vector<double> fir, firHist;
    int firLen = 0, firPos = 0, decCnt = 0;
};
