#include "MainComponent.h"
#include "ClipboardImage.h"
#include "listen/ListenPanel.h"
#include "SettingsDialog.h"

namespace
{
    const int kFpsCaps[] = { 0, 30, 48, 57, 60, 72, 75, 90, 97, 100, 117, 120, 138, 141, 144, 157, 162, 165, 200, 237, 240 };

    //==========================================================================
    // "Tempo" window: BPM -> note lengths in milliseconds, with triplet / dotted columns.
    // Click a value to copy it.
    class TempoChart : public juce::Component, private juce::Timer
    {
    public:
        TempoChart (Settings& st, std::function<void()> changedCallback) : s (st), onChanged (std::move (changedCallback))
        {
            bpm.setSliderStyle (juce::Slider::IncDecButtons);
            bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, ui::px (90), ui::px (30));
            bpm.setRange (20.0, 400.0, 0.1);
            bpm.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
            bpm.setValue (s.bpm, juce::dontSendNotification);
            bpm.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff121316));
            bpm.setColour (juce::Slider::textBoxOutlineColourId, juce::Colour (0xff3a3c40));
            bpm.onValueChange = [this] { s.bpm = bpm.getValue(); changed(); };
            addAndMakeVisible (bpm);

            tap.setTooltip ("Tap along to set the tempo");
            tap.onClick = [this] { tapped(); };
            addAndMakeVisible (tap);

            for (auto* t : { &triplets, &dotted }) addAndMakeVisible (t);
            triplets.setToggleState (s.tempoTriplets, juce::dontSendNotification);
            dotted.setToggleState (s.tempoDotted, juce::dontSendNotification);
            triplets.onClick = [this] { s.tempoTriplets = triplets.getToggleState(); resizeToFit(); changed(); };
            dotted.onClick   = [this] { s.tempoDotted   = dotted.getToggleState();   resizeToFit(); changed(); };
            resizeToFit();
        }

        // the tempo changed outside (the VST3 version follows the host's tempo)
        void syncFromSettings() { if (std::abs (bpm.getValue() - s.bpm) > 1e-6) { bpm.setValue (s.bpm, juce::dontSendNotification); repaint(); } }

        void resizeToFit()
        {
            // wide enough for the BPM row (caption, value box, Tap) and for the table columns
            const int topRow = 16 + ui::px (44) + ui::px (150) + 8 + ui::px (56) + 16;
            const int table = ui::px (250) + ui::px (kColW) * ((int) s.tempoTriplets + (int) s.tempoDotted);
            setSize (juce::jmax (topRow, table), ui::px (kTableTop) + 9 * ui::px (kRow) + ui::px (44));
        }

        void resized() override
        {
            auto top = getLocalBounds().reduced (16, 14).removeFromTop (ui::px (32));
            top.removeFromLeft (ui::px (44));                          // "BPM" caption
            bpm.setBounds (top.removeFromLeft (ui::px (150)));
            top.removeFromLeft (8);
            tap.setBounds (top.removeFromLeft (ui::px (56)));
            auto opts = getLocalBounds().reduced (16, 0).withTop (ui::px (54)).withHeight (ui::px (24));
            triplets.setBounds (opts.removeFromLeft (ui::px (90)));
            dotted.setBounds (opts.removeFromLeft (ui::px (90)));
            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setContentComponentSize (getWidth(), getHeight());
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1b1d22));
            g.setColour (juce::Colours::white.withAlpha (0.55f)); g.setFont (juce::FontOptions (ui::fs (12.0f), juce::Font::bold));
            g.drawText ("BPM", 16, 14, ui::px (40), ui::px (32), juce::Justification::centredLeft);

            const auto cols = columns();
            auto table = getLocalBounds().reduced (16, 0).withTop (ui::px (kTableTop) - ui::px (30));
            auto head = table.removeFromTop (ui::px (24));
            g.drawText ("NOTE", head.removeFromLeft (ui::px (kNoteW)), juce::Justification::centredLeft);
            for (auto& c : cols) g.drawText (c.name, head.removeFromLeft (ui::px (kColW)).withTrimmedRight (ui::px (28)), juce::Justification::centredRight);
            g.setColour (juce::Colour (0xff3a3c40)); g.drawHorizontalLine (ui::px (kTableTop) - 4, 16.0f, (float) getWidth() - 16.0f);

            static const char* notes[] = { "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128" };
            const auto mono = juce::Font::getDefaultMonospacedFontName();
            for (int i = 0; i < 9; ++i)
            {
                auto line = juce::Rectangle<int> (16, ui::px (kTableTop) + i * ui::px (kRow), getWidth() - 32, ui::px (kRow));
                if (i % 2 == 1) { g.setColour (juce::Colours::white.withAlpha (0.035f)); g.fillRect (line.expanded (6, 0)); }
                if (i == 3) { g.setColour (juce::Colour (0xff4c8dff).withAlpha (0.12f)); g.fillRect (line.expanded (6, 0)); }  // the beat
                g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (ui::fs (16.0f), juce::Font::bold));
                g.drawText (notes[i], line.removeFromLeft (ui::px (kNoteW)), juce::Justification::centredLeft);
                for (auto& c : cols)
                {
                    const double ms = noteMs (i) * c.factor;
                    auto cell = line.removeFromLeft (ui::px (kColW));
                    const bool copied = copiedRow == i && copiedCol == &c - cols.data();
                    g.setColour (copied ? juce::Colour (0xff3ddc84) : c.colour);
                    g.setFont (juce::FontOptions (mono, ui::fs (17.0f), juce::Font::bold));
                    g.drawText (copied ? juce::String ("copied") : fmt (ms), cell.withTrimmedRight (ui::px (28)), juce::Justification::centredRight);
                    g.setColour (juce::Colours::white.withAlpha (0.45f)); g.setFont (juce::FontOptions (ui::fs (12.0f)));
                    g.drawText ("ms", cell.removeFromRight (ui::px (24)), juce::Justification::centredRight);
                }
            }
            g.setColour (juce::Colours::white.withAlpha (0.4f)); g.setFont (juce::FontOptions (ui::fs (11.5f)));
            g.drawText ("Click a value to copy it", 16, getHeight() - ui::px (30), getWidth() - 32, ui::px (20), juce::Justification::centredLeft);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            const int row = (e.y - ui::px (kTableTop)) / ui::px (kRow), col = (e.x - 16 - ui::px (kNoteW)) / ui::px (kColW);
            const auto cols = columns();
            if (e.y < ui::px (kTableTop) || row < 0 || row > 8 || e.x < 16 + ui::px (kNoteW) || col < 0 || col >= (int) cols.size()) return;
            juce::SystemClipboard::copyTextToClipboard (fmt (noteMs (row) * cols[(size_t) col].factor));
            copiedRow = row; copiedCol = col;
            repaint();
            startTimer (900);
        }

    private:
        static constexpr int kTableTop = 120, kRow = 30, kNoteW = 70, kColW = 112;
        struct Column { juce::String name; double factor; juce::Colour colour; };

        std::vector<Column> columns() const
        {
            std::vector<Column> c { { "STRAIGHT", 1.0, juce::Colours::white } };
            if (s.tempoTriplets) c.push_back ({ "TRIPLET", 2.0 / 3.0, juce::Colour (0xffb8c4ff) });
            if (s.tempoDotted)   c.push_back ({ "DOTTED",  1.5,       juce::Colour (0xffffd9a0) });
            return c;
        }
        double noteMs (int row) const { return 60000.0 / juce::jmax (1.0, s.bpm) * 4.0 * 2.0 / std::pow (2.0, row); }  // row 0 = 2/1
        static juce::String fmt (double ms) { return ms >= 100.0 ? juce::String (juce::roundToInt (ms)) : juce::String (ms, 1); }

        void changed() { repaint(); if (onChanged) onChanged(); }
        void timerCallback() override { stopTimer(); copiedRow = copiedCol = -1; repaint(); }

        void tapped()
        {
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (! taps.empty() && now - taps.back() > 2000.0) taps.clear();   // a pause starts a new tap sequence
            taps.push_back (now);
            if (taps.size() > 8) taps.erase (taps.begin());
            if (taps.size() >= 2)
            {
                const double avg = (taps.back() - taps.front()) / (double) (taps.size() - 1);
                bpm.setValue (juce::jlimit (20.0, 400.0, std::round (600000.0 / avg) / 10.0));
            }
        }

        Settings& s;
        std::function<void()> onChanged;
        juce::Slider bpm;
        juce::TextButton tap { "Tap" };
        juce::ToggleButton triplets { "Triplets" }, dotted { "Dotted" };
        std::vector<double> taps;
        int copiedRow = -1, copiedCol = -1;
    };

    struct TempoWindow : public juce::DocumentWindow
    {
        TempoWindow (Settings& s, std::function<void()> changed, std::function<void()> closed)
            : DocumentWindow ("Tempo", juce::Colour (0xff1b1d22), closeButton), onClosed (std::move (closed))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new TempoChart (s, std::move (changed)), true);
            setAlwaysOnTop (true);
            setResizable (false, false);
        }
        void closeButtonPressed() override { setVisible (false); if (onClosed) onClosed(); }
        std::function<void()> onClosed;
    };

}

MainComponent::MainComponent (juce::PropertiesFile& p, Core& c, AnalysisEngine& e, AudioEngine* d, HostLink* h)
    : props (p), core (c), engine (e), device (d), host (h), view (settings, engine, [this] (Change ch) { onChange (ch); })
{
    setWantsKeyboardFocus (true);

    status.setJustificationType (juce::Justification::topLeft);
    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    status.setFont (juce::FontOptions (ui::fs (12.5f)));
    for (auto* l : { &lblL, &lblR, &lblFps, &lblText }) l->setFont (juce::FontOptions (ui::fs (15.0f)));
    source.setTooltip ("Signal source");
    for (auto* l : { &lblL, &lblR }) { l->setBorderSize ({}); l->setJustificationType (juce::Justification::centred); }
    startBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4c8dff));

    for (auto* c : std::initializer_list<juce::Component*> { &source, &lblL, &chL, &lblR, &chR, &startBtn, &settingsBtn, &rescanBtn,
                                                             &status, &gpuToggle, &lblFps, &fpsBox })
        options.addAndMakeVisible (c);
    addAndMakeVisible (view);
    addChildComponent (analyzerPanel);
    addChildComponent (balanceView);
    addChildComponent (keyPage);
    addChildComponent (scopePage);
    addChildComponent (monitorPage);
    addAndMakeVisible (bottom);

    // tonal balance: targets from the targets folder, measurement on its own thread
    targetLibrary.rescan();
    liveBalance.setWindow (settings.balanceWindow);
    balanceView.setDifferenceMode (settings.balanceMode == 1);
    balanceView.setSmoothing (settings.balanceSmooth);
    liveBalance.setSmoothing (settings.balanceSmooth);
    bottom.onBalanceSmooth = [this] (int i) { balanceView.setSmoothing (i); saveAll(); };
    balanceView.builderAvailable = targetBuilderExe().existsAsFile();
    balanceView.onOpenFolder = [] { auto d = TargetLibrary::folder(); d.createDirectory(); d.startAsProcess(); };
    balanceView.onOpenBuilder = [] { targetBuilderExe().startAsProcess(); };
    balanceView.onTargetsChanged = [this] { refreshBalanceTargets(); };
    bottom.onBalanceTarget = [this] (int i)
    {
        if (i >= 0 && i < (int) targetLibrary.entries().size()) settings.balanceTarget = targetLibrary.entries()[(size_t) i].name;
        balanceView.setTargetIndex (i);
        saveAll();
    };
    bottom.onBalanceWindow = [this] (int w) { liveBalance.setWindow (w); saveAll(); };
    bottom.onBalanceMode = [this] (bool d) { settings.balanceMode = d ? 1 : 0; balanceView.setDifferenceMode (d); bottom.refresh(); saveAll(); };
    bottom.onBalanceReset = [this] { liveBalance.reset(); };
    bottom.onLoudnessReset = [this] { monitorPage.liveModel().reset(); };
    bottom.onSpectrogramDsp = [this] { onChange (Change::Dsp); };
    bottom.onBalanceTargetsAction = [this] (int a) { balanceTargetsAction (a); };
    refreshBalanceTargets();
    addChildComponent (options);
    juce::Desktop::getInstance().addGlobalMouseListener (&outsideClick);

    // display options
    gpuToggle.setToggleState (settings.gpu, juce::dontSendNotification);
    gpuToggle.onClick = [this] { settings.gpu = gpuToggle.getToggleState(); onChange (Change::Bar); };
    for (int cap : kFpsCaps) fpsBox.addItem (cap == 0 ? juce::String ("Off (follow monitor)") : juce::String (cap) + " fps", cap + 1);
    fpsBox.setSelectedId (settings.fpsCap + 1, juce::dontSendNotification);
    if (fpsBox.getSelectedId() == 0) fpsBox.setSelectedId (1, juce::dontSendNotification);
    fpsBox.setTooltip ("Cap the frame rate, e.g. 3 below your monitor's maximum to stay in its G-Sync / FreeSync range");
    fpsBox.onChange = [this] { settings.fpsCap = fpsBox.getSelectedId() - 1; frameclock::capHz = settings.fpsCap; saveAll(); };
    frameclock::capHz = settings.fpsCap;

    manualBtn.setTooltip ("Open the user manual (in your web browser)");
    manualBtn.onClick = [] { juce::URL ("https://github.com/styrant/digicheck-universal/blob/main/docs/MANUAL.md").launchInDefaultBrowser(); };
    options.addAndMakeVisible (manualBtn);
    copyImgBtn.setTooltip ("Copy the page as an image (Ctrl+C; on the Custom page Ctrl+Shift+C copies the pane last clicked). "
                           "It's also saved in Documents\\DigiCheck Universal\\Screenshots");
    copyImgBtn.onClick = [this] { setOptionsVisible (false); juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe) safe->copyViewImage (false); }); };
    backupBtn.setTooltip ("Save all settings, presets, Custom layouts, Scope themes and Balance targets to one file");
    restoreBtn.setTooltip ("Load a backup file (takes effect after a restart)");
    defaultsBtn.setTooltip ("Put every setting back to its default (presets, layouts, themes and targets are kept); takes effect after a restart");
    backupBtn.onClick = [this] { backupSettings(); };
    restoreBtn.onClick = [this] { restoreSettings(); };
    defaultsBtn.onClick = [this] { resetDefaults(); };
    updateToggle.setToggleState (settings.checkUpdates, juce::dontSendNotification);
    updateToggle.setTooltip ("At start-up, check GitHub for a newer Latest build (nothing else is sent)");
    updateToggle.onClick = [this] { settings.checkUpdates = updateToggle.getToggleState(); saveAll(); if (settings.checkUpdates) checkForUpdates(); };
    updateBtn.setTooltip ("A newer build is available: open the download page");
    updateBtn.onClick = [] { juce::URL ("https://github.com/styrant/digicheck-universal/releases/tag/latest").launchInDefaultBrowser(); };
    ui::colourSafe = settings.colourSafe;
    safeToggle.setToggleState (settings.colourSafe, juce::dontSendNotification);
    safeToggle.setTooltip ("Meters that use red / green for good and bad switch to sky blue / yellow / vermillion, "
                           "which stay distinct with red-green colour blindness (loudness, levels, phase, dynamics zones, Balance markers, the LUFS boxes)");
    safeToggle.onClick = [this]
    {
        settings.colourSafe = safeToggle.getToggleState();
        ui::colourSafe = settings.colourSafe;
        saveAll();
        balanceView.invalidateStatic();
        monitorPage.repaint();
        for (auto* c : getChildren()) c->repaint();
        bottom.repaint();
        repaint();
    };
    for (auto* c : std::initializer_list<juce::Component*> { &copyImgBtn, &backupBtn, &restoreBtn, &defaultsBtn, &updateToggle, &safeToggle })
        options.addAndMakeVisible (c);
    options.addChildComponent (updateBtn);
    if (settings.checkUpdates)
        juce::Timer::callAfterDelay (4000, [safe = juce::Component::SafePointer<MainComponent> (this)] { if (safe) safe->checkForUpdates(); });
    hdrBox.addItemList ({ "Keep HDR (1 px gap at the bottom)", "Keep HDR (CPU rendering while full screen)", "Exact full screen (may turn HDR off)" }, 1);
    hdrBox.setSelectedItemIndex (settings.hdrFullscreen, juce::dontSendNotification);
    hdrBox.setTooltip ("On an HDR monitor, some graphics drivers switch a full-screen OpenGL window to exclusive mode and Windows turns HDR off. "
                       "The 1 px gap avoids it with GPU rendering; CPU rendering avoids it on HDR monitors only, at some speed cost.");
    hdrBox.onChange = [this] { settings.hdrFullscreen = hdrBox.getSelectedItemIndex(); saveAll(); };
    for (auto* c : std::initializer_list<juce::Component*> { &lblHdr, &hdrBox }) options.addAndMakeVisible (c);
    lblHdr.setFont (juce::FontOptions (ui::fs (15.0f)));
    textBox.addItemList ({ "Small", "Medium (default)", "Large", "Custom" }, 1);
    textBox.setSelectedItemIndex (juce::jlimit (0, 3, settings.textSize), juce::dontSendNotification);
    textBox.onChange = [this] { settings.textSize = textBox.getSelectedItemIndex(); applyTextScale(); };
    textSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    textSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 22);
    textSlider.setRange (70.0, 200.0, 5.0);
    textSlider.setTextValueSuffix ("%");
    textSlider.setValue (settings.textScaleCustom * 100.0, juce::dontSendNotification);
    textSlider.onDragEnd = [this] { settings.textScaleCustom = textSlider.getValue() / 100.0; applyTextScale(); };
    textSlider.onValueChange = [this] { if (! textSlider.isMouseButtonDown()) { settings.textScaleCustom = textSlider.getValue() / 100.0; applyTextScale(); } };
    for (auto* c : std::initializer_list<juce::Component*> { &lblText, &textBox, &textSlider }) options.addAndMakeVisible (c);
    ui::textScale = ui::presetScale (settings.textSize, settings.textScaleCustom);
    textSlider.setVisible (settings.textSize == 3);

    startBtn.onClick    = [this] { if (inputRunning()) stopSource(); else startSelected(); };
    settingsBtn.onClick = [this] { if (settings.viewMode == BottomBar::Analyzer) analyzerPanel.toggleSettings(); else openSettings(); };
    rescanBtn.onClick   = [this] { rescan(); };
    source.onChange     = [this] { if (inputRunning()) startSelected(); };
    chL.onChange = chR.onChange = [this]
    {
        if (chL.getSelectedItemIndex() < 0 || chR.getSelectedItemIndex() < 0) return;
        settings.chL = chL.getSelectedItemIndex(); settings.chR = chR.getSelectedItemIndex();
        engine.setChannels (settings.chL, settings.chR);
        settings.save (core.state);
    };

    // bottom bar
    view.onPauseChanged      = [this] (bool isPaused) { bottom.setPaused (isPaused); };
    bottom.onViewChange      = [this] (int v) { setView (v); };
    bottom.onCustomChanged   = [this] { setView (BottomBar::Custom); };
    bottom.onLoadLayout      = [this] (const juce::String& n) { loadLayout (n); };
    bottom.onDeleteLayout    = [this] { if (settings.customName.isNotEmpty()) deleteLayout (settings.customName); };
    bottom.onSaveLayout      = [this]
    {
        askPresetName ("Save Custom layout", [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::String& n)
        {
            if (safe != nullptr && n.isNotEmpty()) safe->saveLayoutAs (n);
        });
    };
    bottom.onUpdateLayout    = [this] { if (settings.customName.isNotEmpty()) saveLayoutAs (settings.customName); };
    refreshSavedLayouts();
    bottom.onToggleHelp      = [this] { setHelpMode (! settings.helpMode); };
    refPanel = std::make_unique<ReferencePanel> (core.reference, settings, device != nullptr ? &core.refOutput : nullptr, [this] { referenceChanged(); });
    addChildComponent (*refPanel);
    bottom.onToggleReference = [this] { setReferenceVisible (! refPanel->isVisible()); };
    balanceView.setReference (&core.reference);
    analyzerPanel.getFftView().setReference (&core.reference);
    monitorPage.setReference (&core.reference);
    if (settings.refFile.isNotEmpty() && ! core.reference.ready() && ! core.reference.isLoading() && juce::File (settings.refFile).existsAsFile())
        refPanel->loadFile (juce::File (settings.refFile));          // the last reference comes back (the plugin: this project's)
    referenceChanged();
    addChildComponent (toast);
    toast.setJustificationType (juce::Justification::centred);
    toast.setColour (juce::Label::backgroundColourId, juce::Colour (0xf0181a1f));
    toast.setColour (juce::Label::outlineColourId, juce::Colour (0xff4c8dff));
    toast.setColour (juce::Label::textColourId, juce::Colour (0xffeef0f3));
    toast.setInterceptsMouseClicks (false, false);
    bottom.onToggleHideBar   = [this] { setBarHidden (! barHidden); };
    if (settings.helpMode) setHelpMode (true);
    bottom.onPause           = [this] { view.togglePause(); };
    bottom.onRef             = [this] (int d) { settings.refLevel = juce::jlimit (-80.0, 0.0, settings.refLevel + d); onChange (Change::Visual); };
    bottom.onToggleTempo     = [this] { toggleTempoWindow(); };
    bottom.onToggleFullscreen = [this] { toggleFullscreen(); };
    bottom.onToggleOptions   = [this] { toggleOptions(); };
    bottom.onSpectrogramSettings = [this] { openSettings(); };
    bottom.onToggleLufs      = [this] { settings.showLufs = ! settings.showLufs; onChange (Change::Bar); };
    bottom.onZoomIn          = [this] { analyzerPanel.getView().zoomBy (0.6); };
    bottom.onZoomOut         = [this] { analyzerPanel.getView().zoomBy (1.0 / 0.6); };
    bottom.onFullRange       = [this] { analyzerPanel.getView().resetZoom(); };
    bottom.onFreeze          = [this] { analyzerPanel.getView().toggleFreeze(); };
    bottom.onAnalyzerSettings = [this] { analyzerPanel.toggleSettings(); };
    analyzerPanel.onStyleChanged = [this] (int st) { bottom.refresh(); saveAll(); if (st == 1) grabKeyboardFocus(); else analyzerPanel.getView().grabKeyboardFocus(); };
    bottom.onSpectrogramPresets = [this] (juce::Component& a) { showSpectrogramPresets (a); };
    bottom.onAnalyzerPresets    = [this] (juce::Component& a) { showAnalyzerPresets (a); };
    analyzerPanel.onStateChanged = [this] { bottom.setAnalyzerState (analyzerPanel.getView().isFrozen(), analyzerPanel.getView().isZoomed()); };
    analyzerPanel.onStateChanged();
    setView (settings.viewMode);

    // GPU rendering: one OpenGL context for the whole window. The spectrogram draws its texture in
    // renderOpenGL(); every component (including the analyzer) is then painted on top by the GPU.
    glContext.setRenderer (this);
    glContext.setComponentPaintingEnabled (true);
    glContext.setContinuousRepainting (false);         // frames are triggered by onFrame(), which obeys the frame-rate cap
    glContext.setPreferredVersion ({ 3, 2 });
    applyRenderer();

    setSize (1300, 760);
    startTimerHz (20);
    if (host != nullptr) setUpPlugin();
    if (device == nullptr) { view.sourceStarted(); return; }      // the plugin: the track is the source, always on

    // Resume the last source automatically
    rescan();
    if (settings.source.isNotEmpty())
        for (auto& s : sources)
            if (s.key == settings.source) { juce::MessageManager::callAsync ([this] { startSelected(); }); break; }
}

MainComponent::~MainComponent()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClick);
    if (updateThread != nullptr) updateThread->stopThread (6000);
    glContext.detach();
    saveAll();
    if (device != nullptr) { device->stop(); engine.setAnalyzer (nullptr); }
}

void MainComponent::saveAll()
{
    core.save();
}

void MainComponent::applyRenderer()
{
    // SPECTROSCOPE_NO_GPU: never OpenGL (checks on machines without a real GPU driver)
    static const bool noGpu = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_NO_GPU", {}).isNotEmpty();
    const bool wantGpu = settings.gpu && ! cpuOverride && ! noGpu;
    if (wantGpu && ! glContext.isAttached())
    {
        view.setGpuMode (true);
        glContext.attachTo (*this);
    }
    else if (! wantGpu && glContext.isAttached())
    {
        glContext.detach();
        view.setGpuMode (false);
        repaint();
    }
}

void MainComponent::newOpenGLContextCreated() { view.glInit(); }
void MainComponent::openGLContextClosing()    { view.glRelease(); }

void MainComponent::renderOpenGL()
{
    using namespace juce::gl;
    juce::OpenGLHelpers::clear (juce::Colour (0xff0b0c0e));
    view.glRender (ownerW.load(), ownerH.load());
    glFrames.fetch_add (1);
}

// One display frame (monitor vblank, thinned out to the frame-rate cap). Everything that animates
// is updated from here or from vblank callbacks that consult the same frame clock.
void MainComponent::onFrame (double t)
{
    if (! frameclock::clock().due (t)) return;
    bottom.onFrame();
    balanceView.onFrame (t);
    scopePage.onFrame (t);
    monitorPage.onFrame (t);
    analyzerPanel.onFrame (t);
    if (glContext.isAttached()) glContext.triggerRepaint();
}

void MainComponent::OutsideClick::mouseDown (const juce::MouseEvent& e)
{
    auto& o = owner;
    auto* c = e.eventComponent;
    if (c != nullptr) o.notePaneClick (*c);
    if (c != nullptr && o.refPanel != nullptr && o.refPanel->isVisible() && c->getTopLevelComponent() == o.getTopLevelComponent()
        && c != o.refPanel.get() && ! o.refPanel->isParentOf (c) && c != &o.bottom.getRefButton())
        o.setReferenceVisible (false);
    if (! o.options.isVisible() || c == nullptr) return;
    if (c->getTopLevelComponent() != o.getTopLevelComponent()) return;          // pop-up menus are separate windows
    if (c == &o.options || o.options.isParentOf (c)) return;
    if (c == &o.bottom.getOptionsButton()) return;                               // its own click toggles
    o.setOptionsVisible (false);
}

void MainComponent::toggleOptions() { setOptionsVisible (! options.isVisible()); }

// Text size changed: new fonts everywhere, layouts re-measured, small windows rebuilt at the new size
void MainComponent::applyTextScale()
{
    ui::textScale = ui::presetScale (settings.textSize, settings.textScaleCustom);
    textSlider.setVisible (settings.textSize == 3);
    for (auto* l : { &lblL, &lblR, &lblFps, &lblText, &lblHdr }) l->setFont (juce::FontOptions (ui::fs (15.0f)));
    status.setFont (juce::FontOptions (ui::fs (12.5f)));
    saveAll();
    if (auto* top = getTopLevelComponent()) top->sendLookAndFeelChange();
    view.settingsChanged (Change::Layout);       // margins changed (keeps the history)
    bottom.refresh();
    resized();
    analyzerPanel.resized();
    analyzerPanel.repaint();
    balanceView.resized();
    balanceView.repaint();
    keyPage.resized();
    keyPage.repaint();
    monitorPage.resized();
    monitorPage.repaint();
    if (tempoWindow != nullptr)
    {
        const bool wasOpen = tempoWindow->isVisible();
        tempoWindow.reset();
        if (wasOpen) toggleTempoWindow();
    }
    repaint();
}

void MainComponent::setOptionsVisible (bool show)
{
    if (show) { updateStatus(); layoutOptions(); options.toFront (false); if (refPanel != nullptr && refPanel->isVisible()) setReferenceVisible (false); }
    options.setVisible (show);
    bottom.setOptionsOpen (show);
    if (! show) grabKeyboardFocus();
}

juce::Component* MainComponent::viewComponent (int id)
{
    switch (id)
    {
        case BottomBar::Spectrogram: return &view;
        case BottomBar::Analyzer:    return &analyzerPanel;
        case BottomBar::Balance:     return &balanceView;
        case BottomBar::Key:         return &keyPage;
        case BottomBar::Scope:       return &scopePage;
        case BottomBar::Monitor:     return &monitorPage;
        default:                     return nullptr;
    }
}

bool MainComponent::shows (int id) const
{
    if (settings.viewMode != BottomBar::Custom) return settings.viewMode == id;
    const auto slots = BottomBar::customSlots (settings);
    for (int i = 0; i < BottomBar::panesIn (settings.customLayout); ++i) if (slots[(size_t) i] == id) return true;
    return false;
}

void MainComponent::setView (int v)
{
    settings.viewMode = juce::jlimit (0, (int) BottomBar::NumViews - 1, v);
    const bool spec = shows (BottomBar::Spectrogram), ana = shows (BottomBar::Analyzer), bal = shows (BottomBar::Balance),
               key = shows (BottomBar::Key), sco = shows (BottomBar::Scope), mon = shows (BottomBar::Monitor);
    const bool custom = settings.viewMode == BottomBar::Custom;
    monitorPage.setCustomProfile (custom);
    monitorPage.setVisible (mon);
    monitorPage.setActive (mon);
    keyPage.setVisible (key);
    keyPage.setActive (key);
    scopePage.setVisible (sco);
    scopePage.setActive (sco);
    view.setVisible (spec);
    analyzerPanel.setVisible (ana);
    analyzerPanel.setActive (ana);
    balanceView.setVisible (bal);
    balanceView.setActive (bal);
    liveBalance.setActive (bal);
    engine.setSpectrogramActive (spec);
    if (host != nullptr) if (auto* e = host->sidechainEngine()) e->setSpectrogramActive (spec && settings.specSc);
    if (bal) refreshBalanceTargets();
    if (custom || bal || key || sco || mon) grabKeyboardFocus();
    else if (ana) { if (analyzerPanel.isFft()) grabKeyboardFocus(); else analyzerPanel.getView().grabKeyboardFocus(); }
    bottom.refresh();
    resized();
    repaint();
    saveAll();
}

void MainComponent::rescan()
{
    const auto keep = source.getSelectedItemIndex() >= 0 ? sources[(size_t) source.getSelectedItemIndex()].key : settings.source;
    if (device == nullptr) return;
    sources = device->scanSources();
    source.clear (juce::dontSendNotification);
    juce::String group;
    int preferred = -1;
    for (size_t i = 0; i < sources.size(); ++i)
    {
        const auto& s = sources[i];
        if (s.group != group) { source.addSectionHeading (s.group); group = s.group; }
        source.addItem (s.label, (int) i + 1);
        if (s.key == keep) preferred = (int) i;
    }
    if (preferred < 0)   // first run: prefer the Focusrite/Scarlett on Windows Audio, else the first input
        for (size_t i = 0; i < sources.size(); ++i)
            if (sources[i].kind == AudioEngine::Source::Device && sources[i].typeName == "Windows Audio"
                && (sources[i].deviceName.containsIgnoreCase ("Focusrite") || sources[i].deviceName.containsIgnoreCase ("Scarlett")))
            { preferred = (int) i; break; }
    if (preferred < 0) preferred = 0;
    source.setSelectedId (preferred + 1, juce::dontSendNotification);
}

void MainComponent::startSourceByKey (const juce::String& key)
{
    for (size_t i = 0; i < sources.size(); ++i)
        if (sources[i].key == key) { source.setSelectedId ((int) i + 1, juce::dontSendNotification); startSelected(); return; }
}

void MainComponent::startSelected()
{
    const int i = source.getSelectedItemIndex();
    if (i < 0 || i >= (int) sources.size()) return;
    view.sourceStopped();
    error.clear();
    if (device == nullptr) return;
    if (! device->start (sources[(size_t) i], error))
    {
        startBtn.setButtonText ("Start");
        updateStatus();
        return;
    }
    settings.source = sources[(size_t) i].key;
    settings.save (core.state);
    fillChannelBoxes();
    startBtn.setButtonText ("Stop");
    startBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc0392b));
    view.sourceStarted();
    updateStatus();
}

void MainComponent::stopSource()
{
    if (device == nullptr) return;
    device->stop();
    view.sourceStopped();
    startBtn.setButtonText ("Start");
    startBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4c8dff));
    updateStatus();
}

void MainComponent::fillChannelBoxes()
{
    if (device == nullptr) return;
    const auto& names = device->getChannelNames();
    for (auto* b : { &chL, &chR })
    {
        b->clear (juce::dontSendNotification);
        for (int i = 0; i < names.size(); ++i) b->addItem (juce::String (i + 1) + ": " + names[i], i + 1);
    }
    const int n = names.size();
    const int l = juce::jlimit (0, n - 1, settings.chL), r = juce::jlimit (0, n - 1, n > 1 ? settings.chR : 0);
    chL.setSelectedItemIndex (l, juce::dontSendNotification);
    chR.setSelectedItemIndex (r, juce::dontSendNotification);
    engine.setChannels (l, r);
}

void MainComponent::onChange (Change c)
{
    saveAll();
    view.settingsChanged (c);
    bottom.refresh();
    if (c == Change::Bar) { applyRenderer(); resized(); }   // toolbar / renderer changes (the spectrogram keeps its history)
}

void MainComponent::openSettings()
{
    SettingsDialog::show (settings, [this] (Change c) { onChange (c); }, this);
}

void MainComponent::toggleFullscreen()
{
    // Deferred: the click that asks for this is still being handled by the window that is about
    // to be replaced, so switching windows must wait until that event has finished.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MainComponent> (this), want = ! fullscreen]
    {
        if (safe != nullptr && safe->onFullscreenRequest) safe->onFullscreenRequest (want);
    });
}

// The window owner moves this component to another window; the GL context is detached meanwhile.
void MainComponent::suspendGpu()
{
    if (glContext.isAttached()) { glContext.detach(); gpuSuspended = true; }
}

void MainComponent::resumeGpu()
{
    if (gpuSuspended) { gpuSuspended = false; applyRenderer(); }
}

void MainComponent::setFullscreenState (bool isFull)
{
    fullscreen = isFull;
    if (! isFull) { barHidden = barPeek = false; bottom.setBarHidden (false); }
    if (isFull && std::exchange (pendingHideBar, false)) { barHidden = true; bottom.setBarHidden (true); }
    bottom.setFullscreen (isFull);
    resized();
    grabKeyboardFocus();
}

bool MainComponent::isFullscreen() const { return fullscreen; }

void MainComponent::toggleTempoWindow()
{
    if (tempoWindow == nullptr)
    {
        tempoWindow = std::make_unique<TempoWindow> (settings, [this] { saveAll(); }, [this] { bottom.setTempoOpen (false); });
        auto* w = tempoWindow.get();
        const auto area = getScreenBounds();
        w->setTopLeftPosition (area.getRight() - w->getWidth() - 400, area.getY() + 60);
    }
    const bool show = ! tempoWindow->isVisible();
    tempoWindow->setVisible (show);
    if (show) tempoWindow->toFront (false);
    bottom.setTempoOpen (show);
}

// The VST3 version's extras: the sidechain in the views, Listen, the host's tempo (see HostLink)
void MainComponent::setUpPlugin()
{
    bottom.setPluginMode (true);
    for (auto* c : std::initializer_list<juce::Component*> { &source, &lblL, &chL, &lblR, &chR, &startBtn, &rescanBtn }) c->setVisible (false);
    view.setSidechainEngine (host->sidechainEngine());
    scopePage.setHost (host);
    analyzerPanel.getFftView().setSidechainAvailable (true);
    monitorPage.hostPlaying = [this] { return host->hostPlaying(); };
    if (host->hostBpm() > 0) { settings.bpm = host->hostBpm(); lastHostBpm = settings.bpm; scopePage.applySettings(); }

    bottom.onListen = [this] (juce::Component& anchor)
    {
        auto panel = std::make_unique<ListenPanel> (host->isolation(), settings, [this] { saveAll(); updateListen(); scopePage.applySettings(); });
        juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
    };
    bottom.onSidechainMenu = [this] (juce::Component& anchor)
    {
        juce::PopupMenu m, colours;
        const auto apply = [this] { view.sidechainStyleChanged(); saveAll(); if (auto* e = host->sidechainEngine()) e->setSpectrogramActive (shows (BottomBar::Spectrogram) && settings.specSc); };
        m.addItem ("Show the sidechain", true, settings.specSc, [this, apply] { settings.specSc = ! settings.specSc; apply(); });
        m.addItem ("Style: tint (its colour, by level)", true, settings.specScStyle == 0, [this, apply] { settings.specScStyle = 0; apply(); });
        m.addItem ("Style: contour lines (the main picture stays clear)", true, settings.specScStyle == 1, [this, apply] { settings.specScStyle = 1; apply(); });
        const std::pair<const char*, const char*> presets[] = { { "Orange", "#ff8a3d" }, { "Cyan", "#3ee0ff" }, { "Green", "#4cd07d" }, { "Magenta", "#ff4fa3" }, { "White", "#ffffff" } };
        for (auto& [name, hex] : presets)
            colours.addItem (name, true, settings.specScColour.equalsIgnoreCase (hex), [this, apply, h = juce::String (hex)] { settings.specScColour = h; apply(); });
        m.addSubMenu ("Colour", colours);
        juce::PopupMenu op;
        for (int pct : { 40, 60, 75, 90, 100 })
            op.addItem (juce::String (pct) + " %", true, std::abs (settings.specScOpacity * 100 - pct) < 1, [this, apply, pct] { settings.specScOpacity = pct / 100.0; apply(); });
        m.addSubMenu ("Strength", op);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor));
    };
    bottom.onLinkMenu = [this] (juce::Component& anchor)
    {
        juce::PopupMenu m;
        const auto others = host->otherInstances();
        const auto cur = host->linkName();
        m.addSectionHeader ("Compare with another track");
        for (auto& n : others) m.addItem (n, true, n == cur, [this, n] { host->setLink (n); settings.specSc = true; saveAll(); });
        if (others.isEmpty()) m.addItem ("(no other DigiCheck instances in this project)", false, false, [] {});
        if (cur.isNotEmpty() && ! others.contains (cur)) m.addItem (cur + " (not found)", true, true, [] {});
        m.addSeparator();
        m.addItem ("None (use the sidechain input)", true, cur.isEmpty(), [this] { host->setLink ({}); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor));
    };
    updateListen();
}

void MainComponent::updateListen()
{
    if (host == nullptr) return;
    bottom.setListenState (host->isolation().active(), ListenPanel::summary (host->isolation()));
}

void MainComponent::timerCallback()
{
    if (host != nullptr)
    {
        // the project's tempo is the app's tempo (Tempo window, the Scope's bars)
        const double hb = host->hostBpm();
        if (hb > 0 && std::abs (hb - lastHostBpm) > 1e-4)
        {
            lastHostBpm = hb; settings.bpm = hb;
            scopePage.applySettings();
            if (tempoWindow != nullptr)
                if (auto* tc = dynamic_cast<TempoChart*> (tempoWindow->getContentComponent())) tc->syncFromSettings();
        }
        updateListen();
        bottom.setLinkState (host->linkName(), host->linkLive());
    }
    balanceView.setInputRunning (inputRunning());
    keyPage.setInputRunning (inputRunning());
    scopePage.setInputRunning (inputRunning());
    monitorPage.pump();                          // keeps measuring when display frames stall (minimised)
    if (toast.isVisible() && juce::Time::getMillisecondCounterHiRes() * 0.001 > toastUntil) toast.setVisible (false);
    // the reference plays at the mix's integrated loudness (since Reset)
    core.reference.mixLufs = (float) monitorPage.liveModel().loudness().getIntegrated();
    if (core.reference.ready()) bottom.setReferenceState (refPanel->isVisible(), true, core.reference.listen.load() || core.reference.isPlaying());
    if (fullscreen && barHidden)
    {
        // the hidden bar peeks back while the mouse is at the bottom edge or over it (or a menu from it is open)
        const auto p = getMouseXYRelative();
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const bool inside = p.x >= 0 && p.x < getWidth();
        const bool want = (inside && p.y >= getHeight() - 3 && p.y <= getHeight() + 4)
                       || (barPeek && inside && p.y >= getHeight() - BottomBar::height() - ui::px (14) && p.y <= getHeight() + 4)
                       || (barPeek && (options.isVisible() || juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0));
        if (want) { barLeaveTime = now; if (! barPeek) { barPeek = true; resized(); } }
        else if (barPeek && now - barLeaveTime > 0.7) { barPeek = false; resized(); grabKeyboardFocus(); }
    }
    if (++tick % 10 == 0) updateStatus();
}

void MainComponent::updateStatus()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    double fps = 0;
    if (glContext.isAttached())
    {
        const int n = glFrames.exchange (0);
        fps = (now - glFpsTime) > 0 && (now - glFpsTime) < 10 ? n / (now - glFpsTime) : 0;
        view.measureFps();
    }
    else fps = view.measureFps();
    glFpsTime = now;

    const juce::String render = juce::String (glContext.isAttached() ? "GPU (OpenGL)" : "CPU")
        + (glContext.isAttached() || shows (BottomBar::Spectrogram) ? ", " + juce::String (juce::roundToInt (fps)) + " fps" : juce::String())
        + (settings.fpsCap > 0 ? " (limit " + juce::String (settings.fpsCap) + ")" : juce::String());
    juce::String text;
    if (error.isNotEmpty()) text = "Could not start: " + error;
    else if (device == nullptr) text = host != nullptr ? host->describe() : juce::String();
    else if (! device->isRunning()) text = "Stopped";
    else
    {
        const auto& src = device->getCurrentSource();
        const juce::String kind = src.kind == AudioEngine::Source::Loopback ? "Loopback"
                                : src.kind == AudioEngine::Source::Test     ? "Test signal"
                                : src.typeName;
        text = kind + "  |  " + juce::String (engine.getSampleRate() / 1000.0, 1) + " kHz  |  "
             + juce::String (device->getChannelNames().size()) + " ch  |  latency "
             + juce::String (juce::roundToInt (engine.getLatencySeconds() * 1000)) + " ms  |  DSP "
             + juce::String (juce::roundToInt (engine.getLoad() * 100)) + "%";
    }
    status.setText (text + "\nRendering: " + render, juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    if (settings.viewMode == BottomBar::Custom)
    {
        // the gaps between panes and any empty pane (not over the spectrogram: OpenGL draws it underneath)
        if (view.isVisible()) g.excludeClipRegion (view.getBounds());
        g.fillAll (juce::Colour (0xff0b0c0e));
        const auto slots = BottomBar::customSlots (settings);
        const auto panes = customPanes (contentArea);
        for (size_t i = 0; i < panes.size(); ++i)
            if (slots[i] < 0)
            {
                g.setColour (juce::Colour (0xff15171b)); g.fillRoundedRectangle (panes[i].toFloat().reduced (1), 6.0f);
                g.setColour (juce::Colour (0xffb0b6c0)); g.setFont (juce::FontOptions (ui::fs (14.0f)));
                g.drawFittedText ("Empty pane " + juce::String ((int) i + 1) + ": choose a view for it in the bottom bar", panes[i].reduced (12), juce::Justification::centred, 3);
            }
        // the focused pane (its controls are in the bar): a thin outline in the gap around it
        for (size_t i = 0; i < panes.size(); ++i)
            if (slots[i] >= 0 && slots[i] == settings.customFocus)
            {
                g.setColour (juce::Colour (0xff4c8dff).withAlpha (0.85f));
                g.drawRoundedRectangle (panes[i].toFloat().expanded (2.5f), 4.0f, 1.5f);
            }
        for (auto& d : dividers)
        {
            g.setColour (juce::Colour (0xff2a2d33));
            const auto c = d.r.getCentre();
            if (d.vertical) g.fillRoundedRectangle ((float) c.x - 1.5f, (float) c.y - 14, 3, 28, 1.5f);
            else            g.fillRoundedRectangle ((float) c.x - 14, (float) c.y - 1.5f, 28, 3, 1.5f);
        }
        return;
    }
    if (! glContext.isAttached()) g.fillAll (juce::Colour (0xff0b0c0e));   // GPU: children cover everything
}

void MainComponent::resized()
{
    ownerW = (float) getWidth(); ownerH = (float) getHeight();
    auto r = getLocalBounds();
    // full screen without the bar: the views take the whole screen; the bar peeks back over them at the bottom edge
    const bool hideBar = fullscreen && barHidden;
    const auto barArea = r.withTop (r.getBottom() - BottomBar::height());
    if (! hideBar) r.removeFromBottom (BottomBar::height());
    bottom.setBounds (barArea);
    bottom.setVisible (! hideBar || barPeek);
    if (hideBar && barPeek) bottom.toFront (false);
    contentArea = r;
    dividers.clear();
    if (settings.viewMode == BottomBar::Custom)
    {
        const auto panes = customPanes (r);
        const auto slots = BottomBar::customSlots (settings);
        for (int id = 0; id < 6; ++id)
        {
            auto* c = viewComponent (id);
            for (size_t i = 0; i < panes.size(); ++i) if (slots[i] == id) c->setBounds (panes[i]);
        }
    }
    else
        for (int id = 0; id < 6; ++id) viewComponent (id)->setBounds (r);   // no top toolbar: the analysis uses the full height
    layoutOptions();
}

// The Custom page's panes for its layout (in the order of the bar's pane boxes), and its dividers
std::vector<juce::Rectangle<int>> MainComponent::customPanes (juce::Rectangle<int> area) const
{
    const int gap = ui::px (6);
    const double a = settings.customSplitA, b = settings.customSplitB;
    auto* self = const_cast<MainComponent*> (this);
    auto& dv = self->dividers;
    dv.clear();
    // split r at fraction f (vertical: a column boundary); the divider covers the gap
    auto split = [&] (juce::Rectangle<int> r, double f, bool vertical, int which, juce::Rectangle<int>& first, juce::Rectangle<int>& second)
    {
        if (vertical)
        {
            const int x = r.getX() + juce::roundToInt ((r.getWidth() - gap) * f);
            first = r.withRight (x); second = r.withLeft (x + gap);
            dv.push_back ({ { x, r.getY(), gap, r.getHeight() }, true, which, r });
        }
        else
        {
            const int y = r.getY() + juce::roundToInt ((r.getHeight() - gap) * f);
            first = r.withBottom (y); second = r.withTop (y + gap);
            dv.push_back ({ { r.getX(), y, r.getWidth(), gap }, false, which, r });
        }
    };
    juce::Rectangle<int> p, q, p2, q2;
    std::vector<juce::Rectangle<int>> out;
    switch (juce::jlimit (0, 5, settings.customLayout))
    {
        case 0: split (area, a, true, 0, p, q);  out = { p, q }; break;
        case 1: split (area, a, false, 0, p, q); out = { p, q }; break;
        case 2: split (area, a, true, 0, p, q);  split (q, b, false, 1, p2, q2); out = { p, p2, q2 }; break;
        case 3: split (area, a, false, 0, p, q); split (q, b, true, 1, p2, q2);  out = { p, p2, q2 }; break;
        case 4: split (area, a, true, 0, p, q); split (q, b, true, 1, p2, q2); out = { p, p2, q2 }; break;
        default:
        {
            // a 2 x 2 grid: one vertical divider (splitA) and one horizontal (splitB) across the whole page
            const int x = area.getX() + juce::roundToInt ((area.getWidth() - gap) * a), y = area.getY() + juce::roundToInt ((area.getHeight() - gap) * b);
            out = { juce::Rectangle<int>::leftTopRightBottom (area.getX(), area.getY(), x, y), juce::Rectangle<int>::leftTopRightBottom (x + gap, area.getY(), area.getRight(), y),
                    juce::Rectangle<int>::leftTopRightBottom (area.getX(), y + gap, x, area.getBottom()), juce::Rectangle<int>::leftTopRightBottom (x + gap, y + gap, area.getRight(), area.getBottom()) };
            dv.push_back ({ { x, area.getY(), gap, area.getHeight() }, true, 0, area });
            dv.push_back ({ { area.getX(), y, area.getWidth(), gap }, false, 1, area });
        }
    }
    return out;
}

void MainComponent::mouseMove (const juce::MouseEvent& e)
{
    for (auto& d : dividers)
        if (d.r.expanded (2).contains (e.getPosition())) { setMouseCursor (d.vertical ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::UpDownResizeCursor); return; }
    setMouseCursor (juce::MouseCursor::NormalCursor);
}

void MainComponent::mouseDown (const juce::MouseEvent& e)
{
    dragDivider = -1;
    for (size_t i = 0; i < dividers.size(); ++i) if (dividers[i].r.expanded (2).contains (e.getPosition())) dragDivider = (int) i;
}

void MainComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (dragDivider < 0 || dragDivider >= (int) dividers.size()) return;
    const auto d = dividers[(size_t) dragDivider];
    const double f = d.vertical ? (double) (e.x - d.span.getX()) / juce::jmax (1, d.span.getWidth())
                                : (double) (e.y - d.span.getY()) / juce::jmax (1, d.span.getHeight());
    double& target = d.which == 0 ? settings.customSplitA : settings.customSplitB;
    target = juce::jlimit (0.15, 0.85, f);
    resized();
    repaint();
}

void MainComponent::mouseUp (const juce::MouseEvent&)
{
    if (dragDivider >= 0) saveAll();
    dragDivider = -1;
}

void MainComponent::copyViewImage (bool paneOnly)
{
    juce::Rectangle<int> area = contentArea;
    juce::String what = BottomBar::viewNames()[juce::jlimit (0, 5, settings.viewMode)];
    if (settings.viewMode == BottomBar::Custom)
    {
        what = "Custom";
        if (paneOnly && shows (settings.customFocus))
            if (auto* c = viewComponent (settings.customFocus)) { area = c->getBounds(); what = BottomBar::viewNames()[settings.customFocus]; }
    }
    view.forSnapshot = true;                       // the spectrogram paints its picture even with the GPU renderer
    const auto scale = juce::jmax (1.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
    const auto img = createComponentSnapshot (area, true, scale);
    view.forSnapshot = false;

    const bool copied = clipboard::copyImage (img);
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DigiCheck Universal").getChildFile ("Screenshots");
    dir.createDirectory();
    auto file = dir.getNonexistentChildFile (what + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H%M%S"), ".png", false);
    bool saved = false;
    if (auto st = file.createOutputStream()) saved = juce::PNGImageFormat().writeImageToStream (img, *st);
    showToast ((copied ? "Copied to the clipboard" : juce::String ("Saved")) + (saved ? (copied ? " and saved as " : " as ") + file.getFileName() : juce::String()));
}

void MainComponent::showToast (const juce::String& text)
{
    toast.setText (text, juce::dontSendNotification);
    toast.setFont (juce::FontOptions (ui::fs (14.0f)));
    const int w = juce::GlyphArrangement::getStringWidthInt (toast.getFont(), text) + ui::px (32);
    toast.setBounds (contentArea.getCentreX() - w / 2, contentArea.getBottom() - ui::px (56), w, ui::px (34));
    toast.setVisible (true);
    toast.toFront (false);
    toastUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 2.5;
}

//==============================================================================
// Update check: GitHub's Latest build release is named "Latest build (1.3.34)"
struct MainComponent::UpdateCheck : public juce::Thread
{
    UpdateCheck (juce::Component::SafePointer<MainComponent> o) : juce::Thread ("Update check"), owner (o) {}
    void run() override
    {
        juce::String name;
        if (auto in = juce::URL ("https://api.github.com/repos/styrant/digicheck-universal/releases/tags/latest")
                          .createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                  .withConnectionTimeoutMs (5000).withExtraHeaders ("Accept: application/vnd.github+json\r\nUser-Agent: DigiCheck-Universal")))
            name = juce::JSON::parse (in->readEntireStreamAsString()).getProperty ("name", {}).toString();
        const auto ver = name.fromLastOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false).trim();
        juce::MessageManager::callAsync ([o = owner, ver] { if (o != nullptr) o->onUpdateResult (ver); });
    }
    juce::Component::SafePointer<MainComponent> owner;
};

void MainComponent::checkForUpdates()
{
    if (! settings.checkUpdates) return;
    if (updateThread != nullptr && updateThread->isThreadRunning()) return;
    updateThread = std::make_unique<UpdateCheck> (juce::Component::SafePointer<MainComponent> (this));
    updateThread->startThread (juce::Thread::Priority::low);
}

void MainComponent::onUpdateResult (const juce::String& latest)
{
    // compare a.b.c numerically
    auto parts = [] (const juce::String& v) { auto t = juce::StringArray::fromTokens (v, ".", {}); std::array<int, 3> r {}; for (int i = 0; i < 3 && i < t.size(); ++i) r[(size_t) i] = t[i].getIntValue(); return r; };
    if (latest.isEmpty() || parts (latest) <= parts (ProjectInfo::versionString)) return;
    newerVersion = latest;
    updateBtn.setButtonText ("Download " + latest);
    updateBtn.setVisible (true);
    bottom.setUpdateAvailable (latest);
}

//==============================================================================
// Settings backup: one XML file with the stored settings, the app-wide presets and layouts, Scope themes
// and Balance targets
void MainComponent::backupSettings()
{
    saveAll();
    juce::XmlElement root ("DIGICHECK_UNIVERSAL_BACKUP");
    root.setAttribute ("version", ProjectInfo::versionString);
    root.setAttribute ("plugin", core.plugin ? 1 : 0);
    auto addProps = [&] (const char* tag, juce::PropertySet& p)
    {
        auto* e = root.createNewChildElement (tag);
        const auto& all = p.getAllProperties();
        for (int i = 0; i < all.size(); ++i) { auto* v = e->createNewChildElement ("V"); v->setAttribute ("k", all.getAllKeys()[i]); v->setAttribute ("v", all.getAllValues()[i]); }
    };
    addProps ("SETTINGS", core.state);
    if (&props != &core.state) addProps ("GLOBAL", props);
    auto addFolder = [&] (const char* tag, const juce::File& dir)
    {
        auto* e = root.createNewChildElement (tag);
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.json"))
            if (f.getSize() < 4 * 1024 * 1024) { auto* x = e->createNewChildElement ("FILE"); x->setAttribute ("name", f.getFileName()); x->addTextElement (f.loadFileAsString()); }
    };
    addFolder ("THEMES", ScopeTheme::folder());
    addFolder ("TARGETS", TargetLibrary::folder());

    chooser = std::make_unique<juce::FileChooser> ("Back up settings",
        juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DigiCheck Universal backup " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d") + ".dcubackup"),
        "*.dcubackup");
    auto xml = std::make_shared<juce::String> (root.toString());
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe = juce::Component::SafePointer<MainComponent> (this), xml] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (safe == nullptr || f == juce::File()) return;
        safe->showToast (f.replaceWithText (*xml) ? "Settings backed up to " + f.getFileName() : juce::String ("Couldn't write ") + f.getFileName());
    });
}

void MainComponent::restoreSettings()
{
    chooser = std::make_unique<juce::FileChooser> ("Restore settings", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.dcubackup");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe = juce::Component::SafePointer<MainComponent> (this)] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (safe == nullptr || ! f.existsAsFile()) return;
        auto root = juce::XmlDocument::parse (f);
        if (root == nullptr || ! root->hasTagName ("DIGICHECK_UNIVERSAL_BACKUP")) { safe->showToast ("That isn't a DigiCheck Universal backup"); return; }
        auto& self = *safe;
        auto readProps = [&] (const char* tag, juce::PropertySet& p)
        {
            if (auto* e = root->getChildByName (tag))
                for (auto* v : e->getChildIterator()) p.setValue (v->getStringAttribute ("k"), v->getStringAttribute ("v"));
        };
        readProps ("SETTINGS", self.core.state);
        readProps ("GLOBAL", self.props);
        auto readFolder = [&] (const char* tag, const juce::File& dir)
        {
            if (auto* e = root->getChildByName (tag))
            {
                dir.createDirectory();
                for (auto* x : e->getChildIterator())
                {
                    const auto name = juce::File::createLegalFileName (x->getStringAttribute ("name"));
                    if (name.endsWithIgnoreCase (".json")) dir.getChildFile (name).replaceWithText (x->getAllSubText());
                }
            }
        };
        readFolder ("THEMES", ScopeTheme::folder());
        readFolder ("TARGETS", TargetLibrary::folder());
        if (auto* pf = dynamic_cast<juce::PropertiesFile*> (&self.core.state)) pf->saveIfNeeded();
        self.props.saveIfNeeded();
        self.core.saveBlocked = true;            // keep what was restored: nothing overwrites it before the restart
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Settings restored",
            self.core.plugin ? "Close and reopen the project (or remove and re-add the plugin) to use the restored settings."
                             : "Restart DigiCheck Universal to use the restored settings.");
    });
}

void MainComponent::resetDefaults()
{
    auto opts = juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::QuestionIcon).withTitle ("Reset to defaults")
                    .withMessage ("Put every setting back to its default? Presets, Custom layouts, Scope themes and Balance targets are kept. "
                                  "Tip: Back up... first if you might want them back.")
                    .withButton ("Reset", 1).withButton ("Cancel", 0).withAssociatedComponent (this);
    juce::AlertWindow::showAsync (opts, [safe = juce::Component::SafePointer<MainComponent> (this)] (int r)
    {
        if (safe == nullptr || r != 1) return;
        Settings fresh;
        fresh.source = safe->settings.source;            // keep the audio input, so the app still hears something
        fresh.save (safe->core.state);
        safe->core.state.setValue ("analyzerState", juce::String());
        if (auto* pf = dynamic_cast<juce::PropertiesFile*> (&safe->core.state)) pf->saveIfNeeded();
        safe->core.saveBlocked = true;
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Settings reset",
            safe->core.plugin ? "Close and reopen the project to use the default settings." : "Restart DigiCheck Universal to use the default settings.");
    });
}

void MainComponent::setReferenceVisible (bool show)
{
    if (show)
    {
        setOptionsVisible (false);
        const int w = juce::jmin (ui::px (470), getWidth() - 16), h = ReferencePanel::preferredHeight();
        const auto b = bottom.getRefButton().getBounds().translated (bottom.getX(), bottom.getY());
        refPanel->setBounds (juce::jlimit (8, juce::jmax (8, getWidth() - w - 8), b.getX() - 20), bottom.getY() - h - 6, w, h);
        refPanel->refresh();
        refPanel->toFront (false);
    }
    refPanel->setVisible (show);
    referenceChanged();
}

// the reference loaded, cleared, or shown somewhere else: the views follow
void MainComponent::referenceChanged()
{
    auto& r = core.reference;
    balanceView.setReferenceShown (r.ready() && settings.refShowBalance);
    analyzerPanel.getFftView().setReferenceShown (r.ready() && settings.refShowFft);
    monitorPage.setReferenceShown (r.ready() && settings.refShowMonitor);
    bottom.setReferenceState (refPanel != nullptr && refPanel->isVisible(), r.ready(), r.listen.load() || r.isPlaying());
    saveAll();
}

// Custom page: a click in a pane makes it the focused pane (the bottom bar shows its controls)
void MainComponent::notePaneClick (juce::Component& c)
{
    if (settings.viewMode != BottomBar::Custom || ! isParentOf (&c)) return;
    for (int id = 0; id < 6; ++id)
        if (auto* v = viewComponent (id); v != nullptr && v->isVisible() && (v == &c || v->isParentOf (&c)))
        {
            if (settings.customFocus != id) { settings.customFocus = id; bottom.refresh(); repaint(); saveAll(); }
            return;
        }
}

// Saved Custom layouts live with the presets (app-wide), as <LAYOUT name=...> elements
std::unique_ptr<juce::XmlElement> MainComponent::loadLayouts() const
{
    auto x = props.getXmlValue ("customLayouts");
    return x != nullptr ? std::move (x) : std::make_unique<juce::XmlElement> ("LAYOUTS");
}

void MainComponent::refreshSavedLayouts()
{
    juce::StringArray names;
    if (auto x = loadLayouts()) for (auto* e : x->getChildIterator()) names.add (e->getStringAttribute ("name"));
    bottom.savedLayouts = names;
}

void MainComponent::saveLayoutAs (const juce::String& name)
{
    auto x = loadLayouts();
    for (auto* e : x->getChildIterator()) if (e->getStringAttribute ("name") == name) { x->removeChildElement (e, true); break; }
    auto* e = x->createNewChildElement ("LAYOUT");
    e->setAttribute ("name", name);
    e->setAttribute ("layout", settings.customLayout); e->setAttribute ("slots", settings.customSlots);
    e->setAttribute ("splitA", settings.customSplitA); e->setAttribute ("splitB", settings.customSplitB);
    e->setAttribute ("monOrder", settings.customMonOrder); e->setAttribute ("monShown", settings.customMonShown);
    e->setAttribute ("monWide", settings.customMonWide); e->setAttribute ("monMax", settings.customMonMax); e->setAttribute ("monCols", settings.customMonCols);
    props.setValue ("customLayouts", x.get());
    props.saveIfNeeded();
    settings.customName = name;
    refreshSavedLayouts();
    bottom.refresh();
    saveAll();
}

void MainComponent::loadLayout (const juce::String& name)
{
    auto x = loadLayouts();
    for (auto* e : x->getChildIterator())
        if (e->getStringAttribute ("name") == name)
        {
            settings.customLayout = juce::jlimit (0, 5, e->getIntAttribute ("layout", settings.customLayout));
            settings.customSlots = e->getStringAttribute ("slots", settings.customSlots);
            settings.customSplitA = juce::jlimit (0.15, 0.85, e->getDoubleAttribute ("splitA", 0.5));
            settings.customSplitB = juce::jlimit (0.15, 0.85, e->getDoubleAttribute ("splitB", 0.5));
            settings.customMonOrder = e->getStringAttribute ("monOrder", settings.customMonOrder);
            settings.customMonShown = e->getIntAttribute ("monShown", settings.customMonShown) & 63;
            settings.customMonWide = e->getIntAttribute ("monWide", settings.customMonWide) & 63;
            settings.customMonMax = juce::jlimit (-1, 5, e->getIntAttribute ("monMax", -1));
            settings.customMonCols = juce::jlimit (0, 3, e->getIntAttribute ("monCols", 0));
            settings.customName = name;
            monitorPage.setCustomProfile (false);       // re-reads the module layout when it switches back on
            setView (BottomBar::Custom);
            return;
        }
}

void MainComponent::deleteLayout (const juce::String& name)
{
    auto x = loadLayouts();
    for (auto* e : x->getChildIterator()) if (e->getStringAttribute ("name") == name) { x->removeChildElement (e, true); break; }
    props.setValue ("customLayouts", x.get());
    props.saveIfNeeded();
    if (settings.customName == name) settings.customName = {};
    refreshSavedLayouts();
    bottom.refresh();
    saveAll();
}

void MainComponent::setBarHidden (bool h)
{
    barHidden = h && fullscreen;
    barPeek = false;
    bottom.setBarHidden (barHidden);
    resized();
    repaint();
}

void MainComponent::setHelpMode (bool on)
{
    settings.helpMode = on;
    if (on && helpTips == nullptr) helpTips = std::make_unique<juce::TooltipWindow> (this, 350);
    if (! on) helpTips.reset();
    bottom.setHelp (on);
    saveAll();
}

void MainComponent::layoutOptions()
{
    using ui::px;
    const int w = juce::jmin (px (470), getWidth() - 16), h = px ((settings.textSize == 3 ? 404 : 372) + (device != nullptr ? 34 : 0) + 38 + 30 + 28);
    options.setBounds (8, getHeight() - BottomBar::height() - h - 6, w, h);
    options.headings.clear();
    auto r = options.getLocalBounds().reduced (14, 10);
    auto heading = [&] (const juce::String& t) { options.headings.push_back ({ r.removeFromTop (px (20)), t }); };

    heading (device != nullptr ? "AUDIO INPUT" : "PLUGIN");
    source.setBounds (r.removeFromTop (px (28))); r.removeFromTop (px (6));
    auto row = r.removeFromTop (px (28));
    lblL.setBounds (row.removeFromLeft (px (18))); chL.setBounds (row.removeFromLeft ((row.getWidth() - px (26)) / 2)); row.removeFromLeft (px (8));
    lblR.setBounds (row.removeFromLeft (px (18))); chR.setBounds (row);
    r.removeFromTop (px (8));
    row = r.removeFromTop (px (28));
    startBtn.setBounds (row.removeFromLeft (px (90))); row.removeFromLeft (px (6));
    rescanBtn.setBounds (row.removeFromLeft (px (80))); row.removeFromLeft (px (6));
    settingsBtn.setBounds (row.removeFromLeft (px (110)));
    r.removeFromTop (px (6));
    status.setBounds (r.removeFromTop (px (40)));
    r.removeFromTop (px (6));

    heading ("DISPLAY");
    gpuToggle.setBounds (r.removeFromTop (px (26))); r.removeFromTop (px (4));
    row = r.removeFromTop (px (28));
    lblFps.setBounds (row.removeFromLeft (px (130))); fpsBox.setBounds (row.removeFromLeft (px (190)));
    r.removeFromTop (px (6));
    if (device != nullptr)                          // the standalone's full screen (the plugin has none)
    {
        row = r.removeFromTop (px (28));
        lblHdr.setBounds (row.removeFromLeft (px (130))); hdrBox.setBounds (row.removeFromLeft (px (300)));
        r.removeFromTop (px (6));
    }
    lblHdr.setVisible (device != nullptr); hdrBox.setVisible (device != nullptr);
    row = r.removeFromTop (px (28));
    lblText.setBounds (row.removeFromLeft (px (130))); textBox.setBounds (row.removeFromLeft (px (190)));
    if (settings.textSize == 3)
    {
        r.removeFromTop (px (4));
        textSlider.setBounds (r.removeFromTop (px (28)).withTrimmedLeft (px (130)).withWidth (px (260)));
    }
    r.removeFromTop (px (10));
    row = r.removeFromTop (px (28));
    manualBtn.setBounds (row.removeFromLeft (px (120))); row.removeFromLeft (px (6));
    copyImgBtn.setBounds (row.removeFromLeft (px (160)));
    r.removeFromTop (px (6));
    row = r.removeFromTop (px (24));
    updateToggle.setBounds (row.removeFromLeft (px (170))); row.removeFromLeft (px (6));
    updateBtn.setBounds (row.removeFromLeft (px (200)));
    safeToggle.setBounds (r.removeFromTop (px (24)).withWidth (px (280)));
    heading ("SETTINGS");
    row = r.removeFromTop (px (28));
    backupBtn.setBounds (row.removeFromLeft (px (100))); row.removeFromLeft (px (6));
    restoreBtn.setBounds (row.removeFromLeft (px (90))); row.removeFromLeft (px (6));
    defaultsBtn.setBounds (row.removeFromLeft (px (140)));
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    // on the Custom page, keys act on the pane last clicked (none: no view keys)
    const int kv = settings.viewMode == BottomBar::Custom ? (shows (settings.customFocus) ? settings.customFocus : -1) : settings.viewMode;
    const bool ana = kv == BottomBar::Analyzer, bal = kv == BottomBar::Balance || kv == BottomBar::Key || kv == BottomBar::Scope
                    || kv == BottomBar::Monitor || kv < 0;
    if (k == juce::KeyPress::spaceKey)
    {
        if (kv == BottomBar::Key) keyPage.togglePlay();
        else if (kv == BottomBar::Scope) scopePage.toggleFreeze();
        else if (ana) { if (analyzerPanel.isFft()) analyzerPanel.getFftView().toggleFreeze(); else analyzerPanel.getView().toggleFreeze(); }
        else if (! bal) view.togglePause();
        return true;
    }
    if (k.getTextCharacter() == 'v' || k.getTextCharacter() == 'V') { setView ((settings.viewMode + 1) % BottomBar::NumViews); return true; }
    if (k.getTextCharacter() >= '1' && k.getTextCharacter() <= '7') { setView ((int) (k.getTextCharacter() - '1')); return true; }
    if (k == juce::KeyPress (juce::KeyPress::F11Key, juce::ModifierKeys::shiftModifier, 0))
    {
        // straight to full screen without the bar (or out of it)
        if (! fullscreen) { pendingHideBar = true; toggleFullscreen(); } else setBarHidden (! barHidden);
        return true;
    }
    if (k == juce::KeyPress::F11Key)            { toggleFullscreen(); return true; }
    if ((k.getTextCharacter() == 'b' || k.getTextCharacter() == 'B') && fullscreen) { setBarHidden (! barHidden); return true; }
    if (k == juce::KeyPress ('c', juce::ModifierKeys::commandModifier, 0)) { copyViewImage (false); return true; }
    if (k == juce::KeyPress ('c', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { copyViewImage (true); return true; }
    if (k.getTextCharacter() == '?' || k == juce::KeyPress::F1Key) { setHelpMode (! settings.helpMode); return true; }
    if (k.getTextCharacter() == 'o' || k.getTextCharacter() == 'O') { toggleOptions(); return true; }
    if (k == juce::KeyPress::escapeKey)
    {
        if (options.isVisible()) setOptionsVisible (false);
        else if (refPanel != nullptr && refPanel->isVisible()) setReferenceVisible (false);
        else if (fullscreen) toggleFullscreen();
        else view.clearCursor();
        return true;
    }
    if (k.getTextCharacter() == 'k' || k.getTextCharacter() == 'K') { setView (BottomBar::Key); return true; }
    if ((k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R') && kv == BottomBar::Key) { keyPage.toggleRecording(); return true; }
    if (k.getTextCharacter() == 't' || k.getTextCharacter() == 'T') { toggleTempoWindow(); return true; }
    if (k.getTextCharacter() == 'p' || k.getTextCharacter() == 'P')
    {
        if (! settings.hidePanel && ! bal) bottom.clickPresets (ana);
        return true;
    }
    if (k.getTextCharacter() == 's' || k.getTextCharacter() == 'S') { if (ana) analyzerPanel.toggleSettings(); else if (! bal) openSettings(); return true; }
    if (ana || bal) return false;
    if (k == juce::KeyPress::upKey)             { settings.refLevel = juce::jmin (0.0, settings.refLevel + 1);   onChange (Change::Visual); return true; }
    if (k == juce::KeyPress::downKey)           { settings.refLevel = juce::jmax (-80.0, settings.refLevel - 1); onChange (Change::Visual); return true; }
    return false;
}

//==============================================================================
// Presets (bottom bar "Presets" buttons)

void MainComponent::askPresetName (const juce::String& title, std::function<void (const juce::String&)> done)
{
    auto* w = new juce::AlertWindow (title, "Name for the preset:", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", {}, {});
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        const auto name = w->getTextEditorContents ("name").trim();
        if (r == 1 && name.isNotEmpty()) done (name);
    }), true);
    if (auto* ed = w->getTextEditor ("name")) ed->grabKeyboardFocus();
}

void MainComponent::showSpectrogramPresets (juce::Component& anchor)
{
    const auto& builtIn = specpresets::builtIn();
    auto user = specpresets::loadUser (props);

    juce::PopupMenu m, del;
    m.addSectionHeader ("Views");
    for (size_t i = 0; i < builtIn.size(); ++i)
    {
        juce::PopupMenu::Item it (builtIn[i].name);
        it.itemID = 1 + (int) i;
        it.isTicked = specpresets::matches (builtIn[i], settings);
        m.addItem (it);
    }
    if (! user.empty())
    {
        m.addSectionHeader ("My presets");
        for (size_t i = 0; i < user.size(); ++i)
        {
            m.addItem (1000 + (int) i, user[i].name, true, specpresets::matches (user[i], settings));
            del.addItem (2000 + (int) i, user[i].name);
        }
    }
    m.addSeparator();
    m.addItem (3000, "Save current view as preset...");
    if (! user.empty()) m.addSubMenu ("Delete preset", del);

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards),
                     [safe, user] (int id) mutable
    {
        auto* self = safe.getComponent();
        if (self == nullptr || id == 0) return;
        auto applyPreset = [self] (const specpresets::Preset& p)
        {
            specpresets::apply (p, self->settings);
            self->onChange (Change::Dsp);
            self->onChange (Change::Overlay);
            self->onChange (Change::Visual);
        };
        const auto& b = specpresets::builtIn();
        if (id >= 1 && id <= (int) b.size()) applyPreset (b[(size_t) id - 1]);
        else if (id >= 1000 && id < 1000 + (int) user.size()) applyPreset (user[(size_t) id - 1000]);
        else if (id >= 2000 && id < 2000 + (int) user.size())
        {
            user.erase (user.begin() + (id - 2000));
            specpresets::saveUser (self->props, user);
        }
        else if (id == 3000)
            self->askPresetName ("Save spectrogram preset", [safe] (const juce::String& name)
            {
                auto* me = safe.getComponent();
                if (me == nullptr) return;
                auto list = specpresets::loadUser (me->props);
                auto p = specpresets::fromSettings (me->settings, name);
                auto it = std::find_if (list.begin(), list.end(), [&] (auto& x) { return x.name == name; });
                if (it != list.end()) *it = p; else list.push_back (p);   // same name replaces
                specpresets::saveUser (me->props, list);
            });
    });
}

namespace
{
    // analyzer user presets: full analyzer state (settings and colours) under "analyzerPresets"
    std::vector<std::pair<juce::String, juce::String>> loadAnalyzerPresets (juce::PropertiesFile& props)
    {
        std::vector<std::pair<juce::String, juce::String>> out;
        if (auto xml = props.getXmlValue ("analyzerPresets"))
            for (auto* e : xml->getChildIterator())
                if (e->getStringAttribute ("name").isNotEmpty())
                    out.emplace_back (e->getStringAttribute ("name"), e->getStringAttribute ("state"));
        return out;
    }
    void saveAnalyzerPresets (juce::PropertiesFile& props, const std::vector<std::pair<juce::String, juce::String>>& list)
    {
        juce::XmlElement root ("Presets");
        for (auto& p : list)
        {
            auto* e = root.createNewChildElement ("Preset");
            e->setAttribute ("name", p.first);
            e->setAttribute ("state", p.second);
        }
        props.setValue ("analyzerPresets", &root);
        props.saveIfNeeded();
    }
}

void MainComponent::importAnalyzerPreset()
{
    chooser = std::make_unique<juce::FileChooser> ("Import Spectrum Analyzer v1.x preset",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fxp;*.fxb");
    juce::Component::SafePointer<MainComponent> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        auto* self = safe.getComponent();
        if (self == nullptr || fc.getResult() == juce::File()) return;
        juce::String importError;
        if (! self->analyzerProc.importLegacyFile (fc.getResult(), importError))
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Import failed", importError);
        self->analyzerPanel.refreshSettings();
        self->saveAll();
    });
}

void MainComponent::showAnalyzerPresets (juce::Component& anchor)
{
    auto user = loadAnalyzerPresets (props);
    juce::PopupMenu m, del;
    m.addSectionHeader ("Factory presets");
    for (int i = 0; i < analyzerProc.getNumPrograms(); ++i)
        m.addItem (1 + i, analyzerProc.getProgramName (i));
    if (! user.empty())
    {
        m.addSectionHeader ("My presets");
        for (size_t i = 0; i < user.size(); ++i)
        {
            m.addItem (1000 + (int) i, user[i].first);
            del.addItem (2000 + (int) i, user[i].first);
        }
    }
    m.addSeparator();
    m.addItem (3000, "Save current settings as preset...");
    if (! user.empty()) m.addSubMenu ("Delete preset", del);
    m.addItem (3001, "Import v1.x preset (.fxp / .fxb)...");

    juce::Component::SafePointer<MainComponent> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor).withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards),
                     [safe, user] (int id) mutable
    {
        auto* self = safe.getComponent();
        if (self == nullptr || id == 0) return;
        if (id >= 1 && id <= self->analyzerProc.getNumPrograms()) self->analyzerProc.setCurrentProgram (id - 1);
        else if (id >= 1000 && id < 1000 + (int) user.size()) self->analyzerProc.restoreStateXml (user[(size_t) id - 1000].second);
        else if (id >= 2000 && id < 2000 + (int) user.size())
        {
            user.erase (user.begin() + (id - 2000));
            saveAnalyzerPresets (self->props, user);
            return;
        }
        else if (id == 3000)
        {
            self->askPresetName ("Save analyzer preset", [safe] (const juce::String& name)
            {
                auto* me = safe.getComponent();
                if (me == nullptr) return;
                auto list = loadAnalyzerPresets (me->props);
                const auto state = me->analyzerProc.saveStateXml();
                auto it = std::find_if (list.begin(), list.end(), [&] (auto& x) { return x.first == name; });
                if (it != list.end()) it->second = state; else list.emplace_back (name, state);
                saveAnalyzerPresets (me->props, list);
            });
            return;
        }
        else if (id == 3001) { self->importAnalyzerPreset(); return; }
        self->analyzerPanel.refreshSettings();
        self->saveAll();
    });
}

//==============================================================================
// Tonal balance targets

juce::File MainComponent::targetBuilderExe()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory();
   #if JUCE_WINDOWS
    return dir.getChildFile ("TargetBuilder.exe");
   #else
    return dir.getChildFile ("TargetBuilder");
   #endif
}

void MainComponent::refreshBalanceTargets()
{
    targetLibrary.rescanIfChanged();
    juce::StringArray names;
    for (auto& e : targetLibrary.entries()) names.add (e.name);
    int sel = targetLibrary.indexOf (settings.balanceTarget);
    if (sel < 0 && ! names.isEmpty())                         // first time: a sensible default
    {
        for (auto pref : { "Modern", "Pop" })
            if ((sel = targetLibrary.indexOf (pref)) >= 0) break;
        if (sel < 0) sel = 0;
        settings.balanceTarget = names[sel];
    }
    bottom.setBalanceTargets (names, sel);
    balanceView.setTargetIndex (sel);
}

void MainComponent::balanceTargetsAction (int action)
{
    if (action == 1) { auto d = TargetLibrary::folder(); d.createDirectory(); d.startAsProcess(); }
    if (action == 3)
    {
        if (targetBuilderExe().existsAsFile()) targetBuilderExe().startAsProcess();
        else juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Target Builder",
                 "TargetBuilder.exe isn't next to Spectroscope.exe. It comes with the installer, or download it from the release page.");
    }
    if (action == 2)
    {
        targetLibrary.rescan();
        refreshBalanceTargets();
        const auto problems = targetLibrary.skipped();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Targets",
            juce::String ((int) targetLibrary.entries().size()) + " target(s) in " + TargetLibrary::folder().getFullPathName()
            + (problems.isEmpty() ? juce::String() : "\n\nThese files couldn't be read:\n" + problems.joinIntoString ("\n")));
    }
}
