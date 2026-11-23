#include "NoteFilter.h"
#include <algorithm>
#include <cmath>

namespace transcribe
{
    namespace
    {
        // Hann-windowed Goertzel amplitude of frequency f over x[0..n)
        double amplitude (const float* x, int n, double f)
        {
            if (n < 16 || f >= kSampleRate * 0.5) return 0;
            const double w = 2 * 3.14159265358979 * f / kSampleRate, c = 2 * std::cos (w);
            double s1 = 0, s2 = 0, wsum = 0;
            for (int i = 0; i < n; ++i)
            {
                const double h = 0.5 - 0.5 * std::cos (2 * 3.14159265358979 * i / (n - 1));
                const double s0 = x[i] * h + c * s1 - s2;
                s2 = s1; s1 = s0;
                wsum += h;
            }
            const double re = s1 - s2 * std::cos (w), im = s2 * std::sin (w);
            return 2 * std::sqrt (re * re + im * im) / wsum;
        }
    }

    std::vector<float> noteStrengths (const std::vector<Note>& notes, const float* audio, std::size_t N)
    {
        std::vector<float> level (notes.size(), -200.0f), rel (notes.size(), 0.0f);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            if (n.drum || audio == nullptr || N == 0) continue;
            const auto a = (std::size_t) std::max (0.0, (n.onset + 0.01) * kSampleRate);
            const auto b = std::min (N, (std::size_t) (std::min (n.offset, n.onset + 0.5) * kSampleRate));
            if (b <= a + 64) { level[i] = -120; continue; }
            const double f = 440.0 * std::pow (2.0, (n.pitch - 69) / 12.0);
            const double amp = amplitude (audio + a, (int) (b - a), f) + 0.5 * amplitude (audio + a, (int) (b - a), 2 * f)
                             + 0.33 * amplitude (audio + a, (int) (b - a), 3 * f);
            level[i] = (float) (20 * std::log10 (amp + 1e-9));
        }
        // relative to the strongest note starting within half a second
        std::vector<size_t> order (notes.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::sort (order.begin(), order.end(), [&] (size_t a, size_t b) { return notes[a].onset < notes[b].onset; });
        size_t lo = 0;
        for (size_t k = 0; k < order.size(); ++k)
        {
            const size_t i = order[k];
            if (notes[i].drum) continue;
            while (lo < order.size() && notes[order[lo]].onset < notes[i].onset - 0.5) ++lo;
            float best = level[i];
            for (size_t j = lo; j < order.size() && notes[order[j]].onset <= notes[i].onset + 0.5; ++j)
                if (! notes[order[j]].drum) best = std::max (best, level[order[j]]);
            rel[i] = std::min (0.0f, level[i] - best);
            // likely an overtone of a stronger note below it, sounding at the same time
            for (size_t j = lo; j < order.size() && notes[order[j]].onset < notes[i].offset; ++j)
            {
                const auto& o = notes[order[j]];
                const int d = notes[i].pitch - o.pitch;
                if (o.drum || o.offset <= notes[i].onset || (d != 12 && d != 19 && d != 24)) continue;
                if (level[order[j]] > level[i] + 3.0f) { rel[i] -= 10.0f; break; }
            }
        }
        return rel;
    }

    std::vector<Note> filterNotes (const std::vector<Note>& notes, const std::vector<float>& strengths, int s)
    {
        if (s >= 100 || strengths.size() != notes.size()) return notes;
        const double minDb = Sensitivity::minStrengthDb (s), minDur = Sensitivity::minDuration (s);
        std::vector<Note> out;
        out.reserve (notes.size());
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            if (n.drum || (strengths[i] >= minDb && n.offset - n.onset >= minDur)) out.push_back (n);
        }
        return out;
    }
}
