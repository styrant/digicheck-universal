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
    int    fpsCap     = 60;       // frame-rate limit, 0 = follow the monitor (60 by default: smooth, and no wasted GPU work)
    int    textSize   = 1;        // 0 small, 1 medium (default), 2 large, 3 custom
    double textScaleCustom = 1.0;
    bool   autoHideBar = true;    // hide the top toolbar in full screen (reveal at the top edge)

    int    viewMode   = 0;        // 0 spectrogram, 1 analyzer, 2 balance, 3 key, 4 scope, 5 monitor
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
    int    keyListen     = 1;     // playback: 0 original, 1 mix, 2 transcription only
    int    keySynth      = 0;     // what the synth plays: 0 melody + chords, 1 all notes, 2 melody, 3 chords
    float  keyVolume     = 0.8f;
    int    keySensitivity = 70;   // 0..100, see transcribe/NoteFilter.h
    juce::String keyOutput;       // playback output device; empty = system default
    // Scope (oscilloscope, after Scyllascope)
    int    scopeMode     = 0;     // 0 free (sweep), 1 scroll
    int    scopeDivision = 2;     // 0 1/4, 1 1/2, 2 1, 3 2, 4 4, 5 8 bars
    bool   scopeTriplet  = false;
    double scopeGainDb   = 0.0;   // display gain
    bool   scopeSpectral = true, scopeAA = true, scopeGuides = true;
    bool   scopeBP       = false; double scopeBPHz = 100.0;
    bool   scopeSide     = false; // Side2SC overlay
    int    scopeSource   = 2;     // 0 left, 1 right, 2 mid, 3 peak
    juce::String scopeTheme = "Dark";
    int    scopePalette  = 0;     // spectral colours: 0 the theme's, see ScopeTheme::paletteNames()
    juce::String scopeColours;    // the user's colours on top of the theme ("waveColor=#rrggbb;...")
    bool   scopeRecNormalize = false, scopeRecTrim = true, scopeRecSnap = false;
    // Analyzer FFT style (SPAN / Pro-Q-like curve)
    int    anaStyle      = 0;     // 0 bars (Spectrum Analyzer 2), 1 FFT curve
    int    fftOrder      = 13;    // 11..15: 2048..32768 points
    int    fftChannel    = 0;     // fft::Channel
    int    fftSecondary  = 0;     // 0 none, 1 left, 2 right, 3 mid, 4 side, 5 sidechain (VST3)
    double fftSlope      = 4.5;   // dB per octave, pivot 1 kHz
    double fftAvgMs      = 300;   // 0 real time
    int    fftSmooth     = 0;     // 0 off, 1 1/24, 2 1/12, 3 1/6, 4 1/3 octave
    bool   fftHold       = true;
    double fftTop        = 6, fftBottom = -90;
    juce::String fftColour = "#5aa9ff", fftSecColour = "#ff8a3d";
    int    fftSecStyle   = 0;     // 0 line, 1 filled, 2 dashed

    juce::String monitorOrder = "0,1,2,3,4";   // module order: loudness, dynamics, levels, sound field, phase
    int    monitorShown  = 31;    // bit per module
    int    monitorWide   = 3;     // bit per module: spans the whole row (loudness, dynamics)
    int    monitorMax    = -1;    // the maximised module, -1 none
    int    monitorCols   = 0;     // 0 auto, 1..3
    int    monitorTarget = 0;     // see MonitorPage.cpp kTargets
    bool   monitorMomMax = false; // momentary readout shows its maximum
    bool   monitorMS     = false; // levels: add Mid and Side bars
    int    monitorRange  = 0;     // levels scale: 0 -60, 1 -40, 2 -90 dB
    bool   monitorAes    = false; // RMS + 3 dB (AES17: a sine reads its peak)
    int    monitorField  = 0;     // 0 polar sample, 1 polar level, 2 lissajous
    int    monitorHistory = 1;    // 0 30 s, 1 1 min, 2 5 min
    bool   monitorDynLoud = true; // dynamics history: short-term loudness line
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
        if (p.getIntValue ("settingsVersion", 1) < 2 && fpsCap == 0) fpsCap = 60;   // v2: 60 fps became the default
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
        keyListen     = juce::jlimit (0, 2, p.getIntValue ("keyListen", keyListen));
        keySynth      = juce::jlimit (0, 3, p.getIntValue ("keySynth", keySynth));
        keyVolume     = juce::jlimit (0.0f, 1.0f, (float) p.getDoubleValue ("keyVolume", keyVolume));
        keySensitivity = juce::jlimit (0, 100, p.getIntValue ("keySensitivity", keySensitivity));
        keyOutput     = p.getValue ("keyOutput", keyOutput);
        scopeMode     = juce::jlimit (0, 1, p.getIntValue ("scopeMode", scopeMode));
        scopeDivision = juce::jlimit (0, 5, p.getIntValue ("scopeDivision", scopeDivision));
        scopeTriplet  = p.getBoolValue ("scopeTriplet", scopeTriplet);
        scopeGainDb   = juce::jlimit (-24.0, 24.0, p.getDoubleValue ("scopeGainDb", scopeGainDb));
        scopeSpectral = p.getBoolValue ("scopeSpectral", scopeSpectral);
        scopeAA       = p.getBoolValue ("scopeAA", scopeAA);
        scopeGuides   = p.getBoolValue ("scopeGuides", scopeGuides);
        scopeBP       = p.getBoolValue ("scopeBP", scopeBP);
        scopeBPHz     = juce::jlimit (20.0, 20000.0, p.getDoubleValue ("scopeBPHz", scopeBPHz));
        scopeSide     = p.getBoolValue ("scopeSide", scopeSide);
        scopeSource   = juce::jlimit (0, 3, p.getIntValue ("scopeSource", scopeSource));
        scopeTheme    = p.getValue ("scopeTheme", scopeTheme);
        scopePalette  = juce::jlimit (0, 7, p.getIntValue ("scopePalette", scopePalette));
        scopeColours  = p.getValue ("scopeColours", scopeColours);
        scopeRecNormalize = p.getBoolValue ("scopeRecNormalize", scopeRecNormalize);
        scopeRecTrim  = p.getBoolValue ("scopeRecTrim", scopeRecTrim);
        scopeRecSnap  = p.getBoolValue ("scopeRecSnap", scopeRecSnap);
        anaStyle      = juce::jlimit (0, 1, p.getIntValue ("anaStyle", anaStyle));
        fftOrder      = juce::jlimit (11, 15, p.getIntValue ("fftOrder", fftOrder));
        fftChannel    = juce::jlimit (0, 4, p.getIntValue ("fftChannel", fftChannel));
        fftSecondary  = juce::jlimit (0, 5, p.getIntValue ("fftSecondary", fftSecondary));
        fftSlope      = juce::jlimit (0.0, 6.0, p.getDoubleValue ("fftSlope", fftSlope));
        fftAvgMs      = juce::jlimit (0.0, 10000.0, p.getDoubleValue ("fftAvgMs", fftAvgMs));
        fftSmooth     = juce::jlimit (0, 4, p.getIntValue ("fftSmooth", fftSmooth));
        fftHold       = p.getBoolValue ("fftHold", fftHold);
        fftTop        = juce::jlimit (-60.0, 30.0, p.getDoubleValue ("fftTop", fftTop));
        fftBottom     = juce::jlimit (-150.0, fftTop - 12, p.getDoubleValue ("fftBottom", fftBottom));
        fftColour     = p.getValue ("fftColour", fftColour);
        fftSecColour  = p.getValue ("fftSecColour", fftSecColour);
        fftSecStyle   = juce::jlimit (0, 2, p.getIntValue ("fftSecStyle", fftSecStyle));
        monitorOrder  = p.getValue ("monitorOrder", monitorOrder);
        monitorShown  = p.getIntValue ("monitorShown", monitorShown) & 31;
        monitorWide   = p.getIntValue ("monitorWide", monitorWide) & 31;
        monitorMax    = juce::jlimit (-1, 4, p.getIntValue ("monitorMax", monitorMax));
        monitorCols   = juce::jlimit (0, 3, p.getIntValue ("monitorCols", monitorCols));
        monitorTarget = juce::jlimit (0, 6, p.getIntValue ("monitorTarget", monitorTarget));
        monitorMomMax = p.getBoolValue ("monitorMomMax", monitorMomMax);
        monitorMS     = p.getBoolValue ("monitorMS", monitorMS);
        monitorRange  = juce::jlimit (0, 2, p.getIntValue ("monitorRange", monitorRange));
        monitorAes    = p.getBoolValue ("monitorAes", monitorAes);
        monitorField  = juce::jlimit (0, 2, p.getIntValue ("monitorField", monitorField));
        monitorHistory = juce::jlimit (0, 2, p.getIntValue ("monitorHistory", monitorHistory));
        monitorDynLoud = p.getBoolValue ("monitorDynLoud", monitorDynLoud);
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
        p.setValue ("settingsVersion", 2);
        p.setValue ("textSize", textSize);     p.setValue ("textScaleCustom", textScaleCustom);     p.setValue ("autoHideBar", autoHideBar);
        p.setValue ("chL", chL);               p.setValue ("chR", chR);
        p.setValue ("windowState", windowState);
        p.setValue ("viewMode", viewMode);     p.setValue ("analyzerState", analyzerState);
        p.setValue ("balanceTarget", balanceTarget); p.setValue ("balanceWindow2", balanceWindow); p.setValue ("balanceMode", balanceMode);
        p.setValue ("keyModel", keyModel); p.setValue ("keyDevice", keyDevice); p.setValue ("keyInstruments", keyInstruments);
        p.setValue ("keyAuto", keyAuto); p.setValue ("keySevenths", keySevenths); p.setValue ("keyShowNotes", keyShowNotes);
        p.setValue ("keyListen", keyListen); p.setValue ("keySynth", keySynth); p.setValue ("keyVolume", keyVolume);
        p.setValue ("keySensitivity", keySensitivity); p.setValue ("keyOutput", keyOutput);
        p.setValue ("scopeMode", scopeMode); p.setValue ("scopeDivision", scopeDivision); p.setValue ("scopeTriplet", scopeTriplet);
        p.setValue ("scopeGainDb", scopeGainDb); p.setValue ("scopeSpectral", scopeSpectral); p.setValue ("scopeAA", scopeAA);
        p.setValue ("scopeGuides", scopeGuides); p.setValue ("scopeBP", scopeBP); p.setValue ("scopeBPHz", scopeBPHz);
        p.setValue ("scopeSide", scopeSide); p.setValue ("scopeSource", scopeSource); p.setValue ("scopeTheme", scopeTheme);
        p.setValue ("scopePalette", scopePalette); p.setValue ("scopeColours", scopeColours);
        p.setValue ("scopeRecNormalize", scopeRecNormalize); p.setValue ("scopeRecTrim", scopeRecTrim); p.setValue ("scopeRecSnap", scopeRecSnap);
        p.setValue ("anaStyle", anaStyle); p.setValue ("fftOrder", fftOrder); p.setValue ("fftChannel", fftChannel);
        p.setValue ("fftSecondary", fftSecondary); p.setValue ("fftSlope", fftSlope); p.setValue ("fftAvgMs", fftAvgMs);
        p.setValue ("fftSmooth", fftSmooth); p.setValue ("fftHold", fftHold); p.setValue ("fftTop", fftTop); p.setValue ("fftBottom", fftBottom);
        p.setValue ("fftColour", fftColour); p.setValue ("fftSecColour", fftSecColour); p.setValue ("fftSecStyle", fftSecStyle);
        p.setValue ("monitorOrder", monitorOrder); p.setValue ("monitorShown", monitorShown); p.setValue ("monitorWide", monitorWide);
        p.setValue ("monitorMax", monitorMax); p.setValue ("monitorCols", monitorCols); p.setValue ("monitorTarget", monitorTarget);
        p.setValue ("monitorMomMax", monitorMomMax); p.setValue ("monitorMS", monitorMS); p.setValue ("monitorRange", monitorRange);
        p.setValue ("monitorAes", monitorAes); p.setValue ("monitorField", monitorField); p.setValue ("monitorHistory", monitorHistory);
        p.setValue ("monitorDynLoud", monitorDynLoud);
        p.setValue ("bpm", bpm);               p.setValue ("tempoTriplets", tempoTriplets);
        p.setValue ("tempoDotted", tempoDotted);
        if (auto* file = dynamic_cast<juce::PropertiesFile*> (&p)) file->saveIfNeeded();     // the app writes its file at once
    }
};

// What kind of change happened, so the UI only does the work it needs to.
enum class Change { Visual, Overlay, Layout, Dsp, Bar };   // Bar: toolbar/bottom-bar only, the display is untouched
