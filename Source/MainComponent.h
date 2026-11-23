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
    void setFullscreenState (bool isFullscreen);     // called by the window owner
    void suspendGpu();                               // around moving to another window
    void resumeGpu();
    std::function<void (bool wantFullscreen)> onFullscreenRequest;
    void toggleTempoWindow();
    void setCursorForTest (double hz) { view.setCursorFrequency (hz); }
    void setView (int viewMode);
    void applyRendererForTest() { applyRenderer(); }
    Settings& getSettings() { return settings; }
    KeyPage& getKeyPage() { return keyPage; }
    ScopePage& getScopePage() { return scopePage; }
    MonitorPage& getMonitorPage() { return monitorPage; }
    AnalyzerPanel& getAnalyzerPanel() { return analyzerPanel; }
    void refreshBottomBarForTest() { bottom.refresh(); }

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
    Settings settings;
    SpectrumAnalyzerProcessor analyzerProc;   // declared before the engine: it must outlive the DSP thread
    // analysis taps fed from the DSP thread: declared before the engine so they outlive it
    KeyFinder keyFinder;
    LiveBalance liveBalance;
    LiveRecorder recorder;
    ScopeTap scopeTap;
    FifoTap monitorTap;
    FifoTap fftTap;
    TranscriptionSession transcription;
    ModelStore modelStore;
    TranscriptionPlayer keyPlayer;
    PlaybackOutput keyOutput { keyPlayer };
    TargetLibrary targetLibrary;
    AudioEngine engine;
    SpectrogramView view;
    AnalyzerPanel analyzerPanel { analyzerProc, fftTap, settings, [this] { saveAll(); } };
    BalanceView balanceView { liveBalance, targetLibrary };
    ScopePage scopePage { scopeTap, settings, [this] { saveAll(); } };
    MonitorPage monitorPage { monitorTap, settings, [this] { saveAll(); }, [this] { engine.resetLoudness(); } };
    KeyPage keyPage { keyFinder, transcription, recorder, modelStore, keyPlayer, &keyOutput, settings, [this] { saveAll(); } };
    void refreshBalanceTargets();
    void balanceTargetsAction (int action);
    static juce::File targetBuilderExe();
    BottomBar bottom { settings, engine, analyzerProc };

    juce::Label status;
    juce::ComboBox source, chL, chR, fpsBox, textBox;
    juce::Slider textSlider;
    juce::Label lblL { {}, "L" }, lblR { {}, "R" }, lblFps { {}, "Frame rate limit" }, lblText { {}, "Text size" };
    bool fullscreen = false, gpuSuspended = false;
    juce::TextButton startBtn { "Start" }, settingsBtn { "View settings" }, rescanBtn { "Rescan" };
    juce::ToggleButton gpuToggle { "GPU rendering (OpenGL)" };
    juce::VBlankAttachment frameTick { this, [this] (double t) { onFrame (t); } };
    std::vector<AudioEngine::Source> sources;
    juce::String error;
};
