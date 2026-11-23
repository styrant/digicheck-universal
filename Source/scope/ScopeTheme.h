// Colour themes for the Scope view. The colour names are Scyllascope's (see its overrides.json and
// theme files), so its themes load here: put .json theme files in Documents\DigiCheck Universal\Scope
// Themes. A file may hold the keys at the top level, or under one theme name ({"Neon": {...}}).
#pragma once
#include <JuceHeader.h>
#include <array>

struct ScopeTheme
{
    juce::String name;
    juce::Colour bg, scopeBg, controlBg, buttonOutline, text, textBtnOff, textBtnOn, accent, accentText,
                 secondaryAccent, secondaryAccentText, recording, wave, hold, sideChain, grid, playhead,
                 clipping, hoverBg, hoverText, selection, sliderTrack;
    float sideChainAlpha = 0.8f;
    std::array<juce::Colour, 5> spectral;           // sub, low, mid, high, air

    static ScopeTheme dark();                        // Scyllascope's default ("Dark")
    static ScopeTheme neon();
    static ScopeTheme digicheck();                   // matches the rest of this app

    static juce::File folder();
    static juce::Array<ScopeTheme> all();            // the built-in ones, then any in folder()
    static ScopeTheme named (const juce::String& n); // falls back to dark()
    static bool fromJson (const juce::var& v, ScopeTheme& into);   // missing keys keep their current value
};
