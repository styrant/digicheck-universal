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

    // Help mode's descriptions: larger text, wrapped to a readable width, on a dark card with a blue edge
    static juce::TextLayout tipLayout (const juce::String& text, juce::Colour c)
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::topLeft);
        s.append (text, juce::FontOptions (ui::fs (14.5f)), c);
        juce::TextLayout tl;
        tl.createLayoutWithBalancedLineLengths (s, (float) ui::px (340));
        return tl;
    }
    juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
    {
        const auto tl = tipLayout (text, juce::Colours::white);
        const int w = (int) (tl.getWidth() + ui::px (22)), h = (int) (tl.getHeight() + ui::px (16));
        return juce::Rectangle<int> (pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 16,
                                     pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin (parent);
    }
    void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
    {
        const auto r = juce::Rectangle<float> ((float) w, (float) h);
        g.setColour (juce::Colour (0xf5181a1f)); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colour (0xff4c8dff)); g.drawRoundedRectangle (r.reduced (0.75f), 6.0f, 1.5f);
        tipLayout (text, juce::Colour (0xffeef0f3)).draw (g, r.reduced ((float) ui::px (11), (float) ui::px (8)));
    }

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
