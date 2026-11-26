// The VST3 version: an analysis plugin for one track, with the same views as the standalone app.
//
//   buses   main stereo (or mono) in and out; an optional stereo "Sidechain" input (the host routes
//           another track to it; mono works too)
//   audio   passes through unchanged (bit for bit, no latency) unless band listening is on
//   feeds   the shared AnalysisEngine (spectrogram, loudness, analyzer, key, balance, recorder, Monitor),
//           a second AnalysisEngine for the sidechain's spectrogram, and the Scope and FFT taps straight
//           from here with the sidechain and the Scope's sync phase (from the host's musical position)
//   tempo   the host's tempo becomes the app's tempo (the Tempo window, the Scope's bar lengths)
//
// The state (all the view settings) is saved in the host's project; presets and other app-wide things
// go to "DigiCheck Universal Plugin.settings" next to the app's settings.
#pragma once
#include <JuceHeader.h>
#include "../Core.h"
#include "../listen/BandIsolator.h"
#include "InstanceLink.h"
#include "../bridge/Bridge.h"

class DcuProcessor : public juce::AudioProcessor, public HostLink, private juce::Timer
{
public:
    DcuProcessor();
    ~DcuProcessor() override;

    void prepareToPlay (double sampleRate, int maxBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                  { return true; }
    const juce::String getName() const override      { return "Ultimate Audio Viewer"; }
    bool acceptsMidi() const override                { return false; }
    bool producesMidi() const override               { return false; }
    bool isMidiEffect() const override               { return false; }
    double getTailLengthSeconds() const override     { return 0.0; }
    int getNumPrograms() override                    { return 1; }
    int getCurrentProgram() override                 { return 0; }
    void setCurrentProgram (int) override            {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // HostLink (for the editor)
    bool isProcessing() const override;
    double hostBpm() const override                  { return bpm.load(); }
    bool hostPlaying() const override                { return playing.load(); }
    bool sidechainConnected() const override         { return scLive.load(); }
    AnalysisEngine* sidechainEngine() override       { return &scEngine; }
    void setScopeSyncBars (double bars) override     { syncBars = bars; }
    void setScopeListen (bool on, double hz, int type) override { iso.bandpassType = type; iso.bandpass = on; iso.bandpassHz = (float) hz; }
    BandIsolatorParams& isolation() override         { return iso; }
    juce::String describe() const override;
    juce::StringArray otherInstances() const override;
    void setLink (const juce::String& trackName) override;
    juce::String linkName() const override           { return core.settings.linkName; }
    bool linkLive() const override                   { return linkOk.load(); }
    void updateTrackProperties (const TrackProperties&) override;
    void setBridgeSend (bool on) override            { core.settings.bridgeSend = on; core.save(); bridgeSender.setEnabled (on); }
    bool bridgeSending() const override              { return core.settings.bridgeSend && bridgeSender.isSending(); }

    Core& getCore()                                  { return core; }
    AnalysisEngine& getEngine()                      { return engine; }
    juce::PropertiesFile& globalProps()              { return *global; }
    std::atomic<bool> stateRestored { false };       // the editor rebuilds its views when it sees this

private:
    // the Monitor keeps measuring while the window is closed (and restarts with playback if asked to)
    void timerCallback() override
    {
        resolveLink();
        const bool p = playing.load();
        if (getActiveEditor() == nullptr)
        {
            if (p && ! wasPlaying && core.settings.monitorResetOnPlay) { core.monitorModel.reset(); engine.resetLoudness(); }
            core.pumpMonitor();
        }
        wasPlaying = p;
    }
    bool wasPlaying = false;
    // instance linking: this instance's audio for the others, and the one linked to (swapped under linkLock;
    // the audio thread only try-locks it)
    std::shared_ptr<instancelink::Ring> ownRing = std::make_shared<instancelink::Ring>();
    int hubId = instancelink::Hub::get().add (ownRing);
    std::shared_ptr<instancelink::Ring> linked;
    juce::String linkedTo;
    juce::SpinLock linkLock;
    std::vector<float> linkL, linkR;
    std::atomic<bool> linkOk { false };
    bridge::Sender bridgeSender;                     // to the app (see bridge/Bridge.h)
    void resolveLink();
    std::unique_ptr<juce::PropertiesFile> global;
    juce::PropertySet state;                         // this instance's settings, saved in the project
    Core core { state, true };                       // before the engines: the taps outlive their DSP threads
    AnalysisEngine engine, scEngine;
    BandIsolatorParams iso;
    BandIsolator isolator;
    std::vector<float> phase, monoL, monoR;
    std::atomic<double> bpm { 0 }, syncBars { 1 }, lastBlockMs { 0 }, lastScSignalMs { 0 };
    std::atomic<bool> playing { false }, scLive { false };
    double sr = 48000;
    bool scPrepared = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DcuProcessor)
};
