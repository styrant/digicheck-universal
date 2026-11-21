#pragma once
#include <JuceHeader.h>

// All user settings, mirroring DigiCheck NG's Spectroscope dialog, persisted between sessions.
struct Settings
{
    int    channel    = 2;        // 0 left, 1 right, 2 left+right, 3 left-right
    bool   reassign   = true;
    int    fftSize    = 4096;
    double refLevel   = -20.0;    // dBFS shown at full brightness
    double range      = 60.0;     // dB
    double compExp    = 0.0;      // -1 .. +1
    int    colour     = 0;
    double speed      = 300.0;    // px/s
    int    freqPreset = 0;
    int    grid       = 1;        // 0 off, 1 horizontal, 2 vertical, 3 both
    bool   shortRange = true;
    bool   stationary = false;
    bool   hidePanel  = false;
    bool   showScale  = false;
    int    noteRef    = 440;      // 0 = off
    bool   showLufs   = true;
    bool   gpu        = true;     // OpenGL rendering
    int    fpsCap     = 0;        // frame-rate limit, 0 = follow the monitor
    bool   autoHideBar = true;    // hide the top toolbar in full screen (reveal at the top edge)

    int    viewMode   = 0;        // 0 spectrogram, 1 analyzer
    double bpm        = 120.0;    // tempo calculator
    bool   tempoTriplets = false, tempoDotted = false;
    juce::String analyzerState;   // Spectrum Analyzer settings (XML)

    juce::String source;          // last used source key
    int    chL = 0, chR = 1;
    juce::String windowState;

    static const juce::StringArray& freqPresetNames()
    {
        static const juce::StringArray n { "20Hz - 20kHz", "20Hz - 10kHz", "20Hz - 5kHz", "20Hz - 2kHz", "20Hz - 1kHz",
                                           "50Hz - 20kHz", "100Hz - 20kHz", "200Hz - 20kHz", "1kHz - 20kHz" };
        return n;
    }
    std::pair<double, double> freqRange() const
    {
        static const double lo[] = { 20, 20, 20, 20, 20, 50, 100, 200, 1000 };
        static const double hi[] = { 20000, 10000, 5000, 2000, 1000, 20000, 20000, 20000, 20000 };
        const int i = juce::jlimit (0, 8, freqPreset);
        return { lo[i], hi[i] };
    }

    void load (juce::PropertiesFile& p)
    {
        channel    = p.getIntValue ("channel", channel);
        reassign   = p.getBoolValue ("reassign", reassign);
        fftSize    = p.getIntValue ("fftSize", fftSize);
        refLevel   = p.getDoubleValue ("refLevel", refLevel);
        range      = p.getDoubleValue ("range", range);
        compExp    = p.getDoubleValue ("compExp", compExp);
        colour     = p.getIntValue ("colour", colour);
        speed      = p.getDoubleValue ("speed", speed);
        freqPreset = p.getIntValue ("freqPreset", freqPreset);
        grid       = p.getIntValue ("grid", grid);
        shortRange = p.getBoolValue ("shortRange", shortRange);
        stationary = p.getBoolValue ("stationary", stationary);
        hidePanel  = p.getBoolValue ("hidePanel", hidePanel);
        showScale  = p.getBoolValue ("showScale", showScale);
        noteRef    = p.getIntValue ("noteRef", noteRef);
        showLufs   = p.getBoolValue ("showLufs", showLufs);
        gpu        = p.getBoolValue ("gpu", gpu);
        fpsCap     = p.getIntValue ("fpsCap", fpsCap);
        autoHideBar = p.getBoolValue ("autoHideBar", autoHideBar);
        viewMode   = p.getIntValue ("viewMode", viewMode);
        bpm        = p.getDoubleValue ("bpm", bpm);
        tempoTriplets = p.getBoolValue ("tempoTriplets", tempoTriplets);
        tempoDotted   = p.getBoolValue ("tempoDotted", tempoDotted);
        analyzerState = p.getValue ("analyzerState", analyzerState);
        source     = p.getValue ("source", source);
        chL        = p.getIntValue ("chL", chL);
        chR        = p.getIntValue ("chR", chR);
        windowState = p.getValue ("windowState", windowState);
    }

    void save (juce::PropertiesFile& p) const
    {
        p.setValue ("channel", channel);       p.setValue ("reassign", reassign);
        p.setValue ("fftSize", fftSize);       p.setValue ("refLevel", refLevel);
        p.setValue ("range", range);           p.setValue ("compExp", compExp);
        p.setValue ("colour", colour);         p.setValue ("speed", speed);
        p.setValue ("freqPreset", freqPreset); p.setValue ("grid", grid);
        p.setValue ("shortRange", shortRange); p.setValue ("stationary", stationary);
        p.setValue ("hidePanel", hidePanel);   p.setValue ("showScale", showScale);
        p.setValue ("noteRef", noteRef);       p.setValue ("source", source);
        p.setValue ("showLufs", showLufs);     p.setValue ("gpu", gpu);
        p.setValue ("fpsCap", fpsCap);     p.setValue ("autoHideBar", autoHideBar);
        p.setValue ("chL", chL);               p.setValue ("chR", chR);
        p.setValue ("windowState", windowState);
        p.setValue ("viewMode", viewMode);     p.setValue ("analyzerState", analyzerState);
        p.setValue ("bpm", bpm);               p.setValue ("tempoTriplets", tempoTriplets);
        p.setValue ("tempoDotted", tempoDotted);
        p.saveIfNeeded();
    }
};

// What kind of change happened, so the UI only does the work it needs to.
enum class Change { Visual, Overlay, Layout, Dsp, Bar };   // Bar: toolbar/bottom-bar only, the display is untouched
