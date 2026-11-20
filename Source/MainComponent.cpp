#include "MainComponent.h"
#include "SettingsDialog.h"

namespace
{
    constexpr int kBarH = 41;

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
    setWantsKeyboardFocus (true);

    title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    status.setJustificationType (juce::Justification::centredRight);
    status.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    source.setTooltip ("Signal source");
    for (auto* l : { &lblL, &lblR }) { l->setBorderSize ({}); l->setJustificationType (juce::Justification::centred); }
    startBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff4c8dff));

    for (auto* c : std::initializer_list<juce::Component*> { &title, &source, &lblL, &chL, &lblR, &chR, &startBtn, &settingsBtn, &rescanBtn, &status })
        bar.addAndMakeVisible (c);
    addAndMakeVisible (view);
    addAndMakeVisible (bar);

    startBtn.onClick    = [this] { if (engine.isRunning()) stopSource(); else startSelected(); };
    settingsBtn.onClick = [this] { openSettings(); };
    rescanBtn.onClick   = [this] { rescan(); };
    source.onChange     = [this] { if (engine.isRunning()) startSelected(); };
    chL.onChange = chR.onChange = [this]
    {
        if (chL.getSelectedItemIndex() < 0 || chR.getSelectedItemIndex() < 0) return;
        settings.chL = chL.getSelectedItemIndex(); settings.chR = chR.getSelectedItemIndex();
        engine.setChannels (settings.chL, settings.chR);
        settings.save (props);
    };

    view.onToggleFullscreen = [this] { toggleFullscreen(); };
    view.onToggleKey = [this] { toggleKeyWindow(); };

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
    settings.save (props);
    engine.stop();
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
    settings.save (props);
    view.settingsChanged (c);
    if (c == Change::Layout) resized();      // auto-hide toolbar may have changed
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
    view.setFullscreenState (goFull);
    barRevealed = false;
    resized();
    grabKeyboardFocus();
}

bool MainComponent::isFullscreen() const
{
    return juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent();
}

void MainComponent::toggleKeyWindow()
{
    if (keyWindow == nullptr)
    {
        keyWindow = std::make_unique<KeyWindow> ([this] { view.setKeyWindowOpen (false); });
        auto* w = keyWindow.get();
        const auto area = getScreenBounds();
        w->setTopLeftPosition (area.getRight() - w->getWidth() - 60, area.getY() + 60);
    }
    const bool show = ! keyWindow->isVisible();
    keyWindow->setVisible (show);
    if (show) keyWindow->toFront (false);
    view.setKeyWindowOpen (show);
}

// Full screen with auto-hide: the toolbar is hidden and slides in when the mouse touches the top
// edge; it stays while the mouse is over it (or a menu/dialog from it is open) and hides shortly after.
void MainComponent::updateBarReveal()
{
    if (! barAutoHidden()) return;
    const auto pos = getLocalPoint (nullptr, juce::Desktop::getMousePosition());
    const auto now = juce::Time::getMillisecondCounter();
    const bool menuOpen = juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0;
    const bool inside = pos.x >= 0 && pos.x < getWidth();
    if (inside && pos.y <= 3) barRevealed = true;
    if (barRevealed && ((inside && pos.y >= 0 && pos.y < kBarH + 12) || menuOpen)) lastInBar = now;
    if (barRevealed && now - lastInBar > 700) barRevealed = false;
    if (bar.isVisible() != barRevealed) { bar.setVisible (barRevealed); if (barRevealed) bar.toFront (false); }
}

void MainComponent::timerCallback()
{
    updateBarReveal();
    if (++tick % 10 == 0) updateStatus();
}

void MainComponent::updateStatus()
{
    if (error.isNotEmpty()) { status.setText ("Could not start: " + error, juce::dontSendNotification); return; }
    if (! engine.isRunning()) { status.setText ("Stopped", juce::dontSendNotification); return; }
    const auto& src = engine.getCurrentSource();
    juce::String kind = src.kind == AudioEngine::Source::Loopback ? "Loopback"
                      : src.kind == AudioEngine::Source::Test     ? "Test"
                      : src.typeName;
    status.setText (kind + "  |  " + juce::String (engine.getSampleRate() / 1000.0, 1) + " kHz  |  "
                    + juce::String (engine.getChannelNames().size()) + " ch  |  latency "
                    + juce::String (juce::roundToInt (engine.getLatencySeconds() * 1000)) + " ms  |  DSP "
                    + juce::String (juce::roundToInt (engine.getLoad() * 100)) + "%",
                    juce::dontSendNotification);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0c0e));
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    if (barAutoHidden())
    {
        view.setBounds (r);                                   // display uses the whole screen
        bar.setBounds (r.removeFromTop (kBarH));              // toolbar overlays it when revealed
        bar.setVisible (barRevealed);
        if (barRevealed) bar.toFront (false);
    }
    else
    {
        bar.setBounds (r.removeFromTop (kBarH));
        bar.setVisible (true);
        view.setBounds (r);
    }
    layoutBar();
}

void MainComponent::layoutBar()
{
    auto row = bar.getLocalBounds().withTrimmedBottom (1).reduced (8, 7);
    title.setBounds (row.removeFromLeft (100));
    source.setBounds (row.removeFromLeft (juce::jmin (380, juce::jmax (180, getWidth() / 4)))); row.removeFromLeft (8);
    lblL.setBounds (row.removeFromLeft (18)); chL.setBounds (row.removeFromLeft (120)); row.removeFromLeft (6);
    lblR.setBounds (row.removeFromLeft (18)); chR.setBounds (row.removeFromLeft (120)); row.removeFromLeft (8);
    startBtn.setBounds (row.removeFromLeft (70)); row.removeFromLeft (6);
    settingsBtn.setBounds (row.removeFromLeft (80)); row.removeFromLeft (6);
    rescanBtn.setBounds (row.removeFromLeft (70)); row.removeFromLeft (8);
    status.setBounds (row);
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)          { view.togglePause(); return true; }
    if (k == juce::KeyPress::F11Key)            { toggleFullscreen(); return true; }
    if (k == juce::KeyPress::escapeKey)
    {
        if (juce::Desktop::getInstance().getKioskModeComponent() == getTopLevelComponent()) toggleFullscreen();
        else view.clearCursor();
        return true;
    }
    if (k == juce::KeyPress::upKey)             { settings.refLevel = juce::jmin (0.0, settings.refLevel + 1);   onChange (Change::Visual); return true; }
    if (k == juce::KeyPress::downKey)           { settings.refLevel = juce::jmax (-80.0, settings.refLevel - 1); onChange (Change::Visual); return true; }
    if (k.getTextCharacter() == 's' || k.getTextCharacter() == 'S') { openSettings(); return true; }
    if (k.getTextCharacter() == 'k' || k.getTextCharacter() == 'K') { toggleKeyWindow(); return true; }
    return false;
}
