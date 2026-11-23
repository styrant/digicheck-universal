// Tonal balance analysis tests (the measurement behind TargetBuilder and the Balance view).
#include "../Source/balance/TonalAnalysis.h"
#include <cmath>
#include <cstdio>
#include <random>

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; }

// pink-ish noise (white noise through a -3 dB/octave filter), optionally tilted by a first-order shelf
static std::vector<float> noise (double seconds, double gain, unsigned seed, double lowBoostDb = 0)
{
    std::mt19937 rng (seed);
    std::normal_distribution<float> nd (0.0f, 1.0f);
    const int n = (int) (seconds * tonal::kSampleRate);
    std::vector<float> x ((size_t) n);
    double b0 = 0, b1 = 0, b2 = 0, lp = 0;
    const double shelf = std::pow (10.0, lowBoostDb / 20.0) - 1.0, a = std::exp (-2 * 3.14159265 * 200.0 / tonal::kSampleRate);
    for (int i = 0; i < n; ++i)
    {
        const double w = nd (rng);
        b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.96300 * b1 + w * 0.2965164; b2 = 0.57000 * b2 + w * 1.0526913;
        const double p = (b0 + b1 + b2 + w * 0.1848) * 0.05;
        lp = lp * a + p * (1 - a);
        x[(size_t) i] = (float) (gain * (p + shelf * lp));
    }
    return x;
}

static std::vector<double> measure (const std::vector<float>& x)
{
    tonal::SpectrumAccumulator acc;
    for (size_t p = 0; p < x.size(); p += 1000) acc.process (x.data() + p, (int) std::min<size_t> (1000, x.size() - p));
    return acc.normalizedDb();
}

static double rmsDiff (const std::vector<double>& a, const std::vector<double>& b)
{
    double e = 0; int n = 0;
    for (int k = 0; k < tonal::kBins; ++k)
    {
        const double f = tonal::binFrequency (k);
        if (f < 40 || f > 16000) continue;
        e += (a[(size_t) k] - b[(size_t) k]) * (a[(size_t) k] - b[(size_t) k]); ++n;
    }
    return std::sqrt (e / n);
}

int main()
{
    // 1. normalisation: linear magnitudes add up to 1, so level doesn't matter
    const auto quiet = measure (noise (20, 0.05, 1)), loud = measure (noise (20, 0.5, 1));
    double sum = 0;
    for (double d : loud) sum += std::pow (10.0, d / 20.0);
    check (std::abs (sum - 1.0) < 1e-6, "normalised spectrum sums to 1");
    check (rmsDiff (quiet, loud) < 0.01, "level independent (20 dB quieter gives the same curve)");

    // 2. gating: leading silence and a quiet tail (-40 dB) leave the result unchanged
    auto withSilence = noise (20, 0.5, 1);
    withSilence.insert (withSilence.begin(), (size_t) (5 * tonal::kSampleRate), 0.0f);
    auto withTail = withSilence;
    const auto tail = noise (10, 0.5 * 0.01, 9);
    withTail.insert (withTail.end(), tail.begin(), tail.end());
    const double dTail = rmsDiff (measure (withTail), measure (withSilence)), dSilence = rmsDiff (measure (withSilence), loud);
    std::printf ("      quiet tail changes the curve by %.4f dB; leading silence by %.3f dB (frame grid shift)\n", dTail, dSilence);
    check (dTail < 0.02, "quiet passages are gated out (only the boundary frame remains)");
    check (dSilence < 0.2, "silence is gated out");

    // 3. pink noise is roughly -3 dB/octave in RMS magnitude per bin (~ -3 dB per octave of bin)
    const auto sm = tonal::smoothDb (loud);
    const double slope = sm[(size_t) std::lround (4000 / (tonal::kSampleRate / tonal::kFftSize))]
                       - sm[(size_t) std::lround (2000 / (tonal::kSampleRate / tonal::kFftSize))];
    std::printf ("      pink slope 2k->4k: %.2f dB\n", slope);
    check (slope < -2.0 && slope > -4.0, "pink noise slope about -3 dB/octave");

    // 4. smoothing keeps a flat curve flat, and band shares add up to 1
    std::vector<double> flat ((size_t) tonal::kBins, -60.0);
    const auto fs = tonal::smoothDb (flat);
    double mx = 0; for (double d : fs) mx = std::max (mx, std::abs (d + 60.0));
    check (mx < 1e-9, "smoothing a flat curve leaves it flat");
    {
        // bell smoothing: flat stays flat, a narrow peak spreads smoothly (no corners: small second differences)
        std::vector<double> flat ((size_t) tonal::kBins, -40.0), spike ((size_t) tonal::kBins, -60.0);
        double mxb = 0;
        for (double v : tonal::smoothDbBell (flat, 1.0)) mxb = std::max (mxb, std::abs (v + 40.0));
        check (mxb < 1e-9, "bell smoothing leaves a flat curve flat");
        for (int k = 180; k <= 190; ++k) spike[(size_t) k] = -20.0;          // about 2.1-2.2 kHz
        const auto a = tonal::smoothDbBell (spike, 1.0 / 3.0), b = tonal::smoothDbBell (spike, 1.0);
        double d2a = 0, d2b = 0;
        for (int k = 60; k < 700; ++k) { d2a = std::max (d2a, std::abs (a[(size_t) k + 1] - 2 * a[(size_t) k] + a[(size_t) k - 1])); d2b = std::max (d2b, std::abs (b[(size_t) k + 1] - 2 * b[(size_t) k] + b[(size_t) k - 1])); }
        check (b[185] < a[185] && d2b < d2a, "wider bell smoothing is lower at the peak and smoother");
        check (b[93] > -59.0 && b[370] > -59.0, "1-octave bell reaches an octave either side");
    }
    const auto sh = tonal::bandShares (loud);
    check (std::abs (sh[0] + sh[1] + sh[2] + sh[3] - 1.0) < 1e-9, "band shares add up to 1");

    // 5. combining: 11 songs with low end boosted 0..10 dB -> median is the 5 dB one, range spans them
    std::vector<std::vector<double>> songs;
    for (int i = 0; i <= 10; ++i) songs.push_back (measure (noise (15, 0.3, 100 + (unsigned) i, i)));
    const auto t = tonal::combine (songs, 10, 90);
    const double dMid = tonal::deviationDb (songs[5], t), dEdge = tonal::deviationDb (songs[0], t);
    std::printf ("      deviation of middle song %.2f dB, of extreme song %.2f dB\n", dMid, dEdge);
    check (dMid < dEdge, "median target sits in the middle of the songs");
    bool inside = true;
    for (int k = 0; k < tonal::kBins; ++k) inside &= t.low[(size_t) k] <= t.mean[(size_t) k] + 1e-9 && t.mean[(size_t) k] <= t.high[(size_t) k] + 1e-9;
    check (inside, "range contains the target curve everywhere");
    const auto low1 = tonal::bandShares (songs[1]), low9 = tonal::bandShares (songs[9]);
    check (t.regions[0][0] <= low9[0] && t.regions[0][1] >= low1[0] && low9[0] > low1[0], "low band range spans 10th-90th percentile songs");
    double w100 = 0, w5k = 0;
    w100 = t.high[(size_t) std::lround (100 / (tonal::kSampleRate / tonal::kFftSize))] - t.low[(size_t) std::lround (100 / (tonal::kSampleRate / tonal::kFftSize))];
    w5k  = t.high[(size_t) std::lround (5000 / (tonal::kSampleRate / tonal::kFftSize))] - t.low[(size_t) std::lround (5000 / (tonal::kSampleRate / tonal::kFftSize))];
    std::printf ("      range width at 100 Hz %.1f dB, at 5 kHz %.1f dB\n", w100, w5k);
    check (w100 > w5k, "range is wider where the songs differ (the low end)");

    // 5b. wide ranges: never narrower than the typical Tonal Balance Control range (11-17 dB in total)
    const auto wideT = tonal::combine (songs, 10, 90, true);
    double minTotal = 1e9, maxTotal = 0;
    bool containsTight = true;
    for (int k = 0; k < tonal::kBins; ++k)
    {
        const double f = tonal::binFrequency (k);
        if (f < 20 || f > 20000) continue;
        const double w = wideT.high[(size_t) k] - wideT.low[(size_t) k];
        minTotal = std::min (minTotal, w); maxTotal = std::max (maxTotal, w);
        containsTight &= wideT.low[(size_t) k] <= t.low[(size_t) k] + 1e-9 && wideT.high[(size_t) k] >= t.high[(size_t) k] - 1e-9;
    }
    std::printf ("      wide range: %.1f to %.1f dB in total\n", minTotal, maxTotal);
    check (minTotal > 9.0 && maxTotal < 25.0 && containsTight, "wide range is at least the typical target width and contains the songs' range");
    check (wideT.regions[1][1] / wideT.regions[1][0] > std::pow (10.0, (tonal::kWideBandBelowDb[1] + tonal::kWideBandAboveDb[1] - 0.01) / 20.0),
           "wide band ranges");

    // 6. one song: still a usable target with a minimum range
    const auto one = tonal::combine ({ loud });
    check (one.tracks == 1 && one.high[500] - one.low[500] >= 4.0 - 1e-9, "single-song target gets a +/-2 dB range");
    const auto json = tonal::toJson (one, "Test \"quoted\"", { "a.wav" });
    check (json.find ("\"region_3_frequencies\": [8000, 22050]") != std::string::npos && json.find ("Test \\\"quoted\\\"") != std::string::npos,
           "JSON has the target file layout");

    // 7. sample rate: the same music at 44.1 / 48 / 96 kHz measures the same, and content above
    //    22.05 kHz (only possible at 96 kHz) does not fold back into the measurement
    {
        std::mt19937 rng (42);
        std::uniform_real_distribution<double> uf (30.0, 19000.0), ua (0.01, 1.0), up (0.0, 6.283);
        struct Tone { double f, a, p; };
        std::vector<Tone> tones;
        for (int i = 0; i < 150; ++i) { const double f = std::exp (std::log (30.0) + (std::log (19000.0) - std::log (30.0)) * (i + 0.5) / 150); tones.push_back ({ f, ua (rng) / std::sqrt (f / 30), up (rng) }); }
        (void) uf;
        auto render = [&] (double sr, bool ultrasonic)
        {
            std::vector<float> x ((size_t) (sr * 6));
            for (size_t n = 0; n < x.size(); ++n)
            {
                double v = 0; const double t = (double) n / sr;
                for (auto& tn : tones) v += tn.a * std::sin (6.283185307 * tn.f * t + tn.p);
                if (ultrasonic) v += 3.0 * std::sin (6.283185307 * 30000.0 * t);
                x[n] = (float) (0.01 * v);
            }
            tonal::SpectrumAccumulator acc (sr);
            acc.process (x.data(), (int) x.size());
            return tonal::smoothDb (acc.normalizedDb());
        };
        const auto a44 = render (44100, false), a48 = render (48000, false), a96 = render (96000, false), u96 = render (96000, true);
        const double d48 = rmsDiff (a44, a48), d96 = rmsDiff (a44, a96), dUltra = rmsDiff (a96, u96);
        std::printf ("      44.1 vs 48 kHz %.3f dB, vs 96 kHz %.3f dB; a loud 30 kHz tone changes the 96 kHz curve by %.3f dB\n", d48, d96, dUltra);
        check (d48 < 0.3 && d96 < 0.3, "same result at 44.1, 48 and 96 kHz");
        check (dUltra < 0.05, "ultrasonic content doesn't alias into the measurement");
    }

    std::printf (fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
