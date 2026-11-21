#include "MainComponent.h"
#include "SettingsDialog.h"

namespace
{
    const int kFpsCaps[] = { 0, 30, 48, 57, 60, 72, 75, 90, 97, 100, 117, 120, 138, 141, 144, 157, 162, 165, 200, 237, 240 };

    // "Key" window: major keys and their relative minors
    struct KeyChart : public juce::Component
    {
        KeyChart() { setSize (300, 452); }
        void paint (juce::Graphics& g) override
        {
            static const char* rows[][2] = { { "C", "A" }, { "C#", "A#" }, { "D", "B" }, { "D#", "C" }, { "E", "C#" }, { "F", "D" },
                                             { "F#", "D#" }, { "G", "E" }, { "G#", "F" }, { "A", "F#" }, { "A#", "G" }, { "B", "G#" } };
            g.fillAll (juce::Colour (0xff1b1d22));
            auto r = getLocalBounds().reduced (20, 14);
            g.setColour (juce::Colours::white.withAlpha (0.55f)); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            auto head = r.removeFromTop (24);
            g.drawText ("MAJOR", head.removeFromLeft (r.getWidth() / 2), juce::Justification::centredLeft);
            g.drawText ("RELATIVE MINOR", head, juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xff3a3c40)); g.drawHorizontalLine (r.getY() + 2, (float) r.getX(), (float) r.getRight());
            r.removeFromTop (8);
            g.setFont (juce::FontOptions (17.0f));
            for (auto& row : rows)
            {
                auto line = r.removeFromTop (32);
                g.setColour (juce::Colours::white);
                g.drawText (juce::String (row[0]) + " major", line.removeFromLeft (r.getWidth() / 2), juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xffb8c4ff));
                g.drawText (juce::String (row[1]) + " minor", line, juce::Justification::centredLeft);
            }
        }
    };

    //==========================================================================
    // "Tempo" window: BPM -> note lengths in milliseconds, with triplet / dotted columns.
    // Click a value to copy it.
    class TempoChart : public juce::Component, private juce::Timer
    {
    public:
        TempoChart (Settings& st, std::function<void()> changedCallback) : s (st), onChanged (std::move (changedCallback))
        {
            bpm.setSliderStyle (juce::Slider::IncDecButtons);
            bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 90, 30);
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

        void resizeToFit() { setSize (250 + 112 * ((int) s.tempoTriplets + (int) s.tempoDotted), kTableTop + 9 * kRow + 44); }

        void resized() override
        {
            auto top = getLocalBounds().reduced (16, 14).removeFromTop (32);
            top.removeFromLeft (44);                                   // "BPM" caption
            bpm.setBounds (top.removeFromLeft (150));
            top.removeFromLeft (8);
            tap.setBounds (top.removeFromLeft (56));
            auto opts = getLocalBounds().reduced (16, 0).withTop (54).withHeight (24);
            triplets.setBounds (opts.removeFromLeft (90));
            dotted.setBounds (opts.removeFromLeft (90));
            if (auto* w = findParentComponentOfClass<juce::DocumentWindow>()) w->setContentComponentSize (getWidth(), getHeight());
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1b1d22));
            g.setColour (juce::Colours::white.withAlpha (0.55f)); g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText ("BPM", 16, 14, 40, 32, juce::Justification::centredLeft);

            const auto cols = columns();
            auto table = getLocalBounds().reduced (16, 0).withTop (kTableTop - 30);
            auto head = table.removeFromTop (24);
            g.drawText ("NOTE", head.removeFromLeft (kNoteW), juce::Justification::centredLeft);
            for (auto& c : cols) g.drawText (c.name, head.removeFromLeft (kColW).withTrimmedRight (28), juce::Justification::centredRight);
            g.setColour (juce::Colour (0xff3a3c40)); g.drawHorizontalLine (kTableTop - 4, 16.0f, (float) getWidth() - 16.0f);

            static const char* notes[] = { "2/1", "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64", "1/128" };
            const auto mono = juce::Font::getDefaultMonospacedFontName();
            for (int i = 0; i < 9; ++i)
            {
                auto line = juce::Rectangle<int> (16, kTableTop + i * kRow, getWidth() - 32, kRow);
                if (i % 2 == 1) { g.setColour (juce::Colours::white.withAlpha (0.035f)); g.fillRect (line.expanded (6, 0)); }
                if (i == 3) { g.setColour (juce::Colour (0xff4c8dff).withAlpha (0.12f)); g.fillRect (line.expanded (6, 0)); }  // the beat
                g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
                g.drawText (notes[i], line.removeFromLeft (kNoteW), juce::Justification::centredLeft);
                for (auto& c : cols)
                {
                    const double ms = noteMs (i) * c.factor;
                    auto cell = line.removeFromLeft (kColW);
                    const bool copied = copiedRow == i && copiedCol == &c - cols.data();
                    g.setColour (copied ? juce::Colour (0xff3ddc84) : c.colour);
                    g.setFont (juce::FontOptions (mono, 17.0f, juce::Font::bold));
                    g.drawText (copied ? juce::String ("copied") : fmt (ms), cell.withTrimmedRight (28), juce::Justification::centredRight);
                    g.setColour (juce::Colours::white.withAlpha (0.45f)); g.setFont (juce::FontOptions (12.0f));
                    g.drawText ("ms", cell.removeFromRight (24), juce::Justification::centredRight);
                }
            }
            g.setColour (juce::Colours::white.withAlpha (0.4f)); g.setFont (juce::FontOptions (11.5f));
            g.drawText ("Click a value to copy it", 16, getHeight() - 30, getWidth() - 32, 20, juce::Justification::centredLeft);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            const int row = (e.y - kTableTop) / kRow, col = (e.x - 16 - kNoteW) / kColW;
            const auto cols = columns();
            if (e.y < kTableTop || row < 0 || row > 8 || e.x < 16 + kNoteW || col < 0 || col >= (int) cols.size()) return;
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

    struct KeyWindow : public juce::DocumentWindow
    {
        explicit KeyWindow (std::function<void()> closed)
            : DocumentWindow ("Keys", juce::Colour (0xff1b1d22), closeButton), onClosed (std::move (closed))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new KeyChart(), true);
            setAlwaysOnTop (true);          // stays visible over the full-screen display
            setResizable (false, false);
        }
        void closeButtonPressed() override { setVisible (false); if (onClosed) onClosed(); }
        std::function<void()> onClosed;
    };
}

MainComponent::MainComponent (juce::PropertiesFile& p)
    : props (p), view (settings, engine, [this] (Change c) { onChange (c); })
{
    settings.load (props);
    if (settings.analyzerState.isNotEmpty()) analyzerProc.restoreStateXml (settings.analyzerState);
    engine.setAnalyzer (&analyzerProc);
    setWantsKeyboardFocus (true);

    status.setJustificationType (juce::Justification::topLeft);
    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    status.setFont (juce::FontOptions (12.5f));
    source.setTooltip ("Signal source");
    for (auto* l : { &lblL, &lblR }) { l->setBorderSize ({}); l->setJustificationType (juce::Justification::centred); }
    startBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4c8dff));

    for (auto* c : std::initializer_list<juce::Component*> { &source, &lblL, &chL, &lblR, &chR, &startBtn, &settingsBtn, &rescanBtn,
                                                             &status, &gpuToggle, &lblFps, &fpsBox })
        options.addAndMakeVisible (c);
    addAndMakeVisible (view);
    addChildComponent (analyzerPanel);
    addAndMakeVisible (bottom);
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

    startBtn.onClick    = [this] { if (engine.isRunning()) stopSource(); else startSelected(); };
    settingsBtn.onClick = [this] { if (settings.viewMode == BottomBar::Analyzer) analyzerPanel.toggleSettings(); else openSettings(); };
    rescanBtn.onClick   = [this] { rescan(); };
    source.onChange     = [this] { if (engine.isRunning()) startSelected(); };
    chL.onChange = chR.onChange = [this]
    {
        if (chL.getSelectedItemIndex() < 0 || chR.getSelectedItemIndex() < 0) return;
        settings.chL = chL.getSelectedItemIndex(); settings.chR = chR.getSelectedItemIndex();
        engine.setChannels (settings.chL, settings.chR);
        settings.save (props);
    };

    // bottom bar
    view.onPauseChanged      = [this] (bool isPaused) { bottom.setPaused (isPaused); };
    bottom.onViewChange      = [this] (int v) { setView (v); };
    bottom.onPause           = [this] { view.togglePause(); };
    bottom.onRef             = [this] (int d) { settings.refLevel = juce::jlimit (-80.0, 0.0, settings.refLevel + d); onChange (Change::Visual); };
    bottom.onToggleKey       = [this] { toggleKeyWindow(); };
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

    rescan();
    setSize (1300, 760);
    startTimerHz (20);

    // Resume the last source automatically
    if (settings.source.isNotEmpty())
        for (auto& s : sources)
            if (s.key == settings.source) { juce::MessageManager::callAsync ([this] { startSelected(); }); break; }
}

MainComponent::~MainComponent()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (&outsideClick);
    glContext.detach();
    saveAll();
    engine.stop();
    engine.setAnalyzer (nullptr);
}

void MainComponent::saveAll()
{
    settings.analyzerState = analyzerProc.saveStateXml();
    settings.save (props);
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

void MainComponent::setOptionsVisible (bool show)
{
    if (show) { updateStatus(); layoutOptions(); options.toFront (false); }
    options.setVisible (show);
    bottom.setOptionsOpen (show);
    if (! show) grabKeyboardFocus();
}

void MainComponent::setView (int v)
{
    settings.viewMode = juce::jlimit (0, 1, v);
    const bool ana = settings.viewMode == BottomBar::Analyzer;
    view.setVisible (! ana);
    analyzerPanel.setVisible (ana);
    analyzerPanel.setActive (ana);
    engine.setSpectrogramActive (! ana);
    if (ana) analyzerPanel.getView().grabKeyboardFocus();
    bottom.refresh();
    saveAll();
}

void MainComponent::rescan()
{
    const auto keep = source.getSelectedItemIndex() >= 0 ? sources[(size_t) source.getSelectedItemIndex()].key : settings.source;
    sources = engine.scanSources();
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
    if (! engine.start (sources[(size_t) i], error))
    {
        startBtn.setButtonText ("Start");
        updateStatus();
        return;
    }
    settings.source = sources[(size_t) i].key;
    settings.save (props);
    fillChannelBoxes();
    startBtn.setButtonText ("Stop");
    startBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xffc0392b));
    view.sourceStarted();
    updateStatus();
}

void MainComponent::stopSource()
{
    engine.stop();
    view.sourceStopped();
    startBtn.setButtonText ("Start");
    startBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4c8dff));
    updateStatus();
}

void MainComponent::fillChannelBoxes()
{
    const auto& names = engine.getChannelNames();
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
    auto& desk = juce::Desktop::getInstance();
    auto* top = getTopLevelComponent();
    const bool goFull = desk.getKioskModeComponent() != top;
    desk.setKioskModeComponent (goFull ? top : nullptr, false);
    bottom.setFullscreen (goFull);
    resized();
    grabKeyboardFocus();
}

bool MainComponent::isFullscreen() const
{
    return juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent();
}

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

void MainComponent::toggleKeyWindow()
{
    if (keyWindow == nullptr)
    {
        keyWindow = std::make_unique<KeyWindow> ([this] { bottom.setKeyOpen (false); });
        auto* w = keyWindow.get();
        const auto area = getScreenBounds();
        w->setTopLeftPosition (area.getRight() - w->getWidth() - 60, area.getY() + 60);
    }
    const bool show = ! keyWindow->isVisible();
    keyWindow->setVisible (show);
    if (show) keyWindow->toFront (false);
    bottom.setKeyOpen (show);
}

void MainComponent::timerCallback()
{
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
    else if (! engine.isRunning()) text = "Stopped";
    else
    {
        const auto& src = engine.getCurrentSource();
        const juce::String kind = src.kind == AudioEngine::Source::Loopback ? "Loopback"
                                : src.kind == AudioEngine::Source::Test     ? "Test signal"
                                : src.typeName;
        text = kind + "  |  " + juce::String (engine.getSampleRate() / 1000.0, 1) + " kHz  |  "
             + juce::String (engine.getChannelNames().size()) + " ch  |  latency "
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
    bottom.setBounds (r.removeFromBottom (BottomBar::kHeight));
    view.setBounds (r);                       // no top toolbar: the analysis uses the full height
    analyzerPanel.setBounds (r);
    layoutOptions();
}

void MainComponent::layoutOptions()
{
    const int w = juce::jmin (460, getWidth() - 16), h = 330;
    options.setBounds (8, getHeight() - BottomBar::kHeight - h - 6, w, h);
    options.headings.clear();
    auto r = options.getLocalBounds().reduced (14, 10);
    auto heading = [&] (const juce::String& t) { options.headings.push_back ({ r.removeFromTop (20), t }); };

    heading ("AUDIO INPUT");
    source.setBounds (r.removeFromTop (28)); r.removeFromTop (6);
    auto row = r.removeFromTop (28);
    lblL.setBounds (row.removeFromLeft (18)); chL.setBounds (row.removeFromLeft ((row.getWidth() - 26) / 2)); row.removeFromLeft (8);
    lblR.setBounds (row.removeFromLeft (18)); chR.setBounds (row);
    r.removeFromTop (8);
    row = r.removeFromTop (28);
    startBtn.setBounds (row.removeFromLeft (90)); row.removeFromLeft (6);
    rescanBtn.setBounds (row.removeFromLeft (80)); row.removeFromLeft (6);
    settingsBtn.setBounds (row.removeFromLeft (110));
    r.removeFromTop (6);
    status.setBounds (r.removeFromTop (40));
    r.removeFromTop (6);

    heading ("DISPLAY");
    gpuToggle.setBounds (r.removeFromTop (26)); r.removeFromTop (4);
    row = r.removeFromTop (28);
    lblFps.setBounds (row.removeFromLeft (120)); fpsBox.setBounds (row.removeFromLeft (190));
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    const bool ana = settings.viewMode == BottomBar::Analyzer;
    if (k == juce::KeyPress::spaceKey)          { if (ana) analyzerPanel.getView().toggleFreeze(); else view.togglePause(); return true; }
    if (k.getTextCharacter() == 'v' || k.getTextCharacter() == 'V') { setView (ana ? BottomBar::Spectrogram : BottomBar::Analyzer); return true; }
    if (k == juce::KeyPress::F11Key)            { toggleFullscreen(); return true; }
    if (k.getTextCharacter() == 'o' || k.getTextCharacter() == 'O') { toggleOptions(); return true; }
    if (k == juce::KeyPress::escapeKey)
    {
        if (options.isVisible()) setOptionsVisible (false);
        else if (juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent()) toggleFullscreen();
        else view.clearCursor();
        return true;
    }
    if (k.getTextCharacter() == 'k' || k.getTextCharacter() == 'K') { toggleKeyWindow(); return true; }
    if (k.getTextCharacter() == 't' || k.getTextCharacter() == 'T') { toggleTempoWindow(); return true; }
    if (k.getTextCharacter() == 's' || k.getTextCharacter() == 'S') { if (ana) analyzerPanel.toggleSettings(); else openSettings(); return true; }
    if (ana) return false;
    if (k == juce::KeyPress::upKey)             { settings.refLevel = juce::jmin (0.0, settings.refLevel + 1);   onChange (Change::Visual); return true; }
    if (k == juce::KeyPress::downKey)           { settings.refLevel = juce::jmax (-80.0, settings.refLevel - 1); onChange (Change::Visual); return true; }
    return false;
}
