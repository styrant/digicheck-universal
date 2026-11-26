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
}

BottomBar::BottomBar (Settings& st, AnalysisEngine& e, SpectrumAnalyzerProcessor& proc) : s (st), engine (e)
{
    smallLnf = std::make_unique<SmallButtonLnf>();          // per instance: no static look-and-feel outliving the UI
    for (auto* b : { &optionsBtn, &viewSpec, &viewAna, &viewBal, &viewKey, &viewScope, &viewMon, &viewCustom, &helpBtn, &hideBarBtn, &balCurve, &balDiff, &balReset, &lufsBtn, &tempoBtn, &refBtn, &specPresets, &anaPresets, &pause, &refDown, &refUp, &specSettings,
                     &zoomOut, &zoomIn, &fullRange, &freeze, &anaSettings, &resetBtn, &full, &listenBtn, &scBtn, &linkBtn })
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
    viewAna.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    viewBal.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    viewKey.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    viewScope.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    viewMon.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    viewCustom.setConnectedEdges (juce::Button::ConnectedOnLeft);
    viewCustom.setTooltip ("Custom: several views on one page, side by side (7, V cycles). Choose the layout and the view in each pane in the bar");
    viewCustom.onClick = [this] { if (onViewChange) onViewChange (Custom); };

    // custom page: one button with the layout and the view in each pane (picking a view that is already in
    // another pane swaps them)
    customBtn.setTooltip ("The Custom page's layout and what each pane shows. Drag the gaps between panes to resize them. "
                          "Each view's own settings (targets, presets) are set on its own page and carry over; the Monitor keeps a separate module layout here");
    customBtn.setColour (juce::TextButton::buttonColourId, kButton);
    customBtn.onClick = [this] { showCustomMenu(); };
    addAndMakeVisible (customBtn);
    helpBtn.setTooltip ("Help: while on, resting the mouse on a button or control shows what it does");
    helpBtn.onClick = [this] { if (onToggleHelp) onToggleHelp(); };
    hideBarBtn.setTooltip ("Hide this bar too, for the cleanest full screen (B, or Shift+F11). Move the mouse to the bottom edge to bring it back");
    hideBarBtn.onClick = [this] { if (onToggleHideBar) onToggleHideBar(); };
    viewSpec.setTooltip ("Reassigned spectrogram (1, V cycles)");
    viewAna.setTooltip ("Filter-bank spectrum analyzer (2, V cycles)");
    viewBal.setTooltip ("Tonal balance against a target (3, V cycles)");
    viewKey.setTooltip ("Key finder, chords and melody (4 or K, V cycles)");
    viewScope.setTooltip ("Oscilloscope (5, V cycles)");
    viewMon.setTooltip ("Monitor: loudness, dynamics, levels, sound field, phase (6, V cycles)");
    listenBtn.setTooltip ("Hear only some bands, or Mid / Side / Left / Right (the plugin's output)");
    listenBtn.onClick = [this] { if (onListen) onListen (listenBtn); };
    scBtn.setTooltip ("How the sidechain is drawn on the spectrogram");
    scBtn.onClick = [this] { if (onSidechainMenu) onSidechainMenu (scBtn); };
    linkBtn.setTooltip ("Link: use another UAV instance's track as the comparison signal (Spectrogram overlay, Scope SC, the FFT's second curve), "
                        "like a sidechain but with no routing");
    linkBtn.onClick = [this] { if (onLinkMenu) onLinkMenu (linkBtn); };
    viewSpec.onClick = [this] { if (onViewChange) onViewChange (Spectrogram); };
    viewAna.onClick  = [this] { if (onViewChange) onViewChange (Analyzer); };
    viewBal.onClick  = [this] { if (onViewChange) onViewChange (Balance); };
    viewKey.onClick  = [this] { if (onViewChange) onViewChange (Key); };
    viewScope.onClick = [this] { if (onViewChange) onViewChange (Scope); };
    viewMon.onClick   = [this] { if (onViewChange) onViewChange (Monitor); };

    // balance controls
    balTarget.setTooltip ("Target to compare with (files in the targets folder)");
    balTarget.setTextWhenNoChoicesAvailable ("No targets");
    balTarget.setTextWhenNothingSelected ("Choose a target");
    balTarget.onChange = [this]
    {
        const int id = balTarget.getSelectedId();
        if (id > 1000)                                    // an action at the end of the list
        {
            balTarget.setSelectedId (balSelected, juce::dontSendNotification);
            if (onBalanceTargetsAction) onBalanceTargetsAction (id - 1000);
            return;
        }
        balSelected = id;
        if (onBalanceTarget) onBalanceTarget (id - 1);
    };
    balWindow.addItemList (LiveBalance::windowNames(), 1);

    // spectrogram scale and channel (also in Settings)
    specScale.addItemList (Settings::freqScaleNames(), 1);
    specScale.setTooltip ("Frequency scale: Log (octaves equal), Linear (Hz equal), Ext Log (log down to 0 Hz), "
                          "Mel / Bark / ERB (scales of hearing)");
    specScale.onChange = [this] { s.freqScale = specScale.getSelectedId() - 1; if (onSpectrogramDsp) onSpectrogramDsp(); };
    specChannel.addItemList ({ "Left", "Right", "Mid", "Side" }, 1);
    specChannel.setTooltip ("What the spectrogram shows: Left, Right, Mid (what both sides share) or Side (the stereo difference)");
    specChannel.onChange = [this] { s.channel = specChannel.getSelectedId() - 1; if (onSpectrogramDsp) onSpectrogramDsp(); };
    for (auto* c : { &specScale, &specChannel }) addAndMakeVisible (c);
    balWindow.setTooltip ("How much recent audio the measurement averages. Realtime follows the music as it plays (about 0.3 s)");
    balWindow.onChange = [this] { s.balanceWindow = balWindow.getSelectedId() - 1; if (onBalanceWindow) onBalanceWindow (s.balanceWindow); };
    balSmooth.addItemList ({ "Smooth", "Medium", "Detailed", "Fine" }, 1);
    balSmooth.setTooltip ("Curve smoothing: Smooth (1 octave, like Tonal Balance Control), Medium (1/2 octave), "
                          "Detailed (1/3 octave), Fine (1/6 octave). The target's range is smoothed to match");
    balSmooth.onChange = [this] { s.balanceSmooth = balSmooth.getSelectedId() - 1; if (onBalanceSmooth) onBalanceSmooth (s.balanceSmooth); };
    for (auto* c : { &balTarget, &balWindow, &balSmooth }) addAndMakeVisible (c);
    balCurve.setConnectedEdges (juce::Button::ConnectedOnRight);
    balDiff.setConnectedEdges (juce::Button::ConnectedOnLeft);
    balCurve.setTooltip ("Your audio over the target's range");
    balDiff.setTooltip ("Difference: your audio minus the target (0 dB = on target)");
    balCurve.onClick = [this] { if (onBalanceMode) onBalanceMode (false); };
    balDiff.onClick  = [this] { if (onBalanceMode) onBalanceMode (true); };
    balReset.setTooltip ("Start the measurement again");
    balReset.onClick = [this] { if (onBalanceReset) onBalanceReset(); };


    lufsBtn.setTooltip ("Show or hide the loudness meter");
    lufsBtn.onClick = [this] { if (onToggleLufs) onToggleLufs(); };
    tempoBtn.setTooltip ("BPM to milliseconds calculator (T)");
    tempoBtn.onClick = [this] { if (onToggleTempo) onToggleTempo(); };
    refBtn.setTooltip ("Reference track: load a song to compare with your mix (loudness, Balance and FFT curves, A/B listening)");
    refBtn.setClickingTogglesState (false);
    refBtn.onClick = [this] { if (onToggleReference) onToggleReference(); };

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
    // the corner buttons use the bar's normal size and font, like every other button
    resetBtn.setTooltip ("Restart integrated loudness and true-peak hold");
    full.setTooltip ("Full screen (F11, Esc to leave)");
    resetBtn.onClick = [this] { engine.resetLoudness(); if (onLoudnessReset) onLoudnessReset(); };
    full.onClick     = [this] { if (onToggleFullscreen) onToggleFullscreen(); };

    for (auto* b : { &viewSpec, &viewAna, &viewBal, &viewKey, &viewScope, &viewMon, &viewCustom, &balCurve, &balDiff, &lufsBtn, &helpBtn })
        b->setClickingTogglesState (false);   // state comes from settings via refresh()

    refresh();
    startTimerHz (10);
}

BottomBar::~BottomBar()
{
    for (auto* b : { &resetBtn, &full, &helpBtn, &hideBarBtn, &updateBtn }) b->setLookAndFeel (nullptr);
}

void BottomBar::refresh()
{
    ref.setFont (juce::FontOptions (ui::fs (15.0f)));
    viewSpec.setToggleState (s.viewMode == Spectrogram, juce::dontSendNotification);
    viewAna.setToggleState  (s.viewMode == Analyzer, juce::dontSendNotification);
    viewBal.setToggleState  (s.viewMode == Balance, juce::dontSendNotification);
    viewKey.setToggleState  (s.viewMode == Key, juce::dontSendNotification);
    viewScope.setToggleState (s.viewMode == Scope, juce::dontSendNotification);
    viewMon.setToggleState (s.viewMode == Monitor, juce::dontSendNotification);
    viewCustom.setToggleState (s.viewMode == Custom, juce::dontSendNotification);
    helpBtn.setToggleState (s.helpMode, juce::dontSendNotification);
    customLongText = ((s.customName.isNotEmpty() ? s.customName + ": " : juce::String ("Layout: ")) + juce::StringArray ({ "2 side by side", "2 stacked", "Large left", "Large top", "3 columns", "2 x 2" })[juce::jlimit (0, 5, s.customLayout)]
                             + juce::String::fromUTF8 (" \xe2\x96\xbe"));

    balCurve.setToggleState (s.balanceMode == 0, juce::dontSendNotification);
    balDiff.setToggleState  (s.balanceMode == 1, juce::dontSendNotification);
    balWindow.setSelectedId (juce::jlimit (0, 5, s.balanceWindow) + 1, juce::dontSendNotification);
    balSmooth.setSelectedId (juce::jlimit (0, 3, s.balanceSmooth) + 1, juce::dontSendNotification);
    specScale.setSelectedId (juce::jlimit (0, 5, s.freqScale) + 1, juce::dontSendNotification);
    specChannel.setSelectedId (juce::jlimit (0, 3, s.channel) + 1, juce::dontSendNotification);
    lufsBtn.setToggleState  (s.showLufs, juce::dontSendNotification);
    ref.setText ("Ref: " + juce::String (s.refLevel, 0) + " dB", juce::dontSendNotification);
    resized();
    repaint();
}

void BottomBar::setBalanceTargets (const juce::StringArray& names, int selected)
{
    balTarget.clear (juce::dontSendNotification);
    balTarget.addItemList (names, 1);
    if (! names.isEmpty()) balTarget.addSeparator();
    balTarget.addItem ("Open targets folder...", 1001);
    balTarget.addItem ("Rescan targets", 1002);
    balTarget.addItem ("Open Target Builder...", 1003);
    balSelected = (selected >= 0 && selected < names.size()) ? selected + 1 : 0;
    balTarget.setSelectedId (balSelected, juce::dontSendNotification);
}

void BottomBar::showCustomMenu()
{
    static const char* layouts[6] = { "Two side by side", "Two stacked", "Large left, two right", "Large top, two below", "Three columns", "Grid 2 x 2" };
    juce::PopupMenu menu, lay;
    for (int i = 0; i < 6; ++i)
        lay.addItem (layouts[i], true, s.customLayout == i, [this, i]
        {
            static const double a[6] = { 0.5, 0.5, 0.62, 0.62, 1.0 / 3.0, 0.5 };
            s.customLayout = i; s.customSplitA = a[i]; s.customSplitB = 0.5;
            refresh();
            if (onCustomChanged) onCustomChanged();
        });
    menu.addSubMenu ("Layout", lay);
    menu.addSeparator();
    const auto slots = customSlots (s);
    const auto names = viewNames();
    static const char* where[6][4] = { { "left", "right" }, { "top", "bottom" }, { "large, left", "top right", "bottom right" },
                                       { "large, top", "bottom left", "bottom right" }, { "left", "middle", "right" },
                                       { "top left", "top right", "bottom left", "bottom right" } };
    for (int i = 0; i < panesIn (s.customLayout); ++i)
    {
        juce::PopupMenu sub;
        auto pick = [this, i] (int v)
        {
            auto sl = customSlots (s);
            for (int j = 0; j < 4; ++j) if (j != i && sl[(size_t) j] == v && v >= 0) sl[(size_t) j] = sl[(size_t) i];   // swap
            sl[(size_t) i] = v;
            juce::StringArray a; for (int x : sl) a.add (juce::String (x));
            s.customSlots = a.joinIntoString (",");
            refresh();
            if (onCustomChanged) onCustomChanged();
        };
        for (int v = 0; v < names.size(); ++v) sub.addItem (names[v], true, slots[(size_t) i] == v, [pick, v] { pick (v); });
        sub.addSeparator();
        sub.addItem ("Empty", true, slots[(size_t) i] < 0, [pick] { pick (-1); });
        const auto cur = slots[(size_t) i] >= 0 ? names[slots[(size_t) i]] : juce::String ("empty");
        menu.addSubMenu ("Pane " + juce::String (i + 1) + " (" + where[juce::jlimit (0, 5, s.customLayout)][i] + "):  " + cur, sub);
    }
    menu.addSeparator();
    // saved layouts (app-wide)
    juce::PopupMenu saved;
    for (auto& n : savedLayouts) saved.addItem (n, true, n == s.customName, [this, n] { if (onLoadLayout) onLoadLayout (n); });
    if (savedLayouts.isEmpty()) saved.addItem ("(none yet)", false, false, [] {});
    menu.addSubMenu ("Saved layouts", saved);
    menu.addItem ("Save layout as...", [this] { if (onSaveLayout) onSaveLayout(); });
    if (s.customName.isNotEmpty())
    {
        menu.addItem ("Update \"" + s.customName + "\"", [this] { if (onUpdateLayout) onUpdateLayout(); });
        menu.addItem ("Delete \"" + s.customName + "\"", [this] { if (onDeleteLayout) onDeleteLayout(); });
    }
    menu.addSeparator();
    menu.addItem ("Equal sizes", [this]
    {
        static const double a[6] = { 0.5, 0.5, 0.62, 0.62, 1.0 / 3.0, 0.5 };
        s.customSplitA = a[juce::jlimit (0, 5, s.customLayout)]; s.customSplitB = 0.5;
        if (onCustomChanged) onCustomChanged();
    });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (customBtn));
}

std::array<int, 4> BottomBar::customSlots (const Settings& st)
{
    std::array<int, 4> r { 0, 5, 4, 1 };
    auto t = juce::StringArray::fromTokens (st.customSlots, ",", {});
    for (int i = 0; i < 4 && i < t.size(); ++i) r[(size_t) i] = juce::jlimit (-1, 5, t[i].trim().getIntValue());
    for (int i = 0; i < 4; ++i)                              // a view shows in one pane only
        for (int j = 0; j < i; ++j) if (r[(size_t) i] >= 0 && r[(size_t) i] == r[(size_t) j]) r[(size_t) i] = -1;
    return r;
}

void BottomBar::setBarHidden (bool h) { hideBarBtn.setButtonText (h ? "Show bar" : "Hide bar"); }
void BottomBar::setReferenceState (bool open, bool loaded, bool listening)
{
    refBtn.setToggleState (open, juce::dontSendNotification);
    refBtn.setColour (juce::TextButton::buttonColourId, listening ? juce::Colour (0xffff9f43) : (loaded ? juce::Colour (0xff6b4a22) : kButton));
    refBtn.setColour (juce::TextButton::textColourOffId, listening ? juce::Colours::black : juce::Colours::white);
}

void BottomBar::setUpdateAvailable (const juce::String& v)
{
    updateBtn.setButtonText (v + " available");
    updateBtn.setTooltip ("A newer build (" + v + ") is on GitHub: open the download page");
    updateBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2f6b3f));
    updateBtn.onClick = [] { juce::URL ("https://github.com/styrant/digicheck-universal/releases/tag/latest").launchInDefaultBrowser(); };
    addAndMakeVisible (updateBtn);
    resized();
}

void BottomBar::setHelp (bool on)     { helpBtn.setToggleState (on, juce::dontSendNotification); }

void BottomBar::setPaused (bool p)     { pause.setButtonText (p ? ">" : "||"); }
void BottomBar::setFullscreen (bool f) { full.setButtonText (f ? "Exit full screen" : "Full screen"); resized(); }   // the corner changes width
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
    lufs.active = engine.isAnalysing();
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

// The view whose controls the bar shows: the current view, or on the Custom page the pane last clicked
int BottomBar::cv() const
{
    if (s.viewMode != Custom) return s.viewMode;
    const auto slots = customSlots (s);
    for (int i = 0; i < panesIn (s.customLayout); ++i) if (slots[(size_t) i] == s.customFocus && s.customFocus >= 0) return s.customFocus;
    return -1;
}

void BottomBar::resized()
{
    const int view = cv();
    const bool cust = s.viewMode == Custom;
    const bool spec = view == Spectrogram, bal = view == Balance;
    const bool controls = ! s.hidePanel;
    auto r = getLocalBounds().withTrimmedTop (1).reduced (8, 6);

    // narrow windows (the plugin's smallest is 900 px): short view names, a short Full button, then no Tempo
    // button (T still opens it); what the fixed parts need at full size:
    // (full screen: "Exit full screen" is wider and the Hide bar button joins the corner)
    const bool fullNow = ! plugin && full.getButtonText().startsWith ("Exit");
    const int need = 740 + (plugin ? 112 : 96) + 268 + 16 + (fullNow ? 32 + 82 : 0);
    const bool compact = getWidth() < ui::px (need);
    const bool noTempo = getWidth() < ui::px (need - 138 - (plugin ? 0 : 44) - 40);
    const char* longNames[]  = { "Spectrogram", "Analyzer", "Balance", "Key", "Scope", "Monitor", "Custom" };
    const char* shortNames[] = { "Spec", "Ana", "Bal", "Key", "Scope", "Mon", "Custom" };
    const int longW[] = { 94, 74, 70, 46, 58, 70, 68 }, shortW[] = { 54, 46, 44, 40, 52, 46, 60 };
    juce::TextButton* tabs[] = { &viewSpec, &viewAna, &viewBal, &viewKey, &viewScope, &viewMon, &viewCustom };

    // left: Options, view switch, LUFS / Key / Tempo toggles
    optionsBtn.setBounds (r.removeFromLeft (ui::px (72))); r.removeFromLeft (ui::px (10));
    for (int i = 0; i < 7; ++i)
    {
        tabs[i]->setButtonText (compact ? shortNames[i] : longNames[i]);
        tabs[i]->setBounds (r.removeFromLeft (ui::px (compact ? shortW[i] : longW[i])));
    }
    r.removeFromLeft (ui::px (10));
    lufsBtn.setBounds (r.removeFromLeft (ui::px (48))); r.removeFromLeft (ui::px (4));
    tempoBtn.setVisible (! noTempo);
    if (! noTempo) { tempoBtn.setBounds (r.removeFromLeft (ui::px (56))); r.removeFromLeft (ui::px (4)); }
    refBtn.setBounds (r.removeFromLeft (ui::px (44))); r.removeFromLeft (ui::px (12));
    listenBtn.setVisible (plugin);
    linkBtn.setVisible (plugin);
    scBtn.setVisible (plugin && spec);
    full.setVisible (! plugin);
    if (plugin)
    {
        r.removeFromLeft (ui::px (-6));
        listenBtn.setBounds (r.removeFromLeft (ui::px (listenBtn.getButtonText().length() > 8 ? 190 : 60))); r.removeFromLeft (ui::px (6));
        linkBtn.setBounds (r.removeFromLeft (ui::px (linkBtn.getButtonText().length() > 4 ? 120 : 46))); r.removeFromLeft (ui::px (6));
        if (spec) { scBtn.setBounds (r.removeFromLeft (ui::px (36))); r.removeFromLeft (ui::px (12)); }
    }

    // right corner: small Full screen / Auto-hide / Reset, then the loudness boxes
    auto small = [&] (juce::TextButton& b, int w) { b.setBounds (r.removeFromRight (w)); r.removeFromRight (ui::px (4)); };
    const bool isFull = full.getButtonText().startsWith ("Exit");
    if (compact && ! plugin) full.setButtonText (isFull ? "Exit" : "Full");
    else if (! plugin)       full.setButtonText (isFull ? "Exit full screen" : "Full screen");
    if (! plugin) small (full, ui::px (compact ? 48 : (isFull ? 124 : 92)));
    small (helpBtn, ui::px (30));
    hideBarBtn.setVisible (! plugin && isFull);
    if (! plugin && isFull) small (hideBarBtn, ui::px (78));
    // the corner buttons stay on top and clickable even if a crowded page's controls run into them
    for (auto* b : { &full, &helpBtn, &hideBarBtn }) b->toFront (false);
    if (updateBtn.isVisible()) small (updateBtn, ui::px (124));
    resetBtn.setVisible (s.showLufs);
    lufs.setVisible (s.showLufs);
    if (s.showLufs)
    {
        small (resetBtn, ui::px (54));
        r.removeFromRight (ui::px (4));
        // leave the middle controls their minimum width; the loudness boxes shrink first
        // the analyzer's combo boxes get room only if the loudness boxes can stay comfortably wide
        const int compactW = (cust ? ui::px (160) : 0) + (spec ? ui::px (66 + 6 + 34 + 8 + 88 + 4 + 62)
                            : bal ? ui::px (150 + 4 + 96 + 6 + 96 + 6 + 52 + 44 + 6 + 48)
                          : (view == Key || view == Scope || view == Monitor || view < 0) ? 0
                          : 0);            // the analyzer's controls are in its own header
        auto lufsFor = [&] (int group) { return juce::jmin (ui::px (450), juce::jmin (r.getWidth() / 2, r.getWidth() - group - ui::px (16))); };
        // the Custom page's Layout button always keeps room (at least its icon form)
        const int lufsW = juce::jmin (juce::jmax (ui::px (compact ? 120 : 160), lufsFor (compactW)), r.getWidth() - (cust ? ui::px (50) : 0));
        lufs.setVisible (lufsW >= ui::px (90));
        if (lufs.isVisible()) lufs.setBounds (r.removeFromRight (lufsW).expanded (0, 2));
        r.removeFromRight (ui::px (12));
    }

    // centre: controls for the current view (dropping the least important ones if space is short)
    for (auto* c : std::initializer_list<juce::Component*> { &specPresets, &pause, &ref, &refDown, &refUp, &specScale, &specChannel, &specSettings })
        c->setVisible (spec && controls);
    for (auto* c : std::initializer_list<juce::Component*> { &balTarget, &balWindow, &balSmooth, &balCurve, &balDiff, &balReset })
        c->setVisible (bal && controls);
    customBtn.setVisible (cust);
    if (cust)
    {
        // Custom: the Layout button, then the controls of the pane last clicked
        // a short "Layout" button when the pane's controls need the room
        const bool shortBtn = r.getWidth() < ui::px (158) || (view >= 0 && view != Key && view != Scope && view != Monitor && r.getWidth() < ui::px (158 + 10 + 300));
        customBtn.setButtonText (shortBtn ? "Layout" + juce::String::fromUTF8 (" \xe2\x96\xbe") : customLongText);
        // very narrow: an icon-only Layout button (its tooltip says what it is)
        const bool iconBtn = r.getWidth() < ui::px (80);
        if (iconBtn) customBtn.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xa6 \xe2\x96\xbe"));
        customBtn.setBounds (r.removeFromLeft (juce::jmin (r.getWidth(), ui::px (iconBtn ? 42 : shortBtn ? 80 : 158)))); r.removeFromLeft (juce::jmin (r.getWidth(), ui::px (8)));
    }
    if (! controls) return;

    if (bal)
    {
        // Curve|Diff and Reset always; then the window and smoothing boxes if they fit beside a target box of at
        // least 110 px (the smoothing box goes first when space is short); the target box takes what's left (to 220 px)
        const int base = ui::px (4 + 52 + 44 + 6 + 48), box = ui::px (96 + 6), minT = ui::px (150);   // the target name stays readable; Window / Smoothing go first
        if (r.getWidth() < base + ui::px (60))
        {
            // too narrow for the group: Curve | Diff alone if they fit, else nothing (the window is tiny)
            for (auto* c : std::initializer_list<juce::Component*> { &balTarget, &balWindow, &balSmooth, &balReset }) c->setVisible (false);
            const bool fits = r.getWidth() >= ui::px (96);
            balCurve.setVisible (fits); balDiff.setVisible (fits);
            if (fits) { auto g = r.withWidth (ui::px (96)); balCurve.setBounds (g.removeFromLeft (ui::px (52))); balDiff.setBounds (g); }
            return;
        }
        const bool roomForWindow = r.getWidth() >= minT + base + box;
        const bool roomForSmooth = r.getWidth() >= minT + base + 2 * box;
        const int rest = base + (roomForWindow ? box : 0) + (roomForSmooth ? box : 0);
        const int tw = juce::jlimit (juce::jmin (minT, r.getWidth() - base), ui::px (220), r.getWidth() - rest);
        const int w = juce::jmin (r.getWidth(), tw + rest);
        auto g = juce::Rectangle<int> (juce::jlimit (r.getX(), juce::jmax (r.getX(), r.getRight() - w), getWidth() / 2 - w / 2), r.getY(), w, r.getHeight());
        balTarget.setBounds (g.removeFromLeft (tw)); g.removeFromLeft (ui::px (4));
        balTarget.setTooltip ("Target: " + balTarget.getText() + " (files in the targets folder)");
        balWindow.setVisible (roomForWindow);
        balSmooth.setVisible (roomForSmooth);
        if (roomForWindow) { balWindow.setBounds (g.removeFromLeft (ui::px (96))); g.removeFromLeft (ui::px (6)); }
        if (roomForSmooth) { balSmooth.setBounds (g.removeFromLeft (ui::px (96))); g.removeFromLeft (ui::px (6)); }
        balCurve.setBounds (g.removeFromLeft (ui::px (52)));
        balDiff.setBounds (g.removeFromLeft (ui::px (44))); g.removeFromLeft (ui::px (6));
        balReset.setBounds (g.removeFromLeft (ui::px (48)));
        return;
    }

    if (spec)
    {
        // Order: Presets | Pause | Scale | Channel | Ref - + | Settings. When space is short, Settings goes first,
        // then the Ref -/+ buttons (the mouse wheel and Up/Down still change it), then the Ref readout.
        const int avail = r.getWidth();
        if (avail < ui::px (66 + 6 + 34))                  // no room even for Presets and Pause: the keys still work (P, Space)
        {
            for (auto* c : std::initializer_list<juce::Component*> { &specPresets, &pause, &ref, &refDown, &refUp, &specScale, &specChannel, &specSettings }) c->setVisible (false);
            return;
        }
        // very narrow: the channel box goes, then the scale box (both are in Settings too)
        const bool roomChan = avail >= ui::px (66 + 6 + 34 + 8 + 88 + 4 + 62), roomScale = avail >= ui::px (66 + 6 + 34 + 8 + 88);
        specChannel.setVisible (roomChan); specScale.setVisible (roomScale);
        const int core = 66 + 6 + 34 + (roomScale ? 8 + 88 : 0) + (roomChan ? 4 + 62 : 0);
        const bool roomRef = avail >= ui::px (core + 6 + 62);
        const bool roomRefButtons = avail >= ui::px (core + 8 + 92 + 4 + 30 + 4 + 30);
        const bool roomSettings = avail >= ui::px (core + 8 + 92 + 4 + 30 + 4 + 30 + 10 + 72);
        ref.setVisible (roomRef); refUp.setVisible (roomRefButtons); refDown.setVisible (roomRefButtons); specSettings.setVisible (roomSettings);
        const int w = juce::jmin (avail, ui::px (core + (roomRef ? (roomRefButtons ? 8 + 92 : 6 + 62) : 0) + (roomRefButtons ? 4 + 30 + 4 + 30 : 0) + (roomSettings ? 10 + 72 : 0)));
        auto g = juce::Rectangle<int> (juce::jlimit (r.getX(), juce::jmax (r.getX(), r.getRight() - w), getWidth() / 2 - w / 2), r.getY(), w, r.getHeight());
        specPresets.setBounds (g.removeFromLeft (ui::px (66))); g.removeFromLeft (ui::px (6));
        pause.setBounds (g.removeFromLeft (ui::px (34)));       g.removeFromLeft (ui::px (8));
        if (roomScale) { specScale.setBounds (g.removeFromLeft (ui::px (88))); g.removeFromLeft (ui::px (4)); }
        if (roomChan) specChannel.setBounds (g.removeFromLeft (ui::px (62)));
        // the readout: "Ref: -20 dB" with its buttons, just "-20 dB" when space is short
        ref.setText ((roomRefButtons ? "Ref: " : "") + juce::String (s.refLevel, 0) + " dB", juce::dontSendNotification);
        ref.setTooltip ("Reference level (mouse wheel over the spectrogram, Up / Down)");
        if (roomRef)        { g.removeFromLeft (ui::px (roomRefButtons ? 8 : 6)); ref.setBounds (g.removeFromLeft (ui::px (roomRefButtons ? 92 : 62))); }
        if (roomRefButtons) { g.removeFromLeft (ui::px (4)); refUp.setBounds (g.removeFromLeft (ui::px (30)));
                              g.removeFromLeft (ui::px (4)); refDown.setBounds (g.removeFromLeft (ui::px (30))); }
        if (roomSettings)   { g.removeFromLeft (ui::px (10)); specSettings.setBounds (g.removeFromLeft (ui::px (72))); }
        return;
    }

}

void BottomBar::attachAnalyzerHeader (juce::Component& strip, std::function<void (juce::Rectangle<int>)>& layout)
{
    for (auto* c : std::initializer_list<juce::Component*> { &anaPresets, &bpoBox, &chanBox, &gridBox, &zoomOut, &zoomIn, &fullRange, &freeze, &anaSettings })
        strip.addAndMakeVisible (c);              // (re-parented: removed from the bar)
    layout = [this, &strip] (juce::Rectangle<int> r) { layoutAnalyzer (r, strip.getWidth()); };
}

void BottomBar::layoutAnalyzer (juce::Rectangle<int> r, int)
{
    // what fits, in order; when space is short the least important go first: the three combo boxes, Full,
    // the zoom buttons, Presets, Settings (Freeze stays)
    struct Item { juce::Component* c; int w, gap, drop; };
    std::vector<Item> items { { &anaPresets, 66, 6, 4 }, { &bpoBox, 64, 4, 1 }, { &chanBox, 122, 4, 1 }, { &gridBox, 84, 10, 1 },
                              { &zoomOut, 28, 2, 3 }, { &zoomIn, 28, 4, 3 }, { &fullRange, 46, 4, 2 }, { &freeze, 62, 4, 9 }, { &anaSettings, 72, 0, 5 } };
    auto total = [&] { int t = 0; for (auto& i : items) if (i.drop > 0) t += ui::px (i.w + i.gap); return t; };
    for (int level = 1; level <= 5 && total() > r.getWidth(); ++level)
        for (auto& i : items) if (i.drop == level) i.drop = 0;
    const int w = juce::jmin (r.getWidth(), total());
    auto g = juce::Rectangle<int> (juce::jlimit (r.getX(), juce::jmax (r.getX(), r.getRight() - w), r.getX()), r.getY(), w, r.getHeight());
    for (auto& i : items)
    {
        i.c->setVisible (i.drop > 0);
        if (i.drop == 0) continue;
        i.c->setBounds (g.removeFromLeft (ui::px (i.w))); g.removeFromLeft (ui::px (i.gap));
    }
}

void BottomBar::setLinkState (const juce::String& name, bool live)
{
    const auto text = name.isEmpty() ? juce::String ("Link") : "Link: " + name;
    linkBtn.setColour (juce::TextButton::buttonColourId, name.isEmpty() ? kButton : (live ? juce::Colour (0xff2f6b6b) : juce::Colour (0xff4a3a2a)));
    if (text == linkBtn.getButtonText()) return;
    linkBtn.setButtonText (text);
    resized();
}

void BottomBar::setListenState (bool active, const juce::String& what)
{
    const auto text = active ? "Listening: " + what : juce::String ("Listen");
    if (text == listenBtn.getButtonText()) return;
    listenBtn.setButtonText (text);
    listenBtn.setColour (juce::TextButton::buttonColourId, active ? juce::Colour (0xffff7a45) : kButton);
    listenBtn.setColour (juce::TextButton::textColourOffId, active ? juce::Colours::black : juce::Colours::white);
    resized();
}

void BottomBar::LufsReadout::paint (juce::Graphics& g)
{
    // Each box: caption on top, a large bold value coloured by level, a unit, and a tinted gradient fill.
    auto loudColour = [] (double v)
    {
        if (v < -14.0) return juce::Colours::white;
        if (v < -9.0)  return ui::Sem { 0xff3ddc84, ui::kSafeGood }.get();   // green
        if (v < -6.0)  return ui::Sem { 0xffffd23f, ui::kSafeWarn }.get();   // yellow
        return ui::Sem { 0xffff5252, ui::kSafeBad }.get();                   // red
    };
    auto peakColour = [] (double v)
    {
        if (v < -1.0) return juce::Colours::white;
        if (v < 0.0)  return ui::Sem { 0xffffd23f, ui::kSafeWarn }.get();
        return ui::Sem { 0xffff5252, ui::kSafeBad }.get();
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
        // full caption when it fits, else the short one (S / I / TP)
        const juce::Font capFont (juce::FontOptions (ui::fs (10.0f), juce::Font::bold));
        const char* shortCap[] = { "S", "I", "TP" };
        const bool fullCap = juce::GlyphArrangement::getStringWidth (capFont, items[i].caption) <= (float) r.getWidth();
        g.drawText (fullCap ? items[i].caption : shortCap[i], r.removeFromTop (juce::roundToInt (ui::fs (11.0f))), juce::Justification::centredLeft);
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
