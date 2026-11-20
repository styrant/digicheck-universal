#pragma once
#include <JuceHeader.h>
#include "Settings.h"

// "Spectroscope Settings" — same fields, order and wording as DigiCheck NG's dialog.
// Changes apply live; Cancel restores the settings as they were when the dialog opened.
class SettingsDialog : public juce::Component
{
public:
    SettingsDialog (Settings& s, std::function<void (Change)> notify);
    void resized() override;
    void paint (juce::Graphics&) override;

    static void show (Settings& s, std::function<void (Change)> notify, juce::Component* parent);

private:
    struct Row { juce::String label; juce::Component* control; juce::Label* suffix = nullptr; };
    void addRow (const juce::String& label, juce::Component& c, juce::Label* suffix = nullptr);
    void combo (juce::ComboBox& b, const juce::StringArray& items, int selectedIndex, std::function<void (int)> onPick);
    void slider (juce::Slider& sl, double lo, double hi, double step, double value, std::function<void (double)> onMove);
    void toggle (juce::ToggleButton& t, bool value, std::function<void (bool)> onFlip);
    void close();

    Settings& s;
    Settings snapshot;
    std::function<void (Change)> notify;

    juce::ComboBox chan, fft, range, colour, freq, grid, rrange, note;
    juce::ToggleButton reassign { "On" }, stationary { "On" }, hidePanel { "On" }, showScale { "On" }, showLufs { "On" };
    juce::Slider ref, comp, speed;
    juce::Label refUnit { {}, "dB" }, speedUnit { {}, "px/s" }, hint;
    juce::TextButton ok { "OK" }, cancel { "Cancel" };
    juce::OwnedArray<juce::Label> labels;
    std::vector<Row> rows;
};
