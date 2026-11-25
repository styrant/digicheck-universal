#pragma once
#include <JuceHeader.h>
#include "UiScale.h"
#include "Settings.h"
#include "AudioEngine.h"
#include "SpectrogramView.h"
#include "BottomBar.h"
#include "AnalyzerPanel.h"
#include "FrameClock.h"
#include "SpectrogramPresets.h"
#include "balance/BalanceView.h"
#include "key/KeyPage.h"
#include "scope/ScopePage.h"
#include "monitor/MonitorPage.h"
#include "Core.h"
#include "reference/ReferencePanel.h"

class MainComponent : public juce::Component, private juce::Timer, private juce::OpenGLRenderer
{
public:
    // globalProps: presets and other app-wide things; core: settings, analysers and taps (owned by the
    // app window or the plugin processor); engine: the analysis they're fed by; device: the standalone
    // app's audio input (nullptr in the plugin); host: the plugin (nullptr in the standalone app)
    MainComponent (juce::PropertiesFile& globalProps, Core& core, AnalysisEngine& engine, AudioEngine* device, HostLink* host);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    void startSourceByKey (const juce::String& key);   // used by --test-screenshot
    void openSettings();
    void toggleFullscreen();
    void setBarHidden (bool hidden);                 // full screen only: the bottom bar too (it peeks back at the bottom edge)
    void setHelpMode (bool on);
    void setFullscreenState (bool isFullscreen);     // called by the window owner
    void suspendGpu();                               // around moving to another window
    void setCpuOverride (bool on)                    { cpuOverride = on; applyRenderer(); }   // full screen on an HDR monitor (mode 1)
    void resumeGpu();
    std::function<void (bool wantFullscreen)> onFullscreenRequest;
    void toggleTempoWindow();
    void setCursorForTest (double hz) { view.setCursorFrequency (hz); }
    void setView (int viewMode);
    void setCustomForTest (int layout, const juce::String& slots) { settings.customLayout = layout; settings.customSlots = slots; setView (BottomBar::Custom); }
    void applyRendererForTest() { applyRenderer(); }
    Settings& getSettings() { return settings; }
    KeyPage& getKeyPage() { return keyPage; }
    ScopePage& getScopePage() { return scopePage; }
    MonitorPage& getMonitorPage() { return monitorPage; }
    AnalyzerPanel& getAnalyzerPanel() { return analyzerPanel; }
    void refreshBottomBarForTest() { bottom.refresh(); }
    void blockSavingForTest()       { core.saveBlocked = true; }       // checks that resize the window and change pages

private:
    void timerCallback() override;
    void setUpPlugin();
    void updateListen();
    double lastHostBpm = 0;
    void rescan();
    void startSelected();
    void stopSource();
    void onChange (Change c);
    void fillChannelBoxes();
    void updateStatus();
    bool isFullscreen() const;
    void toggleOptions();
    void applyTextScale();
    void setOptionsVisible (bool);
    void layoutOptions();
    void onFrame (double vblankTime);
    void showSpectrogramPresets (juce::Component& anchor);
    void showAnalyzerPresets (juce::Component& anchor);
    void askPresetName (const juce::String& title, std::function<void (const juce::String&)> done);
    void importAnalyzerPreset();
    std::unique_ptr<juce::FileChooser> chooser;

    // "Options" pop-up above the bottom bar: audio input (source, channels, start/stop) and display
    // (GPU rendering, frame-rate limit). Replaces the old top toolbar.
    struct OptionsPanel : public juce::Component
    {
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            g.setColour (juce::Colour (0xf21b1d22)); g.fillRoundedRectangle (r, 8.0f);
            g.setColour (juce::Colour (0xff3a3c40)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (juce::Colours::white.withAlpha (0.5f)); g.setFont (juce::FontOptions (ui::fs (10.5f), juce::Font::bold));
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
    std::unique_ptr<juce::DocumentWindow> tempoWindow;
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
    Core& core;
    AnalysisEngine& engine;
    AudioEngine* device;
    HostLink* host;
    bool inputRunning() const { return device != nullptr ? device->isRunning() : (host != nullptr && host->isProcessing()); }
    // the core's parts, under the names the rest of this class uses
    Settings& settings = core.settings;
    SpectrumAnalyzerProcessor& analyzerProc = core.analyzerProc;
    KeyFinder& keyFinder = core.keyFinder;
    LiveBalance& liveBalance = core.liveBalance;
    LiveRecorder& recorder = core.recorder;
    FifoTap& scopeTap = core.scopeTap;
    FifoTap& monitorTap = core.monitorTap;
    FifoTap& fftTap = core.fftTap;
    TranscriptionSession& transcription = core.transcription;
    ModelStore& modelStore = core.modelStore;
    TranscriptionPlayer& keyPlayer = core.keyPlayer;
    PlaybackOutput& keyOutput = core.keyOutput;
    TargetLibrary& targetLibrary = core.targetLibrary;
    SpectrogramView view;
    AnalyzerPanel analyzerPanel { analyzerProc, fftTap, settings, [this] { saveAll(); } };
    BalanceView balanceView { liveBalance, targetLibrary };
    ScopePage scopePage { scopeTap, settings, [this] { saveAll(); } };
    MonitorPage monitorPage { monitorTap, settings, [this] { saveAll(); }, [this] { engine.resetLoudness(); }, core.plugin ? &core.monitorModel : nullptr };
    KeyPage keyPage { keyFinder, transcription, recorder, modelStore, keyPlayer, &keyOutput, settings, [this] { saveAll(); } };
    void refreshBalanceTargets();
    void balanceTargetsAction (int action);
    static juce::File targetBuilderExe();
    BottomBar bottom { settings, engine, analyzerProc };

    juce::Label status;
    juce::ComboBox source, chL, chR, fpsBox, textBox, hdrBox;
    juce::Slider textSlider;
    juce::Label lblL { {}, "L" }, lblR { {}, "R" }, lblFps { {}, "Frame rate limit" }, lblText { {}, "Text size" }, lblHdr { {}, "HDR full screen" };
    bool fullscreen = false, gpuSuspended = false, cpuOverride = false;
    bool barHidden = false, barPeek = false, pendingHideBar = false;
    double barLeaveTime = 0;
    std::unique_ptr<juce::TooltipWindow> helpTips;

    // Custom page: the panes, and the dividers between them (dragged to resize)
    juce::Component* viewComponent (int id);
    bool shows (int id) const;                       // shown now (alone, or in a Custom pane)
    std::vector<juce::Rectangle<int>> customPanes (juce::Rectangle<int> area) const;
    struct Divider { juce::Rectangle<int> r; bool vertical; int which; juce::Rectangle<int> span; };   // which: 0 splitA, 1 splitB
    std::vector<Divider> dividers;
    int dragDivider = -1;
    juce::Rectangle<int> contentArea;
    void notePaneClick (juce::Component&);
    std::unique_ptr<ReferencePanel> refPanel;
    void setReferenceVisible (bool);
    void referenceChanged();
    // Ctrl+C: the page (Custom: Ctrl+Shift+C the focused pane) as an image on the clipboard and in the Screenshots folder
    void copyViewImage (bool paneOnly);
    void showToast (const juce::String& text);
    void updateNoInput();
    juce::Label toast;
    // shown over every page (but Key, which has its own) while no input runs in the app
    struct NoInputBanner : public juce::Component
    {
        juce::TextButton open { "Open Options" };
        NoInputBanner() { addAndMakeVisible (open); open.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4c8dff)); setInterceptsMouseClicks (false, true); }
        void paint (juce::Graphics& g) override
        {
            g.setColour (juce::Colour (0xe0181a1e)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
            g.setColour (juce::Colour (0xff3a3d44)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 8.0f, 1.0f);
            g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (ui::fs (15.0f), juce::Font::bold));
            g.drawText ("No input", getLocalBounds().removeFromTop (getHeight() / 3).translated (0, ui::px (6)), juce::Justification::centred);
            g.setColour (juce::Colour (0xffb8bec9)); g.setFont (juce::FontOptions (ui::fs (13.0f)));
            g.drawText ("Choose an input in Options and press Start.", getLocalBounds().withTrimmedTop (getHeight() / 3).removeFromTop (getHeight() / 3), juce::Justification::centred);
        }
        void resized() override { open.setBounds (getLocalBounds().removeFromBottom (getHeight() / 3).withSizeKeepingCentre (ui::px (130), ui::px (28)).translated (0, -ui::px (6))); }
    } noInput;
    double toastUntil = 0;
    std::unique_ptr<juce::XmlElement> loadLayouts() const;
    void refreshSavedLayouts();
    void saveLayoutAs (const juce::String& name);
    void loadLayout (const juce::String& name);
    void deleteLayout (const juce::String& name);
    juce::TextButton startBtn { "Start" }, settingsBtn { "View settings" }, rescanBtn { "Rescan" }, manualBtn { "User manual" }, copyImgBtn { "Copy view as image" },
                     backupBtn { "Back up..." }, restoreBtn { "Restore..." }, defaultsBtn { "Reset to defaults" }, updateBtn;
    juce::ToggleButton updateToggle { "Check for updates" }, safeToggle { "Colour-blind-friendly meters" },
                       bridgeToggle { "Send to the UAV app (its From DAW input)" };
    void backupSettings();
    void restoreSettings();
    void resetDefaults();
    void checkForUpdates();
    void onUpdateResult (const juce::String& latestVersion);
    struct UpdateCheck;
    std::unique_ptr<juce::Thread> updateThread;
    juce::String newerVersion;
    juce::ToggleButton gpuToggle { "GPU rendering (OpenGL)" };
    juce::VBlankAttachment frameTick { this, [this] (double t) { onFrame (t); } };
    std::vector<AudioEngine::Source> sources;
    juce::String error;
};
