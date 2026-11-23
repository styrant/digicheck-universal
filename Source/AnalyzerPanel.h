#pragma once
#include <JuceHeader.h>
#include "analyzer/PluginProcessor.h"
#include "analyzer/ui/AnalyzerView.h"
#include "analyzer/ui/SettingsPanel.h"
#include "fft/FftView.h"
#include "UiScale.h"

// The Analyzer view, in one of two styles (Settings::anaStyle):
//   Bars  Spectrum Analyzer 2's constant-Q display, with its in-window settings panel on the right
//   FFT   a SPAN / Pro-Q style curve (fft/FftView.h)
// Analysis runs only for the style shown, and only while this view is active.
class AnalyzerPanel : public juce::Component
{
public:
    AnalyzerPanel (SpectrumAnalyzerProcessor& p, FifoTap& fftInput, Settings& st, std::function<void()> changed)
        : proc (p), view (p), settings (p), fftView (fftInput, st, std::move (changed)), s (st)
    {
        addChildComponent (fftView);
        addAndMakeVisible (view);
        for (auto* b : { &barsBtn, &fftBtn })
        {
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xd02c2f35));
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4c8dff));
            addAndMakeVisible (b);
        }
        barsBtn.setConnectedEdges (juce::Button::ConnectedOnRight);
        fftBtn.setConnectedEdges (juce::Button::ConnectedOnLeft);
        barsBtn.setTooltip ("Constant-Q bars (Spectrum Analyzer 2)");
        fftBtn.setTooltip ("FFT curve in the style of SPAN / Pro-Q");
        barsBtn.onClick = [this] { setStyle (0); if (onStyleChanged) onStyleChanged (0); };
        fftBtn.onClick  = [this] { setStyle (1); if (onStyleChanged) onStyleChanged (1); };
        fftView.setHeaderInset (ui::px (46 * 2 + 12));
        addChildComponent (settings);
        view.onSettingsRequested = [this] { toggleSettings(); };
        view.onStateChanged      = [this] { if (onStateChanged) onStateChanged(); };
        settings.onClose         = [this] { toggleSettings(); };
        settings.onResetZoom     = [this] { view.resetZoom(); };
    }

    void setActive (bool a)
    {
        active = a;
        const bool fft = s.anaStyle == 1;
        proc.setUiActive (a && ! fft);
        fftView.setActive (a && fft);
        view.setVisible (! fft);
        fftView.setVisible (fft);
        barsBtn.setToggleState (! fft, juce::dontSendNotification);
        fftBtn.setToggleState (fft, juce::dontSendNotification);
        if ((! a || fft) && settings.isVisible()) toggleSettings();
    }
    void setStyle (int style) { s.anaStyle = style; setActive (active); resized(); fftView.setHeaderInset (ui::px (46 * 2 + 12)); }
    bool isFft() const        { return s.anaStyle == 1; }
    FftView& getFftView()     { return fftView; }
    void onFrame (double t)   { fftView.onFrame (t); }

    void toggleSettings()
    {
        settings.setVisible (! settings.isVisible());
        if (settings.isVisible()) { settings.refresh(); settings.toFront (false); }
        resized();
    }

    AnalyzerView& getView() noexcept { return view; }
    void refreshSettings() { if (settings.isVisible()) settings.refresh(); view.repaint(); }
    std::function<void()> onStateChanged;      // freeze / zoom changed
    std::function<void (int)> onStyleChanged;  // Bars / FFT switched here

    void resized() override
    {
        auto r = getLocalBounds();
        view.setBounds (r);
        fftView.setBounds (r);
        // the style switch: in the FFT header's first slot, or over the bars' top-left corner
        const int bw = ui::px (46), bh = ui::px (26);
        auto sw = isFft() ? juce::Rectangle<int> (ui::px (8), ui::px (5), bw * 2, bh)
                          : juce::Rectangle<int> (ui::px (44), ui::px (24), bw * 2, ui::px (22));
        barsBtn.setBounds (sw.removeFromLeft (bw)); fftBtn.setBounds (sw);
        barsBtn.toFront (false); fftBtn.toFront (false);
        if (settings.isVisible())
            settings.setBounds (r.removeFromRight (juce::jmin (340, r.getWidth())));
    }

private:
    SpectrumAnalyzerProcessor& proc;
    AnalyzerView view;
    SettingsPanel settings;
    FftView fftView;
    juce::TextButton barsBtn { "Bars" }, fftBtn { "FFT" };
    Settings& s;
    bool active = false;
};
