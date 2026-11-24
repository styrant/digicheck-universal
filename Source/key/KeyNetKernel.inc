// KeyNet's hot loop ('same' convolution as im2col + blocked GEMM, then ELU), compiled twice: once for any
// x86-64 CPU and once with AVX2 + FMA (KeyNetAvx2.cpp). KeyNet::convElu picks one at run time, so CPUs
// without AVX2 (older and budget ones) don't crash. Included with KEYNET_KERNEL set to the function name.
// in: [cin][H][W], out: [cout][H][W] (sized by the caller), col: cin*k*k*256 floats of scratch.
#include <algorithm>
#include <cmath>

void KEYNET_KERNEL (const float* w, const float* bias, int cout, int cin, int k_, const float* in, int H, int W, float* out, float* col)
{
    const int k = k_, pad = k / 2, P = H * W, K = cin * k * k;
    constexpr int kBlock = 256;
    std::fill (out, out + (size_t) cout * P, 0.0f);

    for (int p0 = 0; p0 < P; p0 += kBlock)
    {
        const int n = std::min (kBlock, P - p0);
        // im2col for positions p0 .. p0 + n: col[kk][i]
        for (int c = 0; c < cin; ++c)
        {
            const float* src = in + (size_t) c * P;
            for (int dy = 0; dy < k; ++dy)
                for (int dx = 0; dx < k; ++dx)
                {
                    float* dst = col + (size_t) ((c * k + dy) * k + dx) * kBlock;
                    int y = p0 / W, x = p0 % W;
                    for (int i = 0; i < n; ++i)
                    {
                        const int sy = y + dy - pad, sx = x + dx - pad;
                        dst[i] = (sy >= 0 && sy < H && sx >= 0 && sx < W) ? src[sy * W + sx] : 0.0f;
                        if (++x == W) { x = 0; ++y; }
                    }
                }
        }

        // GEMM out[cout][block] = W[cout][K] x col[K][block], 4 output channels at a time, so each
        // loaded column value feeds four multiply-adds (the compiler vectorises the inner loop)
        int o = 0;
        for (; o + 4 <= cout; o += 4)
        {
            float* __restrict d0 = out + (size_t) o * P + p0;
            float* __restrict d1 = d0 + P;
            float* __restrict d2 = d1 + P;
            float* __restrict d3 = d2 + P;
            const float* w0 = w + (size_t) o * K;
            const float* w1 = w0 + K;
            const float* w2 = w1 + K;
            const float* w3 = w2 + K;
            for (int kk = 0; kk < K; ++kk)
            {
                const float a0 = w0[kk], a1 = w1[kk], a2 = w2[kk], a3 = w3[kk];
                const float* __restrict src = col + (size_t) kk * kBlock;
                for (int i = 0; i < n; ++i)
                {
                    const float v = src[i];
                    d0[i] += a0 * v; d1[i] += a1 * v; d2[i] += a2 * v; d3[i] += a3 * v;
                }
            }
        }
        for (; o < cout; ++o)
        {
            float* __restrict dst = out + (size_t) o * P + p0;
            const float* wrow = w + (size_t) o * K;
            for (int kk = 0; kk < K; ++kk)
            {
                const float w = wrow[kk];
                const float* __restrict src = col + (size_t) kk * kBlock;
                for (int i = 0; i < n; ++i) dst[i] += w * src[i];
            }
        }
        for (o = 0; o < cout; ++o)
        {
            float* dst = out + (size_t) o * P + p0;
            const float b = bias[o];
            for (int i = 0; i < n; ++i) { const float v = dst[i] + b; dst[i] = v > 0.0f ? v : std::expm1 (v); }
        }
    }
}
