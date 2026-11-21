#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include "AudioEngine.h"
#include "analyzer/PluginProcessor.h"

// The bottom control strip shared by both views (it stays put when you switch views):
//
//   [Options] [Spectrogram|Analyzer] [LUFS] [Key] [Tempo]   <controls for the current view>   [LUFS boxes] [Reset] [Full screen]
//
// LUFS colours (loudness / estimated dynamics): white below -14, green -14..-9, yellow -9..-6, red -6 and up.
// True peak: white below -1 dBTP, yellow -1..0, red above 0.
// Spectrogram controls: pause, reference level -/+.
// Analyzer controls: bands/oct, channel, frequency grid, zoom -/+, Full, Freeze, Settings.
class BottomBar : public juce::Component, private juce::Timer
{
public:
    enum View { Spectrogram = 0, Analyzer = 1 };
    static constexpr int kHeight = 42;

    BottomBar (Settings&, AudioEngine&, SpectrumAnalyzerProcessor&);
    ~BottomBar() override;

    void refresh();                          // re-read settings (view, toggles, ref level)
    void setPaused (bool);
    void setFullscreen (bool);
    void setKeyOpen (bool);
    void setTempoOpen (bool);
    void setOptionsOpen (bool);
    void onFrame();                          // called on each (capped) display frame
    juce::Component& getOptionsButton() noexcept { return optionsBtn; }
    void setAnalyzerState (bool frozen, bool zoomed);

    std::function<void (int view)> onViewChange;
    std::function<void()> onPause, onToggleKey, onToggleTempo, onToggleFullscreen, onToggleLufs, onToggleOptions, onSpectrogramSettings;
    std::function<void (int deltaDb)> onRef;
    std::function<void()> onZoomIn, onZoomOut, onFullRange, onFreeze, onAnalyzerSettings;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    struct LufsReadout : public juce::Component
    {
        double st = -INFINITY, in = -INFINITY, tp = -INFINITY;
        bool active = false;
        void paint (juce::Graphics&) override;
    };

    Settings& s;
    AudioEngine& engine;

    juce::TextButton optionsBtn { "Options" }, viewSpec { "Spectrogram" }, viewAna { "Analyzer" }, lufsBtn { "LUFS" }, keyBtn { "Key" }, tempoBtn { "Tempo" };
    // spectrogram group
    juce::TextButton pause { "||" }, refDown { "-" }, refUp { "+" }, specSettings { "Settings" };
    juce::Label ref;
    // analyzer group
    juce::ComboBox bpoBox, chanBox, gridBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bpoAtt, chanAtt, gridAtt;
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, fullRange { "Full" }, freeze { "Freeze" }, anaSettings { "Settings" };
    // right corner (small)
    LufsReadout lufs;
    juce::TextButton resetBtn { "Reset" }, full { "Full screen" };
    bool lufsDirty = false;
};
