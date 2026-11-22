// Tonal balance targets, read at run time from Documents\DigiCheck Universal\Targets (nothing is built
// into the app). Reads the files Target Builder writes and Tonal Balance Control-style target files,
// including the variant that wraps every value as {"Type": ..., "Value": ...}.
#pragma once
#include <JuceHeader.h>
#include "TonalAnalysis.h"

class TargetLibrary
{
public:
    struct Entry
    {
        juce::String name;
        juce::File file;
        tonal::Target target;
    };

    static juce::File folder();
    bool rescanIfChanged();                          // true if the list changed
    void rescan();
    const std::vector<Entry>& entries() const noexcept { return list; }
    int indexOf (const juce::String& name) const;
    juce::StringArray skipped() const { return problems; }

    // parses one target file; returns false (with a reason) if it isn't a usable target
    static bool load (const juce::File& f, Entry& out, juce::String& error);
    static bool parse (const juce::var& json, tonal::Target& out, juce::String& name, juce::String& error);

private:
    std::vector<Entry> list;
    juce::StringArray problems;
    juce::String signature;                          // file names + sizes + times, to notice changes
    bool scanned = false;
    juce::String currentSignature() const;
};
