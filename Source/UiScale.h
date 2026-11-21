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
