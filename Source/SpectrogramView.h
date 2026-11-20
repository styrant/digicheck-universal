#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include "AudioEngine.h"
#include <optional>

// The Spectroscope display: scrolling or stationary reassigned spectrogram with a log frequency
// axis, grid, colour scale, frequency/note cursor and a small on-display control panel.
// Columns are one logical pixel wide; rows use full physical (DPI-scaled) resolution.
class SpectrogramView : public juce::Component, private juce::Timer
{
public:
    SpectrogramView (Settings& s, AudioEngine& e, std::function<void (Change)> notify);

    void settingsChanged (Change c);        // called after any settings change
    void sourceStarted();                   // engine (re)started: new sample rate
    void sourceStopped();
    void togglePause();
    void clearCursor();
    bool isPaused() const noexcept { return paused; }

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::Colour colourAt (int map, double x);     // 0..1 along a colour map
    static const juce::StringArray& colourNames();

private:
    void timerCallback() override;
    void rebuild();
    void sendConfig();
    void buildLut();
    void repaintAll();
    int lutIndex (float db) const noexcept;
    std::pair<double, double> effectiveRange() const;
    float freqToY (double f) const;
    double yToFreq (float y) const;
    void setCursorFromY (float y);
    juce::String cursorText() const;

    Settings& s;
    AudioEngine& engine;
    std::function<void (Change)> notify;

    juce::Image img;
    std::vector<float> hist;
    int W = 1, H = 1, writeCol = 0, plotW = 1;
    std::array<juce::PixelARGB, 512> lut;
    bool paused = false, running = false;
    std::optional<double> cursorF;

    struct Panel : public juce::Component
    {
        Panel();
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::TextButton pause { "||" }, down { "-" }, up { "+" };
        juce::Label ref, readout;
    } panel;
};
