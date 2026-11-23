// A colour picker that applies every change at once, for a call-out box:
//   juce::CallOutBox::launchAsynchronously (std::make_unique<LiveColourPicker> (colour, onChange), area, nullptr);
#pragma once
#include <JuceHeader.h>

struct LiveColourPicker : public juce::Component, private juce::ChangeListener
{
    LiveColourPicker (juce::Colour c, std::function<void (juce::Colour)> f) : onChange (std::move (f))
    {
        sel.setCurrentColour (c, juce::dontSendNotification);
        sel.addChangeListener (this);
        addAndMakeVisible (sel);
        setSize (300, 280);
    }
    void resized() override { sel.setBounds (getLocalBounds()); }
    void changeListenerCallback (juce::ChangeBroadcaster*) override { onChange (sel.getCurrentColour()); }
    juce::ColourSelector sel { juce::ColourSelector::showColourAtTop | juce::ColourSelector::showSliders | juce::ColourSelector::showColourspace };
    std::function<void (juce::Colour)> onChange;
};
