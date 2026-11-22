// Tonal Balance view: the live tonal balance of the input against a target.
//
//   left:  four band meters (Low 0-250 Hz, Low-mid 250-2k, High-mid 2k-8k, High 8k+). The blue zone is
//          the target's range; the marker shows where the input sits (green in range, amber just
//          outside, red further out) with how many dB it is above or below the range.
//   right: the detailed curve, 20 Hz - 20 kHz. "Curve" shows the input over the target's range;
//          "Diff" shows the input minus the target, so 0 dB means right on target.
//
// Drawing: driven by the app's display frame clock (vblank, obeying the frame-rate cap), not a timer.
// The curve and meters glide towards each new measurement, so they move smoothly at the display's
// refresh rate even though measurements arrive every 23-100 ms. Everything that doesn't move (panels,
// grid, labels, the target's range) is drawn once into a cached image; per frame only the live curve
// (about one point per two pixels), the meters and the hover readout are drawn. With the GPU renderer
// on, all of it is composited by OpenGL like the other views.
#pragma once
#include <JuceHeader.h>
#include "LiveBalance.h"
#include "TargetLibrary.h"

class BalanceView : public juce::Component, private juce::Timer
{
public:
    BalanceView (LiveBalance&, TargetLibrary&);

    void setTargetIndex (int index);              // -1: none
    void setDifferenceMode (bool d);
    void setInputRunning (bool r);
    void setActive (bool a);
    void onFrame (double vblankSeconds);          // from the display frame clock
    void invalidateStatic()                       { staticDirty = true; repaint(); }

    std::function<void()> onOpenFolder, onOpenBuilder, onTargetsChanged;
    bool builderAvailable = false;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override;                // slow housekeeping: notice new target files
    void renderStatic();
    void paintStatic (juce::Graphics&);
    void paintBandsLive (juce::Graphics&);
    void paintCurveLive (juce::Graphics&);
    const tonal::Target* target() const;
    float xOf (double hz) const;
    double hzAt (float x) const;
    float yOf (double db) const;
    double baseAt (float binPos) const;                // what the curve is drawn relative to (target mean in Diff mode)

    LiveBalance& live;
    TargetLibrary& library;
    int targetIndex = -1, lastSerial = -1, tick = 0;
    bool difference = false, running = false, active = false, staticDirty = true;

    // what is on screen: glides towards the latest measurement
    bool shownValid = false;
    std::vector<double> shownCurve;               // smoothed spectrum (kBins)
    std::array<double, 4> shownBandDb {};         // band share relative to the band range's centre, dB
    double shownSeconds = 0, lastFrameTime = 0;
    LiveBalance::Snapshot latest;

    juce::Rectangle<int> bandArea, curveArea, plot;
    std::vector<std::pair<float, float>> columns; // x position and (fractional) spectrum bin for each drawn curve point
    double top = -30, span = 60;
    juce::Image staticLayer;
    float staticScale = 1.0f;
    juce::Point<float> hover { -1, -1 };
    juce::TextButton openFolder { "Open targets folder" }, openBuilder { "Open Target Builder" };

    // optional paint timing (SPECTROSCOPE_PAINT_LOG=1)
    double paintMsSum = 0;
    int paintCount = 0;
};
