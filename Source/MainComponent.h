#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include "AudioEngine.h"
#include "SpectrogramView.h"
#include "BottomBar.h"
#include "AnalyzerPanel.h"

class MainComponent : public juce::Component, private juce::Timer
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
    void setCursorForTest (double hz) { view.setCursorFrequency (hz); }
    void setView (int viewMode);
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
    bool barAutoHidden() const { return isFullscreen() && settings.autoHideBar; }
    void updateBarReveal();
    void layoutBar();

    // Top toolbar as its own component so it can overlay the display when auto-hidden in full screen
    struct Bar : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff16181c));
            g.setColour (juce::Colour (0xff2a2d33)); g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        }
    } bar;
    bool barRevealed = false;
    juce::uint32 lastInBar = 0;
    int tick = 0;
    std::unique_ptr<juce::DocumentWindow> keyWindow;

    void saveAll();

    juce::PropertiesFile& props;
    Settings settings;
    SpectrumAnalyzerProcessor analyzerProc;   // declared before the engine: it must outlive the DSP thread
    AudioEngine engine;
    SpectrogramView view;
    AnalyzerPanel analyzerPanel { analyzerProc };
    BottomBar bottom { settings, engine, analyzerProc };

    juce::Label title { {}, "Spectroscope" }, status;
    juce::ComboBox source, chL, chR;
    juce::Label lblL { {}, "L" }, lblR { {}, "R" };
    juce::TextButton startBtn { "Start" }, settingsBtn { "Settings" }, rescanBtn { "Rescan" };
    std::vector<AudioEngine::Source> sources;
    juce::String error;
};
