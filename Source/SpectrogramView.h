#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include "AudioEngine.h"
#include <optional>

// The Spectroscope display, laid out like DigiCheck NG: a black plot inside a grey frame with
// frequency labels in both side margins. The control strip lives in BottomBar (shared by both views).
// Columns are one logical pixel wide; rows use full physical (DPI-scaled) resolution.
class SpectrogramView : public juce::Component, private juce::Timer
{
public:
    SpectrogramView (Settings& s, AudioEngine& e, std::function<void (Change)> notify);

    void settingsChanged (Change c);        // called after any settings change
    void sourceStarted();
    void sourceStopped();
    void togglePause();
    bool isPaused() const noexcept { return paused; }
    void clearCursor();
    void setCursorFrequency (double hz) { cursorF = hz; repaint(); }
    std::function<void (bool paused)> onPauseChanged;

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

    juce::Rectangle<int> plot;
    juce::Image img;
    std::vector<float> hist;
    int W = 1, H = 1, writeCol = 0, tick = 0;
    std::array<juce::PixelARGB, 512> lut;
    bool paused = false, running = false;
    std::optional<double> cursorF;
};
