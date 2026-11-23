// Everything that listens to the stream and must outlive a window: the settings, the analysers and
// the taps the pages read from. The standalone app's window owns one; in the VST3 version the
// processor owns one per instance and lends it to its editor, which comes and goes with the plugin
// window while the analysis (loudness, key, balance...) keeps running.
//
// The taps are registered with an AnalysisEngine (fed from its DSP thread). In the plugin the Scope and
// FFT taps are fed straight from processBlock instead, together with the sidechain and the host's
// musical position (FifoTap extras), so registerWith() leaves them out there.
#pragma once
#include <JuceHeader.h>
#include "Settings.h"
#include "FifoTap.h"
#include "AnalysisEngine.h"
#include "analyzer/PluginProcessor.h"
#include "key/KeyFinder.h"
#include "balance/LiveBalance.h"
#include "balance/TargetLibrary.h"
#include "transcribe/LiveRecorder.h"
#include "transcribe/TranscriptionSession.h"
#include "transcribe/ModelStore.h"
#include "transcribe/TranscriptionPlayer.h"
#include "listen/BandIsolator.h"
#include "monitor/MonitorModel.h"

struct Core
{
    // state: where the settings live (the app's settings file, or the plugin instance's state)
    Core (juce::PropertySet& stateStore, bool pluginBuild)
        : state (stateStore), plugin (pluginBuild), scopeTap (pluginBuild), fftTap (pluginBuild)
    {
        settings.load (state);
        if (settings.analyzerState.isNotEmpty()) analyzerProc.restoreStateXml (settings.analyzerState);
    }

    void registerWith (AnalysisEngine& e)
    {
        e.setAnalyzer (&analyzerProc);
        e.addTap (&keyFinder);
        e.addTap (&liveBalance);
        e.addTap (&recorder);
        e.addTap (&monitorTap);
        if (! plugin) { e.addTap (&scopeTap); e.addTap (&fftTap); }
    }

    // the plugin: the Monitor's measurements live here and are fed while no window is open
    monitor::MonitorModel monitorModel;
    void pumpMonitor()
    {
        if (std::abs (monitorTap.sampleRate() - monitorModel.sampleRate()) > 0.5) monitorModel.configure (monitorTap.sampleRate());
        monitorTap.drain ([this] (const float* l, const float* r, int n) { monitorModel.push (l, r, n); });
    }

    void save()
    {
        settings.analyzerState = analyzerProc.saveStateXml();
        settings.save (state);
    }

    juce::PropertySet& state;
    const bool plugin;
    Settings settings;
    SpectrumAnalyzerProcessor analyzerProc;
    KeyFinder keyFinder;
    LiveBalance liveBalance;
    LiveRecorder recorder;
    FifoTap scopeTap, monitorTap, fftTap;
    TranscriptionSession transcription;
    ModelStore modelStore;
    TranscriptionPlayer keyPlayer;
    PlaybackOutput keyOutput { keyPlayer };
    TargetLibrary targetLibrary;
};

// What the editor can ask of the plugin it lives in (nullptr in the standalone app)
struct HostLink
{
    virtual ~HostLink() = default;
    virtual bool isProcessing() const = 0;                  // audio has flowed recently
    virtual double hostBpm() const = 0;                     // 0 when the host doesn't say
    virtual bool hostPlaying() const = 0;
    virtual bool sidechainConnected() const = 0;            // the sidechain bus is enabled and carries audio
    virtual AnalysisEngine* sidechainEngine() = 0;          // the sidechain's spectrogram analysis
    virtual void setScopeSyncBars (double barsPerWindow) = 0; // the Scope's window in Sync (time signature from the host)
    virtual void setScopeListen (bool on, double hz) = 0;  // hear the Scope's band-pass
    virtual BandIsolatorParams& isolation() = 0;            // band listening (Listen panel)
    virtual juce::String describe() const = 0;              // for the Options panel: host, rate, buses
};
