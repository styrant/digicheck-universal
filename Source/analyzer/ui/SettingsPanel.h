#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"

// In-editor settings overlay (replaces the separate Win32 settings window of
// the original, which was fragile in many hosts).
class SettingsPanel final : public juce::Component
{
public:
    explicit SettingsPanel (SpectrumAnalyzerProcessor&);
    ~SettingsPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onClose;
    std::function<void()> onResetZoom;
    void refresh();   // re-read non-parameter state (colours, inspector)

private:
    class Content;
    SpectrumAnalyzerProcessor& processor;
    juce::Label title;
    juce::TextButton closeButton { "Close" };
    juce::Viewport viewport;
    std::unique_ptr<Content> content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SettingsPanel)
};
