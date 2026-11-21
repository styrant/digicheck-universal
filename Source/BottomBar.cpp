#include "BottomBar.h"

namespace
{
    const juce::Colour kFrame (0xff2a2b2e), kFrameLine (0xff3a3c40), kLabel (0xffc8cacd);
    const juce::Colour kButton (0xff3a3c41), kOn (0xff4c8dff);

    // Smaller text for the right-hand corner buttons
    struct SmallButtonLnf : public juce::LookAndFeel_V4
    {
        SmallButtonLnf() { setColourScheme (getDarkColourScheme()); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (ui::fs (11.5f)); }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (ui::fs (13.0f)); }
    };
    SmallButtonLnf& smallLnf() { static SmallButtonLnf l; return l; }
}

BottomBar::BottomBar (Settings& st, AudioEngine& e, SpectrumAnalyzerProcessor& proc) : s (st), engine (e)
{
    for (auto* b : { &optionsBtn, &viewSpec, &viewAna, &lufsBtn, &keyBtn, &tempoBtn, &specPresets, &anaPresets, &pause, &refDown, &refUp, &specSettings,
                     &zoomOut, &zoomIn, &fullRange, &freeze, &anaSettings, &resetBtn, &full })
    {
        addAndMakeVisible (b);
        b->setColour (juce::TextButton::buttonColourId, kButton);
        b->setColour (juce::TextButton::buttonOnColourId, kOn);
    }

    optionsBtn.setTooltip ("Audio input, rendering and frame-rate options (O)");
    optionsBtn.onClick = [this] { if (onToggleOptions) onToggleOptions(); };
    specSettings.setTooltip ("Spectrogram settings (S)");
    specSettings.onClick = [this] { if (onSpectrogramSettings) onSpectrogramSettings(); };

    // view switch: two joined buttons
    viewSpec.setConnectedEdges (juce::Button::ConnectedOnRight);
    viewAna.setConnectedEdges (juce::Button::ConnectedOnLeft);
    viewSpec.setTooltip ("Reassigned spectrogram (V)");
    viewAna.setTooltip ("Filter-bank spectrum analyzer (V)");
    viewSpec.onClick = [this] { if (onViewChange) onViewChange (Spectrogram); };
    viewAna.onClick  = [this] { if (onViewChange) onViewChange (Analyzer); };

    lufsBtn.setTooltip ("Show or hide the loudness meter");
    keyBtn.setTooltip ("Major keys and their relative minors (K)");
    lufsBtn.onClick = [this] { if (onToggleLufs) onToggleLufs(); };
    keyBtn.onClick  = [this] { if (onToggleKey) onToggleKey(); };
    tempoBtn.setTooltip ("BPM to milliseconds calculator (T)");
    tempoBtn.onClick = [this] { if (onToggleTempo) onToggleTempo(); };

    specPresets.setTooltip ("Spectrogram presets: built-in views and your own (P)");
    anaPresets.setTooltip ("Analyzer presets (P)");
    specPresets.onClick = [this] { if (onSpectrogramPresets) onSpectrogramPresets (specPresets); };
    anaPresets.onClick  = [this] { if (onAnalyzerPresets) onAnalyzerPresets (anaPresets); };
    pause.setTooltip ("Pause (Space)");
    refDown.setTooltip ("Reference level -1 dB (mouse wheel / Down)");
    refUp.setTooltip ("Reference level +1 dB (mouse wheel / Up)");
    pause.onClick   = [this] { if (onPause) onPause(); };
    refDown.onClick = [this] { if (onRef) onRef (-1); };
    refUp.onClick   = [this] { if (onRef) onRef (+1); };
    ref.setJustificationType (juce::Justification::centred);
    ref.setFont (juce::FontOptions (ui::fs (15.0f)));
    ref.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1e1f22));
    ref.setColour (juce::Label::outlineColourId, juce::Colour (0xff4a4c51));
    addAndMakeVisible (ref);

    // analyzer controls bound straight to the analyzer's parameters
    auto setupChoice = [&] (juce::ComboBox& box, const char* pid, const juce::String& tip,
                            std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& att)
    {
        if (auto* param = dynamic_cast<juce::AudioParameterChoice*> (proc.apvts.getParameter (pid)))
            box.addItemList (param->choices, 1);
        box.setTooltip (tip);
        att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, pid, box);
        addAndMakeVisible (box);
    };
    setupChoice (bpoBox,  sa::params::bpo,      "Bands per octave (resolution)", bpoAtt);
    setupChoice (chanBox, sa::params::channel,  "Analyzer input channel",        chanAtt);
    setupChoice (gridBox, sa::params::freqGrid, "Frequency grid",                gridAtt);
    zoomIn.setTooltip ("Zoom in (mouse wheel)");
    zoomOut.setTooltip ("Zoom out");
    fullRange.setTooltip ("Show the full frequency range (0)");
    freeze.setTooltip ("Freeze the display (F)");
    anaSettings.setTooltip ("Analyzer settings (right-click the display)");
    zoomIn.onClick      = [this] { if (onZoomIn) onZoomIn(); };
    zoomOut.onClick     = [this] { if (onZoomOut) onZoomOut(); };
    fullRange.onClick   = [this] { if (onFullRange) onFullRange(); };
    freeze.onClick      = [this] { if (onFreeze) onFreeze(); };
    anaSettings.onClick = [this] { if (onAnalyzerSettings) onAnalyzerSettings(); };

    addAndMakeVisible (lufs);
    for (auto* b : { &resetBtn, &full }) b->setLookAndFeel (&smallLnf());
    resetBtn.setTooltip ("Restart integrated loudness and true-peak hold");
    full.setTooltip ("Full screen (F11, Esc to leave)");
    resetBtn.onClick = [this] { engine.resetLoudness(); };
    full.onClick     = [this] { if (onToggleFullscreen) onToggleFullscreen(); };

    for (auto* b : { &viewSpec, &viewAna, &lufsBtn, &keyBtn })
        b->setClickingTogglesState (false);   // state comes from settings via refresh()

    refresh();
    startTimerHz (10);
}

BottomBar::~BottomBar()
{
    for (auto* b : { &resetBtn, &full }) b->setLookAndFeel (nullptr);
}

void BottomBar::refresh()
{
    ref.setFont (juce::FontOptions (ui::fs (15.0f)));
    viewSpec.setToggleState (s.viewMode == Spectrogram, juce::dontSendNotification);
    viewAna.setToggleState  (s.viewMode == Analyzer, juce::dontSendNotification);
    lufsBtn.setToggleState  (s.showLufs, juce::dontSendNotification);
    ref.setText ("Ref: " + juce::String (s.refLevel, 0) + " dB", juce::dontSendNotification);
    resized();
    repaint();
}

void BottomBar::setPaused (bool p)     { pause.setButtonText (p ? ">" : "||"); }
void BottomBar::setFullscreen (bool f) { full.setButtonText (f ? "Exit full screen" : "Full screen"); }
void BottomBar::setKeyOpen (bool o)    { keyBtn.setToggleState (o, juce::dontSendNotification); }
void BottomBar::setTempoOpen (bool o)  { tempoBtn.setToggleState (o, juce::dontSendNotification); }
void BottomBar::setOptionsOpen (bool o) { optionsBtn.setToggleState (o, juce::dontSendNotification); }
void BottomBar::setAnalyzerState (bool frozen, bool zoomed)
{
    freeze.setToggleState (frozen, juce::dontSendNotification);
    fullRange.setEnabled (zoomed);
}

void BottomBar::timerCallback()
{
    if (! s.showLufs) return;
    lufs.active = engine.isRunning();
    lufs.st = engine.getShortTermLufs(); lufs.in = engine.getIntegratedLufs(); lufs.tp = engine.getTruePeakDb();
    lufsDirty = true;                 // drawn on the next display frame, so it respects the frame-rate cap
}

void BottomBar::onFrame()
{
    if (lufsDirty) { lufsDirty = false; lufs.repaint(); }
}

void BottomBar::paint (juce::Graphics& g)
{
    g.fillAll (kFrame);
    g.setColour (kFrameLine); g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void BottomBar::resized()
{
    const bool spec = s.viewMode == Spectrogram;
    const bool controls = ! s.hidePanel;
    auto r = getLocalBounds().withTrimmedTop (1).reduced (8, 6);

    // left: Options, view switch, LUFS / Key / Tempo toggles
    optionsBtn.setBounds (r.removeFromLeft (ui::px (72))); r.removeFromLeft (ui::px (10));
    viewSpec.setBounds (r.removeFromLeft (ui::px (90))); viewAna.setBounds (r.removeFromLeft (ui::px (78))); r.removeFromLeft (ui::px (10));
    lufsBtn.setBounds (r.removeFromLeft (ui::px (48))); r.removeFromLeft (ui::px (4));
    keyBtn.setBounds (r.removeFromLeft (ui::px (40)));  r.removeFromLeft (ui::px (4));
    tempoBtn.setBounds (r.removeFromLeft (ui::px (56))); r.removeFromLeft (ui::px (12));

    // right corner: small Full screen / Auto-hide / Reset, then the loudness boxes
    auto small = [&] (juce::TextButton& b, int w) { b.setBounds (r.removeFromRight (w).withSizeKeepingCentre (w, ui::px (20))); r.removeFromRight (ui::px (4)); };
    small (full, ui::px (full.getButtonText().startsWith ("Exit") ? 92 : 70));
    resetBtn.setVisible (s.showLufs);
    lufs.setVisible (s.showLufs);
    if (s.showLufs)
    {
        small (resetBtn, ui::px (44));
        r.removeFromRight (ui::px (4));
        // leave the middle controls their minimum width; the loudness boxes shrink first
        // the analyzer's combo boxes get room only if the loudness boxes can stay comfortably wide
        const int compact = spec ? ui::px (66 + 6 + 34 + 4 + 96 + 4 + 30 + 4 + 30) : ui::px (66 + 6 + 28 + 2 + 28 + 4 + 46 + 4 + 62 + 4 + 72);
        const int withCombos = compact + ui::px (64 + 4 + 122 + 4 + 84 + 10);
        auto lufsFor = [&] (int group) { return juce::jmin (ui::px (450), juce::jmin (r.getWidth() / 2, r.getWidth() - group - ui::px (16))); };
        const int minGroup = (! spec && lufsFor (withCombos) >= ui::px (330)) ? withCombos : compact;
        const int lufsW = juce::jmax (ui::px (240), lufsFor (minGroup));
        lufs.setBounds (r.removeFromRight (lufsW).expanded (0, 2));
        r.removeFromRight (ui::px (12));
    }

    // centre: controls for the current view (dropping the least important ones if space is short)
    for (auto* c : std::initializer_list<juce::Component*> { &specPresets, &pause, &ref, &refDown, &refUp, &specSettings })
        c->setVisible (spec && controls);
    for (auto* c : std::initializer_list<juce::Component*> { &anaPresets, &bpoBox, &chanBox, &gridBox, &zoomOut, &zoomIn, &fullRange, &freeze, &anaSettings })
        c->setVisible (! spec && controls);
    if (! controls) return;

    if (spec)
    {
        const bool roomForSettings = r.getWidth() >= ui::px (66 + 6 + 34 + 4 + 96 + 4 + 30 + 4 + 30 + 10 + 72);
        specSettings.setVisible (roomForSettings);
        const int w = juce::jmin (r.getWidth(), ui::px (66 + 6 + 34 + 4 + 96 + 4 + 30 + 4 + 30 + (roomForSettings ? 10 + 72 : 0)));
        auto g = juce::Rectangle<int> (juce::jlimit (r.getX(), juce::jmax (r.getX(), r.getRight() - w), getWidth() / 2 - w / 2), r.getY(), w, r.getHeight());
        specPresets.setBounds (g.removeFromLeft (ui::px (66))); g.removeFromLeft (ui::px (6));
        pause.setBounds (g.removeFromLeft (ui::px (34))); g.removeFromLeft (ui::px (4));
        ref.setBounds (g.removeFromLeft (ui::px (96)));   g.removeFromLeft (ui::px (4));
        refUp.setBounds (g.removeFromLeft (ui::px (30))); g.removeFromLeft (ui::px (4));
        refDown.setBounds (g.removeFromLeft (ui::px (30))); g.removeFromLeft (ui::px (10));
        specSettings.setBounds (g.removeFromLeft (ui::px (72)));
        return;
    }

    const bool roomForCombos = r.getWidth() >= ui::px (66 + 6 + 64 + 4 + 122 + 4 + 84 + 10 + 28 + 2 + 28 + 4 + 46 + 4 + 62 + 4 + 72);
    for (auto* c : { &bpoBox, &chanBox, &gridBox }) c->setVisible (roomForCombos);
    const int wButtons = ui::px (66 + 6 + 28 + 2 + 28 + 4 + 46 + 4 + 62 + 4 + 72);
    const int w = wButtons + (roomForCombos ? ui::px (64 + 4 + 122 + 4 + 84 + 10) : 0);
    auto g = juce::Rectangle<int> (juce::jlimit (r.getX(), juce::jmax (r.getX(), r.getRight() - w), getWidth() / 2 - w / 2), r.getY(), w, r.getHeight());
    anaPresets.setBounds (g.removeFromLeft (ui::px (66))); g.removeFromLeft (ui::px (6));
    if (roomForCombos)
    {
        bpoBox.setBounds (g.removeFromLeft (ui::px (64)));  g.removeFromLeft (ui::px (4));
        chanBox.setBounds (g.removeFromLeft (ui::px (122))); g.removeFromLeft (ui::px (4));
        gridBox.setBounds (g.removeFromLeft (ui::px (84)));  g.removeFromLeft (ui::px (10));
    }
    zoomOut.setBounds (g.removeFromLeft (ui::px (28)));   g.removeFromLeft (ui::px (2));
    zoomIn.setBounds (g.removeFromLeft (ui::px (28)));    g.removeFromLeft (ui::px (4));
    fullRange.setBounds (g.removeFromLeft (ui::px (46))); g.removeFromLeft (ui::px (4));
    freeze.setBounds (g.removeFromLeft (ui::px (62)));    g.removeFromLeft (ui::px (4));
    anaSettings.setBounds (g.removeFromLeft (ui::px (72)));
}

void BottomBar::LufsReadout::paint (juce::Graphics& g)
{
    // Each box: caption on top, a large bold value coloured by level, a unit, and a tinted gradient fill.
    auto loudColour = [] (double v)
    {
        if (v < -14.0) return juce::Colours::white;
        if (v < -9.0)  return juce::Colour (0xff3ddc84);   // green
        if (v < -6.0)  return juce::Colour (0xffffd23f);   // yellow
        return juce::Colour (0xffff5252);                  // red
    };
    auto peakColour = [] (double v)
    {
        if (v < -1.0) return juce::Colours::white;
        if (v < 0.0)  return juce::Colour (0xffffd23f);
        return juce::Colour (0xffff5252);
    };
    struct Item { const char* caption; double v; const char* unit; bool peak; };
    const Item items[] = { { "SHORT-TERM", st, "LUFS", false }, { "INTEGRATED", in, "LUFS", false }, { "TRUE PEAK", tp, "dBTP", true } };
    const int w = getWidth() / 3, h = getHeight();
    const auto mono = juce::Font::getDefaultMonospacedFontName();
    for (int i = 0; i < 3; ++i)
    {
        const bool has = active && std::isfinite (items[i].v);
        const auto c = has ? (items[i].peak ? peakColour (items[i].v) : loudColour (items[i].v)) : kLabel.withAlpha (0.35f);
        auto box = juce::Rectangle<int> (i * w, 0, w, h).reduced (3, 0).toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff1b1c1f), box.getX(), box.getY(),
                                                 has ? juce::Colour (0xff1b1c1f).interpolatedWith (c, 0.30f) : juce::Colour (0xff222327),
                                                 box.getRight(), box.getBottom(), false));
        g.fillRoundedRectangle (box, 5.0f);
        g.setColour (has ? c.withAlpha (0.55f) : juce::Colour (0xff3a3c40));
        g.drawRoundedRectangle (box.reduced (0.5f), 5.0f, 1.0f);

        auto r = box.toNearestInt().reduced (9, 2);
        g.setColour (kLabel.withAlpha (0.75f)); g.setFont (juce::FontOptions (ui::fs (10.0f), juce::Font::bold));
        g.drawText (items[i].caption, r.removeFromTop (juce::roundToInt (ui::fs (11.0f))), juce::Justification::centredLeft);
        // the number always fits: drop the unit, then shrink the number, when a box gets narrow
        const float valueSize = ui::fs (21.0f);
        const int unitW = juce::roundToInt (ui::fs (34.0f));
        const bool showUnit = r.getWidth() - unitW >= juce::roundToInt (valueSize * 3.2f);
        if (showUnit)
        {
            g.setColour (kLabel.withAlpha (0.7f)); g.setFont (juce::FontOptions (ui::fs (11.5f), juce::Font::bold));
            g.drawText (items[i].unit, r.removeFromRight (unitW), juce::Justification::bottomRight);
        }
        g.setColour (c);
        g.setFont (juce::FontOptions (mono, juce::jmin (valueSize, (float) r.getWidth() / 3.3f), juce::Font::bold));
        g.drawText (has ? juce::String (items[i].v, 1) : juce::String ("--.-"), r.withTrimmedRight (showUnit ? 5 : 0).translated (0, 1),
                    juce::Justification::centredRight);
    }
}
