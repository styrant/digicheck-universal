// The VST3 version's Listen panel (opened from the bottom bar's Listen button): hear only some bands
// and / or only Mid, Side, Left or Right, like Metric AB's band solo or ISOL8.
//
//   Stereo | Mid | Side | Left | Right
//   [Sub] [Low] [Low-mid] [High-mid] [High]      click: heard / not; double-click: that band alone
//   the four crossover frequencies (sliders)
//   All bands | Off (everything back to normal)
#pragma once
#include <JuceHeader.h>
#include "BandIsolator.h"
#include "../Settings.h"

class ListenPanel : public juce::Component, private juce::Timer
{
public:
    ListenPanel (BandIsolatorParams&, Settings&, std::function<void()> changed);
    void resized() override;
    void paint (juce::Graphics&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;   // on a band button: solo
    static juce::String summary (const BandIsolatorParams&);    // "Low + Mid, Side" (for the button)

private:
    void timerCallback() override { update(); }
    void update();
    void saveSplits();

    BandIsolatorParams& p;
    Settings& s;
    std::function<void()> changed;
    std::array<juce::TextButton, 5> modeBtns, bandBtns;
    std::array<juce::Slider, 4> splitSliders;
    juce::TextButton allBtn { "All bands" }, offBtn { "Off" };
};
