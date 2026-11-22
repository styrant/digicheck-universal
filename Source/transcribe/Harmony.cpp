#include "Harmony.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>

namespace harmony
{
    namespace
    {
        const char* kSharp[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const char* kFlat[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

        bool usesFlats (const Key& k)
        {
            if (! k.valid) return false;
            static const int majorFlat[] = { 5, 10, 3, 8, 1 };          // F Bb Eb Ab Db
            static const int minorFlat[] = { 2, 7, 0, 5, 10, 3 };       // d g c f bb eb
            for (int pc : k.minor ? std::vector<int> (std::begin (minorFlat), std::end (minorFlat))
                                  : std::vector<int> (std::begin (majorFlat), std::end (majorFlat)))
                if (pc == k.tonic) return true;
            return false;
        }

        bool isMinorQuality (int q) { return q == Minor || q == Dim || q == HalfDim7 || q == Dim7 || q == Min7 || q == Min6 || q == MinMaj7; }

        // how much less likely a quality is than a plain triad, before any evidence
        const float kPrior[kQualities] = { 0.0f, 0.0f, -0.05f, -0.07f, -0.06f, -0.05f, -0.025f, -0.035f, -0.025f, -0.05f, -0.07f, -0.06f, -0.07f, -0.09f };

        bool pitched (const Note& n) { return ! n.drum && n.program != transcribe::kDrumProgram && n.offset > n.onset; }

        // Krumhansl-Kessler key profiles
        const double kMajorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
        const double kMinorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

        double correlate (const double* x, const double* y)
        {
            double mx = 0, my = 0;
            for (int i = 0; i < 12; ++i) { mx += x[i]; my += y[i]; }
            mx /= 12; my /= 12;
            double sxy = 0, sxx = 0, syy = 0;
            for (int i = 0; i < 12; ++i) { sxy += (x[i] - mx) * (y[i] - my); sxx += (x[i] - mx) * (x[i] - mx); syy += (y[i] - my) * (y[i] - my); }
            return sxx > 0 && syy > 0 ? sxy / std::sqrt (sxx * syy) : 0;
        }

        // melody, keeping track of which input notes it came from
        std::vector<MelodyNote> melodyWithSources (const std::vector<Note>& notes, const Options& o, std::vector<char>* isMelody)
        {
            if (isMelody) isMelody->assign (notes.size(), 0);

            // candidates: pitched, not bass instruments, not too low
            std::vector<int> cand;
            double end = 0, voiceTime = 0;
            for (int i = 0; i < (int) notes.size(); ++i)
            {
                const auto& n = notes[(size_t) i];
                end = std::max (end, n.offset);
                if (! pitched (n)) continue;
                if (o.melodyProgram >= 0) { if (n.program == o.melodyProgram) cand.push_back (i); continue; }
                if (transcribe::isBassProgram (n.program) || n.pitch < 45) continue;
                cand.push_back (i);
                if (n.program >= 52 && n.program <= 54) voiceTime += n.offset - n.onset;     // choir / voice / synth voice
            }
            // a real vocal part: take the melody from it alone
            if (o.melodyProgram < 0 && end > 0 && voiceTime > 0.15 * end)
                cand.erase (std::remove_if (cand.begin(), cand.end(), [&] (int i) { const int p = notes[(size_t) i].program; return p < 52 || p > 54; }), cand.end());
            if (cand.empty()) return {};

            // skyline over the elementary intervals between all onsets and offsets
            std::vector<double> cuts;
            for (int i : cand) { cuts.push_back (notes[(size_t) i].onset); cuts.push_back (notes[(size_t) i].offset); }
            std::sort (cuts.begin(), cuts.end());
            cuts.erase (std::unique (cuts.begin(), cuts.end(), [] (double a, double b) { return std::abs (a - b) < 1e-6; }), cuts.end());

            std::vector<int> byOnset = cand;
            std::sort (byOnset.begin(), byOnset.end(), [&] (int a, int b) { return notes[(size_t) a].onset < notes[(size_t) b].onset; });

            std::vector<MelodyNote> out;
            int curSource = -1;                 // note the current melody note started from
            size_t next = 0;
            std::vector<int> active;
            for (size_t c = 0; c + 1 < cuts.size(); ++c)
            {
                const double a = cuts[c], b = cuts[c + 1], mid = 0.5 * (a + b);
                while (next < byOnset.size() && notes[(size_t) byOnset[next]].onset <= mid) active.push_back (byOnset[next++]);
                active.erase (std::remove_if (active.begin(), active.end(), [&] (int i) { return notes[(size_t) i].offset <= mid; }), active.end());
                if (active.empty()) { curSource = -1; continue; }

                // The top voice: among the notes within a major third of the highest one, the one struck
                // most recently (a new note just under a held one is the line moving on; transcriptions
                // often hold a doubled note far too long), the highest of those struck together.
                int highest = active[0];
                for (int i : active) if (notes[(size_t) i].pitch > notes[(size_t) highest].pitch) highest = i;
                int top = highest;
                for (int i : active)
                {
                    const auto& n = notes[(size_t) i]; const auto& t = notes[(size_t) top];
                    if (n.pitch < notes[(size_t) highest].pitch - 4) continue;
                    const bool later = n.onset > t.onset + 0.05, together = std::abs (n.onset - t.onset) <= 0.05;
                    if (later || (together && n.pitch > t.pitch)) top = i;
                }
                const auto& tn = notes[(size_t) top];
                const bool startsHere = std::abs (tn.onset - a) < 1e-6;

                if (! out.empty() && curSource >= 0 && notes[(size_t) curSource].pitch == tn.pitch && ! startsHere)
                {
                    out.back().offset = b;                                  // the same note carries on
                    if (isMelody) (*isMelody)[(size_t) top] = 1;
                    continue;
                }
                if (! startsHere) { curSource = -1; continue; }             // a held note surfacing: not a melody onset
                // doubled notes (same pitch, same onset in another instrument) are one melody note
                if (! out.empty() && std::abs (out.back().onset - tn.onset) < 0.03 && out.back().pitch == tn.pitch)
                {
                    out.back().offset = std::max (out.back().offset, b);
                }
                else
                {
                    if (! out.empty() && out.back().offset > a) out.back().offset = a;      // monophonic
                    out.push_back ({ a, b, tn.pitch, tn.program });
                }
                curSource = top;
                if (isMelody) (*isMelody)[(size_t) top] = 1;
            }
            // doubled notes in other instruments count as melody too (for the chord weighting)
            if (isMelody)
                for (size_t i = 0; i < notes.size(); ++i)
                    if (! (*isMelody)[i] && pitched (notes[i]))
                        for (auto& m : out)
                            if (m.pitch == notes[i].pitch && std::abs (m.onset - notes[i].onset) < 0.03) { (*isMelody)[i] = 1; break; }

            out.erase (std::remove_if (out.begin(), out.end(), [] (const MelodyNote& m) { return m.offset - m.onset < 0.06; }), out.end());
            return out;
        }
    }

    //==========================================================================
    const std::array<int, 12>& qualityTemplate (int q)
    {
        static const std::array<std::array<int, 12>, kQualities> t = [] {
            std::array<std::array<int, 12>, kQualities> a {};
            auto set = [&] (int q, std::initializer_list<int> iv) { for (int i : iv) a[(size_t) q][(size_t) i] = 1; };
            set (Major, { 0, 4, 7 });     set (Minor, { 0, 3, 7 });     set (Dim, { 0, 3, 6 });       set (Aug, { 0, 4, 8 });
            set (Sus2, { 0, 2, 7 });      set (Sus4, { 0, 5, 7 });      set (Dom7, { 0, 4, 7, 10 });  set (Maj7, { 0, 4, 7, 11 });
            set (Min7, { 0, 3, 7, 10 });  set (HalfDim7, { 0, 3, 6, 10 }); set (Dim7, { 0, 3, 6, 9 }); set (Maj6, { 0, 4, 7, 9 });
            set (Min6, { 0, 3, 7, 9 });   set (MinMaj7, { 0, 3, 7, 11 });
            return a;
        }();
        return t[(size_t) std::clamp (q, 0, kQualities - 1)];
    }

    const char* qualitySuffix (int q)
    {
        static const char* s[kQualities] = { "", "m", "dim", "aug", "sus2", "sus4", "7", "maj7", "m7", "m7b5", "dim7", "6", "m6", "m(maj7)" };
        return s[std::clamp (q, 0, kQualities - 1)];
    }

    std::string pitchName (int pc, const Key& k) { return (usesFlats (k) ? kFlat : kSharp)[((pc % 12) + 12) % 12]; }
    std::string noteName (int midi, const Key& k) { return pitchName (midi, k) + std::to_string (midi / 12 - 1); }

    std::string Key::name() const
    {
        if (! valid) return "?";
        return pitchName (tonic, *this) + (minor ? " minor" : " major");
    }

    std::string chordName (int root, int q, int bass, const Key& k)
    {
        if (root < 0) return "N.C.";
        std::string s = pitchName (root, k) + qualitySuffix (q);
        if (bass >= 0 && bass != root) s += "/" + pitchName (bass, k);
        return s;
    }

    std::string romanNumeral (int root, int q, const Key& k)
    {
        if (! k.valid || root < 0) return {};
        static const char* major[12] = { "I", "bII", "II", "bIII", "III", "IV", "#IV", "V", "bVI", "VI", "bVII", "VII" };
        static const char* minor[12] = { "I", "bII", "II", "III", "#III", "IV", "#IV", "V", "VI", "#VI", "VII", "#VII" };
        std::string r = (k.minor ? minor : major)[(root - k.tonic + 12) % 12];
        if (isMinorQuality (q)) for (auto& c : r) c = (char) std::tolower ((unsigned char) c);
        static const char* suffix[kQualities] = { "", "", "°", "+", "sus2", "sus4", "7", "maj7", "7", "ø7", "°7", "6", "6", "(maj7)" };
        return r + suffix[std::clamp (q, 0, kQualities - 1)];
    }

    //==========================================================================
    Key estimateKey (const std::vector<Note>& notes)
    {
        double w[12] {};
        double lowest = 1e9;
        for (auto& n : notes) if (pitched (n)) lowest = std::min (lowest, (double) n.pitch);
        for (auto& n : notes)
        {
            if (! pitched (n)) continue;
            const double d = std::min (n.offset - n.onset, 4.0);                    // long drones don't dominate
            w[n.pitch % 12] += d * (transcribe::isBassProgram (n.program) || n.pitch < 48 ? 1.5 : 1.0);
        }
        double total = 0; for (double x : w) total += x;
        Key k;
        if (total <= 0) return k;
        double best = -2, second = -2;
        for (int t = 0; t < 12; ++t)
            for (int m = 0; m < 2; ++m)
            {
                double prof[12];
                for (int i = 0; i < 12; ++i) prof[(i + t) % 12] = (m ? kMinorProfile : kMajorProfile)[i];
                const double r = correlate (w, prof);
                if (r > best) { second = best; best = r; k.tonic = t; k.minor = m == 1; }
                else if (r > second) second = r;
            }
        k.valid = true;
        k.confidence = (float) std::clamp ((best - second) * 6.0, 0.0, 1.0);
        return k;
    }

    std::vector<MelodyNote> melody (const std::vector<Note>& notes, const Options& o) { return melodyWithSources (notes, o, nullptr); }

    //==========================================================================
    std::vector<Chord> chords (const std::vector<Note>& notes, const Key& key, const Options& o)
    {
        double end = 0;
        for (auto& n : notes) if (pitched (n)) end = std::max (end, n.offset);
        const int F = (int) std::ceil (end / o.frame);
        if (F <= 0) return {};

        std::vector<char> isMel;
        melodyWithSources (notes, o, &isMel);

        // ---- per-frame pitch-class weights and the bass
        std::vector<std::array<double, 12>> chroma ((size_t) F);
        std::vector<int> bassPc ((size_t) F, -1), bassPitch ((size_t) F, 999);
        for (size_t i = 0; i < notes.size(); ++i)
        {
            const auto& n = notes[i];
            if (! pitched (n)) continue;
            const int a = std::max (0, (int) std::floor (n.onset / o.frame)), b = std::min (F - 1, (int) std::floor ((n.offset - 1e-9) / o.frame));
            double w = isMel[i] ? 0.45 : 1.0;
            if (n.offset - n.onset < 0.12) w *= 0.5;                                // passing notes count less
            for (int f = a; f <= b; ++f)
            {
                const double t0 = f * o.frame, t1 = t0 + o.frame;
                const double ov = (std::min (t1, n.offset) - std::max (t0, n.onset)) / o.frame;
                if (ov <= 0) continue;
                const double attack = (n.onset >= t0 && n.onset < t1) ? 1.25 : 1.0;
                chroma[(size_t) f][(size_t) (n.pitch % 12)] += w * ov * attack;
                if (n.pitch < bassPitch[(size_t) f]) { bassPitch[(size_t) f] = n.pitch; bassPc[(size_t) f] = n.pitch % 12; }
            }
        }
        for (int f = 0; f < F; ++f)
            if (bassPc[(size_t) f] >= 0) chroma[(size_t) f][(size_t) bassPc[(size_t) f]] += 0.5;      // the bass anchors the chord

        // ---- emissions: cosine match of each frame against every chord (state 0 = no chord)
        std::vector<std::pair<int, int>> states { { -1, 0 } };
        for (int q = 0; q < kQualities; ++q)
        {
            if (! o.sevenths && (q == Dom7 || q == Maj7 || q == Min7 || q == HalfDim7 || q == Dim7 || q == Maj6 || q == Min6 || q == MinMaj7)) continue;
            for (int r = 0; r < 12; ++r) states.push_back ({ r, q });
        }
        const int S = (int) states.size();
        std::vector<float> emit ((size_t) F * (size_t) S, 0.0f);
        std::array<std::array<double, 12>, kQualities> unitT {};
        for (int q = 0; q < kQualities; ++q)
        {
            const auto& t = qualityTemplate (q);
            double nrm = 0; for (int i : t) nrm += i;
            for (int i = 0; i < 12; ++i) unitT[(size_t) q][(size_t) i] = t[(size_t) i] / std::sqrt (nrm);
        }
        constexpr double kScale = 10.0;
        for (int f = 0; f < F; ++f)
        {
            const auto& c = chroma[(size_t) f];
            double nrm = 0; for (double x : c) nrm += x * x;
            nrm = std::sqrt (nrm);
            float* e = &emit[(size_t) f * (size_t) S];
            if (nrm < 1e-6) { e[0] = (float) kScale; continue; }                  // silence: no chord
            e[0] = (float) (0.25 * kScale);
            for (int s = 1; s < S; ++s)
            {
                const auto [r, q] = states[(size_t) s];
                double dot = 0;
                for (int i = 0; i < 12; ++i) dot += c[(size_t) ((r + i) % 12)] * unitT[(size_t) q][(size_t) i];
                double score = dot / nrm + kPrior[q];
                const int bp = bassPc[(size_t) f];
                if (bp >= 0) score += bp == r ? 0.06 : (qualityTemplate (q)[(size_t) ((bp - r + 12) % 12)] ? 0.015 : -0.03);
                e[s] = (float) (score * kScale);
            }
        }

        // ---- Viterbi: stay unless the evidence is worth the switching cost
        std::vector<float> score ((size_t) S), prev ((size_t) S);
        std::vector<int> back ((size_t) F * (size_t) S);
        for (int s = 0; s < S; ++s) score[(size_t) s] = emit[(size_t) s];
        for (int f = 1; f < F; ++f)
        {
            prev = score;
            int bestPrev = 0;
            for (int s = 1; s < S; ++s) if (prev[(size_t) s] > prev[(size_t) bestPrev]) bestPrev = s;
            const float* e = &emit[(size_t) f * (size_t) S];
            for (int s = 0; s < S; ++s)
            {
                const float stay = prev[(size_t) s], jump = prev[(size_t) bestPrev] - (float) o.switchCost;
                if (stay >= jump) { score[(size_t) s] = stay + e[s]; back[(size_t) f * (size_t) S + (size_t) s] = s; }
                else              { score[(size_t) s] = jump + e[s]; back[(size_t) f * (size_t) S + (size_t) s] = bestPrev; }
            }
        }
        std::vector<int> path ((size_t) F);
        path[(size_t) F - 1] = (int) (std::max_element (score.begin(), score.end()) - score.begin());
        for (int f = F - 1; f > 0; --f) path[(size_t) f - 1] = back[(size_t) f * (size_t) S + (size_t) path[(size_t) f]];

        // ---- segments
        struct Seg { int a, b, s; };
        std::vector<Seg> segs;
        for (int f = 0; f < F; ++f)
            if (segs.empty() || segs.back().s != path[(size_t) f]) segs.push_back ({ f, f + 1, path[(size_t) f] });
            else segs.back().b = f + 1;

        // merge chords that are too short into the neighbour that fits them best
        const int minFrames = std::max (1, (int) std::lround (o.minChord / o.frame));
        auto segScore = [&] (const Seg& g, int s) { double t = 0; for (int f = g.a; f < g.b; ++f) t += emit[(size_t) f * (size_t) S + (size_t) s]; return t; };
        for (bool changed = true; changed && segs.size() > 1;)
        {
            changed = false;
            for (size_t i = 0; i < segs.size(); ++i)
            {
                if (segs[i].b - segs[i].a >= minFrames || segs[i].s == 0) continue;
                const bool hasL = i > 0, hasR = i + 1 < segs.size();
                const double l = hasL ? segScore (segs[i], segs[i - 1].s) : -1e9, r = hasR ? segScore (segs[i], segs[i + 1].s) : -1e9;
                if (hasL && l >= r) { segs[i - 1].b = segs[i].b; segs.erase (segs.begin() + (long) i); }
                else if (hasR)      { segs[i + 1].a = segs[i].a; segs.erase (segs.begin() + (long) i); }
                else continue;
                changed = true;
                break;
            }
            for (size_t i = 1; i < segs.size(); ++i)
                if (segs[i].s == segs[i - 1].s) { segs[i - 1].b = segs[i].b; segs.erase (segs.begin() + (long) i); changed = true; break; }
        }

        std::vector<Chord> out;
        for (auto& g : segs)
        {
            Chord c;
            c.start = g.a * o.frame; c.end = std::min (end, g.b * o.frame);
            const auto [r, q] = states[(size_t) g.s];
            c.root = r; c.quality = q;
            if (r >= 0)
            {
                double best = 0; int bassCount[12] {}; int withBass = 0;
                for (int f = g.a; f < g.b; ++f)
                {
                    best += emit[(size_t) f * (size_t) S + (size_t) g.s] / kScale;
                    // a slash chord needs a real bass note (E3 or lower), not just the lowest chord tone
                    if (bassPc[(size_t) f] >= 0 && bassPitch[(size_t) f] <= 52) ++bassCount[bassPc[(size_t) f]];
                    if (bassPc[(size_t) f] >= 0) ++withBass;
                }
                c.confidence = (float) std::clamp (best / (g.b - g.a), 0.0, 1.0);
                const int bp = (int) (std::max_element (std::begin (bassCount), std::end (bassCount)) - std::begin (bassCount));
                if (withBass > 0 && bp != r && bassCount[bp] * 2 > withBass && qualityTemplate (q)[(size_t) ((bp - r + 12) % 12)])
                    c.bass = bp;
            }
            c.name = chordName (c.root, c.quality, c.bass, key);
            c.roman = romanNumeral (c.root, c.quality, key);
            out.push_back (c);
        }
        return out;
    }

    std::string chordSheet (const std::vector<Chord>& cs, const Key& key)
    {
        std::string out;
        if (key.valid) out += "Key: " + key.name() + "\n\n";
        int onLine = 0;
        for (auto& c : cs)
        {
            if (c.root < 0) continue;
            if (onLine == 0)
            {
                char t[16];
                std::snprintf (t, sizeof t, "%d:%02d  ", (int) c.start / 60, (int) c.start % 60);
                out += t;
            }
            out += c.name;
            if (++onLine == 8) { out += "\n"; onLine = 0; }
            else out += "  ";
        }
        while (! out.empty() && out.back() == ' ') out.pop_back();
        return out + "\n";
    }
}
