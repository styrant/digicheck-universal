// The VST3 editor: the app's main view (all six pages and the bottom bar) on the processor's Core and
// AnalysisEngine, with the plugin's extras switched on through HostLink (Sync, sidechain, Listen).
#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "../MainComponent.h"
#include "../AppLookAndFeel.h"

class DcuEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit DcuEditor (DcuProcessor&);
    ~DcuEditor() override;
    void resized() override;
    MainComponent* content() { return mc.get(); }

private:
    void timerCallback() override;
    void build();

    DcuProcessor& proc;
    AppLookAndFeel lnf;                          // per editor: never a static look-and-feel in a plugin
    std::unique_ptr<MainComponent> mc;
};
