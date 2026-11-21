#include "KeyNet.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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

// 'same' convolution as im2col + blocked GEMM, then ELU.  in: [cin][H][W], out: [cout][H][W]
void KeyNet::convElu (const Layer& L, const std::vector<float>& in, int H, int W, std::vector<float>& out,
                      std::vector<float>& col)
{
    const int k = L.k, pad = k / 2, P = H * W, K = L.cin * k * k;
    col.assign ((size_t) K * P, 0.0f);
    for (int c = 0; c < L.cin; ++c)
        for (int dy = 0; dy < k; ++dy)
            for (int dx = 0; dx < k; ++dx)
            {
                float* dst = col.data() + (size_t) ((c * k + dy) * k + dx) * P;
                const float* src = in.data() + (size_t) c * P;
                for (int y = 0; y < H; ++y)
                {
                    const int sy = y + dy - pad;
                    if (sy < 0 || sy >= H) continue;
                    const int x0 = std::max (0, pad - dx), x1 = std::min (W, W + pad - dx);
                    std::memcpy (dst + y * W + x0, src + sy * W + x0 + dx - pad, sizeof (float) * (size_t) std::max (0, x1 - x0));
                }
            }

    out.assign ((size_t) L.cout * P, 0.0f);
    // GEMM out[cout][P] = W[cout][K] x col[K][P]: blocks of 256 columns, 4 output channels at a time,
    // so each loaded column value feeds four multiply-adds (the compiler vectorises the inner loop)
    constexpr int kBlock = 256;
    for (int p0 = 0; p0 < P; p0 += kBlock)
    {
        const int n = std::min (kBlock, P - p0);
        int o = 0;
        for (; o + 4 <= L.cout; o += 4)
        {
            float* __restrict d0 = out.data() + (size_t) o * P + p0;
            float* __restrict d1 = d0 + P;
            float* __restrict d2 = d1 + P;
            float* __restrict d3 = d2 + P;
            const float* w0 = L.w.data() + (size_t) o * K;
            const float* w1 = w0 + K;
            const float* w2 = w1 + K;
            const float* w3 = w2 + K;
            for (int kk = 0; kk < K; ++kk)
            {
                const float a0 = w0[kk], a1 = w1[kk], a2 = w2[kk], a3 = w3[kk];
                const float* __restrict src = col.data() + (size_t) kk * P + p0;
                for (int i = 0; i < n; ++i)
                {
                    const float v = src[i];
                    d0[i] += a0 * v; d1[i] += a1 * v; d2[i] += a2 * v; d3[i] += a3 * v;
                }
            }
        }
        for (; o < L.cout; ++o)
        {
            float* __restrict dst = out.data() + (size_t) o * P + p0;
            const float* wrow = L.w.data() + (size_t) o * K;
            for (int kk = 0; kk < K; ++kk)
            {
                const float w = wrow[kk];
                const float* __restrict src = col.data() + (size_t) kk * P + p0;
                for (int i = 0; i < n; ++i) dst[i] += w * src[i];
            }
        }
        for (o = 0; o < L.cout; ++o)
        {
            float* dst = out.data() + (size_t) o * P + p0;
            const float b = L.b[(size_t) o];
            for (int i = 0; i < n; ++i) { const float v = dst[i] + b; dst[i] = v > 0.0f ? v : std::expm1 (v); }
        }
    }
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
