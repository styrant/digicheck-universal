// Key finder tests: KeyNet against PyTorch, the streaming CQT against librosa, and end-to-end key
// predictions at 48 kHz against the original Python pipeline (reference files made by tools/*.py).
//   key_test <Resources/keynet.bin> <tests/data>
#include "../Source/key/KeyNet.h"
#include "../Source/key/KeyCqt.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

static std::vector<char> readFile (const std::string& p)
{
    std::ifstream f (p, std::ios::binary);
    return { std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>() };
}

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; }

// same signal as tools/make_key_test_data.py test_signal()
static std::vector<float> testSignal (double sr, int root, bool minor, double secs = 24.0)
{
    const int scaleMaj[7] = { 0, 2, 4, 5, 7, 9, 11 }, scaleMin[7] = { 0, 2, 3, 5, 7, 8, 11 };
    const int degMaj[4] = { 0, 5, 3, 4 }, degMin[8] = { 0, 3, 4, 0, 5, 3, 4, 0 };
    const int* scale = minor ? scaleMin : scaleMaj;
    const int n = (int) (sr * secs);
    std::vector<float> x ((size_t) n);
    for (int s = 0; s < n; ++s)
    {
        const double t = s / sr;
        const int i = (int) (t / 2);
        const int d = minor ? degMin[i % 8] : degMaj[i % 4];
        const double tt = t - 2 * i, env = std::exp (-tt * 0.7);
        int notes[3];
        for (int j = 0; j < 3; ++j) notes[j] = scale[(d + 2 * j) % 7] + 12 * ((d + 2 * j) / 7);
        double y = 0;
        for (int v = 0; v < 4; ++v)
        {
            const int note = v == 0 ? notes[0] - 12 : notes[v - 1];
            const double gain = v == 0 ? 0.25 : 0.15;
            const double f = 220.0 * std::pow (2.0, (root + note - 9) / 12.0);
            for (int h = 0; h < 6; ++h) y += gain * std::pow (0.6, h) * std::sin (2 * 3.14159265358979323846 * f * (h + 1) * tt + 0.7 * h);
        }
        x[(size_t) s] = (float) (0.25 * y * env);
    }
    return x;
}

int main (int argc, char** argv)
{
    const std::string model = argc > 1 ? argv[1] : "Resources/keynet.bin", dir = argc > 2 ? argv[2] : "tests/data";
    KeyNet net;
    const auto blob = readFile (model);
    std::string err;
    check (net.load (blob.data(), blob.size(), err), ("load model " + err).c_str());
    if (! net.isLoaded()) return 1;

    // 1. network vs PyTorch on a fixed input
    {
        const auto ref = readFile (dir + "/keynet_ref.bin");
        int H, T; std::memcpy (&H, ref.data(), 4); std::memcpy (&T, ref.data() + 4, 4);
        const float* x = reinterpret_cast<const float*> (ref.data() + 8);
        const float* y = x + (size_t) H * T;
        const auto t0 = std::chrono::steady_clock::now();
        const auto z = net.run (x, H, T);
        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
        float md = 0; int am = 0, bm = 0;
        for (int i = 0; i < 24; ++i) { md = std::max (md, std::abs (z[(size_t) i] - y[i])); if (z[(size_t) i] > z[(size_t) am]) am = i; if (y[i] > y[bm]) bm = i; }
        std::printf ("      logits max diff %.5f, %d frames in %.0f ms\n", md, T, ms);
        check (md < 0.01f && am == bm, "KeyNet matches PyTorch");

        std::vector<float> big ((size_t) 105 * 100, 0.3f);
        const auto t1 = std::chrono::steady_clock::now();
        net.run (big.data(), 105, 100);
        std::printf ("      20 s window: %.0f ms\n", std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t1).count());
    }

    // 2. labels
    check (KeyNet::keyName (7) == "A minor" && KeyNet::camelot (7) == "8A" && KeyNet::openKey (7) == "1m", "labels 8A = A minor = 1m");
    check (KeyNet::keyName (19) == "C major" && KeyNet::camelot (19) == "8B" && KeyNet::openKey (19) == "1d", "labels 8B = C major = 1d");
    check (KeyNet::keyName (13) == "F# major" && KeyNet::openKey (13) == "7d", "labels 2B = F# major = 7d");

    // 3. CQT vs librosa (44.1 kHz) and keys at 44.1 / 48 / 96 kHz
    const auto ref = readFile (dir + "/key_cqt_ref.bin");
    unsigned cases; std::memcpy (&cases, ref.data(), 4);
    size_t off = 4;
    for (unsigned c = 0; c < cases; ++c)
    {
        int root, minor, cls; unsigned T;
        std::memcpy (&root, ref.data() + off, 4); std::memcpy (&minor, ref.data() + off + 4, 4);
        std::memcpy (&cls, ref.data() + off + 8, 4); std::memcpy (&T, ref.data() + off + 12, 4);
        const float* C = reinterpret_cast<const float*> (ref.data() + off + 16);
        off += 16 + (size_t) 105 * T * 4;

        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            KeyCqt q; q.configure (sr);
            std::vector<std::vector<float>> frames;
            auto x = testSignal (sr, root, minor != 0);
            x.resize (x.size() + (size_t) (sr * 0.6), 0.0f);           // trailing silence, as librosa pads
            for (size_t p = 0; p < x.size(); p += 480)
                q.process (x.data() + p, (int) std::min<size_t> (480, x.size() - p), [&] (const float* f, float) { frames.emplace_back (f, f + 105); });
            const unsigned n = std::min<unsigned> ((unsigned) frames.size(), T);
            double sum = 0, mx = 0; int cnt = 0;
            for (unsigned t = 3; t + 3 < n; ++t)
                for (int b = 0; b < 105; ++b)
                {
                    const double d = std::abs (frames[t][(size_t) b] - C[(size_t) b * T + t]);
                    sum += d; mx = std::max (mx, d); ++cnt;
                }
            std::vector<float> spec ((size_t) 105 * (T - 2));
            for (unsigned t = 0; t < T - 2 && t < frames.size(); ++t)
                for (int b = 0; b < 105; ++b) spec[(size_t) b * (T - 2) + t] = frames[t][(size_t) b];
            const auto z = net.run (spec.data(), 105, (int) T - 2);
            int am = 0; for (int i = 1; i < 24; ++i) if (z[(size_t) i] > z[(size_t) am]) am = i;
            char msg[160];
            std::snprintf (msg, sizeof msg, "%5.1f kHz %-8s CQT mean %.3f max %.3f, predicted %s (python: %s)", sr / 1000, KeyNet::keyName (cls).c_str(),
                           sum / cnt, mx, KeyNet::keyName (am).c_str(), KeyNet::keyName (cls).c_str());
            check (sum / cnt < 0.03 && mx < 0.25 && am == cls, msg);
        }
    }
    std::printf (fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
