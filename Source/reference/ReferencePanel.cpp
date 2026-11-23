#include "ReferencePanel.h"
#include "../transcribe/TranscriptionPlayer.h"

namespace
{
    const juce::Colour kBtn (0xff3a3c41), kOn (0xff4c8dff), kRefColour (0xffff9f43), kText (0xffeef0f3), kDim (0xffb0b6c0);
    juce::String lufs (double v, const char* unit) { return std::isfinite (v) && v > -70 ? juce::String (v, 1) + " " + unit : juce::String ("-"); }
}

int ReferencePanel::preferredHeight() { return ui::px (262); }

ReferencePanel::ReferencePanel (ReferenceTrack& r, Settings& st, PlaybackOutput* out, std::function<void()> changed)
    : ref (r), s (st), output (out), onChanged (std::move (changed))
{
    for (auto* b : { &loadBtn, &clearBtn, &mixBtn, &refBtn, &playBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, kBtn);
        b->setColour (juce::TextButton::buttonOnColourId, kOn);
        addAndMakeVisible (b);
    }
    for (auto* t : { &matchToggle, &showBal, &showFft, &showMon }) addAndMakeVisible (t);
    loadBtn.setTooltip ("Load a song to compare your mix with (WAV, AIFF, FLAC, MP3...). It's measured once: loudness, tonal balance and spectrum");
    loadBtn.onClick = [this] { choose(); };
    clearBtn.onClick = [this] { ref.stop(); ref.clear(); s.refFile = {}; status = {}; refresh(); if (onChanged) onChanged(); };
    mixBtn.setConnectedEdges (juce::Button::ConnectedOnRight);
    refBtn.setConnectedEdges (juce::Button::ConnectedOnLeft);
    mixBtn.setTooltip ("Hear your mix (the plugin's input)");
    refBtn.setTooltip ("Hear the reference instead, in step with the transport (it fades over 10 ms, so it never clicks)");
    mixBtn.onClick = [this] { ref.listen = false; refresh(); };
    refBtn.onClick = [this] { ref.listen = ref.ready(); refresh(); };
    playBtn.setTooltip ("Play the reference through Windows Audio (the system output), from the position below. Mute your DAW to compare");
    playBtn.onClick = [this]
    {
        if (ref.isPlaying()) { ref.stop(); refresh(); return; }
        if (output != nullptr && ! output->isOpen())
        {
            juce::String err;
            if (! output->open ({}, err)) { status = "Couldn't open the audio output: " + err; refresh(); return; }
        }
        ref.play(); refresh();
    };
    matchToggle.setTooltip ("Play the reference at your mix's integrated loudness, so you compare the sound, not the level");
    matchToggle.onClick = [this] { ref.matchLoudness = matchToggle.getToggleState(); s.refMatch = matchToggle.getToggleState(); if (onChanged) onChanged(); };
    showBal.onClick = [this] { s.refShowBalance = showBal.getToggleState(); if (onChanged) onChanged(); };
    showFft.onClick = [this] { s.refShowFft = showFft.getToggleState(); if (onChanged) onChanged(); };
    showMon.onClick = [this] { s.refShowMonitor = showMon.getToggleState(); if (onChanged) onChanged(); };
    showBal.setTooltip ("Draw the reference's tonal balance on the Balance curve (orange)");
    showFft.setTooltip ("Draw the reference's average spectrum on the FFT analyzer (orange)");
    showMon.setTooltip ("Show the reference's loudness in the Monitor's Loudness module");
    posSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    posSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    posSlider.onValueChange = [this] { if (posSlider.isMouseButtonDown()) ref.seek (posSlider.getValue()); };
    addAndMakeVisible (posSlider);
    ref.matchLoudness = s.refMatch;
    refresh();
    startTimerHz (8);
}

ReferencePanel::~ReferencePanel() { stopTimer(); }

void ReferencePanel::choose()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a reference song",
        s.refFile.isNotEmpty() ? juce::File (s.refFile).getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg;*.m4a;*.wma");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) loadFile (fc.getResult()); });
}

void ReferencePanel::loadFile (const juce::File& f)
{
    status = "Loading " + f.getFileName() + "...";
    ref.load (f, [safe = juce::Component::SafePointer<ReferencePanel> (this), f] (bool ok, const juce::String& err)
    {
        if (safe == nullptr) return;
        safe->status = ok ? juce::String() : err;
        if (ok) safe->s.refFile = f.getFullPathName();
        safe->refresh();
        if (safe->onChanged) safe->onChanged();
    });
    refresh();
}

void ReferencePanel::refresh()
{
    const bool has = ref.ready();
    clearBtn.setEnabled (has);
    for (auto* c : std::initializer_list<juce::Component*> { &mixBtn, &refBtn, &playBtn, &posSlider, &matchToggle }) c->setEnabled (has);
    mixBtn.setVisible (output == nullptr); refBtn.setVisible (output == nullptr);
    playBtn.setVisible (output != nullptr); posSlider.setVisible (output != nullptr);
    mixBtn.setToggleState (! ref.listen.load(), juce::dontSendNotification);
    refBtn.setToggleState (ref.listen.load(), juce::dontSendNotification);
    refBtn.setColour (juce::TextButton::buttonOnColourId, kRefColour.darker (0.2f));
    playBtn.setButtonText (ref.isPlaying() ? "Stop" : "Play");
    playBtn.setToggleState (ref.isPlaying(), juce::dontSendNotification);
    matchToggle.setToggleState (s.refMatch, juce::dontSendNotification);
    showBal.setToggleState (s.refShowBalance, juce::dontSendNotification);
    showFft.setToggleState (s.refShowFft, juce::dontSendNotification);
    showMon.setToggleState (s.refShowMonitor, juce::dontSendNotification);
    posSlider.setRange (0.0, juce::jmax (1.0, ref.lengthSeconds()), 0.0);
    repaint();
}

void ReferencePanel::timerCallback()
{
    if (ref.isLoading() || ref.isPlaying() || ref.listen.load()) repaint();
    if (ref.isPlaying() && ! posSlider.isMouseButtonDown()) posSlider.setValue (ref.position(), juce::dontSendNotification);
}

void ReferencePanel::resized()
{
    using ui::px;
    auto r = getLocalBounds().reduced (14, 10);
    headArea = r.removeFromTop (px (20));
    auto row = r.removeFromTop (px (28));
    loadBtn.setBounds (row.removeFromLeft (px (140))); row.removeFromLeft (px (6));
    clearBtn.setBounds (row.removeFromLeft (px (64)));
    r.removeFromTop (px (8));
    statsArea = r.removeFromTop (px (96));
    r.removeFromTop (px (6));
    row = r.removeFromTop (px (28));
    if (output == nullptr) { mixBtn.setBounds (row.removeFromLeft (px (64))); refBtn.setBounds (row.removeFromLeft (px (96))); }
    else { playBtn.setBounds (row.removeFromLeft (px (70))); row.removeFromLeft (px (8)); posSlider.setBounds (row.removeFromLeft (px (190))); }
    row.removeFromLeft (px (12));
    matchToggle.setBounds (row.removeFromLeft (px (150)));
    r.removeFromTop (px (8));
    row = r.removeFromTop (px (24));
    showLabel = row.removeFromLeft (px (58));
    showBal.setBounds (row.removeFromLeft (px (86)));
    showFft.setBounds (row.removeFromLeft (px (130)));
    showMon.setBounds (row.removeFromLeft (px (90)));
}

void ReferencePanel::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xf21b1d22)); g.fillRoundedRectangle (r, 8.0f);
    g.setColour (juce::Colour (0xff3a3c40)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (juce::Colours::white.withAlpha (0.5f)); g.setFont (juce::FontOptions (ui::fs (10.5f), juce::Font::bold));
    g.drawText ("REFERENCE TRACK", headArea, juce::Justification::centredLeft);

    auto a = statsArea;
    g.setFont (juce::FontOptions (ui::fs (13.0f)));
    if (ref.isLoading())
    {
        g.setColour (kDim); g.drawText (status + "  " + juce::String (juce::roundToInt (ref.loadProgress() * 100)) + " %", a.removeFromTop (ui::px (22)), juce::Justification::centredLeft);
        return;
    }
    if (! ref.ready())
    {
        g.setColour (kDim);
        g.drawFittedText (status.isNotEmpty() ? status : "No reference loaded. Load a finished song you like in the same style: "
                                                         "its loudness, tonal balance and spectrum appear beside your mix's.",
                          a, juce::Justification::topLeft, 4);
        return;
    }
    const auto& st = ref.stats();
    const double len = ref.lengthSeconds();
    g.setColour (kRefColour); g.setFont (juce::FontOptions (ui::fs (13.5f), juce::Font::bold));
    g.drawText (ref.name() + "   (" + juce::String ((int) len / 60) + ":" + juce::String ((int) len % 60).paddedLeft ('0', 2) + ")", a.removeFromTop (ui::px (22)), juce::Justification::centredLeft);
    // a small table: reference | your mix
    const float mix = ref.mixLufs.load();
    auto rowText = [&] (const juce::String& k, const juce::String& rv, const juce::String& mv)
    {
        auto line = a.removeFromTop (ui::px (18));
        g.setFont (juce::FontOptions (ui::fs (12.5f)));
        g.setColour (kDim); g.drawText (k, line.removeFromLeft (ui::px (130)), juce::Justification::centredLeft);
        g.setColour (kRefColour); g.drawText (rv, line.removeFromLeft (ui::px (110)), juce::Justification::centredLeft);
        g.setColour (kText); g.drawText (mv, line, juce::Justification::centredLeft);
    };
    rowText ("", "Reference", "Your mix");
    rowText ("Integrated", lufs (st.integrated, "LUFS"), lufs (mix, "LUFS") + (std::isfinite (mix) && std::isfinite (st.integrated) ? "   (" + juce::String (mix - st.integrated >= 0 ? "+" : "") + juce::String (mix - st.integrated, 1) + " LU)" : juce::String()));
    rowText ("LRA / True peak", juce::String (st.lra, 1) + " LU / " + lufs (st.truePeak, "dBTP"), "");
    rowText ("PLR", std::isfinite (st.plr) ? juce::String (st.plr, 1) + " dB" : juce::String ("-"),
             matchToggle.getToggleState() ? "match gain " + juce::String (ref.matchGainDb(), 1) + " dB" : juce::String());
    g.setColour (kDim); g.setFont (juce::FontOptions (ui::fs (12.5f)));
    g.drawText ("Show in", showLabel, juce::Justification::centredLeft);
}
