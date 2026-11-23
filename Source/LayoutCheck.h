// Layout check (CI): every page at several window sizes, looking for controls that overlap each other,
// button / box text that doesn't fit, and controls that stick out of their parent. It also writes a
// fingerprint of every control's place, which CI compares with the previous build's to list what moved.
//
//   Spectroscope.exe --layout-check <folder>   writes layout.json, layout_report.txt, a PNG per page and size;
//                                              exits 1 when controls overlap (an error), 0 otherwise
#pragma once
#include <JuceHeader.h>

namespace layoutcheck
{
    struct Result { juce::StringArray errors, warnings; juce::var fingerprint; };

    // the controls under root: overlaps among siblings, cut text, out-of-parent; `page` labels the findings
    void inspect (juce::Component& root, const juce::String& page, Result& into);
}
