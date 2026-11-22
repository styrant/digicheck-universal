#pragma once
#include <JuceHeader.h>
#include "UiScale.h"
#include "Settings.h"
#include "AudioEngine.h"
#include "analyzer/PluginProcessor.h"
#include "balance/LiveBalance.h"

// The bottom control strip shared by both views (it stays put when you switch views):
//
//   [Options] [Spectrogram|Analyzer|Balance] [LUFS] [Key] [Tempo]   <controls for the current view>   [LUFS boxes] [Reset] [Full screen]
//
// LUFS colours (loudness / estimated dynamics): white below -14, green -14..-9, yellow -9..-6, red -6 and up.
// True peak: white below -1 dBTP, yellow -1..0, red above 0.
// Spectrogram controls: presets, pause, reference level -/+, settings.
// Balance controls: target (its list ends with Open targets folder / Rescan / Target Builder), window
// (3 s / 10 s / 30 s / since reset), Curve|Diff, Reset.
// Analyzer controls: presets, bands/oct, channel, frequency grid, zoom -/+, Full, Freeze, Settings.
class BottomBar : public juce::Component, private juce::Timer
{
public:
    enum View { Spectrogram = 0, Analyzer = 1, Balance = 2 };
    static int height() { return ui::px (42); }

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
    void clickPresets (bool analyzer) { (analyzer ? anaPresets : specPresets).triggerClick(); }
    void setBalanceTargets (const juce::StringArray& names, int selected);   // selected -1: none

    std::function<void (int view)> onViewChange;
    std::function<void()> onPause, onToggleKey, onToggleTempo, onToggleFullscreen, onToggleLufs, onToggleOptions, onSpectrogramSettings;
    std::function<void (int deltaDb)> onRef;
    std::function<void()> onZoomIn, onZoomOut, onFullRange, onFreeze, onAnalyzerSettings;
    std::function<void (juce::Component& anchor)> onSpectrogramPresets, onAnalyzerPresets;
    std::function<void (int action)> onBalanceTargetsAction;     // 1 open folder, 2 rescan, 3 Target Builder
    std::function<void (int)> onBalanceTarget, onBalanceWindow;
    std::function<void (bool difference)> onBalanceMode;
    std::function<void()> onBalanceReset;

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

    juce::TextButton optionsBtn { "Options" }, viewSpec { "Spectrogram" }, viewAna { "Analyzer" }, viewBal { "Balance" }, lufsBtn { "LUFS" }, keyBtn { "Key" }, tempoBtn { "Tempo" };
    // spectrogram group
    juce::TextButton specPresets { "Presets" }, pause { "||" }, refDown { "-" }, refUp { "+" }, specSettings { "Settings" };
    juce::Label ref;
    // analyzer group
    juce::ComboBox bpoBox, chanBox, gridBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bpoAtt, chanAtt, gridAtt;
    juce::TextButton anaPresets { "Presets" }, zoomOut { "-" }, zoomIn { "+" }, fullRange { "Full" }, freeze { "Freeze" }, anaSettings { "Settings" };
    // balance group
    juce::ComboBox balTarget, balWindow;
    juce::TextButton balCurve { "Curve" }, balDiff { "Diff" }, balReset { "Reset" };
    int balSelected = 0;
    // right corner (small)
    LufsReadout lufs;
    juce::TextButton resetBtn { "Reset" }, full { "Full screen" };
    bool lufsDirty = false;
};
