#include "Reassigner.h"
#include <algorithm>
#include <limits>

static constexpr double kPi = 3.14159265358979323846;

void Reassigner::configure (const Config& c)
{
    cfg = c;
    dec = decimationFor (c.sampleRate);              // keep CPU constant above 48 kHz (as DigiCheck does)
    srEff = c.sampleRate / dec;

    if (dec > 1)
    {
        firLen = 48 * dec + 1;
        const double M = (firLen - 1) / 2.0, fc = 0.5 / dec * 0.92;
        fir.assign ((size_t) firLen, 0.0);
        double sum = 0;
        for (int n = 0; n < firLen; ++n)
        {
            const double x = n - M;
            const double sinc = 2 * n == firLen - 1 ? 2 * fc : std::sin (2 * kPi * fc * x) / (kPi * x);
            const double w = 0.42 - 0.5 * std::cos (2 * kPi * n / (firLen - 1)) + 0.08 * std::cos (4 * kPi * n / (firLen - 1));
            fir[(size_t) n] = sinc * w;
            sum += fir[(size_t) n];
        }
        for (auto& v : fir) v /= sum;
        firHist.assign ((size_t) firLen * 2, 0.0);
        firPos = decCnt = 0;
    }

    N   = c.fftSize;
    hop = std::max (1, (int) std::lround (srEff / c.pixelsPerSecond));
    H   = std::max (1, c.rows);
    fmaxEff = std::min (c.fmax, srEff / 2 * 0.999);

    win.assign ((size_t) N, 0); wt.assign ((size_t) N, 0); wd.assign ((size_t) N, 0);
    double sw = 0, sw2 = 0;
    for (int n = 0; n < N; ++n)
    {
        win[(size_t) n] = 0.5 - 0.5 * std::cos (2 * kPi * n / N);
        wt[(size_t) n]  = (n - N / 2) * win[(size_t) n];
        wd[(size_t) n]  = (kPi / N) * std::sin (2 * kPi * n / N);
        sw += win[(size_t) n]; sw2 += win[(size_t) n] * win[(size_t) n];
    }
    normB = (sw / 2) * (sw / 2);    // full-scale sine on a bin centre -> 0 dB (plain FFT)
    normR = (N / 4.0) * sw2;        // total one-sided energy of a full-scale sine -> 0 dB (reassigned)

    maxShift = c.shortRange ? N / 4.0 : N / 2.0;
    S = c.reassign ? (int) std::ceil (maxShift / hop) + 1 : 0;
    C = 2 * S + 3;
    acc.assign ((size_t) C, std::vector<double> ((size_t) H, 0.0));

    kmin = std::max (1, (int) std::floor (c.fmin * N / srEff) - 2);
    kmax = std::min (N / 2 - 1, (int) std::ceil (fmaxEff * N / srEff) + 2);
    axis.set (c.scale, c.fmin, fmaxEff);
    cfg.fmin = axis.lo;

    rowA.assign ((size_t) H, 0); rowB.assign ((size_t) H, 0); rowF.assign ((size_t) H, 0);
    for (int j = 0; j < H; ++j)
    {
        const double blo = axis.freq ((double) j / H) * N / srEff;
        const double bhi = axis.freq ((double) (j + 1) / H) * N / srEff;
        if (bhi - blo < 1)
        {
            const double bc = axis.freq ((j + 0.5) / H) * N / srEff;
            int k0 = std::clamp ((int) std::floor (bc), kmin, kmax - 1);
            rowA[(size_t) j] = k0; rowB[(size_t) j] = -1; rowF[(size_t) j] = std::clamp (bc - k0, 0.0, 1.0);
        }
        else
        {
            const int a = std::max (kmin, (int) std::ceil (blo));
            const int b = std::max (a, std::min (kmax, (int) std::floor (bhi)));
            rowA[(size_t) j] = a; rowB[(size_t) j] = b;
        }
    }

    // radix-2 FFT tables
    int bits = 0; while ((1 << bits) < N) ++bits;
    rev.assign ((size_t) N, 0);
    for (int i = 0; i < N; ++i) { unsigned r = 0, x = (unsigned) i; for (int b = 0; b < bits; ++b) { r = (r << 1) | (x & 1); x >>= 1; } rev[(size_t) i] = r; }
    cosT.resize ((size_t) N / 2); sinT.resize ((size_t) N / 2);
    for (int i = 0; i < N / 2; ++i) { cosT[(size_t) i] = std::cos (2 * kPi * i / N); sinT[(size_t) i] = std::sin (2 * kPi * i / N); }

    re.assign ((size_t) N, 0); im.assign ((size_t) N, 0); re2.assign ((size_t) N, 0); im2.assign ((size_t) N, 0);
    binP.assign ((size_t) N / 2 + 1, 0);
    buf.assign ((size_t) N * 2 + 16384, 0.0f);
    bufLen = framePos = 0; frame = 0;
    column.assign ((size_t) H, 0.0f);
    setFloorDb (c.floorDb);
}

void Reassigner::setFloorDb (double db) { cfg.floorDb = db; thrP = std::pow (10.0, db / 10.0) * normB; }

double Reassigner::getLatencySeconds() const noexcept
{
    return (N / 2.0 + (cfg.reassign ? maxShift : 0.0)) / srEff;
}

void Reassigner::fft (std::vector<double>& xr, std::vector<double>& xi) const
{
    for (int i = 0; i < N; ++i)
    {
        const int j = (int) rev[(size_t) i];
        if (j > i) { std::swap (xr[(size_t) i], xr[(size_t) j]); std::swap (xi[(size_t) i], xi[(size_t) j]); }
    }
    for (int size = 2; size <= N; size <<= 1)
    {
        const int half = size >> 1, step = N / size;
        for (int i = 0; i < N; i += size)
            for (int j = i, k = 0; j < i + half; ++j, k += step)
            {
                const int l = j + half;
                const double c = cosT[(size_t) k], s = sinT[(size_t) k];
                const double tr = xr[(size_t) l] * c + xi[(size_t) l] * s;
                const double ti = xi[(size_t) l] * c - xr[(size_t) l] * s;
                xr[(size_t) l] = xr[(size_t) j] - tr; xi[(size_t) l] = xi[(size_t) j] - ti;
                xr[(size_t) j] += tr;                 xi[(size_t) j] += ti;
            }
    }
}

void Reassigner::processFrame (int o)
{
    for (int n = 0; n < N; ++n)
    {
        const double s = buf[(size_t) (o + n)];
        re[(size_t) n] = s * win[(size_t) n];       // X_h (real part) and X_dh (imag part) share one complex FFT
        im[(size_t) n] = s * wd[(size_t) n];
        if (cfg.reassign) { re2[(size_t) n] = s * wt[(size_t) n]; im2[(size_t) n] = 0; }
    }
    fft (re, im);
    if (cfg.reassign) fft (re2, im2);

    const double bscale = N / (2 * kPi), fscale = srEff / N, fmin = cfg.fmin;
    const long long f = frame;

    for (int k = kmin; k <= kmax; ++k)
    {
        const int nk = N - k;
        const double Zr = re[(size_t) k], Zi = im[(size_t) k], Wr = re[(size_t) nk], Wi = im[(size_t) nk];
        const double Ar = (Zr + Wr) * 0.5, Ai = (Zi - Wi) * 0.5;      // X_h
        const double P = Ar * Ar + Ai * Ai;
        if (! cfg.reassign) { binP[(size_t) k] = P / normB; continue; }
        if (P < thrP) continue;
        const double Br = (Zi + Wi) * 0.5, Bi = (Wr - Zr) * 0.5;      // X_dh
        const double dt = (re2[(size_t) k] * Ar + im2[(size_t) k] * Ai) / P;
        if (dt > maxShift || dt < -maxShift) continue;
        const double fr = (k - (Bi * Ar - Br * Ai) / P * bscale) * fscale;
        if (fr <= fmin || fr >= fmaxEff) continue;

        const double yp = axis.pos (fr) * H - 0.5;
        const double tc = (double) f + dt / hop;
        const long long i0 = (long long) std::floor (tc);
        const double a = tc - (double) i0;
        const int j0 = (int) std::floor (yp);
        const double b = yp - j0;
        auto& c0 = acc[(size_t) (((i0 % C) + C) % C)];
        auto& c1 = acc[(size_t) ((((i0 + 1) % C) + C) % C)];
        const double p0 = P * (1 - a), p1 = P * a;
        if (j0 >= 0)     { c0[(size_t) j0] += p0 * (1 - b);     c1[(size_t) j0] += p1 * (1 - b); }
        if (j0 + 1 < H)  { c0[(size_t) j0 + 1] += p0 * b;       c1[(size_t) j0 + 1] += p1 * b; }
    }

    if (! cfg.reassign)
    {
        for (int j = 0; j < H; ++j)
        {
            double v;
            if (rowB[(size_t) j] < 0)
            {
                const int k0 = rowA[(size_t) j]; const double t = rowF[(size_t) j];
                v = binP[(size_t) k0] * (1 - t) + binP[(size_t) k0 + 1] * t;
            }
            else
            {
                v = 0;
                for (int k = rowA[(size_t) j]; k <= rowB[(size_t) j]; ++k) v = std::max (v, binP[(size_t) k]);
            }
            column[(size_t) j] = (float) (10.0 * std::log10 (v + 1e-20));
        }
    }
    else
    {
        // No later frame can deposit into column f - S any more: emit and clear it.
        auto& col = acc[(size_t) ((((f - S) % C) + C) % C)];
        for (int j = 0; j < H; ++j)
        {
            column[(size_t) j] = (float) (10.0 * std::log10 (col[(size_t) j] / normR + 1e-20));
            col[(size_t) j] = 0;
        }
    }
    if (sink) sink (column.data(), H);
    ++frame;
}

void Reassigner::process (const float* L, const float* R, int n)
{
    if (N == 0) return;
    const int need = n / dec + 1;
    if (bufLen + need > (int) buf.size())
    {
        std::copy (buf.begin() + framePos, buf.begin() + bufLen, buf.begin());
        bufLen -= framePos; framePos = 0;
        if (bufLen + need > (int) buf.size()) bufLen = 0;
    }

    for (int i = 0; i < n; ++i)
    {
        float s;
        switch (cfg.channel)
        {
            case Left:  s = L[i]; break;
            case Right: s = R[i]; break;
            case Diff:  s = (L[i] - R[i]) * 0.5f; break;
            default:    s = (L[i] + R[i]) * 0.5f; break;
        }
        if (dec == 1) { buf[(size_t) bufLen++] = s; continue; }

        firHist[(size_t) firPos] = s; firHist[(size_t) (firPos + firLen)] = s;
        firPos = (firPos + 1) % firLen;
        if (++decCnt >= dec)
        {
            decCnt = 0;
            double a = 0;
            const double* h = firHist.data() + firPos;
            for (int k = 0; k < firLen; ++k) a += fir[(size_t) k] * h[k];
            buf[(size_t) bufLen++] = (float) a;
        }
    }

    while (bufLen - framePos >= N) { processFrame (framePos); framePos += hop; }

    if (framePos > (int) buf.size() / 2)
    {
        std::copy (buf.begin() + framePos, buf.begin() + bufLen, buf.begin());
        bufLen -= framePos; framePos = 0;
    }
}

void Reassigner::emitGap (int numInputSamples)
{
    if (N == 0) return;
    const int cols = std::max (1, (int) std::lround ((double) numInputSamples / dec / hop));
    std::fill (column.begin(), column.end(), std::numeric_limits<float>::quiet_NaN());
    for (int i = 0; i < cols; ++i) if (sink) sink (column.data(), H);
    bufLen = framePos = 0;
    for (auto& a : acc) std::fill (a.begin(), a.end(), 0.0);
}

void Reassigner::resetStream()
{
    bufLen = framePos = 0;
    for (auto& a : acc) std::fill (a.begin(), a.end(), 0.0);
    std::fill (firHist.begin(), firHist.end(), 0.0);
    firPos = decCnt = 0;
}
