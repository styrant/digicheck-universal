#pragma once
#include <JuceHeader.h>

// All user settings, mirroring DigiCheck NG's Spectroscope dialog, persisted between sessions.
struct Settings
{
    int    channel    = 2;        // 0 left, 1 right, 2 mid (L+R)/2, 3 side (L-R)/2
    int    freqScale  = 0;        // freqscale::Scale: 0 log, 1 linear, 2 ext log, 3 mel, 4 bark, 5 ERB
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
    int    textSize   = 1;        // 0 small, 1 medium (default), 2 large, 3 custom
    double textScaleCustom = 1.0;
    bool   autoHideBar = true;    // hide the top toolbar in full screen (reveal at the top edge)

    int    viewMode   = 0;        // 0 spectrogram, 1 analyzer, 2 balance, 3 key
    juce::String balanceTarget;   // name of the tonal balance target
    int    balanceWindow = 3;     // 0: realtime, 1: 1 s, 2: 3 s, 3: 10 s, 4: 30 s, 5: since reset
    int    balanceMode   = 0;     // 0 curve, 1 difference
    // Key page: chords & melody (transcription)
    int    keyModel      = 0;     // ModelStore::Size: 0 small, 1 medium, 2 large
    juce::String keyDevice;       // device name; empty = Auto
    juce::String keyInstruments;  // comma-separated instrument group ids; empty = all (the model decides)
    bool   keyAuto       = true;  // transcribe as soon as a file is loaded (when the model is installed)
    bool   keySevenths   = true;  // 7th and 6th chords (off: triads and sus only)
    bool   keyShowNotes  = true;  // all transcribed notes in the piano roll (melody is always shown)
    double bpm        = 120.0;    // tempo calculator
    bool   tempoTriplets = false, tempoDotted = false;
    juce::String analyzerState;   // Spectrum Analyzer settings (XML)

    juce::String source;          // last used source key
    int    chL = 0, chR = 1;
    juce::String windowState;

    static const juce::StringArray& channelNames()
    {
        static const juce::StringArray n { "Left", "Right", "Mid (L+R)", "Side (L-R)" };
        return n;
    }
    static const juce::StringArray& freqScaleNames()
    {
        static const juce::StringArray n { "Log", "Linear", "Ext Log", "Mel", "Bark", "ERB" };
        return n;
    }

    static const juce::StringArray& freqPresetNames()
    {
        static const juce::StringArray n { "20Hz - 20kHz", "20Hz - 10kHz", "20Hz - 5kHz", "20Hz - 2kHz", "20Hz - 1kHz",
                                           "50Hz - 20kHz", "100Hz - 20kHz", "200Hz - 20kHz", "1kHz - 20kHz",
                                           "5Hz - 20kHz", "10Hz - 20kHz", "2Hz - 200Hz", "5Hz - 1kHz" };
        return n;
    }
    std::pair<double, double> freqRange() const
    {
        static const double lo[] = { 20, 20, 20, 20, 20, 50, 100, 200, 1000, 5, 10, 2, 5 };
        static const double hi[] = { 20000, 10000, 5000, 2000, 1000, 20000, 20000, 20000, 20000, 20000, 20000, 200, 1000 };
        const int i = juce::jlimit (0, 12, freqPreset);
        return { lo[i], hi[i] };
    }

    // Any PropertySet: the app's settings file (juce::PropertiesFile), or in a plugin a PropertySet that
    // goes into the host's project via getStateInformation (PropertySet::createXml / restoreFromXml).
    void load (const juce::PropertySet& p)
    {
        channel    = p.getIntValue ("channel", channel);
        freqScale  = juce::jlimit (0, 5, p.getIntValue ("freqScale", freqScale));
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
        textSize   = p.getIntValue ("textSize", textSize);
        textScaleCustom = p.getDoubleValue ("textScaleCustom", textScaleCustom);
        autoHideBar = p.getBoolValue ("autoHideBar", autoHideBar);
        viewMode   = p.getIntValue ("viewMode", viewMode);
        balanceTarget = p.getValue ("balanceTarget", balanceTarget);
        balanceWindow = p.getIntValue ("balanceWindow2", p.containsKey ("balanceWindow") ? juce::jlimit (2, 5, p.getIntValue ("balanceWindow") + 2) : balanceWindow);   // old list had no Realtime / 1 s
        balanceMode   = p.getIntValue ("balanceMode", balanceMode);
        keyModel      = juce::jlimit (0, 2, p.getIntValue ("keyModel", keyModel));
        keyDevice     = p.getValue ("keyDevice", keyDevice);
        keyInstruments = p.getValue ("keyInstruments", keyInstruments);
        keyAuto       = p.getBoolValue ("keyAuto", keyAuto);
        keySevenths   = p.getBoolValue ("keySevenths", keySevenths);
        keyShowNotes  = p.getBoolValue ("keyShowNotes", keyShowNotes);
        bpm        = p.getDoubleValue ("bpm", bpm);
        tempoTriplets = p.getBoolValue ("tempoTriplets", tempoTriplets);
        tempoDotted   = p.getBoolValue ("tempoDotted", tempoDotted);
        analyzerState = p.getValue ("analyzerState", analyzerState);
        source     = p.getValue ("source", source);
        chL        = p.getIntValue ("chL", chL);
        chR        = p.getIntValue ("chR", chR);
        windowState = p.getValue ("windowState", windowState);
    }

    void save (juce::PropertySet& p) const
    {
        p.setValue ("channel", channel);       p.setValue ("freqScale", freqScale);       p.setValue ("reassign", reassign);
        p.setValue ("fftSize", fftSize);       p.setValue ("refLevel", refLevel);
        p.setValue ("range", range);           p.setValue ("compExp", compExp);
        p.setValue ("colour", colour);         p.setValue ("speed", speed);
        p.setValue ("freqPreset", freqPreset); p.setValue ("grid", grid);
        p.setValue ("shortRange", shortRange); p.setValue ("stationary", stationary);
        p.setValue ("hidePanel", hidePanel);   p.setValue ("showScale", showScale);
        p.setValue ("noteRef", noteRef);       p.setValue ("source", source);
        p.setValue ("showLufs", showLufs);     p.setValue ("gpu", gpu);
        p.setValue ("fpsCap", fpsCap);
        p.setValue ("textSize", textSize);     p.setValue ("textScaleCustom", textScaleCustom);     p.setValue ("autoHideBar", autoHideBar);
        p.setValue ("chL", chL);               p.setValue ("chR", chR);
        p.setValue ("windowState", windowState);
        p.setValue ("viewMode", viewMode);     p.setValue ("analyzerState", analyzerState);
        p.setValue ("balanceTarget", balanceTarget); p.setValue ("balanceWindow2", balanceWindow); p.setValue ("balanceMode", balanceMode);
        p.setValue ("keyModel", keyModel); p.setValue ("keyDevice", keyDevice); p.setValue ("keyInstruments", keyInstruments);
        p.setValue ("keyAuto", keyAuto); p.setValue ("keySevenths", keySevenths); p.setValue ("keyShowNotes", keyShowNotes);
        p.setValue ("bpm", bpm);               p.setValue ("tempoTriplets", tempoTriplets);
        p.setValue ("tempoDotted", tempoDotted);
        if (auto* file = dynamic_cast<juce::PropertiesFile*> (&p)) file->saveIfNeeded();     // the app writes its file at once
    }
};

// What kind of change happened, so the UI only does the work it needs to.
enum class Change { Visual, Overlay, Layout, Dsp, Bar };   // Bar: toolbar/bottom-bar only, the display is untouched
