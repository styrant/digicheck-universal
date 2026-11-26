#pragma once
// Adapted from Spectrum Analyzer 2 (MIT, see LICENSE in this folder) for use inside Spectroscope:
// the processor is no longer a plug-in. It keeps the parameters, colours, presets and the filter
// bank, and the app's DSP thread feeds it with processStereo() instead of a host calling processBlock().

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/FilterBank.h"
#include "dsp/LegacyPresets.h"

class SpectrumAnalyzerProcessor final : public juce::AudioProcessor
{
public:
    SpectrumAnalyzerProcessor();
    ~SpectrumAnalyzerProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    void processBlock (juce::AudioBuffer<float>&,  juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override { return true; }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }

    const juce::String getName() const override { return "Spectrum Analyzer"; }

    // Spectroscope integration (called from the app's DSP thread)
    void processStereo (const float* left, const float* right, int numSamples) noexcept;
    // Whole analyzer state (parameters, colours, view) as XML text, saved in the app's settings
    juce::String saveStateXml();
    void restoreStateXml (const juce::String& xml);
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // UI-facing API (message thread)
    using Ring = sa::SpscRing<sa::Snapshot, 64>;
    Ring& getRing() noexcept { return *ring; }

    // The editor tells the processor when it is visible; analysis is skipped
    // while no editor is open (saves CPU, and avoids stale data on open).
    void setUiActive (bool active) noexcept;
    double getAnalysisSampleRate() const noexcept { return sampleRate.load (std::memory_order_relaxed); }

    juce::AudioProcessorValueTreeState apvts;

    static constexpr int kNumColours = sa::legacy::ColorsCount;
    enum ColourIndex { PeakColour, HoldColour, AvgColour, BorderColour, LineColour, LabelColour, BkgTopColour, BkgBottomColour };

    juce::Colour getColour (int i) const noexcept { return juce::Colour ((juce::uint32) colours[(size_t) i].load (std::memory_order_relaxed)); }
    void setColour (int i, juce::Colour c) noexcept { colours[(size_t) i].store (c.getARGB(), std::memory_order_relaxed); }
    void applyColourScheme (const sa::legacy::ColourScheme& s);
    juce::String getColourSchemeName() const       { const juce::SpinLock::ScopedLockType l (nameLock); return colourSchemeName; }
    void setColourSchemeName (const juce::String& n) { const juce::SpinLock::ScopedLockType l (nameLock); colourSchemeName = n; }

    // View state (not host parameters; saved with the plugin state)
    std::atomic<double> viewLowHz { 0.0 }, viewHighHz { 0.0 };   // 0 = full range
    std::atomic<bool>   inspectorEnabled { true };
    std::atomic<int>    markerWidth { 1 };          // the inspector / pinned marker's crosshair, 1-4 px
    std::atomic<int>    editorWidth { 0 }, editorHeight { 0 };

    // Presets
    void applyLegacyPreset (const sa::legacy::ImportedPreset&, bool includeColours);
    bool importLegacyFile (const juce::File&, juce::String& errorMessage);
    void saveCurrentAsDefault();
    void forgetUserDefault();

private:
    template <typename T> void process (juce::AudioBuffer<T>&);
    void applyLegacyValues (const sa::legacy::Values&, bool includeColours);
    juce::ValueTree createStateTree();
    bool restoreFromTree (const juce::ValueTree&);
    std::unique_ptr<juce::PropertiesFile> openUserSettings() const;

    std::unique_ptr<sa::FilterBank> bank;
    std::unique_ptr<Ring> ring;
    std::atomic<double> sampleRate { 48000.0 };
    std::atomic<bool> uiActive { false }, resetRequested { true };
    int64_t publishInterval = 512;

    std::array<std::atomic<juce::uint32>, kNumColours> colours {};
    int currentProgram = 0;
    juce::String colourSchemeName { "Default" };
    juce::SpinLock nameLock;   // never taken on the audio thread

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzerProcessor)
};
