#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include "AudioEngine.h"
#include "SpectrogramView.h"

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
    void setCursorForTest (double hz) { view.setCursorFrequency (hz); }
    Settings& getSettings() { return settings; }

private:
    void timerCallback() override;
    void rescan();
    void startSelected();
    void stopSource();
    void onChange (Change c);
    void fillChannelBoxes();
    void updateStatus();

    juce::PropertiesFile& props;
    Settings settings;
    AudioEngine engine;
    SpectrogramView view;

    juce::Label title { {}, "Spectroscope" }, status;
    juce::ComboBox source, chL, chR;
    juce::Label lblL { {}, "L" }, lblR { {}, "R" };
    juce::TextButton startBtn { "Start" }, settingsBtn { "Settings" }, rescanBtn { "Rescan" };
    std::vector<AudioEngine::Source> sources;
    juce::String error;
};
