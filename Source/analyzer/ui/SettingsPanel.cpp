#include "SettingsPanel.h"
#include "../../UiScale.h"

namespace
{
    namespace P = sa::params;
    using APVTS = juce::AudioProcessorValueTreeState;

    // Colour picker that reports changes through a callback it owns, so it
    // stays safe if the editor closes while the call-out is open.
    struct Picker final : juce::ColourSelector, private juce::ChangeListener
    {
        explicit Picker (std::function<void (juce::Colour)> cb)
            : juce::ColourSelector (showAlphaChannel | showColourAtTop | showSliders | showColourspace),
              callback (std::move (cb))
        {
            addChangeListener (this);
        }
        ~Picker() override { removeChangeListener (this); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { if (callback) callback (getCurrentColour()); }
        std::function<void (juce::Colour)> callback;
    };

    struct Swatch final : juce::Component
    {
        Swatch (SpectrumAnalyzerProcessor& p, int idx, juce::String nm, std::function<void()> changed)
            : processor (p), index (idx), name (std::move (nm)), onChanged (std::move (changed))
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTitle (name + " colour");
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            auto sw = r.removeFromLeft (34.0f).reduced (2.0f);
            g.fillCheckerBoard (sw, 6.0f, 6.0f, juce::Colours::grey, juce::Colours::lightgrey);
            g.setColour (processor.getColour (index));
            g.fillRect (sw);
            g.setColour (juce::Colours::white.withAlpha (0.4f));
            g.drawRect (sw, 1.0f);
            g.setColour (findColour (juce::Label::textColourId));
            g.setFont (juce::FontOptions (ui::fs (13.0f)));
            g.drawText (name, r.withTrimmedLeft (6.0f), juce::Justification::centredLeft, false);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! e.mouseWasClicked()) return;
            juce::Component::SafePointer<Swatch> safe (this);
            auto picker = std::make_unique<Picker> ([safe] (juce::Colour c)
            {
                if (auto* s = safe.getComponent())
                {
                    s->processor.setColour (s->index, c);
                    s->processor.setColourSchemeName ("Custom");
                    s->repaint();
                    if (s->onChanged) s->onChanged();
                }
            });
            picker->setCurrentColour (processor.getColour (index), juce::dontSendNotification);
            picker->setSize (300, 360);
            juce::CallOutBox::launchAsynchronously (std::move (picker), getScreenBounds(), nullptr);
        }

        SpectrumAnalyzerProcessor& processor;
        int index;
        juce::String name;
        std::function<void()> onChanged;
    };
}

//==============================================================================
class SettingsPanel::Content final : public juce::Component
{
public:
    Content (SettingsPanel& o, SpectrumAnalyzerProcessor& p) : owner (o), processor (p), state (p.apvts)
    {
        header ("Analysis");
        choice (P::channel,  "Channel");
        choice (P::bpo,      "Bands / octave");
        choice (P::anchor,   "Band grid");

        header ("Peak");
        toggle (P::peakOn,   "Show peak bars");
        slider (P::peakDecay,"Decay (dB/s)");

        header ("Peak hold");
        toggle (P::holdOn,   "Show peak hold");
        toggle (P::holdInf,  "Infinite hold");
        slider (P::holdTime, "Hold time (s)");
        slider (P::holdDecay,"Decay (dB/s)");
        choice (P::holdType, "Style");
        slider (P::holdSize, "Size (px)");

        header ("Average (RMS)");
        toggle (P::avgOn,    "Show average");
        slider (P::avgTime,  "Time (s)");
        choice (P::avgType,  "Style");
        slider (P::avgSize,  "Size (px)");
        slider (P::avgSlope, "Slope (dB/oct @ 1 kHz)");

        header ("Display");
        slider (P::ceiling,  "Ceiling (dB)");
        slider (P::range,    "Range (dB)");
        slider (P::grid,     "Grid step (dB)");
        choice (P::freqGrid, "Frequency grid");

        inspector.setButtonText ("Band inspector (hover)");
        inspector.onClick = [this] { processor.inspectorEnabled.store (inspector.getToggleState()); };
        addRow (nullptr, &inspector);

        for (int i = 1; i <= 4; ++i) markerBox.addItem (juce::String (i) + " px", i);
        markerBox.onChange = [this] { processor.markerWidth.store (juce::jlimit (1, 4, markerBox.getSelectedId())); };
        addRow (makeLabel ("Marker line"), &markerBox);

        fullRange.onClick = [this] { if (owner.onResetZoom) owner.onResetZoom(); };
        addRow (nullptr, &fullRange);

        header ("Colours");
        for (const auto& s : sa::legacy::colourSchemes())
            schemes.addItem (s.name, schemes.getNumItems() + 1);
        schemes.setTextWhenNothingSelected ("Custom");
        schemes.onChange = [this]
        {
            const int i = schemes.getSelectedItemIndex();
            if (juce::isPositiveAndBelow (i, (int) sa::legacy::colourSchemes().size()))
            {
                processor.applyColourScheme (sa::legacy::colourSchemes()[(size_t) i]);
                for (auto& s : swatches) s->repaint();
            }
        };
        addRow (makeLabel ("Scheme"), &schemes);

        const char* names[] = { "Peak", "Peak hold", "Average", "Grid border", "Grid lines", "Labels", "Background A", "Background B" };
        for (int i = 0; i < SpectrumAnalyzerProcessor::kNumColours; ++i)
        {
            auto* sw = swatches.add (new Swatch (processor, i, names[i], [this] { schemes.setSelectedId (0, juce::dontSendNotification); }));
            addRow (nullptr, sw);
        }

        // Presets (factory, import, default) are in the bottom bar's Presets button.

        help.setText ("Mouse: wheel = zoom, drag = pan, shift+drag = zoom to selection, "
                      "ctrl/cmd+wheel = ceiling, alt+wheel = range, double-click = reset meters, "
                      "double-click axis = full range, alt+click = pin the readout, alt+right-click = remove it, right-click = settings.\n"
                      "Keys: R reset, F freeze, 0 full range, +/- zoom, I inspector.",
                      juce::dontSendNotification);
        help.setFont (juce::FontOptions (ui::fs (12.0f)));
        help.setJustificationType (juce::Justification::topLeft);
        help.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (help);

        refresh();
    }

    void refresh()
    {
        inspector.setToggleState (processor.inspectorEnabled.load(), juce::dontSendNotification);
        markerBox.setSelectedId (processor.markerWidth.load(), juce::dontSendNotification);
        const auto name = processor.getColourSchemeName();
        int id = 0;
        for (int i = 0; i < schemes.getNumItems(); ++i)
            if (schemes.getItemText (i) == name) id = i + 1;
        schemes.setSelectedId (id, juce::dontSendNotification);
        for (auto& s : swatches) s->repaint();
    }

    int layoutHeight (int width)
    {
        int y = 4;
        for (auto& r : rows) y += r.header ? 30 : 28;
        return y + 8 + helpHeight (width) + 8;
    }

    void resized() override
    {
        const int w = getWidth();
        int y = 4;
        for (auto& r : rows)
        {
            if (r.header)
            {
                r.label->setBounds (8, y + 8, w - 16, 20);
                y += 30;
                continue;
            }
            if (r.label != nullptr)
            {
                r.label->setBounds (8, y + 2, 128, 24);
                r.control->setBounds (140, y + 2, w - 148, 24);
            }
            else
            {
                r.control->setBounds (8, y + 2, w - 16, 24);
            }
            y += 28;
        }
        help.setBounds (8, y + 8, w - 16, helpHeight (w));
    }

private:
    struct Row
    {
        juce::Label*     label   = nullptr;
        juce::Component* control = nullptr;
        bool header = false;
    };

    int helpHeight (int width) const { return width < 300 ? 130 : 96; }

    juce::Label* makeLabel (const juce::String& text, bool isHeader = false)
    {
        auto* l = labels.add (new juce::Label ({}, text));
        l->setFont (juce::FontOptions (ui::fs (isHeader ? 14.0f : 13.0f), isHeader ? juce::Font::bold : juce::Font::plain));
        if (isHeader) l->setColour (juce::Label::textColourId, juce::Colour (0xff8ec5ff));
        addAndMakeVisible (l);
        return l;
    }

    void addRow (juce::Label* l, juce::Component* c)
    {
        addAndMakeVisible (c);
        rows.push_back ({ l, c, false });
    }

    void header (const juce::String& text)
    {
        rows.push_back ({ makeLabel (text, true), nullptr, true });
    }

    void choice (const char* pid, const juce::String& text)
    {
        auto* box = combos.add (new juce::ComboBox());
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (pid)))
            box->addItemList (p->choices, 1);
        comboAttachments.add (new APVTS::ComboBoxAttachment (state, pid, *box));
        addRow (makeLabel (text), box);
    }

    void slider (const char* pid, const juce::String& text)
    {
        auto* s = sliders.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight));
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
        sliderAttachments.add (new APVTS::SliderAttachment (state, pid, *s));
        addRow (makeLabel (text), s);
    }

    void toggle (const char* pid, const juce::String& text)
    {
        auto* b = toggles.add (new juce::ToggleButton (text));
        buttonAttachments.add (new APVTS::ButtonAttachment (state, pid, *b));
        addRow (nullptr, b);
    }

    SettingsPanel& owner;
    SpectrumAnalyzerProcessor& processor;
    APVTS& state;

    std::vector<Row> rows;
    juce::OwnedArray<juce::Label> labels;
    juce::OwnedArray<juce::ComboBox> combos;
    juce::OwnedArray<juce::Slider> sliders;
    juce::OwnedArray<juce::ToggleButton> toggles;
    juce::OwnedArray<Swatch> swatches;
    juce::OwnedArray<APVTS::ComboBoxAttachment> comboAttachments;
    juce::OwnedArray<APVTS::SliderAttachment> sliderAttachments;
    juce::OwnedArray<APVTS::ButtonAttachment> buttonAttachments;

    juce::ToggleButton inspector;
    juce::ComboBox markerBox;
    juce::TextButton fullRange { "Show full frequency range" };
    juce::ComboBox schemes;
    juce::Label help;
};

//==============================================================================
SettingsPanel::SettingsPanel (SpectrumAnalyzerProcessor& p) : processor (p)
{
    title.setText ("Settings", juce::dontSendNotification);
    title.setFont (juce::FontOptions (ui::fs (16.0f), juce::Font::bold));
    addAndMakeVisible (title);

    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (closeButton);

    content = std::make_unique<Content> (*this, processor);
    viewport.setViewedComponent (content.get(), false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
}

SettingsPanel::~SettingsPanel() = default;

void SettingsPanel::refresh()
{
    content->refresh();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xf0181c22));
    g.setColour (juce::Colours::white.withAlpha (0.12f));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void SettingsPanel::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (28);
    closeButton.setBounds (top.removeFromRight (64).reduced (0, 2));
    title.setBounds (top);
    viewport.setBounds (r);

    const int w = viewport.getWidth() - viewport.getScrollBarThickness();
    content->setSize (w, content->layoutHeight (w));
}
