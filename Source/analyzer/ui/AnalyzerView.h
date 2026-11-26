#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../dsp/AnalyzerModel.h"

// The analyzer display: bars / curves for peak, peak-hold and average, an
// adaptive frequency grid, zoom & pan, and a band inspector.
//
// Mouse:  wheel = zoom frequency around cursor, drag = pan,
//         shift+drag = zoom to selection, ctrl/cmd+wheel = ceiling,
//         alt+wheel = level range, double-click = reset meters,
//         double-click on frequency axis = full range, right-click = settings
// Keys:   R reset, F freeze, 0 full range, +/- zoom, I inspector
class AnalyzerView final : public juce::Component
{
public:
    explicit AnalyzerView (SpectrumAnalyzerProcessor&);
    ~AnalyzerView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed (const juce::KeyPress&) override;

    void resetMeters();
    void toggleFreeze();
    bool isFrozen() const noexcept { return frozen; }
    void resetZoom();
    void zoomBy (double factor, double aroundHz = 0.0);

    // Advance meters and repaint; normally driven by vsync. Public so that
    // the screenshot tool (tools/RenderPreview.cpp) can drive it offline.
    void advance (double timestampSec);
    void showInspectorAt (juce::Point<float> p) { mousePos = p; hovering = true; }

    std::function<void()> onSettingsRequested;
    std::function<void()> onStateChanged;   // freeze/zoom changed (for toolbar)

    // full frequency span of the current band layout
    juce::Range<double> getFullRange() const noexcept;
    juce::Range<double> getViewRange() const noexcept;
    void setViewRange (double lowHz, double highHz);
    bool isZoomed() const noexcept { return processor.viewLowHz.load() > 0.0 && processor.viewHighHz.load() > 0.0; }

private:
    void syncLayout();

    juce::Rectangle<float> plotArea() const noexcept;
    float freqToX (double f, juce::Range<double> view, juce::Rectangle<float> plot) const noexcept;
    double xToFreq (float x, juce::Range<double> view, juce::Rectangle<float> plot) const noexcept;
    float levelToY (double db, juce::Rectangle<float> plot) const noexcept;
    double yToLevel (float y, juce::Rectangle<float> plot) const noexcept;
    int bandAt (double freq) const noexcept;

    void drawBackground (juce::Graphics&, juce::Rectangle<float> plot, juce::Range<double> view);
    void drawFrequencyGrid (juce::Graphics&, juce::Rectangle<float> plot, juce::Range<double> view);
    void drawLevelGrid (juce::Graphics&, juce::Rectangle<float> plot);
    void drawBars (juce::Graphics&, const double* level, bool full, int size, juce::Colour,
                   juce::Rectangle<float> plot, juce::Range<double> view);
    void drawCurve (juce::Graphics&, const double* level, bool fill, int size, juce::Colour,
                    juce::Rectangle<float> plot, juce::Range<double> view);
    void drawInspector (juce::Graphics&, juce::Rectangle<float> plot, juce::Range<double> view,
                        const double* peak, const double* hold, const double* avg);
    void drawStatus (juce::Graphics&, juce::Rectangle<float> plot, juce::Range<double> view);

    SpectrumAnalyzerProcessor& processor;
    juce::AudioProcessorValueTreeState& state;

    std::unique_ptr<sa::BandLayout>    layout;
    std::unique_ptr<sa::AnalyzerModel> model;

    bool frozen = false;
    std::array<double, sa::kMaxBands> frozenPeak {}, frozenHold {}, frozenAvg {};

    double lastTimestamp = 0.0;
    juce::Point<float> mousePos;
    bool hovering = false;
    bool lastFrozenDrawn = false;
    std::optional<std::pair<double, double>> pin;   // Alt+click: the readout stays at this (Hz, dB)

    enum class Drag { None, Pan, Select };
    Drag drag = Drag::None;
    float dragStartX = 0.0f, dragCurrentX = 0.0f;
    juce::Range<double> dragStartView;
    float wheelAccumulator = 0.0f;

    juce::Font smallFont, infoFont;
    juce::VBlankAttachment vblank;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnalyzerView)
};
