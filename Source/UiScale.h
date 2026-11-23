#pragma once
#include <JuceHeader.h>
#include <atomic>

// App-wide text size (Options -> Text size). Medium = 1.0 keeps the original sizes.
//  - ui::fs(size)  scales a font size
//  - ui::px(value) scales a layout dimension, but only when text gets larger (Large / Custom > 100%),
//                  so smaller text never squeezes the layout.
namespace ui
{
    inline std::atomic<float> textScale { 1.0f };

    inline float scale() noexcept               { return textScale.load(); }
    inline float k() noexcept                   { return juce::jmax (1.0f, textScale.load()); }
    inline float fs (float base) noexcept       { return base * textScale.load(); }
    inline int   px (int v) noexcept            { return juce::roundToInt ((float) v * k()); }
    inline float pxf (float v) noexcept         { return v * k(); }

    // Colour-blind-friendly meters (Options): colours that carry meaning (good / warning / bad, zones) switch
    // to an Okabe-Ito based set (sky blue / yellow / vermillion) that stays distinct with red-green colour
    // blindness. A Sem converts to the right juce::Colour wherever it's used.
    inline std::atomic<bool> colourSafe { false };
    struct Sem
    {
        juce::uint32 normal, safe;
        juce::Colour get() const noexcept                       { return juce::Colour (colourSafe.load() ? safe : normal); }
        operator juce::Colour() const noexcept                  { return get(); }
        juce::Colour withAlpha (float a) const noexcept         { return get().withAlpha (a); }
        juce::Colour brighter (float a = 0.4f) const noexcept   { return get().brighter (a); }
        juce::Colour darker (float a = 0.4f) const noexcept     { return get().darker (a); }
        juce::Colour interpolatedWith (juce::Colour o, float p) const noexcept { return get().interpolatedWith (o, p); }
    };
    // the shared set
    inline constexpr juce::uint32 kSafeGood = 0xff56b4e9, kSafeWarn = 0xfff0e442, kSafeBad = 0xffe8601c, kSafeOrange = 0xffe69f00, kSafeBlue = 0xff3b8fd9;

    inline float presetScale (int preset, double custom) noexcept
    {
        switch (preset)
        {
            case 0:  return 0.85f;          // Small
            case 2:  return 1.25f;          // Large
            case 3:  return (float) juce::jlimit (0.7, 2.0, custom);   // Custom
            default: return 1.0f;           // Medium (default)
        }
    }
}
