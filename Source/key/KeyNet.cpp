#include "KeyNet.h"
#if defined (_MSC_VER)
 #include <intrin.h>
#elif defined (__x86_64__) || defined (__i386__)
 #include <cpuid.h>
#endif
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>

namespace
{
    float halfToFloat (uint16_t h) noexcept
    {
        const uint32_t sign = (uint32_t) (h & 0x8000u) << 16;
        uint32_t exp = (h >> 10) & 0x1fu, man = h & 0x3ffu, bits;
        if (exp == 0)
        {
            if (man == 0) bits = sign;
            else                                   // subnormal: normalise
            {
                int e = -1;
                do { ++e; man <<= 1; } while ((man & 0x400u) == 0);
                bits = sign | ((uint32_t) (127 - 15 - e) << 23) | ((man & 0x3ffu) << 13);
            }
        }
        else if (exp == 31) bits = sign | 0x7f800000u | (man << 13);
        else bits = sign | ((exp + 127 - 15) << 23) | (man << 13);
        float f; std::memcpy (&f, &bits, 4); return f;
    }

    template <typename T> bool readVal (const uint8_t*& p, const uint8_t* end, T& v)
    {
        if (end - p < (ptrdiff_t) sizeof (T)) return false;
        std::memcpy (&v, p, sizeof (T)); p += sizeof (T); return true;
    }
}

bool KeyNet::load (const void* data, size_t size, std::string& error)
{
    layers.clear();
    auto* p = static_cast<const uint8_t*> (data);
    const uint8_t* end = p + size;
    if (size < 12 || std::memcmp (p, "KNET", 4) != 0) { error = "not a key model"; return false; }
    p += 4;
    uint32_t version = 0, count = 0;
    readVal (p, end, version); readVal (p, end, count);
    if (version != 1 || count != 9) { error = "unsupported key model version"; return false; }

    for (uint32_t i = 0; i < count; ++i)
    {
        Layer L; uint32_t co, ci, k;
        if (! readVal (p, end, co) || ! readVal (p, end, ci) || ! readVal (p, end, k)) { error = "truncated key model"; return false; }
        L.cout = (int) co; L.cin = (int) ci; L.k = (int) k;
        const size_t nw = (size_t) co * ci * k * k;
        if ((size_t) (end - p) < nw * 2 + (size_t) co * 4) { error = "truncated key model"; return false; }
        L.w.resize (nw);
        for (size_t j = 0; j < nw; ++j) { uint16_t h; std::memcpy (&h, p + j * 2, 2); L.w[j] = halfToFloat (h); }
        p += nw * 2;
        L.b.resize (co);
        std::memcpy (L.b.data(), p, (size_t) co * 4); p += (size_t) co * 4;
        layers.push_back (std::move (L));
    }
    return true;
}

namespace keynet_kernels
{
    void convEluBase (const float*, const float*, int, int, int, const float*, int, int, float*, float*);
    void convEluAvx2 (const float*, const float*, int, int, int, const float*, int, int, float*, float*);
}

// AVX2 and FMA in the CPU, and the OS saving the YMM registers (XGETBV)
static bool cpuHasAvx2Fma()
{
#if defined (_M_X64) || defined (__x86_64__)
    unsigned a = 0, b = 0, c = 0, d = 0;
   #if defined (_MSC_VER)
    int r[4];
    __cpuid (r, 1); c = (unsigned) r[2];
    const bool osxsave = (c >> 27) & 1, avx = (c >> 28) & 1, fma = (c >> 12) & 1;
    if (! (osxsave && avx && fma)) return false;
    if ((_xgetbv (0) & 6) != 6) return false;
    __cpuidex (r, 7, 0); b = (unsigned) r[1];
   #else
    __cpuid (1, a, b, c, d);
    const bool osxsave = (c >> 27) & 1, avx = (c >> 28) & 1, fma = (c >> 12) & 1;
    if (! (osxsave && avx && fma)) return false;
    unsigned lo, hi;
    __asm__ volatile ("xgetbv" : "=a" (lo), "=d" (hi) : "c" (0));
    if ((lo & 6) != 6) return false;
    __cpuid_count (7, 0, a, b, c, d);
   #endif
    return (b >> 5) & 1;                         // AVX2
#else
    return false;
#endif
}

// 'same' convolution + ELU (KeyNetKernel.inc): the AVX2 build where the CPU has it
void KeyNet::convElu (const Layer& L, const std::vector<float>& in, int H, int W, std::vector<float>& out,
                      std::vector<float>& col)
{
    static const bool avx2 = cpuHasAvx2Fma() && std::getenv ("KEYNET_NO_AVX2") == nullptr;   // the env var: tests of the plain build
    col.resize ((size_t) L.cin * L.k * L.k * 256);
    out.resize ((size_t) L.cout * H * W);
    (avx2 ? keynet_kernels::convEluAvx2 : keynet_kernels::convEluBase)
        (L.w.data(), L.b.data(), L.cout, L.cin, L.k, in.data(), H, W, out.data(), col.data());
}

void KeyNet::maxPool (const std::vector<float>& in, int C, int H, int W, std::vector<float>& out)
{
    const int h = H / 2, w = W / 2;
    out.assign ((size_t) C * h * w, 0.0f);
    for (int c = 0; c < C; ++c)
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const float* s = in.data() + ((size_t) c * H + 2 * y) * W + 2 * x;
                out[((size_t) c * h + y) * w + x] = std::max (std::max (s[0], s[1]), std::max (s[W], s[W + 1]));
            }
}

std::vector<float> KeyNet::run (const float* spec, int bins, int frames) const
{
    if (layers.empty() || bins <= 0 || frames < 8) return {};
    std::vector<float> a (spec, spec + (size_t) bins * frames), b, col;
    int H = bins, W = frames;
    for (size_t i = 0; i < layers.size(); ++i)
    {
        convElu (layers[i], a, H, W, b, col);
        a.swap (b);
        if (i == 1 || i == 3 || i == 5) { maxPool (a, layers[i].cout, H, W, b); a.swap (b); H /= 2; W /= 2; }
    }
    const int C = layers.back().cout, P = H * W;
    std::vector<float> logits ((size_t) C, 0.0f);
    for (int c = 0; c < C; ++c)
    {
        double s = 0;
        for (int i = 0; i < P; ++i) s += a[(size_t) c * P + i];
        logits[(size_t) c] = (float) (s / std::max (1, P));
    }
    return logits;
}

std::vector<float> KeyNet::runLong (const float* spec, int bins, int frames, int chunk,
                                   const std::function<bool (double)>& progress) const
{
    if (frames <= chunk) { auto z = run (spec, bins, frames); if (progress) progress (1.0); return z; }
    const int n = (frames + chunk - 1) / chunk, len = (frames + n - 1) / n;      // equal-sized chunks
    std::vector<double> sum (24, 0.0);
    double weight = 0;
    std::vector<float> part;
    for (int c = 0; c < n; ++c)
    {
        const int t0 = c * len, t1 = std::min (frames, t0 + len), T = t1 - t0;
        if (T < 8) break;
        part.resize ((size_t) bins * T);
        for (int b = 0; b < bins; ++b) std::copy (spec + (size_t) b * frames + t0, spec + (size_t) b * frames + t1, part.data() + (size_t) b * T);
        const auto z = run (part.data(), bins, T);
        const double w = (double) (bins / 8) * (T / 8);
        for (size_t i = 0; i < z.size() && i < 24; ++i) sum[i] += w * z[i];
        weight += w;
        if (progress && ! progress ((double) t1 / frames)) return {};
    }
    std::vector<float> out (24);
    for (int i = 0; i < 24; ++i) out[(size_t) i] = (float) (sum[(size_t) i] / std::max (1e-9, weight));
    return out;
}

std::vector<float> KeyNet::softmax (const std::vector<float>& z)
{
    std::vector<float> p (z.size());
    if (z.empty()) return p;
    const float m = *std::max_element (z.begin(), z.end());
    double s = 0;
    for (size_t i = 0; i < z.size(); ++i) { p[i] = std::exp (z[i] - m); s += p[i]; }
    for (auto& v : p) v = (float) (v / s);
    return p;
}

// Camelot 1A..12A = G#m D#m A#m Fm Cm Gm Dm Am Em Bm F#m C#m ; 1B..12B = B F# C# G# D# A# F C G D A E
int KeyNet::tonicPitchClass (int cls) noexcept
{
    static const int minor[12] = { 8, 3, 10, 5, 0, 7, 2, 9, 4, 11, 6, 1 };
    static const int major[12] = { 11, 6, 1, 8, 3, 10, 5, 0, 7, 2, 9, 4 };
    cls = std::clamp (cls, 0, 23);
    return cls < 12 ? minor[cls] : major[cls - 12];
}

std::string KeyNet::keyName (int cls)
{
    static const char* names[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string (names[tonicPitchClass (cls)]) + (isMajor (cls) ? " major" : " minor");
}

std::string KeyNet::camelot (int cls)
{
    cls = std::clamp (cls, 0, 23);
    return std::to_string (cls % 12 + 1) + (cls < 12 ? "A" : "B");
}

std::string KeyNet::openKey (int cls)
{
    cls = std::clamp (cls, 0, 23);
    const int n = ((cls % 12 + 1) - 8 + 12) % 12 + 1;
    return std::to_string (n) + (cls < 12 ? "m" : "d");
}
