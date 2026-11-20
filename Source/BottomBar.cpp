#include "BottomBar.h"

namespace
{
    const juce::Colour kFrame (0xff2a2b2e), kFrameLine (0xff3a3c40), kLabel (0xffc8cacd);
    const juce::Colour kButton (0xff3a3c41), kOn (0xff4c8dff);

    // Smaller text for the right-hand corner buttons
    struct SmallButtonLnf : public juce::LookAndFeel_V4
    {
        SmallButtonLnf() { setColourScheme (getDarkColourScheme()); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return juce::FontOptions (11.5f); }
    };
    SmallButtonLnf& smallLnf() { static SmallButtonLnf l; return l; }
}

BottomBar::BottomBar (Settings& st, AudioEngine& e, SpectrumAnalyzerProcessor& proc) : s (st), engine (e)
{
    for (auto* b : { &viewSpec, &viewAna, &lufsBtn, &keyBtn, &tempoBtn, &pause, &refDown, &refUp,
                     &zoomOut, &zoomIn, &fullRange, &freeze, &anaSettings, &resetBtn, &autoHide, &full })
    {
        addAndMakeVisible (b);
        b->setColour (juce::TextButton::buttonColourId, kButton);
        b->setColour (juce::TextButton::buttonOnColourId, kOn);
    }

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

    pause.setTooltip ("Pause (Space)");
    refDown.setTooltip ("Reference level -1 dB (mouse wheel / Down)");
    refUp.setTooltip ("Reference level +1 dB (mouse wheel / Up)");
    pause.onClick   = [this] { if (onPause) onPause(); };
    refDown.onClick = [this] { if (onRef) onRef (-1); };
    refUp.onClick   = [this] { if (onRef) onRef (+1); };
    ref.setJustificationType (juce::Justification::centred);
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
    for (auto* b : { &resetBtn, &autoHide, &full }) b->setLookAndFeel (&smallLnf());
    resetBtn.setTooltip ("Restart integrated loudness and true-peak hold");
    autoHide.setTooltip ("Hide the top toolbar in full screen; touch the top edge to show it");
    full.setTooltip ("Full screen (F11, Esc to leave)");
    resetBtn.onClick = [this] { engine.resetLoudness(); };
    autoHide.onClick = [this] { if (onToggleAutoHide) onToggleAutoHide(); };
    full.onClick     = [this] { if (onToggleFullscreen) onToggleFullscreen(); };

    for (auto* b : { &viewSpec, &viewAna, &lufsBtn, &keyBtn, &autoHide })
        b->setClickingTogglesState (false);   // state comes from settings via refresh()

    refresh();
    startTimerHz (10);
}

BottomBar::~BottomBar()
{
    for (auto* b : { &resetBtn, &autoHide, &full }) b->setLookAndFeel (nullptr);
}

void BottomBar::refresh()
{
    viewSpec.setToggleState (s.viewMode == Spectrogram, juce::dontSendNotification);
    viewAna.setToggleState  (s.viewMode == Analyzer, juce::dontSendNotification);
    lufsBtn.setToggleState  (s.showLufs, juce::dontSendNotification);
    autoHide.setToggleState (s.autoHideBar, juce::dontSendNotification);
    ref.setText ("Ref: " + juce::String (s.refLevel, 0) + " dB", juce::dontSendNotification);
    resized();
    repaint();
}

void BottomBar::setPaused (bool p)     { pause.setButtonText (p ? ">" : "||"); }
void BottomBar::setFullscreen (bool f) { full.setButtonText (f ? "Exit full screen" : "Full screen"); }
void BottomBar::setKeyOpen (bool o)    { keyBtn.setToggleState (o, juce::dontSendNotification); }
void BottomBar::setTempoOpen (bool o)  { tempoBtn.setToggleState (o, juce::dontSendNotification); }
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
    lufs.repaint();
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

    // left: view switch, LUFS and Key toggles
    viewSpec.setBounds (r.removeFromLeft (90)); viewAna.setBounds (r.removeFromLeft (78)); r.removeFromLeft (10);
    lufsBtn.setBounds (r.removeFromLeft (48)); r.removeFromLeft (4);
    keyBtn.setBounds (r.removeFromLeft (40));  r.removeFromLeft (4);
    tempoBtn.setBounds (r.removeFromLeft (56)); r.removeFromLeft (12);

    // right corner: small Full screen / Auto-hide / Reset, then the loudness boxes
    auto small = [&] (juce::TextButton& b, int w) { b.setBounds (r.removeFromRight (w).withSizeKeepingCentre (w, 20)); r.removeFromRight (4); };
    small (full, full.getButtonText().startsWith ("Exit") ? 92 : 70);
    small (autoHide, 62);
    resetBtn.setVisible (s.showLufs);
    lufs.setVisible (s.showLufs);
    if (s.showLufs)
    {
        small (resetBtn, 44);
        r.removeFromRight (4);
        lufs.setBounds (r.removeFromRight (juce::jmin (450, juce::jmax (330, r.getWidth() / 2))).expanded (0, 2));
        r.removeFromRight (12);
    }

    // centre: controls for the current view (dropping the least important ones if space is short)
    for (auto* c : std::initializer_list<juce::Component*> { &pause, &ref, &refDown, &refUp })
        c->setVisible (spec && controls);
    for (auto* c : std::initializer_list<juce::Component*> { &bpoBox, &chanBox, &gridBox, &zoomOut, &zoomIn, &fullRange, &freeze, &anaSettings })
        c->setVisible (! spec && controls);
    if (! controls) return;

    if (spec)
    {
        const int w = 34 + 4 + 96 + 4 + 30 + 4 + 30;
        auto g = juce::Rectangle<int> (juce::jlimit (r.getX(), juce::jmax (r.getX(), r.getRight() - w), getWidth() / 2 - w / 2), r.getY(), w, r.getHeight());
        pause.setBounds (g.removeFromLeft (34)); g.removeFromLeft (4);
        ref.setBounds (g.removeFromLeft (96));   g.removeFromLeft (4);
        refUp.setBounds (g.removeFromLeft (30)); g.removeFromLeft (4);
        refDown.setBounds (g.removeFromLeft (30));
        return;
    }

    const bool roomForCombos = r.getWidth() >= 560;
    for (auto* c : { &bpoBox, &chanBox, &gridBox }) c->setVisible (roomForCombos);
    const int wButtons = 28 + 2 + 28 + 4 + 46 + 4 + 62 + 4 + 72;
    const int w = wButtons + (roomForCombos ? 64 + 4 + 122 + 4 + 84 + 10 : 0);
    auto g = juce::Rectangle<int> (juce::jlimit (r.getX(), juce::jmax (r.getX(), r.getRight() - w), getWidth() / 2 - w / 2), r.getY(), w, r.getHeight());
    if (roomForCombos)
    {
        bpoBox.setBounds (g.removeFromLeft (64));  g.removeFromLeft (4);
        chanBox.setBounds (g.removeFromLeft (122)); g.removeFromLeft (4);
        gridBox.setBounds (g.removeFromLeft (84));  g.removeFromLeft (10);
    }
    zoomOut.setBounds (g.removeFromLeft (28));   g.removeFromLeft (2);
    zoomIn.setBounds (g.removeFromLeft (28));    g.removeFromLeft (4);
    fullRange.setBounds (g.removeFromLeft (46)); g.removeFromLeft (4);
    freeze.setBounds (g.removeFromLeft (62));    g.removeFromLeft (4);
    anaSettings.setBounds (g.removeFromLeft (72));
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
        g.setColour (kLabel.withAlpha (0.75f)); g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText (items[i].caption, r.removeFromTop (11), juce::Justification::centredLeft);
        g.setColour (kLabel.withAlpha (0.7f)); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (items[i].unit, r.removeFromRight (32), juce::Justification::bottomRight);
        g.setColour (c);
        g.setFont (juce::FontOptions (mono, 21.0f, juce::Font::bold));
        g.drawText (has ? juce::String (items[i].v, 1) : juce::String ("--.-"), r.withTrimmedRight (5).translated (0, 1),
                    juce::Justification::centredRight);
    }
}
