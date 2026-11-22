// Tonal Balance view: the live tonal balance of the input against a target.
//
//   left:  four band meters (Low 0-250 Hz, Low-mid 250-2k, High-mid 2k-8k, High 8k+). The blue zone is
//          the target's range; the marker shows where the input sits (green in range, amber just
//          outside, red further out) with how many dB it is above or below the range.
//   right: the detailed curve, 20 Hz - 20 kHz. "Curve" shows the input over the target's range;
//          "Difference" shows the input minus the target, so 0 dB means right on target.
// Targets come from the targets folder (TargetLibrary); the measurement is LiveBalance.
#pragma once
#include <JuceHeader.h>
#include "LiveBalance.h"
#include "TargetLibrary.h"

class BalanceView : public juce::Component, private juce::Timer
{
public:
    BalanceView (LiveBalance&, TargetLibrary&);

    void setTargetIndex (int index);              // -1: none
    void setDifferenceMode (bool d)               { difference = d; repaint(); }
    void setInputRunning (bool r)                 { running = r; }
    void setActive (bool a);

    std::function<void()> onOpenFolder, onOpenBuilder, onTargetsChanged;
    bool builderAvailable = false;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void paintBands (juce::Graphics&, juce::Rectangle<int>);
    void paintCurve (juce::Graphics&, juce::Rectangle<int>);
    void paintEmpty (juce::Graphics&, juce::Rectangle<int>);
    const tonal::Target* target() const;
    float xOf (double hz) const;
    double hzAt (float x) const;

    LiveBalance& live;
    TargetLibrary& library;
    LiveBalance::Snapshot snap;
    int targetIndex = -1, lastSerial = -1;
    bool difference = false, running = false;
    juce::Rectangle<int> bandArea, curveArea, plot;
    juce::Point<float> hover { -1, -1 };
    juce::TextButton openFolder { "Open targets folder" }, openBuilder { "Open Target Builder" };
};
