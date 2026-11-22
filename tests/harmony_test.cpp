// Chord, melody and key detection (Source/transcribe/Harmony.*) on hand-made note lists and on the real
// model's transcription of a synthetic progression (tests/data/model_progression_notes.inc).
#include "../Source/transcribe/Harmony.h"
#include "../Source/transcribe/Resample.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using harmony::Note;
static int failures = 0;

static void expect (bool ok, const std::string& what)
{
    std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok) ++failures;
}

static std::string names (const std::vector<harmony::Chord>& cs)
{
    std::string s;
    for (auto& c : cs) s += (s.empty() ? "" : " ") + c.name;
    return s;
}

static void add (std::vector<Note>& v, double a, double b, std::initializer_list<int> pitches, int program = 0)
{
    for (int p : pitches) v.push_back ({ a, b, p, program, false });
}

int main()
{
    // ---- the model's own transcription of C - Am - F - G with a melody on top
    {
        const std::vector<Note> notes {
#include "data/model_progression_notes.inc"
        };
        const auto key = harmony::estimateKey (notes);
        expect (key.valid && key.tonic == 0 && ! key.minor, "model notes: key " + key.name() + " (C major)");
        const auto cs = harmony::chords (notes, key);
        expect (names (cs) == "C Am F G", "model notes: chords '" + names (cs) + "' (C Am F G)");
        std::string rn; for (auto& c : cs) rn += c.roman + " ";
        expect (rn == "I vi IV V ", "model notes: numerals " + rn);
        const auto mel = harmony::melody (notes);
        const int want[] = { 76, 74, 72, 74, 72, 71, 69, 71, 69, 72, 77, 76, 74, 71, 67, 74 };
        bool same = mel.size() == 16;
        for (size_t i = 0; same && i < 16; ++i) same = mel[i].pitch == want[i] && std::abs (mel[i].onset - 0.5 * (double) i) < 0.06;
        std::string got; for (auto& m : mel) got += std::to_string (m.pitch) + " ";
        expect (same, "model notes: melody of " + std::to_string (mel.size()) + " notes, doubled parts merged: " + got);
    }

    // ---- slash chord, seventh, passing note, silence
    {
        std::vector<Note> v;
        add (v, 0, 2, { 40, 55, 60, 64 });             // C/E: E in the bass
        add (v, 2, 4, { 43, 53, 59, 62 });             // G7 (F on top of G B D)
        add (v, 2.9, 3.0, { 70 });                     // a 0.1 s passing note
        add (v, 5, 7, { 45, 57, 60, 64 });             // Am after a second of silence
        const harmony::Key c { 0, false, 1, true };
        const auto cs = harmony::chords (v, c);
        expect (names (cs) == "C/E G7 N.C. Am", "slash, seventh, passing note, gap: '" + names (cs) + "'");
        if (cs.size() == 4) expect (cs[1].roman == "V7" && cs[0].roman == "I", "numerals " + cs[0].roman + " " + cs[1].roman);
        harmony::Options triads; triads.sevenths = false;
        expect (names (harmony::chords (v, c, triads)) == "C/E G N.C. Am", "triads only: '" + names (harmony::chords (v, c, triads)) + "'");
    }

    // ---- spelling for the key, minor key numerals
    {
        std::vector<Note> v;
        add (v, 0, 2, { 41, 57, 60, 65 });             // F
        add (v, 2, 4, { 46, 58, 62, 65 });             // Bb
        add (v, 4, 6, { 48, 55, 60, 64, 58 });         // C7
        add (v, 6, 8, { 41, 57, 60, 65 });             // F
        const auto key = harmony::estimateKey (v);
        expect (key.valid && key.tonic == 5 && ! key.minor, "key of F-Bb-C7-F: " + key.name());
        const auto cs = harmony::chords (v, key);
        expect (names (cs) == "F Bb C7 F", "flats in F major: '" + names (cs) + "'");

        std::vector<Note> m;
        add (m, 0, 2, { 45, 57, 60, 64 });             // Am
        add (m, 2, 4, { 38, 53, 57, 62 });             // Dm
        add (m, 4, 6, { 40, 56, 59, 64 });             // E (harmonic minor V)
        add (m, 6, 8, { 45, 57, 60, 64 });             // Am
        const auto km = harmony::estimateKey (m);
        expect (km.valid && km.tonic == 9 && km.minor, "key of Am-Dm-E-Am: " + km.name());
        std::string rn; for (auto& c : harmony::chords (m, km)) rn += c.roman + " ";
        expect (rn == "i iv V i ", "minor numerals: " + rn);
    }

    // ---- melody: a sustained chord tone surfacing is not a melody note; drums and bass are ignored
    {
        std::vector<Note> v;
        add (v, 0, 4, { 60, 64, 67 });                 // held C chord
        add (v, 0, 1, { 72 }); add (v, 1, 2, { 74 });  // melody, then a rest from 2 s
        add (v, 0, 4, { 36 }, 33);                     // bass guitar
        v.push_back ({ 0, 0.01, 38, 128, true });      // snare
        const auto mel = harmony::melody (v);
        expect (mel.size() == 2 && mel[0].pitch == 72 && mel[1].pitch == 74 && std::abs (mel[1].offset - 2) < 1e-6,
                "melody stops at the rest (held G not taken): " + std::to_string (mel.size()) + " notes");
        harmony::Options only; only.melodyProgram = 33;
        expect (harmony::melody (v, only).size() == 1, "melody from a chosen instrument");
        const auto sheet = harmony::chordSheet (harmony::chords (v, harmony::estimateKey (v)), harmony::estimateKey (v));
        expect (sheet.find ("0:00  C") != std::string::npos, "chord sheet: " + sheet.substr (0, sheet.find ('\n', sheet.find ("0:00"))));
    }

    // ---- resampling to the model's 16 kHz: flat to 6.5 kHz, nothing folds back from above 8 kHz
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (double f : { 1000.0, 6500.0, 9000.0, 20000.0 })
        {
            std::vector<float> x ((size_t) sr * 2);
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) std::sin (2 * 3.14159265358979 * f * (double) i / sr);
            const auto y = transcribe::resample (x.data(), x.size(), sr);
            double ms = 0; const size_t a = y.size() / 4, b = 3 * y.size() / 4;
            for (size_t i = a; i < b; ++i) ms += (double) y[i] * y[i];
            const double db = 10 * std::log10 (2 * ms / (double) (b - a) + 1e-24);
            expect ((f <= 6500 ? std::abs (db) < 0.1 : db < -60) && y.size() == 32000,
                    "resample " + std::to_string ((int) f) + " Hz at " + std::to_string ((int) sr) + ": " + std::to_string (db).substr (0, 7) + " dB");
        }

    // ---- no slash chord from an inversion higher up when the bass is missing
    {
        std::vector<Note> v;
        add (v, 0, 2, { 57, 60, 65 });                 // F with A lowest, no bass
        add (v, 2, 4, { 40, 55, 60, 64 });             // C with E2 in the bass: C/E
        const harmony::Key c { 0, false, 1, true };
        expect (names (harmony::chords (v, c)) == "F C/E", "slash only with a real bass: '" + names (harmony::chords (v, c)) + "'");
    }

    std::printf (failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
