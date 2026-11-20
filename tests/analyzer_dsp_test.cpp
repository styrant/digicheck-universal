// Standalone tests for the analyzer core (no JUCE). Run via CTest.
#include "../Source/analyzer/dsp/FilterBank.h"
#include "../Source/analyzer/dsp/AnalyzerModel.h"
#include "../Source/analyzer/dsp/LegacyPresets.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; std::printf ("FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

using namespace sa;

static void testLayouts()
{
    auto L = std::make_unique<BandLayout>();
    for (double fs : { 8000.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0, 384000.0 })
        for (int bpo : kBandsPerOctaveChoices)
            for (auto anchor : { Anchor::Iso1k, Anchor::A440 })
            {
                computeLayout (*L, { fs, bpo, anchor });
                CHECK (L->numBands > 0 && L->numBands <= kMaxBands, "fs %g bpo %d: %d bands", fs, bpo, L->numBands);
                CHECK (L->freqMax < 0.5 * fs, "fs %g bpo %d: top band %g above Nyquist", fs, bpo, L->freqMax);
                for (int b = 0; b < L->numBands; ++b)
                {
                    CHECK (std::isfinite (L->emphasis[(size_t) b]) && L->emphasis[(size_t) b] > 0.5 && L->emphasis[(size_t) b] < 8.0,
                           "fs %g bpo %d band %d emphasis %g", fs, bpo, b, L->emphasis[(size_t) b]);
                    // stable poles: |k2| < 1 and |k1| < 1 - k2
                    CHECK (std::abs (L->k2[(size_t) b]) < 1.0 && std::abs (L->k1[(size_t) b]) < 1.0 - L->k2[(size_t) b],
                           "unstable filter fs %g bpo %d band %d", fs, bpo, b);
                }
            }

    // 12 bpo anchored at A440 gives semitone centres
    computeLayout (*L, { 48000.0, 12, Anchor::A440 });
    bool found440 = false;
    for (int b = 0; b < L->numBands; ++b)
        if (std::abs (L->centre[(size_t) b] - 440.0) < 1e-6) found440 = true;
    CHECK (found440, "A440 anchor has no 440 Hz band");
}

static double runSine (FilterBank& fb, double fs, double freq, double amp, double seconds, int& argmax, double& readingDb, ChannelMode mode = ChannelMode::Left)
{
    const int block = 256;
    std::vector<float> l (block), r (block);
    const float* ch[2] = { l.data(), r.data() };
    auto snap = std::make_unique<Snapshot>();
    const int64_t total = (int64_t) (seconds * fs);
    double phase = 0.0, dphi = 2.0 * detail::pi * freq / fs;
    for (int64_t done = 0; done < total; done += block)
    {
        for (int i = 0; i < block; ++i) { l[(size_t) i] = r[(size_t) i] = (float) (amp * std::sin (phase)); phase += dphi; }
        fb.process (ch, 2, block, mode);
        if (fb.accumulatedSamples() >= (int64_t) (fs / 10))
            fb.takeSnapshot (*snap);
    }
    argmax = 0;
    for (int b = 1; b < snap->numBands; ++b)
        if (snap->peak[(size_t) b] > snap->peak[(size_t) argmax]) argmax = b;
    readingDb = 10.0 * std::log10 (snap->peak[(size_t) argmax] + 1e-30);
    return readingDb;
}

static void testSineCalibration()
{
    auto fb = std::make_unique<FilterBank>();
    for (double fs : { 44100.0, 48000.0, 96000.0 })
        for (int bpo : { 3, 6, 12, 24 })
        {
            fb->configure ({ fs, bpo, Anchor::Iso1k });
            const auto& L = fb->getLayout();
            for (int b : { 3, L.numBands / 2, L.numBands - 4 })
            {
                int am = -1; double db = 0;
                fb->reset();
                runSine (*fb, fs, L.centre[(size_t) b], 1.0, 3.0, am, db);
                CHECK (am == b, "fs %g bpo %d: sine at band %d (%g Hz) peaked in band %d", fs, bpo, b, L.centre[(size_t) b], am);
                CHECK (std::abs (db) < 0.6, "fs %g bpo %d band %d: 0 dBFS sine reads %.2f dB", fs, bpo, b, db);
            }
            // -40 dBFS sine reads -40 dB
            int am; double db;
            fb->reset();
            runSine (*fb, fs, 1000.0, 0.01, 1.0, am, db);
            CHECK (std::abs (db + 40.0) < 0.8, "fs %g bpo %d: -40 dBFS 1 kHz reads %.2f", fs, bpo, db);
        }

    // Side of identical channels is silent; Mid equals input
    fb->configure ({ 48000.0, 6, Anchor::Iso1k });
    int am; double db;
    runSine (*fb, 48000.0, 1000.0, 1.0, 1.0, am, db, ChannelMode::Side);
    CHECK (db < -150.0, "side of mono signal reads %.1f dB", db);
    fb->reset();
    runSine (*fb, 48000.0, 1000.0, 1.0, 1.0, am, db, ChannelMode::Mid);
    CHECK (std::abs (db) < 0.6, "mid reads %.2f dB", db);
}

static void testModel()
{
    auto fb = std::make_unique<FilterBank>();
    auto model = std::make_unique<AnalyzerModel>();
    auto snap = std::make_unique<Snapshot>();
    const double fs = 48000.0;
    fb->configure ({ fs, 12, Anchor::Iso1k });
    model->setLayout (fb->getLayout());

    MeterOptions o;
    o.avgTimeMs = 1000.0;

    const int block = 480; // 10 ms
    std::vector<float> l (block);
    const float* ch[1] = { l.data() };
    double phase = 0.0, dphi = 2.0 * detail::pi * 1000.0 / fs;
    for (int i = 0; i < 300; ++i) // 3 s, UI ticks every 10 ms
    {
        for (auto& s : l) { s = (float) std::sin (phase); phase += dphi; }
        fb->process (ch, 1, block, ChannelMode::Left);
        fb->takeSnapshot (*snap);
        model->push (*snap);
        model->tick (10.0, o);
    }
    int b1k = 0;
    for (int b = 0; b < model->numBands; ++b) if (std::abs (model->centre[(size_t) b] - 1000.0) < 1e-6) b1k = b;
    CHECK (std::abs (model->peak[(size_t) b1k]) < 0.6, "model peak %.2f", model->peak[(size_t) b1k]);
    CHECK (std::abs (model->avg[(size_t) b1k]) < 0.6, "model avg (RMS+3dB) %.2f", model->avg[(size_t) b1k]);
    CHECK (std::abs (model->hold[(size_t) b1k]) < 0.6, "model hold %.2f", model->hold[(size_t) b1k]);

    // transport stops: no more snapshots -> peak decays, average empties
    for (int i = 0; i < 200; ++i) model->tick (10.0, o); // 2 s
    CHECK (model->peak[(size_t) b1k] < -25.0, "peak did not decay without input: %.2f", model->peak[(size_t) b1k]);
    CHECK (model->avg[(size_t) b1k] <= AnalyzerModel::kFloorDb + 1, "average did not empty: %.2f", model->avg[(size_t) b1k]);
}

static void testRing()
{
    SpscRing<int, 4> ring;
    for (int i = 0; i < 4; ++i) { auto* w = ring.beginWrite(); CHECK (w != nullptr, "ring full early"); *w = i; ring.commitWrite(); }
    CHECK (ring.beginWrite() == nullptr, "ring should be full");
    for (int i = 0; i < 4; ++i) { auto* r = ring.beginRead(); CHECK (r && *r == i, "ring order"); ring.commitRead(); }
    CHECK (ring.beginRead() == nullptr, "ring should be empty");
}

static void putLE (std::vector<uint8_t>& v, int32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((uint8_t) ((uint32_t) x >> (8 * i))); }
static void putBE (std::vector<uint8_t>& v, int32_t x) { for (int i = 3; i >= 0; --i) v.push_back ((uint8_t) ((uint32_t) x >> (8 * i))); }

static void testLegacy()
{
    using namespace sa::legacy;
    // v2 preset chunk
    std::vector<uint8_t> chunk;
    putLE (chunk, 2); putLE (chunk, 800); putLE (chunk, 300); putLE (chunk, 0); putLE (chunk, 0);
    for (int i = 0; i < Count; ++i) putLE (chunk, i == bandsPerOctave ? 1 : defaults[(size_t) i]);
    auto p = parseChunk (chunk.data(), chunk.size());
    CHECK (p && p->values[bandsPerOctave] == 1 && p->width == 800, "v2 preset chunk");

    // bank chunk (9 presets)
    std::vector<uint8_t> bank;
    putLE (bank, 9); putLE (bank, 4);
    for (int i = 0; i < 9; ++i) { char name[28] = {}; std::snprintf (name, sizeof name, "Preset %d", i); bank.insert (bank.end(), name, name + 28); }
    for (int i = 0; i < 9; ++i) { putLE (bank, 2); putLE (bank, 653); putLE (bank, 261); putLE (bank, 0); putLE (bank, 0);
                                  for (int j = 0; j < Count; ++j) putLE (bank, j == levelRange ? 60 + i : defaults[(size_t) j]); }
    auto b = parseChunk (bank.data(), bank.size());
    CHECK (b && b->name == "Preset 4" && b->values[levelRange] == 64, "bank chunk");

    // fxp wrapper
    std::vector<uint8_t> fxp;
    fxp.insert (fxp.end(), { 'C','c','n','K' }); putBE (fxp, 0); fxp.insert (fxp.end(), { 'F','P','C','h' });
    putBE (fxp, 1); putBE (fxp, 0x53506841); putBE (fxp, 1009); putBE (fxp, 1);
    char nm[28] = "My Old Preset"; fxp.insert (fxp.end(), nm, nm + 28);
    putBE (fxp, (int32_t) chunk.size()); fxp.insert (fxp.end(), chunk.begin(), chunk.end());
    auto f = parseFxFile (fxp.data(), fxp.size());
    CHECK (f && f->name == "My Old Preset" && f->values[bandsPerOctave] == 1, "fxp import");

    // malformed input must be rejected, never crash (original setChunk divided by zero here)
    std::vector<uint8_t> evil;
    putLE (evil, 0); putLE (evil, 0); for (int i = 0; i < 30; ++i) putLE (evil, 7);
    CHECK (! parseChunk (evil.data(), evil.size(), true), "count == 0 bank accepted");
    std::vector<uint8_t> huge;
    putLE (huge, 100000); putLE (huge, 5); for (int i = 0; i < 30; ++i) putLE (huge, 7);
    CHECK (! parseChunk (huge.data(), huge.size(), true), "huge bank accepted");
    CHECK (! parseChunk (chunk.data(), 10), "truncated chunk accepted");
    CHECK (! parseFxFile (fxp.data(), 70), "truncated fxp accepted");
    std::vector<uint8_t> badVer = chunk; badVer[0] = 77;
    CHECK (! parseChunk (badVer.data(), badVer.size(), false), "unknown version accepted");
}

static void benchmark()
{
    auto fb = std::make_unique<FilterBank>();
    const double fs = 96000.0;
    fb->configure ({ fs, 24, Anchor::Iso1k });
    const int block = 512;
    std::vector<float> l (block, 0.1f), r (block, -0.1f);
    for (int i = 0; i < block; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (i * 0.1);
    const float* ch[2] = { l.data(), r.data() };
    const int blocks = (int) (2.0 * fs / block); // 2 s of audio
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < blocks; ++i) fb->process (ch, 2, block, ChannelMode::Both);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::printf ("bench: 96 kHz stereo, 24 bpo (%d bands): %.1f%% of one core\n", fb->getLayout().numBands, 100.0 * secs / 2.0);
}

int main()
{
    testLayouts();
    testSineCalibration();
    testModel();
    testRing();
    testLegacy();
    benchmark();
    std::printf ("%s (%d failures)\n", failures ? "FAILED" : "all tests passed", failures);
    return failures ? 1 : 0;
}
