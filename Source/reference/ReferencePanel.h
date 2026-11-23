// The Reference pop-up (bottom bar "Ref"): load a reference song, see its loudness beside the mix's,
// choose where it's drawn, and listen to it (VST3: instead of the mix, in step with the transport;
// app: through Windows Audio) at matched loudness.
#pragma once
#include <JuceHeader.h>
#include "ReferenceTrack.h"
#include "../Settings.h"
#include "../UiScale.h"

class PlaybackOutput;

class ReferencePanel : public juce::Component, private juce::Timer
{
public:
    // output: the app's playback device for the reference (nullptr in the plugin, which plays it on its output)
    ReferencePanel (ReferenceTrack&, Settings&, PlaybackOutput* output, std::function<void()> changed);
    ~ReferencePanel() override;

    void loadFile (const juce::File&);
    void refresh();
    static int preferredHeight();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void choose();

    ReferenceTrack& ref;
    Settings& s;
    PlaybackOutput* output;
    std::function<void()> onChanged;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String status;

    juce::TextButton loadBtn { "Load reference..." }, clearBtn { "Clear" }, mixBtn { "Mix" }, refBtn { "Reference" }, playBtn { "Play" };
    juce::ToggleButton matchToggle { "Match loudness" }, showBal { "Balance" }, showFft { "Analyzer (FFT)" }, showMon { "Monitor" };
    juce::Slider posSlider;
    juce::Rectangle<int> statsArea, showLabel, headArea;
};
