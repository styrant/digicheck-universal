// Sensitivity: keeping the transcribed notes the audio supports, dropping the excess.
//
// The transcription model decodes greedily and gives no confidence or velocity per note, so strength is
// measured from the audio: for each note, the level of its fundamental and first two harmonics over
// its first half second (Hann-windowed Goertzel on the 16 kHz signal), relative to the strongest note
// sounding within half a second of it. Excess tones (an overtone heard as a note an octave or a twelfth
// up, ghost notes in reverb tails) come out weak by this measure; real notes, even soft ones in a
// sparse passage, don't, because they're compared with their neighbours, not with the loudest moment
// of the song. A note an octave, a twelfth or two octaves above a much stronger note sounding at the
// same time (its overtone, most likely) is marked down 10 dB more.
//
// Sensitivity 0..100: 100 keeps every note; lower values drop notes weaker than
// -(8 + 0.32 x sensitivity) dB relative to their neighbours (70, the default: -30 dB, about what the
// model wrote; 10: -11 dB, which removes its octave errors in tests/harmony_test; 0: -8 dB) and shorter than 0.1 x (1 - s/100)^1.5 s.
// Drums are kept (short by nature). Pure C++.
#pragma once
#include "TranscribeEngine.h"
#include <cmath>
#include <vector>

namespace transcribe
{
    // dB relative to the strongest neighbour (<= 0), one per note; drums get 0
    std::vector<float> noteStrengths (const std::vector<Note>& notes, const float* audio16k, std::size_t numSamples);

    struct Sensitivity
    {
        static constexpr int kDefault = 70;
        static double minStrengthDb (int s) { return -(8.0 + 0.32 * s); }
        static double minDuration (int s)  { const double x = 1.0 - s / 100.0; return 0.1 * x * std::sqrt (x); }
        // the silence gate for the next transcription: lower = quieter passages are transcribed too
        static float gateDbfs (int s)      { return (float) (-39.0 - 0.3 * s); }        // 0: -39, 70 (default): -60, 100: -69 dBFS
    };

    std::vector<Note> filterNotes (const std::vector<Note>& notes, const std::vector<float>& strengths, int sensitivity);
}
