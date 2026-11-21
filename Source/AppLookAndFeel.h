#pragma once
#include <JuceHeader.h>
#include "UiScale.h"

// Dark LookAndFeel whose built-in widget fonts follow the app's text size. Widget heights already
// grow with ui::px() for larger text, so fonts derived from a height are first divided by ui::k().
class AppLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel() { setColourScheme (getDarkColourScheme()); }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::FontOptions (juce::jmin (16.0f, (float) h / ui::k() * 0.6f) * ui::scale());
    }
    juce::Font getComboBoxFont (juce::ComboBox& b) override
    {
        return juce::FontOptions (juce::jmin (16.0f, (float) b.getHeight() / ui::k() * 0.85f) * ui::scale());
    }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (ui::fs (17.0f)); }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (juce::FontOptions (ui::fs (15.0f)));
        return l;
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool down) override
    {
        const float fontSize = juce::jmin (15.0f, (float) button.getHeight() / ui::k() * 0.75f) * ui::scale();
        const float tick = fontSize * 1.1f;
        drawTickBox (g, button, 4.0f, ((float) button.getHeight() - tick) * 0.5f, tick, tick,
                     button.getToggleState(), button.isEnabled(), highlighted, down);
        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (fontSize);
        if (! button.isEnabled()) g.setOpacity (0.5f);
        g.drawFittedText (button.getButtonText(),
                          button.getLocalBounds().withTrimmedLeft (juce::roundToInt (tick) + 10).withTrimmedRight (2),
                          juce::Justification::centredLeft, 10);
    }
};
