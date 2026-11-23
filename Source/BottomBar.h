#pragma once
#include <JuceHeader.h>
#include "UiScale.h"
#include "Settings.h"
#include "AnalysisEngine.h"
#include "analyzer/PluginProcessor.h"
#include "balance/LiveBalance.h"

// The bottom control strip shared by both views (it stays put when you switch views):
//
//   [Options] [Spectrogram|Analyzer|Balance|Key|Scope|Monitor] [LUFS] [Tempo]   <controls for the current view>   [LUFS boxes] [Reset] [Full screen]
//
// LUFS colours (loudness / estimated dynamics): white below -14, green -14..-9, yellow -9..-6, red -6 and up.
// True peak: white below -1 dBTP, yellow -1..0, red above 0.
// Spectrogram controls: presets, pause, reference level -/+, frequency scale, channel (L / R / Mid / Side), settings.
// Balance controls: target (its list ends with Open targets folder / Rescan / Target Builder), window
// (3 s / 10 s / 30 s / since reset), Curve|Diff, Reset.
// Analyzer controls: presets, bands/oct, channel, frequency grid, zoom -/+, Full, Freeze, Settings.
class BottomBar : public juce::Component, private juce::Timer
{
public:
    enum View { Spectrogram = 0, Analyzer = 1, Balance = 2, Key = 3, Scope = 4, Monitor = 5, Custom = 6, NumViews = 7 };
    static juce::StringArray viewNames() { return { "Spectrogram", "Analyzer", "Balance", "Key", "Scope", "Monitor" }; }
    static int panesIn (int layout)      { static const int n[6] = { 2, 2, 3, 3, 3, 4 }; return n[juce::jlimit (0, 5, layout)]; }
    static std::array<int, 4> customSlots (const Settings&);   // the view in each pane (-1 empty)
    static int height() { return ui::px (42); }

    BottomBar (Settings&, AnalysisEngine&, SpectrumAnalyzerProcessor&);
    ~BottomBar() override;

    void refresh();                          // re-read settings (view, toggles, ref level)
    std::function<void()> onSaveLayout, onDeleteLayout, onUpdateLayout;     // Custom layouts: ask for a name / delete the current one
    std::function<void (const juce::String&)> onLoadLayout;
    juce::StringArray savedLayouts;          // names, set by the owner
    void setPaused (bool);
    void setFullscreen (bool);
    void setBarHidden (bool);
    void setHelp (bool);
    void setUpdateAvailable (const juce::String& version);
    void setTempoOpen (bool);
    void setOptionsOpen (bool);
    void onFrame();                          // called on each (capped) display frame
    juce::Component& getOptionsButton() noexcept { return optionsBtn; }
    void setAnalyzerState (bool frozen, bool zoomed);
    void clickPresets (bool analyzer) { (analyzer ? anaPresets : specPresets).triggerClick(); }
    void setBalanceTargets (const juce::StringArray& names, int selected);   // selected -1: none

    std::function<void (int view)> onViewChange;
    std::function<void()> onCustomChanged, onToggleHelp, onToggleHideBar;
    std::function<void()> onPause, onToggleTempo, onToggleFullscreen, onToggleLufs, onToggleOptions, onSpectrogramSettings;
    std::function<void (int deltaDb)> onRef;
    std::function<void()> onZoomIn, onZoomOut, onFullRange, onFreeze, onAnalyzerSettings;
    std::function<void (juce::Component& anchor)> onSpectrogramPresets, onAnalyzerPresets;
    std::function<void (int action)> onBalanceTargetsAction;     // 1 open folder, 2 rescan, 3 Target Builder
    std::function<void (int)> onBalanceTarget, onBalanceWindow, onBalanceSmooth;
    std::function<void (bool difference)> onBalanceMode;
    std::function<void()> onBalanceReset;
    std::function<void()> onLoudnessReset;                      // after Reset (the Monitor page resets with it)
    // VST3: Listen (band listening) and the spectrogram's sidechain display; Full screen is hidden
    void setPluginMode (bool on)                                { plugin = on; resized(); }
    void setListenState (bool active, const juce::String& what);
    std::function<void (juce::Component& anchor)> onListen, onSidechainMenu;
    std::function<void()> onSpectrogramDsp;                     // scale or channel picked in the bar

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
    AnalysisEngine& engine;
    std::unique_ptr<juce::LookAndFeel> smallLnf;     // smaller text for the corner buttons (declared before them)

    juce::TextButton optionsBtn { "Options" }, viewSpec { "Spectrogram" }, viewAna { "Analyzer" }, viewBal { "Balance" }, viewKey { "Key" }, viewScope { "Scope" }, viewMon { "Monitor" }, viewCustom { "Custom" }, lufsBtn { "LUFS" }, tempoBtn { "Tempo" }, listenBtn { "Listen" }, scBtn { "SC" };
    bool plugin = false;
    // spectrogram group
    juce::TextButton specPresets { "Presets" }, pause { "||" }, refDown { "-" }, refUp { "+" }, specSettings { "Settings" };
    juce::Label ref;
    juce::ComboBox specScale, specChannel;
    // analyzer group
    juce::ComboBox bpoBox, chanBox, gridBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> bpoAtt, chanAtt, gridAtt;
    juce::TextButton anaPresets { "Presets" }, zoomOut { "-" }, zoomIn { "+" }, fullRange { "Full" }, freeze { "Freeze" }, anaSettings { "Settings" };
    // balance group
    juce::ComboBox balTarget, balWindow, balSmooth;
    juce::TextButton balCurve { "Curve" }, balDiff { "Diff" }, balReset { "Reset" };
    int balSelected = 0;
    // right corner (small)
    LufsReadout lufs;
    juce::TextButton resetBtn { "Reset" }, full { "Full screen" }, helpBtn { "?" }, hideBarBtn { "Hide bar" }, updateBtn;
    // custom group
    juce::TextButton customBtn;
    void showCustomMenu();
    int cv() const;
    juce::String customLongText;
    bool lufsDirty = false;
};
