#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include "AudioEngine.h"
#include "analyzer/PluginProcessor.h"

// The bottom control strip shared by both views (it stays put when you switch views):
//
//   [Spectrogram|Analyzer] [LUFS] [Key]   <controls for the current view>   [LUFS boxes] [Reset] [Auto-hide] [Full screen]
//
// Spectrogram controls: pause, reference level -/+.
// Analyzer controls: bands/oct, channel, frequency grid, zoom -/+, Full, Freeze, Settings.
class BottomBar : public juce::Component, private juce::Timer
{
public:
    enum View { Spectrogram = 0, Analyzer = 1 };
    static constexpr int kHeight = 36;

    BottomBar (Settings&, AudioEngine&, SpectrumAnalyzerProcessor&);
    ~BottomBar() override;

    void refresh();                          // re-read settings (view, toggles, ref level)
    void setPaused (bool);
    void setFullscreen (bool);
    void setKeyOpen (bool);
    void setAnalyzerState (bool frozen, bool zoomed);

    std::function<void (int view)> onViewChange;
    std::function<void()> onPause, onToggleKey, onToggleFullscreen, onToggleAutoHide, onToggleLufs;
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

    juce::TextButton viewSpec { "Spectrogram" }, viewAna { "Analyzer" }, lufsBtn { "LUFS" }, keyBtn { "Key" };
    // spectrogram group
    juce::TextButton pause { "||" }, refDown { "-" }, refUp { "+" };
    juce::Label ref;
    // analyzer group
    juce::ComboBox bpoBox, chanBox, gridBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bpoAtt, chanAtt, gridAtt;
    juce::TextButton zoomOut { "-" }, zoomIn { "+" }, fullRange { "Full" }, freeze { "Freeze" }, anaSettings { "Settings" };
    // right corner (small)
    LufsReadout lufs;
    juce::TextButton resetBtn { "Reset" }, autoHide { "Auto-hide" }, full { "Full screen" };
};
