#pragma once
#include <JuceHeader.h>
#include "analyzer/PluginProcessor.h"
#include "analyzer/ui/AnalyzerView.h"
#include "analyzer/ui/SettingsPanel.h"

// The Analyzer view: Spectrum Analyzer 2's display with its in-window settings panel on the right.
// Analysis runs only while this view is active.
class AnalyzerPanel : public juce::Component
{
public:
    explicit AnalyzerPanel (SpectrumAnalyzerProcessor& p) : proc (p), view (p), settings (p)
    {
        addAndMakeVisible (view);
        addChildComponent (settings);
        view.onSettingsRequested = [this] { toggleSettings(); };
        view.onStateChanged      = [this] { if (onStateChanged) onStateChanged(); };
        settings.onClose         = [this] { toggleSettings(); };
        settings.onResetZoom     = [this] { view.resetZoom(); };
    }

    void setActive (bool active)
    {
        proc.setUiActive (active);
        if (! active && settings.isVisible()) toggleSettings();
    }

    void toggleSettings()
    {
        settings.setVisible (! settings.isVisible());
        if (settings.isVisible()) { settings.refresh(); settings.toFront (false); }
        resized();
    }

    AnalyzerView& getView() noexcept { return view; }
    std::function<void()> onStateChanged;      // freeze / zoom changed

    void resized() override
    {
        auto r = getLocalBounds();
        view.setBounds (r);
        if (settings.isVisible())
            settings.setBounds (r.removeFromRight (juce::jmin (340, r.getWidth())));
    }

private:
    SpectrumAnalyzerProcessor& proc;
    AnalyzerView view;
    SettingsPanel settings;
};
