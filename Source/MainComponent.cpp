#include "MainComponent.h"
#include "SettingsDialog.h"

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

    for (auto* c : std::initializer_list<juce::Component*> { &title, &source, &lblL, &chL, &lblR, &chR, &startBtn, &settingsBtn, &rescanBtn, &status, &view })
        addAndMakeVisible (c);

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

    rescan();
    setSize (1300, 760);
    startTimerHz (2);

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
}

void MainComponent::openSettings()
{
    SettingsDialog::show (settings, [this] (Change c) { onChange (c); }, this);
}

void MainComponent::timerCallback() { updateStatus(); }

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
    g.setColour (juce::Colour (0xff16181c)); g.fillRect (0, 0, getWidth(), 40);
    g.setColour (juce::Colour (0xff2a2d33)); g.drawHorizontalLine (40, 0.0f, (float) getWidth());
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    auto bar = r.removeFromTop (40).reduced (8, 7);
    r.removeFromTop (1);
    view.setBounds (r);
    title.setBounds (bar.removeFromLeft (100));
    source.setBounds (bar.removeFromLeft (juce::jmin (380, juce::jmax (180, getWidth() / 4)))); bar.removeFromLeft (8);
    lblL.setBounds (bar.removeFromLeft (18)); chL.setBounds (bar.removeFromLeft (120)); bar.removeFromLeft (6);
    lblR.setBounds (bar.removeFromLeft (18)); chR.setBounds (bar.removeFromLeft (120)); bar.removeFromLeft (8);
    startBtn.setBounds (bar.removeFromLeft (70)); bar.removeFromLeft (6);
    settingsBtn.setBounds (bar.removeFromLeft (80)); bar.removeFromLeft (6);
    rescanBtn.setBounds (bar.removeFromLeft (70)); bar.removeFromLeft (8);
    status.setBounds (bar);
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)          { view.togglePause(); return true; }
    if (k == juce::KeyPress::escapeKey)         { view.clearCursor(); return true; }
    if (k == juce::KeyPress::upKey)             { settings.refLevel = juce::jmin (0.0, settings.refLevel + 1);   onChange (Change::Visual); return true; }
    if (k == juce::KeyPress::downKey)           { settings.refLevel = juce::jmax (-80.0, settings.refLevel - 1); onChange (Change::Visual); return true; }
    if (k.getTextCharacter() == 's' || k.getTextCharacter() == 'S') { openSettings(); return true; }
    return false;
}
