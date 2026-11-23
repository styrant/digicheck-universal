#include "ListenPanel.h"
#include "../UiScale.h"

namespace
{
    const char* kModes[] = { "Stereo", "Mid", "Side", "Left", "Right" };
    const char* kBands[] = { "Sub", "Low", "Low-mid", "High-mid", "High" };
    const juce::Colour kOn (0xff4c8dff), kListen (0xffff7a45), kButton (0xff2c2f35);
    juce::String hz (double f) { return f >= 1000 ? juce::String (f / 1000, f >= 10000 ? 0 : 1) + "k" : juce::String ((int) std::lround (f)); }
}

ListenPanel::ListenPanel (BandIsolatorParams& params, Settings& st, std::function<void()> ch) : p (params), s (st), changed (std::move (ch))
{
    for (int i = 0; i < 5; ++i)
    {
        auto& m = modeBtns[(size_t) i];
        m.setButtonText (kModes[i]);
        m.setClickingTogglesState (false);
        m.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 4 ? juce::Button::ConnectedOnRight : 0));
        m.setColour (juce::TextButton::buttonColourId, kButton);
        m.setColour (juce::TextButton::buttonOnColourId, i == 0 ? kOn : kListen);
        m.onClick = [this, i] { p.mode = i; update(); if (changed) changed(); };
        addAndMakeVisible (m);
    }
    for (int i = 0; i < 5; ++i)
    {
        auto& b = bandBtns[(size_t) i];
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId, kButton.darker (0.3f));
        b.setColour (juce::TextButton::buttonOnColourId, kOn);
        b.onClick = [this, i] { p.mask = p.mask.load() ^ (1 << i); if (p.mask.load() == 0) p.mask = 1 << i; update(); if (changed) changed(); };
        b.setTooltip ("Click: hear this band or not. Double-click: this band alone");
        addAndMakeVisible (b);
        b.addMouseListener (this, false);
    }
    for (int k = 0; k < 4; ++k)
    {
        auto& sl = splitSliders[(size_t) k];
        sl.setSliderStyle (juce::Slider::LinearHorizontal);
        sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        sl.setRange (20.0, 18000.0, 1.0);
        sl.setSkewFactorFromMidPoint (600.0);
        sl.setTextValueSuffix (" Hz");
        sl.setValue (p.splits[(size_t) k].load(), juce::dontSendNotification);
        sl.onValueChange = [this, k]
        {
            // keep the splits in order, at least a third of an octave apart
            double v = splitSliders[(size_t) k].getValue();
            if (k > 0) v = juce::jmax (v, p.splits[(size_t) k - 1].load() * 1.26);
            if (k < 3) v = juce::jmin (v, p.splits[(size_t) k + 1].load() / 1.26);
            p.splits[(size_t) k] = (float) v;
            update();
        };
        sl.onDragEnd = [this] { saveSplits(); };
        addAndMakeVisible (sl);
    }
    allBtn.onClick = [this] { p.mask = 31; update(); if (changed) changed(); };
    offBtn.onClick = [this] { p.mask = 31; p.mode = 0; p.bandpass = false; update(); if (changed) changed(); };
    for (auto* b : { &allBtn, &offBtn }) { b->setColour (juce::TextButton::buttonColourId, kButton); addAndMakeVisible (b); }
    offBtn.setTooltip ("Hear everything again (the plugin passes the audio through untouched)");
    setSize (ui::px (520), ui::px (270));
    update();
    startTimerHz (8);
}

void ListenPanel::saveSplits()
{
    juce::StringArray a;
    for (auto& f : p.splits) a.add (juce::String (f.load(), 1));
    s.listenSplits = a.joinIntoString (",");
    if (changed) changed();
}

void ListenPanel::update()
{
    const int mode = p.mode.load(), mask = p.mask.load();
    for (int i = 0; i < 5; ++i) modeBtns[(size_t) i].setToggleState (i == mode, juce::dontSendNotification);
    for (int i = 0; i < 5; ++i)
    {
        const double lo = i == 0 ? 0 : p.splits[(size_t) i - 1].load(), hi = i == 4 ? 0 : p.splits[(size_t) i].load();
        const auto range = i == 0 ? "< " + hz (hi) : (i == 4 ? "> " + hz (lo) : hz (lo) + "-" + hz (hi));
        auto& b = bandBtns[(size_t) i];
        b.setButtonText (juce::String (kBands[i]) + "\n" + range);
        b.setToggleState ((mask >> i) & 1, juce::dontSendNotification);
        b.setColour (juce::TextButton::buttonOnColourId, mask == 31 ? kOn.darker (0.35f) : kListen);
    }
    for (int k = 0; k < 4; ++k)
        if (! splitSliders[(size_t) k].isMouseButtonDown()) splitSliders[(size_t) k].setValue (p.splits[(size_t) k].load(), juce::dontSendNotification);
    repaint();
}

void ListenPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1d22));
    g.setColour (juce::Colours::white.withAlpha (0.55f));
    g.setFont (juce::FontOptions (ui::fs (11.0f), juce::Font::bold));
    auto r = getLocalBounds().reduced (ui::px (12), ui::px (8));
    g.drawText ("CHANNELS", r.removeFromTop (ui::px (16)), juce::Justification::centredLeft);
    r.removeFromTop (ui::px (34));
    g.drawText ("BANDS  (double-click: solo)", r.removeFromTop (ui::px (16)), juce::Justification::centredLeft);
    r.removeFromTop (ui::px (54));
    g.drawText ("CROSSOVERS", r.removeFromTop (ui::px (16)), juce::Justification::centredLeft);
    if (p.active())
    {
        g.setColour (kListen);
        g.drawText ("Listening: " + summary (p), getLocalBounds().reduced (ui::px (12), ui::px (8)).removeFromBottom (ui::px (24)).withTrimmedRight (ui::px (200)),
                    juce::Justification::centredLeft);
    }
}

void ListenPanel::resized()
{
    auto r = getLocalBounds().reduced (ui::px (12), ui::px (8));
    r.removeFromTop (ui::px (16));
    auto row = r.removeFromTop (ui::px (28));
    const int mw = row.getWidth() / 5;
    for (auto& m : modeBtns) m.setBounds (row.removeFromLeft (mw));
    r.removeFromTop (ui::px (6 + 16));
    row = r.removeFromTop (ui::px (46));
    const int bw = row.getWidth() / 5;
    for (auto& b : bandBtns) { b.setBounds (row.removeFromLeft (bw).reduced (2, 0)); }
    r.removeFromTop (ui::px (8 + 16));
    auto foot = r.removeFromBottom (ui::px (26));
    offBtn.setBounds (foot.removeFromRight (ui::px (70)));
    foot.removeFromRight (ui::px (6));
    allBtn.setBounds (foot.removeFromRight (ui::px (90)));
    const int sh = juce::jmax (ui::px (20), r.getHeight() / 2);
    for (int k = 0; k < 4; ++k)
    {
        auto cell = juce::Rectangle<int> (r.getX() + (k % 2) * r.getWidth() / 2, r.getY() + (k / 2) * sh, r.getWidth() / 2, sh);
        splitSliders[(size_t) k].setBounds (cell.reduced (2, 2));
    }
}

void ListenPanel::mouseDoubleClick (const juce::MouseEvent& e)
{
    for (int i = 0; i < 5; ++i)
        if (e.eventComponent == &bandBtns[(size_t) i]) { p.mask = 1 << i; update(); if (changed) changed(); }
}

juce::String ListenPanel::summary (const BandIsolatorParams& p)
{
    juce::StringArray parts;
    const int mask = p.mask.load();
    if (p.bandpass.load()) parts.add ("Scope band-pass " + hz (p.bandpassHz.load()) + " Hz");
    else if (mask != 31)
    {
        juce::StringArray b;
        for (int i = 0; i < 5; ++i) if ((mask >> i) & 1) b.add (kBands[i]);
        parts.add (b.joinIntoString (" + "));
    }
    if (p.mode.load() != 0) parts.add (kModes[juce::jlimit (0, 4, p.mode.load())]);
    return parts.joinIntoString (", ");
}
