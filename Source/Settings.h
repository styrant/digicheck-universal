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
    int    hdrFullscreen = 0;     // 0 keep HDR: 1 px gap, 1 keep HDR: CPU rendering while full screen on an HDR monitor, 2 exact
    int    fpsCap     = 60;       // frame-rate limit, 0 = follow the monitor (60 by default: smooth, and no wasted GPU work)
    int    textSize   = 1;        // 0 small, 1 medium (default), 2 large, 3 custom
    double textScaleCustom = 1.0;
    bool   autoHideBar = true;    // hide the top toolbar in full screen (reveal at the top edge)

    int    viewMode   = 0;        // 0 spectrogram, 1 analyzer, 2 balance, 3 key, 4 scope, 5 monitor, 6 custom
    // Custom page: a layout of up to 4 panes, each showing one view (same numbers as viewMode, -1 empty)
    int    customLayout = 2;      // 0 two side by side, 1 two stacked, 2 large left + two right, 3 large top + two below,
                                  // 4 three columns, 5 grid 2 x 2
    juce::String customSlots = "0,5,4,1";
    double customSplitA = 0.62, customSplitB = 0.5;   // the main divider and the second one (0..1)
    // the Monitor's own module layout while it is in the Custom page
    juce::String customMonOrder = "0,1,2,3,4,5";
    int    customMonShown = 5, customMonWide = 0, customMonMax = -1, customMonCols = 1;
    int    customFocus = -1;      // the view whose controls the bar shows on the Custom page (the pane last clicked)
    juce::String customName;      // the saved layout last loaded or saved ("" none)
    bool   helpMode = false;
    bool   checkUpdates = true;
    bool   colourSafe = false;
    // reference track (Ref): the file (reloaded at start), where it's drawn, loudness matching
    juce::String refFile;
    juce::String linkName;
    bool   mixBridge = false;     // app: also hear the DAW (UAV Bridge on the master) on top of the chosen input
    bool   bridgeSend = true;     // VST3: send what this instance sees to the app ("From DAW" input)        // VST3: another instance's track used as the comparison signal ("" none)
    bool   refShowBalance = true, refShowFft = true, refShowMonitor = true, refMatch = true;    // colour-blind-friendly meter colours (ui::colourSafe)   // look for a newer Latest build at start-up      // bottom bar "?": descriptions when hovering over controls
    juce::String balanceTarget;   // name of the tonal balance target
    int    balanceWindow = 3;     // 0: realtime, 1: 1 s, 2: 3 s, 3: 10 s, 4: 30 s, 5: since reset
    int    balanceMode   = 0;     // 0 curve, 1 difference
    int    balanceSmooth = 0;     // 0 Smooth (1 oct), 1 Medium (1/2), 2 Detailed (1/3), 3 Fine (1/6)
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
    int    scopeBPType   = 0;     // the BP button's filter: 0 band-pass, 1 low-pass, 2 high-pass
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
    int    fftMarkerWidth = 1;    // the hover / pinned marker's lines, 1-4 px

    // VST3 version
    juce::String listenSplits = "60,250,2000,6000";   // band listening crossover points (Hz)
    bool   scopeSync     = true;  // Scope follows the host's transport (Sync) when it can
    bool   scopeScInput  = true;  // the Scope's overlay shows the sidechain input (else Side, L-R)
    int    scopeScStyle  = 0;     // overlay: 0 filled, 1 outline
    bool   specSc        = true;  // spectrogram: draw the sidechain
    int    specScStyle   = 0;     // 0 tint, 1 contour lines
    juce::String specScColour = "#ff8a3d";
    double specScOpacity = 0.75;
    int    editorW = 1300, editorH = 760;
    // Monitor (modular metering page)
    juce::String monitorText = "1,1,1,1,1,1";    // text size per module (x the Monitor's default)
    // loudness targets (Insight-style): preset index (see MonitorPage.cpp kTargets), or Custom / none
    double monitorTargetLufs = -14, monitorTolerance = 1, monitorPeakTarget = -1, monitorLraLo = 0, monitorLraHi = 30;
    int    monitorGate   = 1;     // 0 off (BS.1770-1), 1 program (BS.1770-2/3/4)
    int    monitorLoudRange = 1;  // loudness bars: 0 dB linear, 1 dB non-linear, 2 BS.1771, 3 EBU +9, 4 EBU +18
    bool   monitorRelative = false;   // loudness bars and History in LU relative to the target
    bool   monitorResetOnPlay = false; // VST3: restart the loudness when playback starts
    int    monitorHoldTime = 2;   // levels peak hold: 0 0.25 s, 1 0.5 s, 2 1.5 s, 3 5 s, 4 until reset
    int    monitorRmsTime = 1;    // levels RMS integration: 0 50 ms, 1 300 ms, 2 1 s, 3 3 s
    int    monitorHistTraces = 7; // History: bit 0 momentary, 1 short-term, 2 integrated
    int    monitorHistLen = 3;    // History: 0 5 s, 1 10 s, 2 30 s, 3 1 min, 4 5 min, 5 10 min
    int    monitorPoints = 1;     // Sound Field point brightness: 0 subtle, 1 normal, 2 bright
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
    int    monitorHistory = 3;    // Dynamics / Phase history: same lengths as monitorHistLen
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
        hdrFullscreen = juce::jlimit (0, 2, p.getIntValue ("hdrFullscreen", hdrFullscreen));
        if (p.getIntValue ("settingsVersion", 1) < 2 && fpsCap == 0) fpsCap = 60;   // v2: 60 fps became the default
        textSize   = p.getIntValue ("textSize", textSize);
        textScaleCustom = p.getDoubleValue ("textScaleCustom", textScaleCustom);
        autoHideBar = p.getBoolValue ("autoHideBar", autoHideBar);
        viewMode   = p.getIntValue ("viewMode", viewMode);
        balanceTarget = p.getValue ("balanceTarget", balanceTarget);
        balanceWindow = p.getIntValue ("balanceWindow2", p.containsKey ("balanceWindow") ? juce::jlimit (2, 5, p.getIntValue ("balanceWindow") + 2) : balanceWindow);   // old list had no Realtime / 1 s
        balanceMode   = p.getIntValue ("balanceMode", balanceMode);
        balanceSmooth = juce::jlimit (0, 3, p.getIntValue ("balanceSmooth", balanceSmooth));
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
        scopeBPType   = juce::jlimit (0, 2, p.getIntValue ("scopeBPType", scopeBPType));
        scopeSide     = p.getBoolValue ("scopeSide", scopeSide);
        scopeSource   = juce::jlimit (0, 3, p.getIntValue ("scopeSource", scopeSource));
        scopeTheme    = p.getValue ("scopeTheme", scopeTheme);
        scopePalette  = juce::jlimit (0, 7, p.getIntValue ("scopePalette", scopePalette));
        scopeColours  = p.getValue ("scopeColours", scopeColours);
        scopeRecNormalize = p.getBoolValue ("scopeRecNormalize", scopeRecNormalize);
        scopeRecTrim  = p.getBoolValue ("scopeRecTrim", scopeRecTrim);
        scopeRecSnap  = p.getBoolValue ("scopeRecSnap", scopeRecSnap);
        listenSplits  = p.getValue ("listenSplits", listenSplits);
        scopeSync     = p.getBoolValue ("scopeSync", scopeSync);
        scopeScInput  = p.getBoolValue ("scopeScInput", scopeScInput);
        scopeScStyle  = juce::jlimit (0, 1, p.getIntValue ("scopeScStyle", scopeScStyle));
        specSc        = p.getBoolValue ("specSc", specSc);
        specScStyle   = juce::jlimit (0, 1, p.getIntValue ("specScStyle", specScStyle));
        specScColour  = p.getValue ("specScColour", specScColour);
        specScOpacity = juce::jlimit (0.1, 1.0, p.getDoubleValue ("specScOpacity", specScOpacity));
        editorW       = juce::jlimit (900, 6000, p.getIntValue ("editorW", editorW));
        editorH       = juce::jlimit (520, 4000, p.getIntValue ("editorH", editorH));
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
        fftMarkerWidth = juce::jlimit (1, 4, p.getIntValue ("fftMarkerWidth", fftMarkerWidth));
        monitorText   = p.getValue ("monitorText", monitorText);
        monitorTargetLufs = juce::jlimit (-40.0, -5.0, p.getDoubleValue ("monitorTargetLufs", monitorTargetLufs));
        monitorTolerance  = juce::jlimit (0.0, 5.0, p.getDoubleValue ("monitorTolerance", monitorTolerance));
        monitorPeakTarget = juce::jlimit (-12.0, 0.0, p.getDoubleValue ("monitorPeakTarget", monitorPeakTarget));
        monitorLraLo  = juce::jlimit (0.0, 30.0, p.getDoubleValue ("monitorLraLo", monitorLraLo));
        monitorLraHi  = juce::jlimit (monitorLraLo, 30.0, p.getDoubleValue ("monitorLraHi", monitorLraHi));
        monitorGate   = juce::jlimit (0, 1, p.getIntValue ("monitorGate", monitorGate));
        monitorLoudRange = juce::jlimit (0, 4, p.getIntValue ("monitorLoudRange", monitorLoudRange));
        monitorRelative = p.getBoolValue ("monitorRelative", monitorRelative);
        monitorResetOnPlay = p.getBoolValue ("monitorResetOnPlay", monitorResetOnPlay);
        monitorHoldTime = juce::jlimit (0, 4, p.getIntValue ("monitorHoldTime", monitorHoldTime));
        monitorRmsTime = juce::jlimit (0, 3, p.getIntValue ("monitorRmsTime", monitorRmsTime));
        monitorHistTraces = p.getIntValue ("monitorHistTraces", monitorHistTraces) & 7;
        monitorHistLen = juce::jlimit (0, 5, p.getIntValue ("monitorHistLen", monitorHistLen));
        monitorPoints = juce::jlimit (0, 2, p.getIntValue ("monitorPoints", monitorPoints));
        monitorOrder  = p.getValue ("monitorOrder", monitorOrder);
        customLayout  = juce::jlimit (0, 5, p.getIntValue ("customLayout", customLayout));
        customSlots   = p.getValue ("customSlots", customSlots);
        customSplitA  = juce::jlimit (0.15, 0.85, p.getDoubleValue ("customSplitA", customSplitA));
        customSplitB  = juce::jlimit (0.15, 0.85, p.getDoubleValue ("customSplitB", customSplitB));
        customMonOrder = p.getValue ("customMonOrder", customMonOrder);
        customMonShown = p.getIntValue ("customMonShown", customMonShown) & 63;
        customMonWide  = p.getIntValue ("customMonWide", customMonWide) & 63;
        customMonMax   = juce::jlimit (-1, 5, p.getIntValue ("customMonMax", customMonMax));
        customMonCols  = juce::jlimit (0, 3, p.getIntValue ("customMonCols", customMonCols));
        helpMode = p.getBoolValue ("helpMode", helpMode);
        checkUpdates = p.getBoolValue ("checkUpdates", checkUpdates);
        colourSafe = p.getBoolValue ("colourSafe", colourSafe);
        refFile = p.getValue ("refFile", refFile);
        linkName = p.getValue ("linkName", linkName);
        bridgeSend = p.getBoolValue ("bridgeSend", bridgeSend);
        mixBridge = p.getBoolValue ("mixBridge", mixBridge);
        refShowBalance = p.getBoolValue ("refShowBalance", refShowBalance);
        refShowFft = p.getBoolValue ("refShowFft", refShowFft);
        refShowMonitor = p.getBoolValue ("refShowMonitor", refShowMonitor);
        refMatch = p.getBoolValue ("refMatch", refMatch);
        customFocus = juce::jlimit (-1, 5, p.getIntValue ("customFocus", customFocus));
        customName = p.getValue ("customName", customName);
        monitorShown  = p.getIntValue ("monitorShown", monitorShown) & 63;
        monitorWide   = p.getIntValue ("monitorWide", monitorWide) & 63;
        monitorMax    = juce::jlimit (-1, 5, p.getIntValue ("monitorMax", monitorMax));
        monitorCols   = juce::jlimit (0, 3, p.getIntValue ("monitorCols", monitorCols));
        monitorTarget = juce::jlimit (0, 16, p.getIntValue ("monitorTarget", monitorTarget));
        monitorMomMax = p.getBoolValue ("monitorMomMax", monitorMomMax);
        monitorMS     = p.getBoolValue ("monitorMS", monitorMS);
        monitorRange  = juce::jlimit (0, 2, p.getIntValue ("monitorRange", monitorRange));
        monitorAes    = p.getBoolValue ("monitorAes", monitorAes);
        monitorField  = juce::jlimit (0, 2, p.getIntValue ("monitorField", monitorField));
        monitorHistory = juce::jlimit (0, 5, p.getIntValue ("monitorHistory", monitorHistory));
        monitorDynLoud = p.getBoolValue ("monitorDynLoud", monitorDynLoud);
        if (p.getIntValue ("settingsVersion", 1) < 3)        // v3: the History module (shown, full width), more targets
        {
            monitorShown |= 32; monitorWide |= 32;
            if (monitorTarget == 6) monitorTarget = 16;      // "No target" moved to the end of the list
        }
        if (p.getIntValue ("settingsVersion", 1) < 4)        // v4: 5 s and 10 s history lengths added in front
        {
            if (p.containsKey ("monitorHistLen")) monitorHistLen = juce::jlimit (0, 5, monitorHistLen + 2);
            if (p.containsKey ("monitorHistory")) monitorHistory = juce::jlimit (0, 5, monitorHistory + 2);
        }
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
        p.setValue ("fpsCap", fpsCap); p.setValue ("hdrFullscreen", hdrFullscreen);
        p.setValue ("settingsVersion", 4);
        p.setValue ("textSize", textSize);     p.setValue ("textScaleCustom", textScaleCustom);     p.setValue ("autoHideBar", autoHideBar);
        p.setValue ("chL", chL);               p.setValue ("chR", chR);
        p.setValue ("windowState", windowState);
        p.setValue ("viewMode", viewMode);     p.setValue ("analyzerState", analyzerState);
        p.setValue ("balanceTarget", balanceTarget); p.setValue ("balanceWindow2", balanceWindow); p.setValue ("balanceMode", balanceMode); p.setValue ("balanceSmooth", balanceSmooth);
        p.setValue ("keyModel", keyModel); p.setValue ("keyDevice", keyDevice); p.setValue ("keyInstruments", keyInstruments);
        p.setValue ("keyAuto", keyAuto); p.setValue ("keySevenths", keySevenths); p.setValue ("keyShowNotes", keyShowNotes);
        p.setValue ("keyListen", keyListen); p.setValue ("keySynth", keySynth); p.setValue ("keyVolume", keyVolume);
        p.setValue ("keySensitivity", keySensitivity); p.setValue ("keyOutput", keyOutput);
        p.setValue ("scopeMode", scopeMode); p.setValue ("scopeDivision", scopeDivision); p.setValue ("scopeTriplet", scopeTriplet);
        p.setValue ("scopeGainDb", scopeGainDb); p.setValue ("scopeSpectral", scopeSpectral); p.setValue ("scopeAA", scopeAA);
        p.setValue ("scopeGuides", scopeGuides); p.setValue ("scopeBP", scopeBP); p.setValue ("scopeBPHz", scopeBPHz); p.setValue ("scopeBPType", scopeBPType);
        p.setValue ("scopeSide", scopeSide); p.setValue ("scopeSource", scopeSource); p.setValue ("scopeTheme", scopeTheme);
        p.setValue ("scopePalette", scopePalette); p.setValue ("scopeColours", scopeColours);
        p.setValue ("scopeRecNormalize", scopeRecNormalize); p.setValue ("scopeRecTrim", scopeRecTrim); p.setValue ("scopeRecSnap", scopeRecSnap);
        p.setValue ("listenSplits", listenSplits); p.setValue ("scopeSync", scopeSync); p.setValue ("scopeScInput", scopeScInput);
        p.setValue ("scopeScStyle", scopeScStyle); p.setValue ("specSc", specSc); p.setValue ("specScStyle", specScStyle);
        p.setValue ("specScColour", specScColour); p.setValue ("specScOpacity", specScOpacity);
        p.setValue ("editorW", editorW); p.setValue ("editorH", editorH);
        p.setValue ("anaStyle", anaStyle); p.setValue ("fftOrder", fftOrder); p.setValue ("fftChannel", fftChannel);
        p.setValue ("fftSecondary", fftSecondary); p.setValue ("fftSlope", fftSlope); p.setValue ("fftAvgMs", fftAvgMs);
        p.setValue ("fftSmooth", fftSmooth); p.setValue ("fftHold", fftHold); p.setValue ("fftTop", fftTop); p.setValue ("fftBottom", fftBottom);
        p.setValue ("fftColour", fftColour); p.setValue ("fftSecColour", fftSecColour); p.setValue ("fftSecStyle", fftSecStyle); p.setValue ("fftMarkerWidth", fftMarkerWidth);
        p.setValue ("monitorText", monitorText);
        p.setValue ("monitorTargetLufs", monitorTargetLufs); p.setValue ("monitorTolerance", monitorTolerance); p.setValue ("monitorPeakTarget", monitorPeakTarget);
        p.setValue ("monitorLraLo", monitorLraLo); p.setValue ("monitorLraHi", monitorLraHi); p.setValue ("monitorGate", monitorGate);
        p.setValue ("monitorLoudRange", monitorLoudRange); p.setValue ("monitorRelative", monitorRelative); p.setValue ("monitorResetOnPlay", monitorResetOnPlay);
        p.setValue ("monitorHoldTime", monitorHoldTime); p.setValue ("monitorRmsTime", monitorRmsTime);
        p.setValue ("monitorHistTraces", monitorHistTraces); p.setValue ("monitorHistLen", monitorHistLen); p.setValue ("monitorPoints", monitorPoints);
        p.setValue ("monitorOrder", monitorOrder);
        p.setValue ("customLayout", customLayout); p.setValue ("customSlots", customSlots);
        p.setValue ("customSplitA", customSplitA); p.setValue ("customSplitB", customSplitB);
        p.setValue ("customMonOrder", customMonOrder); p.setValue ("customMonShown", customMonShown); p.setValue ("customMonWide", customMonWide);
        p.setValue ("customMonMax", customMonMax); p.setValue ("customMonCols", customMonCols); p.setValue ("helpMode", helpMode); p.setValue ("checkUpdates", checkUpdates); p.setValue ("colourSafe", colourSafe);
        p.setValue ("refFile", refFile); p.setValue ("linkName", linkName); p.setValue ("bridgeSend", bridgeSend); p.setValue ("mixBridge", mixBridge); p.setValue ("refShowBalance", refShowBalance); p.setValue ("refShowFft", refShowFft);
        p.setValue ("refShowMonitor", refShowMonitor); p.setValue ("refMatch", refMatch);
        p.setValue ("customFocus", customFocus); p.setValue ("customName", customName); p.setValue ("monitorShown", monitorShown); p.setValue ("monitorWide", monitorWide);
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
