#include "TonalAnalysis.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>

namespace tonal
{
namespace
{
    constexpr double kPi = 3.14159265358979323846;

    void fft (std::vector<double>& re, std::vector<double>& im)
    {
        const int n = (int) re.size();
        for (int i = 1, j = 0; i < n; ++i)
        {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const double a = -2 * kPi / len;
            const double wr = std::cos (a), wi = std::sin (a);
            for (int i = 0; i < n; i += len)
            {
                double cr = 1, ci = 0;
                for (int k = 0; k < len / 2; ++k)
                {
                    const size_t u = (size_t) (i + k), v = (size_t) (i + k + len / 2);
                    const double tr = re[v] * cr - im[v] * ci, ti = re[v] * ci + im[v] * cr;
                    re[v] = re[u] - tr; im[v] = im[u] - ti;
                    re[u] += tr; im[u] += ti;
                    const double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
                }
            }
        }
    }

    double percentile (std::vector<double> v, double p)
    {
        if (v.empty()) return 0;
        std::sort (v.begin(), v.end());
        const double pos = std::clamp (p / 100.0, 0.0, 1.0) * (double) (v.size() - 1);
        const size_t i = (size_t) pos;
        const double f = pos - (double) i;
        return i + 1 < v.size() ? v[i] * (1 - f) + v[i + 1] * f : v[i];
    }
}

double binFrequency (int bin) noexcept { return bin * kSampleRate / kFftSize; }

//==============================================================================
FrameAnalyzer::FrameAnalyzer (double sampleRate, int hopDivisor) : sr (sampleRate > 1000 ? sampleRate : kSampleRate)
{
    len = std::max (16, (int) std::lround (kFftSize * sr / kSampleRate));   // 93 ms, like 4096 samples at 44.1 kHz
    hop = std::max (1, len / std::max (1, hopDivisor));
    nfft = 1;
    while (nfft < len) nfft <<= 1;
    nbins = nfft / 2 + 1;
    buf.assign ((size_t) len, 0.0f);
    window.resize ((size_t) len); re.resize ((size_t) nfft); im.resize ((size_t) nfft); grid.resize ((size_t) kBins);
    for (int i = 0; i < len; ++i) window[(size_t) i] = 0.5 - 0.5 * std::cos (2 * kPi * i / len);   // periodic Hann
}

bool FrameAnalyzer::frame()
{
    double ms = 0;
    for (int i = 0; i < len; ++i) ms += (double) buf[(size_t) i] * buf[(size_t) i];
    meanSquare = ms / len;
    if (10 * std::log10 (meanSquare + 1e-30) < kGateAbsDb) return false;

    std::fill (re.begin(), re.end(), 0.0); std::fill (im.begin(), im.end(), 0.0);
    for (int i = 0; i < len; ++i) re[(size_t) i] = buf[(size_t) i] * window[(size_t) i];
    fft (re, im);
    // power at the 44.1 kHz bin frequencies (linear interpolation between this rate's bins)
    const double perBin = nfft / sr;
    for (int k = 0; k < kBins; ++k)
    {
        const double pos = std::min (binFrequency (k) * perBin, (double) (nbins - 1));
        const int i = (int) pos;
        const double f = pos - i;
        auto pw = [&] (int b) { return re[(size_t) b] * re[(size_t) b] + im[(size_t) b] * im[(size_t) b]; };
        grid[(size_t) k] = i + 1 < nbins ? pw (i) * (1 - f) + pw (i + 1) * f : pw (i);
    }
    return true;
}

//==============================================================================
SpectrumAccumulator::SpectrumAccumulator (double sampleRate)
    : frames (sampleRate), bucketPower ((size_t) kBuckets), bucketEnergy ((size_t) kBuckets, 0.0), bucketFrames ((size_t) kBuckets, 0)
{
}

void SpectrumAccumulator::reset()
{
    for (auto& b : bucketPower) b.clear();
    std::fill (bucketEnergy.begin(), bucketEnergy.end(), 0.0);
    std::fill (bucketFrames.begin(), bucketFrames.end(), 0);
    frames.reset();
}

void SpectrumAccumulator::process (const float* x, int n)
{
    frames.process (x, n, [this] (const double* p, double ms) { addFrame (p, ms); });
}

void SpectrumAccumulator::addFrame (const double* power, double ms)
{
    const double db = 10 * std::log10 (ms + 1e-30);
    if (power == nullptr || db < kGateAbsDb) return;
    const int b = std::clamp ((int) ((db - kGateAbsDb) * 2.0), 0, kBuckets - 1);
    auto& acc = bucketPower[(size_t) b];
    if (acc.empty()) acc.assign ((size_t) kBins, 0.0);
    for (int k = 0; k < kBins; ++k) acc[(size_t) k] += power[k];
    bucketEnergy[(size_t) b] += ms;
    ++bucketFrames[(size_t) b];
}

namespace
{
    // the gate: frames at least as loud as (energy-weighted average level - 20 dB)
    int firstBucket (const std::vector<double>& energy, const std::vector<int>& frames)
    {
        double e = 0; int n = 0;
        for (size_t i = 0; i < energy.size(); ++i) { e += energy[i]; n += frames[i]; }
        if (n == 0) return -1;
        const double avgDb = 10 * std::log10 (e / n);
        return std::max (0, (int) std::floor ((avgDb + kGateRelDb - kGateAbsDb) * 2.0));
    }
}

double SpectrumAccumulator::analysedSeconds() const
{
    const int first = firstBucket (bucketEnergy, bucketFrames);
    if (first < 0) return 0;
    int n = 0;
    for (int b = first; b < kBuckets; ++b) n += bucketFrames[(size_t) b];
    return n * frames.secondsPerFrame();
}

bool SpectrumAccumulator::hasResult() const { return analysedSeconds() > 0; }

std::vector<double> normalizeSummedPower (const std::vector<double>& power, int frames)
{
    if (frames <= 0 || power.size() != (size_t) kBins) return {};
    std::vector<double> mag ((size_t) kBins);
    double sum = 0;
    for (int k = 0; k < kBins; ++k) { mag[(size_t) k] = std::sqrt (power[(size_t) k] / frames); sum += mag[(size_t) k]; }
    std::vector<double> db ((size_t) kBins);
    for (int k = 0; k < kBins; ++k) db[(size_t) k] = 20 * std::log10 (mag[(size_t) k] / std::max (sum, 1e-30) + 1e-30);
    return db;
}

std::vector<double> SpectrumAccumulator::normalizedDb() const
{
    const int first = firstBucket (bucketEnergy, bucketFrames);
    if (first < 0) return {};
    std::vector<double> power ((size_t) kBins, 0.0);
    int n = 0;
    for (int b = first; b < kBuckets; ++b)
        if (! bucketPower[(size_t) b].empty())
        {
            for (int k = 0; k < kBins; ++k) power[(size_t) k] += bucketPower[(size_t) b][(size_t) k];
            n += bucketFrames[(size_t) b];
        }
    return normalizeSummedPower (power, n);
}

//==============================================================================
std::vector<double> smoothDb (const std::vector<double>& db, double frac)
{
    const int n = (int) db.size();
    std::vector<double> pw ((size_t) n), cum ((size_t) n + 1, 0.0), out ((size_t) n);
    for (int k = 0; k < n; ++k) { pw[(size_t) k] = std::pow (10.0, db[(size_t) k] / 10.0); cum[(size_t) k + 1] = cum[(size_t) k] + pw[(size_t) k]; }
    const double half = std::pow (2.0, frac / 2.0);
    for (int k = 0; k < n; ++k)
    {
        int lo = k, hi = k;
        if (k > 0)
        {
            lo = std::max (1, (int) std::ceil (k / half - 1e-9));
            hi = std::min (n - 1, (int) std::floor (k * half + 1e-9));
        }
        const double m = (cum[(size_t) hi + 1] - cum[(size_t) lo]) / (hi - lo + 1);
        out[(size_t) k] = 10 * std::log10 (m + 1e-300);
    }
    return out;
}

std::array<double, 4> bandShares (const std::vector<double>& db)
{
    std::array<double, 4> s {};
    double total = 0;
    for (int k = 0; k < (int) db.size(); ++k)
    {
        const double a = std::pow (10.0, db[(size_t) k] / 20.0), f = binFrequency (k);
        total += a;
        for (int b = 0; b < 4; ++b)
            if (f >= kBandEdges[b] && (f < kBandEdges[b + 1] || (b == 3 && f <= kBandEdges[4]))) { s[(size_t) b] += a; break; }
    }
    for (auto& v : s) v /= std::max (total, 1e-30);
    return s;
}

// Typical range of Tonal Balance Control-style targets around their curve: the median, over 12 genre
// targets, of (high - curve) and (curve - low), averaged in third-octave bands from 20 Hz (held above 16 kHz).
namespace
{
    constexpr double kWideUp[31]   = { 7.09, 7.09, 7.13, 7.12, 6.92, 6.68, 6.37, 6.09, 5.84, 5.68, 5.61, 5.76, 5.87, 5.86, 5.64, 5.39,
                                       5.25, 5.07, 4.88, 4.80, 4.79, 4.65, 4.45, 4.34, 4.38, 4.47, 4.62, 5.09, 5.49, 5.77, 5.77 };
    constexpr double kWideDown[31] = { 10.13, 10.13, 10.28, 10.53, 10.37, 9.86, 9.48, 8.62, 7.78, 7.23, 6.85, 6.68, 6.64, 6.62, 6.48, 6.33,
                                       6.04, 5.88, 5.86, 5.85, 6.11, 6.30, 6.36, 6.41, 6.73, 6.89, 7.05, 7.94, 8.72, 7.56, 7.56 };
    double wideAt (const double* table, double hz)          // third-octave table from 20 Hz, interpolated in log frequency
    {
        const double pos = std::clamp (3.0 * std::log2 (std::max (hz, 1.0) / 20.0), 0.0, 30.0);
        const int i = std::min (29, (int) pos);
        const double f = pos - i;
        return table[i] * (1 - f) + table[i + 1] * f;
    }
}
const double kWideBandBelowDb[4] = { 4.56, 3.05, 4.62, 6.68 }, kWideBandAboveDb[4] = { 4.43, 2.72, 2.82, 3.77 };
double wideUpDb (double hz)   { return wideAt (kWideUp, hz); }
double wideDownDb (double hz) { return wideAt (kWideDown, hz); }

Target combine (const std::vector<std::vector<double>>& tracks, double pLo, double pHi, bool wide)
{
    Target t;
    t.tracks = (int) tracks.size();
    if (tracks.empty()) return t;

    std::vector<std::vector<double>> smooth;
    for (auto& tr : tracks) smooth.push_back (smoothDb (tr));

    t.raw.resize ((size_t) kBins); t.low.resize ((size_t) kBins); t.high.resize ((size_t) kBins);
    std::vector<double> col (tracks.size());
    for (int k = 0; k < kBins; ++k)
    {
        for (size_t i = 0; i < tracks.size(); ++i) col[i] = tracks[i][(size_t) k];
        t.raw[(size_t) k] = percentile (col, 50);
    }
    t.mean = smoothDb (t.raw);

    const double minHalf = tracks.size() < 5 ? 2.0 : 0.5;
    for (int k = 0; k < kBins; ++k)
    {
        for (size_t i = 0; i < tracks.size(); ++i) col[i] = smooth[i][(size_t) k];
        const double m = t.mean[(size_t) k];
        t.low[(size_t) k]  = std::min (percentile (col, pLo), m - (wide ? std::max (minHalf, wideDownDb (binFrequency (k))) : minHalf));
        t.high[(size_t) k] = std::max (percentile (col, pHi), m + (wide ? std::max (minHalf, wideUpDb (binFrequency (k))) : minHalf));
    }

    std::vector<std::array<double, 4>> shares;
    for (auto& tr : tracks) shares.push_back (bandShares (tr));
    const auto mid = bandShares (t.raw);
    for (int b = 0; b < 4; ++b)
    {
        std::vector<double> v;
        for (auto& s : shares) v.push_back (s[(size_t) b]);
        double lo = percentile (v, pLo), hi = percentile (v, pHi);
        if (tracks.size() < 5) { lo = std::min (lo, mid[(size_t) b] * 0.85); hi = std::max (hi, mid[(size_t) b] * 1.15); }
        if (wide)
        {
            lo = std::min (lo, mid[(size_t) b] * std::pow (10.0, -kWideBandBelowDb[b] / 20.0));
            hi = std::max (hi, mid[(size_t) b] * std::pow (10.0, kWideBandAboveDb[b] / 20.0));
        }
        t.regions[(size_t) b] = { std::min (lo, mid[(size_t) b]), std::max (hi, mid[(size_t) b]) };
    }
    return t;
}

double deviationDb (const std::vector<double>& trackDb, const Target& t)
{
    const auto s = smoothDb (trackDb);
    double e = 0; int n = 0;
    for (int k = 0; k < kBins; ++k)
    {
        const double f = binFrequency (k);
        if (f < 40 || f > 16000) continue;
        const double d = s[(size_t) k] - t.mean[(size_t) k];
        e += d * d; ++n;
    }
    return n ? std::sqrt (e / n) : 0;
}

namespace
{
    std::string escape (const std::string& s)
    {
        std::string o;
        for (char c : s)
        {
            if (c == '"' || c == '\\') { o += '\\'; o += c; }
            else if ((unsigned char) c < 0x20) { char b[8]; std::snprintf (b, sizeof b, "\\u%04x", c); o += b; }
            else o += c;
        }
        return o;
    }
    void array (std::string& o, const char* key, const std::vector<double>& v, bool last = false)
    {
        o += "  \""; o += key; o += "\": [";
        char b[32];
        for (size_t i = 0; i < v.size(); ++i) { std::snprintf (b, sizeof b, "%s%.6f", i ? ", " : "", v[i]); o += b; }
        o += last ? "]\n" : "],\n";
    }
}

std::string toJson (const Target& t, const std::string& name, const std::vector<std::string>& trackNames)
{
    std::string o = "{\n";
    o += "  \"name\": \"" + escape (name) + "\",\n";
    o += "  \"created_by\": \"Spectroscope TargetBuilder\",\n";
    o += "  \"track_count\": " + std::to_string (t.tracks) + ",\n";
    o += "  \"tracks\": [";
    for (size_t i = 0; i < trackNames.size(); ++i) o += (i ? ", \"" : "\"") + escape (trackNames[i]) + "\"";
    o += "],\n";
    std::vector<double> f ((size_t) kBins);
    for (int k = 0; k < kBins; ++k) f[(size_t) k] = binFrequency (k);
    array (o, "frequencies_hz", f);
    array (o, "high_normalized_mag_dB", t.high);
    array (o, "low_normalized_mag_dB", t.low);
    array (o, "normalized_mag_dB", t.mean);
    array (o, "raw_normalized_mag_dB", t.raw);
    for (int b = 0; b < 4; ++b)
    {
        char line[160];
        std::snprintf (line, sizeof line, "  \"region_%d_bounds\": [%.6f, %.6f],\n  \"region_%d_frequencies\": [%g, %g]%s\n",
                       b, t.regions[(size_t) b][0], t.regions[(size_t) b][1], b, kBandEdges[b], kBandEdges[b + 1], b == 3 ? "" : ",");
        o += line;
    }
    o += "}\n";
    return o;
}
}
