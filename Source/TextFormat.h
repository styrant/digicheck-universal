#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

// Shared readout text: frequencies and note names, the same everywhere
namespace textfmt
{
    // "62.5 Hz", "440 Hz", "1.25 kHz"
    inline juce::String hz (double f)
    {
        return f >= 1000 ? juce::String (f / 1000, 2) + " kHz" : juce::String (f, f < 100 ? 1 : 0) + " Hz";
    }

    // "A4 +3c" against the user's A4 reference (Settings::noteRef; 0 = the note cursor is off: 440 Hz)
    inline juce::String note (double f, int ref)
    {
        if (f < 8.0) return {};
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const double midi = 69 + 12 * std::log2 (f / (ref > 0 ? ref : 440));
        const int n = (int) std::lround (midi), c = (int) std::lround ((midi - n) * 100);
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String ((int) std::floor (n / 12.0) - 1) + (c >= 0 ? " +" : " ") + juce::String (c) + "c";
    }
}
