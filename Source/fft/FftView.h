// The Analyzer view's FFT style: a smooth spectrum curve in the manner of Voxengo SPAN and FabFilter
// Pro-Q. A filled main curve with a bright outline, a peak-hold line, and an optional secondary curve
// (another channel, or in the VST3 version the sidechain) in its own colour and style (line, filled,
// dashed) so both stay readable on top of each other.
//
//   header   Channel | Secondary | FFT size | Average | Slope | Smoothing | Hold | Freeze | Snapshot | Colours
//   Snapshot keeps the current spectrum as a ghost line (before / after); click again to clear it
//   display  log frequency 20 Hz - 20 kHz, dB scale (wheel scrolls, Ctrl+wheel zooms, double-click resets),
//            hover readout: frequency, note and cents, level of both curves; click resets the hold, Alt+click pins it
//
// The analysis (fft::FftAnalyzer) runs on the message thread from a FifoTap, only while shown.
#pragma once
#include <JuceHeader.h>
#include "../FifoTap.h"
#include "../Settings.h"
#include "FftAnalyzer.h"
#include "../reference/ReferenceTrack.h"

class FftView : public juce::Component
{
public:
    FftView (FifoTap& mainInput, Settings&, std::function<void()> settingsChanged);
    ~FftView() override;

    void setActive (bool shown);
    void onFrame (double vblankSeconds);
    // VST3: the input tap also carries the sidechain bus (FifoTap extras)
    void setSidechainAvailable (bool on) { scAvailable = on; applySettings(); }
    bool hasSidechain() const            { return scAvailable; }
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
    void setReference (ReferenceTrack* r)  { reference = r; }
    void setReferenceShown (bool on)       { refShown = on; repaint(); }
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
    bool scAvailable = false;
    Settings& s;
    std::function<void()> changed;
    fft::FftAnalyzer engine;
    bool active = false, frozen = false;
    double lastT = 0;
    juce::Rectangle<int> plot, header;
    ReferenceTrack* reference = nullptr;
    bool refShown = false;
    std::vector<float> refDb;
    std::vector<float> edges, mainDb, holdDb, secDb, snapPower, snapDb;   // snapPower: the Snapshot's spectrum (empty: none)
    int hoverX = -1, hoverY = -1, headerInset = 0;
    std::optional<std::pair<double, double>> pin;   // Alt+click: the readout stays at this (Hz, dB)

    juce::TextButton chanBtn, secBtn, sizeBtn, avgBtn, slopeBtn, smoothBtn, holdBtn { "Hold" }, freezeBtn { "Freeze" }, colourBtn { "Colours" }, snapBtn { "Snapshot" };
};
