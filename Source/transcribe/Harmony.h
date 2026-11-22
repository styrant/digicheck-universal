// Chords, melody and key from transcribed notes. Pure C++20, no JUCE, no engine dependency (only the
// Note struct from TranscribeEngine.h), so it is unit-tested on its own and shared by every front end.
//
// Chords: duration-weighted pitch-class profiles every 0.1 s (drums ignored, the melody line weighted
// down, the lowest voice weighted up), matched against chord templates, then a Viterbi pass that only
// changes chord when the evidence outweighs a switching cost, so short passing notes don't flip the
// chord. Slash chords when the bass holds another chord tone. Names are spelled for the key (Bb, not
// A#, in F major) and given Roman numerals.
//
// Melody: the top line of the pitched, non-bass instruments ("skyline"), restarted only at real note
// onsets (a held chord tone that surfaces when the melody pauses is not a melody note), with notes
// that several instruments double merged into one. If the transcription has a voice part with enough
// notes, the melody is taken from the voice only.
#pragma once
#include "TranscribeEngine.h"
#include <array>
#include <string>
#include <vector>

namespace harmony
{
    using transcribe::Note;

    enum Quality { Major, Minor, Dim, Aug, Sus2, Sus4, Dom7, Maj7, Min7, HalfDim7, Dim7, Maj6, Min6, MinMaj7, kQualities };

    struct Key
    {
        int tonic = 0;                    // pitch class 0 = C
        bool minor = false;
        float confidence = 0;             // 0..1, how much better than the next best key
        bool valid = false;
        std::string name() const;         // "C major", "F# minor" (spelled for the key)
    };

    struct Chord
    {
        double start = 0, end = 0;
        int root = -1;                    // pitch class; -1 = no chord (N.C.)
        int quality = Major;
        int bass = -1;                    // pitch class of the bass when it isn't the root (slash chord), else -1
        float confidence = 0;             // 0..1 template match
        std::string name;                 // "Am7", "C/E", "N.C."
        std::string roman;                // "vi7", "I6" ... empty without a key or for N.C.
    };

    struct MelodyNote { double onset = 0, offset = 0; int pitch = 0, program = 0; };

    struct Options
    {
        double frame = 0.1;               // seconds per analysis frame
        double minChord = 0.35;           // shorter chords are merged into a neighbour
        double switchCost = 1.6;          // Viterbi penalty for changing chord (in template-score units)
        bool sevenths = true;             // allow 7th / 6th chords (off: triads and sus only)
        int melodyProgram = -1;           // -1 = automatic; otherwise melody from this program only
    };

    Key estimateKey (const std::vector<Note>& notes);
    std::vector<MelodyNote> melody (const std::vector<Note>& notes, const Options& = {});
    std::vector<Chord> chords (const std::vector<Note>& notes, const Key& key, const Options& = {});

    // naming
    std::string pitchName (int pc, const Key& key);           // spelled for the key ("Bb" in F major)
    std::string noteName (int midi, const Key& key);          // "C4"
    std::string chordName (int root, int quality, int bass, const Key& key);
    std::string romanNumeral (int root, int quality, const Key& key);
    const char* qualitySuffix (int quality);                  // "", "m", "dim", "7", "maj7" ...
    const std::array<int, 12>& qualityTemplate (int quality); // weights per interval above the root

    // plain-text chord list: "0:00  C  G/B  Am  F" lines, eight chords per line, N.C. left out
    std::string chordSheet (const std::vector<Chord>& chords, const Key& key);
}
