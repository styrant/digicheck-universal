// TargetBuilder core, shared by the window app and the command-line mode: reading songs,
// measuring them, and writing a target (.json) with its report.
#pragma once
#include <JuceHeader.h>
#include "../balance/TonalAnalysis.h"

namespace targets
{
    struct TrackResult
    {
        juce::File file;
        std::vector<double> db;              // normalised spectrum (empty if skipped)
        double seconds = 0, analysed = 0;
        juce::String error;                  // why it was skipped
        bool ok() const { return error.isEmpty() && ! db.empty(); }
    };

    juce::File defaultFolder();                                   // Documents\Ultimate Audio Viewer\Targets
    void registerFormats (juce::AudioFormatManager& fm);
    TrackResult analyse (juce::AudioFormatManager& fm, const juce::File& f, const std::atomic<bool>* cancel = nullptr);
    juce::Array<juce::File> audioFilesIn (const juce::File& dir, juce::AudioFormatManager& fm, bool recursive);
    juce::String safeName (juce::String s);

    // distance of each track from the target, and which ones are outliers
    struct Deviations { std::vector<double> db; std::vector<bool> outlier; };
    Deviations deviations (const std::vector<std::vector<double>>& spectra, const tonal::Target& t);

    struct Written { juce::File json, report; int songs = 0, outliers = 0; juce::String error; };
    // writes <name>.json and "<name> - report.txt" into outDir from the usable tracks
    // wide: the range is never narrower than Tonal Balance Control's typical range (see tonal::combine)
    Written write (const juce::String& name, const std::vector<const TrackResult*>& tracks,
                   const juce::File& outDir, double pLo = 10, double pHi = 90, bool wide = true);
}
