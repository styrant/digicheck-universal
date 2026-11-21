#include "SettingsDialog.h"
#include "SpectrogramView.h"

namespace
{
    const int fftSizes[]  = { 512, 1024, 2048, 4096, 8192, 16384 };
    const int ranges[]    = { 40, 50, 60, 70, 80, 90, 100, 120 };
    const int noteRefs[]  = { 0, 415, 432, 435, 440, 441, 442, 443, 444, 446 };

    template <typename T, size_t N> int indexOf (const T (&a)[N], T v, int fallback)
    {
        for (size_t i = 0; i < N; ++i) if (a[i] == v) return (int) i;
        return fallback;
    }
}

SettingsDialog::SettingsDialog (Settings& st, std::function<void (Change)> n)
    : s (st), snapshot (st), notify (std::move (n))
{
    combo (chan, { "left", "right", "left + right", "left - right" }, s.channel, [this] (int i) { s.channel = i; notify (Change::Dsp); });
    addRow ("Channel Select", chan);

    toggle (reassign, s.reassign, [this] (bool v) { s.reassign = v; notify (Change::Dsp); });
    addRow ("Enable Reassignment", reassign);

    combo (fft, { "512", "1024", "2048", "4096", "8192", "16384" }, indexOf (fftSizes, s.fftSize, 3),
           [this] (int i) { s.fftSize = fftSizes[i]; notify (Change::Dsp); });
    addRow ("FFT Size", fft);

    slider (ref, -80, 0, 1, s.refLevel, [this] (double v) { s.refLevel = v; notify (Change::Visual); });
    addRow ("Reference Level", ref, &refUnit);

    juce::StringArray rn; for (int r : ranges) rn.add (juce::String (r) + "dB log");
    combo (range, rn, indexOf (ranges, (int) s.range, 2), [this] (int i) { s.range = ranges[i]; notify (Change::Visual); });
    addRow ("Display Range", range);

    slider (comp, -1, 1, 0.1, s.compExp, [this] (double v) { s.compExp = v; notify (Change::Visual); });
    addRow ("Compress/Expand", comp);

    combo (colour, SpectrogramView::colourNames(), s.colour, [this] (int i) { s.colour = i; notify (Change::Visual); });
    addRow ("Color", colour);

    slider (speed, 25, 1000, 5, s.speed, [this] (double v) { s.speed = v; notify (Change::Dsp); });
    addRow ("Display Speed", speed, &speedUnit);

    combo (freq, Settings::freqPresetNames(), s.freqPreset, [this] (int i) { s.freqPreset = i; notify (Change::Dsp); });
    addRow ("Frequency Range", freq);

    combo (grid, { "off", "horizontal", "vertical", "horizontal + vertical" }, s.grid, [this] (int i) { s.grid = i; notify (Change::Overlay); });
    addRow ("Show Grid", grid);

    combo (rrange, { "standard", "short" }, s.shortRange ? 1 : 0, [this] (int i) { s.shortRange = i == 1; notify (Change::Dsp); });
    addRow ("Reassign Range", rrange);

    toggle (stationary, s.stationary, [this] (bool v) { s.stationary = v; notify (Change::Overlay); });
    addRow ("Stationary Display", stationary);

    toggle (hidePanel, s.hidePanel, [this] (bool v) { s.hidePanel = v; notify (Change::Bar); });
    addRow ("Hide Control Panel", hidePanel);

    toggle (showScale, s.showScale, [this] (bool v) { s.showScale = v; notify (Change::Layout); });
    addRow ("Show Color Scale", showScale);

    juce::StringArray nn { "off" }; for (int i = 1; i < (int) std::size (noteRefs); ++i) nn.add ("A4 @ " + juce::String (noteRefs[i]) + "Hz");
    combo (note, nn, indexOf (noteRefs, s.noteRef, 4), [this] (int i) { s.noteRef = noteRefs[i]; notify (Change::Overlay); });
    addRow ("Show Note Cursor", note);

    toggle (showLufs, s.showLufs, [this] (bool v) { s.showLufs = v; notify (Change::Bar); });
    addRow ("Show LUFS Meter", showLufs);


    hint.setText ("Latency and CPU rise with FFT size and display speed. Turn reassignment off to compare with a plain FFT.",
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    hint.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hint);

    ok.onClick = [this] { close(); };
    cancel.onClick = [this]
    {
        const bool dsp = snapshot.channel != s.channel || snapshot.reassign != s.reassign || snapshot.fftSize != s.fftSize
                      || std::abs (snapshot.speed - s.speed) > 1e-9 || snapshot.freqPreset != s.freqPreset || snapshot.shortRange != s.shortRange;
        const bool layout = snapshot.showScale != s.showScale;
        s.channel = snapshot.channel; s.reassign = snapshot.reassign; s.fftSize = snapshot.fftSize;
        s.refLevel = snapshot.refLevel; s.range = snapshot.range; s.compExp = snapshot.compExp; s.colour = snapshot.colour;
        s.speed = snapshot.speed; s.freqPreset = snapshot.freqPreset; s.grid = snapshot.grid; s.shortRange = snapshot.shortRange;
        s.stationary = snapshot.stationary; s.hidePanel = snapshot.hidePanel; s.showScale = snapshot.showScale; s.noteRef = snapshot.noteRef;
        s.showLufs = snapshot.showLufs; s.autoHideBar = snapshot.autoHideBar;
        if (dsp) notify (Change::Dsp);
        if (layout) notify (Change::Layout);
        notify (Change::Visual);
        notify (Change::Bar);
        close();
    };
    addAndMakeVisible (ok); addAndMakeVisible (cancel);
    setSize (510, 40 + (int) rows.size() * 30 + 90);
}

void SettingsDialog::addRow (const juce::String& label, juce::Component& c, juce::Label* suffix)
{
    auto* l = labels.add (new juce::Label ({}, label));
    addAndMakeVisible (l); addAndMakeVisible (c);
    if (suffix) addAndMakeVisible (suffix);
    rows.push_back ({ label, &c, suffix });
    l->setComponentID (label);
}

void SettingsDialog::combo (juce::ComboBox& b, const juce::StringArray& items, int sel, std::function<void (int)> onPick)
{
    b.addItemList (items, 1);
    b.setSelectedItemIndex (juce::jlimit (0, items.size() - 1, sel), juce::dontSendNotification);
    b.onChange = [&b, onPick] { if (b.getSelectedItemIndex() >= 0) onPick (b.getSelectedItemIndex()); };
}

void SettingsDialog::slider (juce::Slider& sl, double lo, double hi, double step, double value, std::function<void (double)> onMove)
{
    sl.setSliderStyle (juce::Slider::LinearHorizontal);
    sl.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 64, 22);
    sl.setRange (lo, hi, step);
    sl.setColour (juce::Slider::trackColourId, juce::Colour (0xff4c8dff));
    sl.setColour (juce::Slider::backgroundColourId, juce::Colour (0xff3a3e46));
    sl.setValue (value, juce::dontSendNotification);
    sl.onValueChange = [&sl, onMove] { onMove (sl.getValue()); };
}

void SettingsDialog::toggle (juce::ToggleButton& t, bool value, std::function<void (bool)> onFlip)
{
    t.setToggleState (value, juce::dontSendNotification);
    t.onClick = [&t, onFlip] { onFlip (t.getToggleState()); };
}

void SettingsDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22));
    g.setColour (juce::Colour (0xff2a2d33));
    g.drawHorizontalLine (getHeight() - 50, 0.0f, (float) getWidth());
}

void SettingsDialog::resized()
{
    auto r = getLocalBounds().reduced (16, 14);
    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto line = r.removeFromTop (30).reduced (0, 3);
        labels[(int) i]->setBounds (line.removeFromLeft (200));
        auto* c = rows[i].control;
        if (dynamic_cast<juce::Slider*> (c) != nullptr)
        {
            if (rows[i].suffix) rows[i].suffix->setBounds (line.removeFromRight (44));
            c->setBounds (line);
        }
        else if (dynamic_cast<juce::ToggleButton*> (c) != nullptr) c->setBounds (line.removeFromLeft (80));
        else c->setBounds (line.removeFromLeft (210));
    }
    r.removeFromTop (4);
    hint.setBounds (r.removeFromTop (34));
    auto buttons = getLocalBounds().removeFromBottom (50).reduced (16, 11);
    cancel.setBounds (buttons.removeFromRight (90));
    buttons.removeFromRight (8);
    ok.setBounds (buttons.removeFromRight (90));
}

void SettingsDialog::close()
{
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>()) dw->exitModalState (0);
}

void SettingsDialog::show (Settings& s, std::function<void (Change)> notify, juce::Component* parent)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new SettingsDialog (s, std::move (notify)));
    o.dialogTitle = "Spectroscope Settings";
    o.dialogBackgroundColour = juce::Colour (0xff1b1d22);
    o.componentToCentreAround = parent;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
}
