// The Analyzer view's FFT style: a smooth spectrum curve in the manner of Voxengo SPAN and FabFilter
// Pro-Q. A filled main curve with a bright outline, a peak-hold line, and an optional secondary curve
// (another channel, or in the VST3 version the sidechain) in its own colour and style (line, filled,
// dashed) so both stay readable on top of each other.
//
//   header   Channel | Secondary | FFT size | Average | Slope | Smoothing | Hold | Freeze | Colours
//   display  log frequency 20 Hz - 20 kHz, dB scale (wheel scrolls, Ctrl+wheel zooms, double-click resets),
//            hover readout: frequency, note and cents, level of both curves; click resets the hold
//
// The analysis (fft::FftAnalyzer) runs on the message thread from a FifoTap, only while shown.
#pragma once
#include <JuceHeader.h>
#include "../FifoTap.h"
#include "../Settings.h"
#include "FftAnalyzer.h"

class FftView : public juce::Component
{
public:
    FftView (FifoTap& mainInput, Settings&, std::function<void()> settingsChanged);
    ~FftView() override;

    void setActive (bool shown);
    void onFrame (double vblankSeconds);
    void setSidechainInput (FifoTap* sc) { sidechain = sc; applySettings(); }   // VST3: the sidechain bus
    bool hasSidechain() const            { return sidechain != nullptr; }
    void applySettings();
    void setHeaderInset (int px)         { headerInset = px; resized(); }      // room on the left for the host's controls
    void toggleFreeze()                  { frozen = ! frozen; updateButtons(); }
    bool isFrozen() const                { return frozen; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    fft::FftAnalyzer& model() { return engine; }
    void loadForTest (const juce::File& f, const juce::File* sidechainFile = nullptr);  // self-test: runs files through it and freezes

private:
    void configure();
    void updateButtons();
    void menuFor (juce::TextButton& b, int which);
    void colourMenu();
    float xOfHz (double hz) const;
    double hzAtX (float x) const;
    float yOfDb (double db) const;
    void computeEdges();
    juce::Path curvePath (const std::vector<float>& db, bool closed) const;

    FifoTap& input;
    FifoTap* sidechain = nullptr;
    Settings& s;
    std::function<void()> changed;
    fft::FftAnalyzer engine;
    bool active = false, frozen = false;
    double lastT = 0;
    juce::Rectangle<int> plot, header;
    std::vector<float> edges, mainDb, holdDb, secDb;
    int hoverX = -1, headerInset = 0;

    juce::TextButton chanBtn, secBtn, sizeBtn, avgBtn, slopeBtn, smoothBtn, holdBtn { "Hold" }, freezeBtn { "Freeze" }, colourBtn { "Colours" };
};
