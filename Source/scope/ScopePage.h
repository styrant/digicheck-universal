// The Scope view: an oscilloscope after Scyllascope's design (https://github.com/styrant/Scyllascope-Docs).
//
//   header   Scope | Sync (VST3 only) / Free | Freeze, Scroll | Spectral, AA | Menu (theme, source, guide
//            lines, recordings folder)
//   display  the waveform over one division (1/4 to 8 bars at the tempo, or triplets): spectral colours
//            (sub, low, mid, high, air), parts over 0 dBFS in the clipping colour, Hold behind, Side2SC on
//            top, guide lines with a dB scale, the dB ruler at the cursor (Ctrl+click pins it), a Ctrl+drag
//            selection with time / peak / RMS / crest. Wheel zooms at the cursor (down to the samples),
//            Shift+wheel scrolls, drag zooms (up / down) and pans (left / right), double-click shows it all;
//            Ctrl+wheel: gain, Alt+wheel: division. Right-click: source and theme
//   footer   1/4 1/2 1 2 4 8, T | BPM | gain | BP + frequency | Side2SC | Hold | Reset | Rec + the wave icon
//            to drag the recording (or the window) out as a WAV
//
// The analysis is ScopeModel (pure C++); the audio arrives through ScopeTap (an AudioTap), so the page
// can be a plugin editor's content unchanged. Sync, Sync to Start, instance / external sidechain and BP
// listening need a host and are left for the VST3 version (docs/VST3.md).
#pragma once
#include <JuceHeader.h>
#include "../AudioTap.h"
#include "../Settings.h"
#include "ScopeModel.h"
#include "ScopeTheme.h"
#include <atomic>

// the live input for the scope: a lock-free stereo ring, drained on the message thread
class ScopeTap : public AudioTap
{
public:
    ScopeTap() : l ((size_t) kSize), r ((size_t) kSize) {}
    void setActive (bool a) { if (a && ! active.load()) fifo.reset(); active = a; }
    double sampleRate() const { return rate.load(); }

    void push (const float* left, const float* right, int n, double sr) noexcept override
    {
        if (! active.load()) return;
        rate = sr;
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        std::copy (left, left + n1, l.begin() + s1);   std::copy (right, right + n1, r.begin() + s1);
        std::copy (left + n1, left + n1 + n2, l.begin() + s2); std::copy (right + n1, right + n1 + n2, r.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }

    template <typename Fn> void drain (Fn&& fn)      // fn (const float* l, const float* r, int n)
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0) return;
        int s1, n1, s2, n2;
        fifo.prepareToRead (ready, s1, n1, s2, n2);
        if (n1 > 0) fn (l.data() + s1, r.data() + s1, n1);
        if (n2 > 0) fn (l.data() + s2, r.data() + s2, n2);
        fifo.finishedRead (n1 + n2);
    }

private:
    static constexpr int kSize = 1 << 19;            // ~10 s at 48 kHz between drains
    std::vector<float> l, r;
    juce::AbstractFifo fifo { kSize };
    std::atomic<bool> active { false };
    std::atomic<double> rate { 48000 };
};

class ScopePage : public juce::Component
{
public:
    ScopePage (ScopeTap&, Settings&, std::function<void()> settingsChanged);
    ~ScopePage() override;

    void setActive (bool shown);
    void setInputRunning (bool r)            { inputRunning = r; }
    void onFrame (double vblankSeconds);     // from the display frame clock
    void toggleFreeze();
    void applySettings();                    // after a settings change from outside (tempo, ...)

    void paint (juce::Graphics&) override;
    void resized() override;

    struct Display; struct ZoomBar; struct WaveIcon; struct Lnf; struct FreqKnob;

    // for the self-test: runs a file's start (one window) through the scope, then freezes it
    void loadForTest (const juce::File& f, double gainDb, bool selection, bool side);
    scope::ScopeModel& model()               { return engine; }

private:
    double windowSeconds() const;
    void repaintZoomBar();
    void configure();
    void showMenu();
    void updateButtons();
    void setDivision (int d);
    void exportTo (const juce::File& wav);
    juce::File nextRecordingFile() const;
    static juce::File recordingsFolder();

    ScopeTap& tap;
    Settings& s;
    std::function<void()> changed;
    scope::ScopeModel engine;
    ScopeTheme theme;
    bool active = false, inputRunning = false;
    double lastBpm = 0;

    std::unique_ptr<Lnf> lnf;
    std::unique_ptr<Display> display;
    std::unique_ptr<ZoomBar> zoomBar;
    std::unique_ptr<WaveIcon> waveIcon;
    std::unique_ptr<FreqKnob> bpFreq;

    juce::TextButton syncBtn { "Sync" }, freeBtn { "Free" }, freezeBtn { "Freeze" }, scrollBtn { "Scroll" },
                     spectralBtn { "Spectral" }, aaBtn { "AA" }, menuBtn { "Menu" };
    std::array<juce::TextButton, 6> divBtns;
    juce::TextButton tripletBtn { "T" }, bpBtn { "BP" }, sideBtn { "Side2SC" }, holdBtn { "Hold" }, resetBtn { "Reset" }, recBtn { "Rec" };
    juce::Label bpmLabel;
    juce::Slider gain { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Rectangle<int> header, footer, gainText, titleArea;
};
