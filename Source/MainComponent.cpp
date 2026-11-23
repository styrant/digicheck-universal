#include "MainComponent.h"
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
    bottom.onLoudnessReset = [this] { monitorPage.model().reset(); };
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
    if (settings.gpu && ! glContext.isAttached())
    {
        view.setGpuMode (true);
        glContext.attachTo (*this);
    }
    else if (! settings.gpu && glContext.isAttached())
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
    for (auto* l : { &lblL, &lblR, &lblFps, &lblText }) l->setFont (juce::FontOptions (ui::fs (15.0f)));
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
    if (show) { updateStatus(); layoutOptions(); options.toFront (false); }
    options.setVisible (show);
    bottom.setOptionsOpen (show);
    if (! show) grabKeyboardFocus();
}

void MainComponent::setView (int v)
{
    settings.viewMode = juce::jlimit (0, 5, v);
    const bool spec = settings.viewMode == BottomBar::Spectrogram, ana = settings.viewMode == BottomBar::Analyzer,
               bal = settings.viewMode == BottomBar::Balance, key = settings.viewMode == BottomBar::Key,
               sco = settings.viewMode == BottomBar::Scope, mon = settings.viewMode == BottomBar::Monitor;
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
    if (bal) { refreshBalanceTargets(); grabKeyboardFocus(); }
    if (key || sco || mon) grabKeyboardFocus();
    if (ana) { if (analyzerPanel.isFft()) grabKeyboardFocus(); else analyzerPanel.getView().grabKeyboardFocus(); }
    bottom.refresh();
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
        const auto apply = [this] { view.sidechainStyleChanged(); saveAll(); if (auto* e = host->sidechainEngine()) e->setSpectrogramActive (settings.viewMode == BottomBar::Spectrogram && settings.specSc); };
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
    }
    balanceView.setInputRunning (inputRunning());
    keyPage.setInputRunning (inputRunning());
    scopePage.setInputRunning (inputRunning());
    monitorPage.pump();                          // keeps measuring when display frames stall (minimised)
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
        + (glContext.isAttached() || settings.viewMode == BottomBar::Spectrogram ? ", " + juce::String (juce::roundToInt (fps)) + " fps" : juce::String())
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
    if (! glContext.isAttached()) g.fillAll (juce::Colour (0xff0b0c0e));   // GPU: children cover everything
}

void MainComponent::resized()
{
    ownerW = (float) getWidth(); ownerH = (float) getHeight();
    auto r = getLocalBounds();
    bottom.setBounds (r.removeFromBottom (BottomBar::height()));
    view.setBounds (r);                       // no top toolbar: the analysis uses the full height
    analyzerPanel.setBounds (r);
    balanceView.setBounds (r);
    keyPage.setBounds (r);
    scopePage.setBounds (r);
    monitorPage.setBounds (r);
    layoutOptions();
}

void MainComponent::layoutOptions()
{
    using ui::px;
    const int w = juce::jmin (px (470), getWidth() - 16), h = px (settings.textSize == 3 ? 404 : 372);
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
    row = r.removeFromTop (px (28));
    lblText.setBounds (row.removeFromLeft (px (130))); textBox.setBounds (row.removeFromLeft (px (190)));
    if (settings.textSize == 3)
    {
        r.removeFromTop (px (4));
        textSlider.setBounds (r.removeFromTop (px (28)).withTrimmedLeft (px (130)).withWidth (px (260)));
    }
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const bool ana = settings.viewMode == BottomBar::Analyzer, bal = settings.viewMode == BottomBar::Balance || settings.viewMode == BottomBar::Key || settings.viewMode == BottomBar::Scope
                    || settings.viewMode == BottomBar::Monitor;
    if (k == juce::KeyPress::spaceKey)
    {
        if (settings.viewMode == BottomBar::Key) keyPage.togglePlay();
        else if (settings.viewMode == BottomBar::Scope) scopePage.toggleFreeze();
        else if (ana) { if (analyzerPanel.isFft()) analyzerPanel.getFftView().toggleFreeze(); else analyzerPanel.getView().toggleFreeze(); }
        else if (! bal) view.togglePause();
        return true;
    }
    if (k.getTextCharacter() == 'v' || k.getTextCharacter() == 'V') { setView ((settings.viewMode + 1) % 6); return true; }
    if (k.getTextCharacter() >= '1' && k.getTextCharacter() <= '6') { setView ((int) (k.getTextCharacter() - '1')); return true; }
    if (k == juce::KeyPress::F11Key)            { toggleFullscreen(); return true; }
    if (k.getTextCharacter() == 'o' || k.getTextCharacter() == 'O') { toggleOptions(); return true; }
    if (k == juce::KeyPress::escapeKey)
    {
        if (options.isVisible()) setOptionsVisible (false);
        else if (fullscreen) toggleFullscreen();
        else view.clearCursor();
        return true;
    }
    if (k.getTextCharacter() == 'k' || k.getTextCharacter() == 'K') { setView (BottomBar::Key); return true; }
    if ((k.getTextCharacter() == 'r' || k.getTextCharacter() == 'R') && settings.viewMode == BottomBar::Key) { keyPage.toggleRecording(); return true; }
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
