#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include "AudioEngine.h"
#include "SpectrogramView.h"
#include "BottomBar.h"
#include "AnalyzerPanel.h"
#include "FrameClock.h"

class MainComponent : public juce::Component, private juce::Timer, private juce::OpenGLRenderer
{
public:
    explicit MainComponent (juce::PropertiesFile& props);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    void startSourceByKey (const juce::String& key);   // used by --test-screenshot
    void openSettings();
    void toggleFullscreen();
    void toggleKeyWindow();
    void toggleTempoWindow();
    void setCursorForTest (double hz) { view.setCursorFrequency (hz); }
    void setView (int viewMode);
    void applyRendererForTest() { applyRenderer(); }
    Settings& getSettings() { return settings; }

private:
    void timerCallback() override;
    void rescan();
    void startSelected();
    void stopSource();
    void onChange (Change c);
    void fillChannelBoxes();
    void updateStatus();
    bool isFullscreen() const;
    void toggleOptions();
    void setOptionsVisible (bool);
    void layoutOptions();
    void onFrame (double vblankTime);

    // "Options" pop-up above the bottom bar: audio input (source, channels, start/stop) and display
    // (GPU rendering, frame-rate limit). Replaces the old top toolbar.
    struct OptionsPanel : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xf21b1d22)); g.fillRoundedRectangle (r, 8.0f);
            g.setColour (juce::Colour (0xff3a3c40)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.5f)); g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            for (auto& h : headings) g.drawText (h.second, h.first, juce::Justification::centredLeft);
        }
        std::vector<std::pair<juce::Rectangle<int>, juce::String>> headings;
    } options;

    // closes the Options panel when clicking anywhere else in the window
    struct OutsideClick : public juce::MouseListener
    {
        explicit OutsideClick (MainComponent& o) : owner (o) {}
        void mouseDown (const juce::MouseEvent& e) override;
        MainComponent& owner;
    } outsideClick { *this };

    int tick = 0;
    std::unique_ptr<juce::DocumentWindow> keyWindow, tempoWindow;
    std::atomic<int> glFrames { 0 };
    double glFpsTime = 0;

    void saveAll();
    void applyRenderer();
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;

    juce::OpenGLContext glContext;
    std::atomic<float> ownerW { 1.0f }, ownerH { 1.0f };

    juce::PropertiesFile& props;
    Settings settings;
    SpectrumAnalyzerProcessor analyzerProc;   // declared before the engine: it must outlive the DSP thread
    AudioEngine engine;
    SpectrogramView view;
    AnalyzerPanel analyzerPanel { analyzerProc };
    BottomBar bottom { settings, engine, analyzerProc };

    juce::Label status;
    juce::ComboBox source, chL, chR, fpsBox;
    juce::Label lblL { {}, "L" }, lblR { {}, "R" }, lblFps { {}, "Frame rate limit" };
    juce::TextButton startBtn { "Start" }, settingsBtn { "View settings" }, rescanBtn { "Rescan" };
    juce::ToggleButton gpuToggle { "GPU rendering (OpenGL)" };
    juce::VBlankAttachment frameTick { this, [this] (double t) { onFrame (t); } };
    std::vector<AudioEngine::Source> sources;
    juce::String error;
};
