#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/FilterBank.h"
#include "dsp/AnalyzerModel.h"

namespace sa::params
{

// Parameter IDs. Never rename these once released - hosts store them.
inline constexpr const char* channel   = "channel";
inline constexpr const char* bpo       = "bpo";
inline constexpr const char* anchor    = "anchor";
inline constexpr const char* peakOn    = "peakOn";
inline constexpr const char* peakDecay = "peakDecay";
inline constexpr const char* holdOn    = "holdOn";
inline constexpr const char* holdInf   = "holdInf";
inline constexpr const char* holdTime  = "holdTime";
inline constexpr const char* holdDecay = "holdDecay";
inline constexpr const char* holdType  = "holdType";
inline constexpr const char* holdSize  = "holdSize";
inline constexpr const char* avgOn     = "avgOn";
inline constexpr const char* avgTime   = "avgTime";
inline constexpr const char* avgType   = "avgType";
inline constexpr const char* avgSize   = "avgSize";
inline constexpr const char* avgSlope  = "avgSlope";
inline constexpr const char* ceiling   = "ceiling";
inline constexpr const char* range     = "range";
inline constexpr const char* grid      = "grid";
inline constexpr const char* freqGrid  = "freqGrid";

enum class BarType  { Bars = 0, Curve, CurveFill };
enum class FreqGrid { Decade = 0, Octave, Notes };

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto id = [] (const char* s) { return ParameterID { s, 1 }; };
    auto seconds = AudioParameterFloatAttributes().withLabel ("s")
                       .withStringFromValueFunction ([] (float v, int) { return String (v, 1); });
    auto dbps = AudioParameterIntAttributes().withLabel ("dB/s");
    auto db   = AudioParameterIntAttributes().withLabel ("dB");
    auto px   = AudioParameterIntAttributes().withLabel ("px");
    const StringArray barTypes { "Bars", "Curve", "Curve Fill" };

    // On/off settings are two-state choices rather than AudioParameterBool so
    // that arbitrary host-set normalised values round-trip exactly through
    // the saved state (pluginval strictness 10).
    auto toggle = [&] (const char* pid, const char* name, bool def)
    {
        return std::make_unique<AudioParameterChoice> (id (pid), name, StringArray { "Off", "On" }, def ? 1 : 0);
    };

    StringArray bpoNames;
    for (auto b : kBandsPerOctaveChoices) bpoNames.add (String (b));

    p.push_back (std::make_unique<AudioParameterChoice> (id (channel), "Channel",
        StringArray { "Left", "Right", "Stereo (L+R)", "Mid", "Side" }, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (id (bpo), "Bands per Octave", bpoNames, 2));
    p.push_back (std::make_unique<AudioParameterChoice> (id (anchor), "Band Grid",
        StringArray { "1 kHz (ISO)", "A4 = 440 Hz (musical)" }, 0));

    p.push_back (toggle (peakOn, "Peak", true));
    p.push_back (std::make_unique<AudioParameterInt>  (id (peakDecay), "Peak Decay", 1, 60, 15, dbps));

    p.push_back (toggle (holdOn, "Peak Hold", true));
    p.push_back (toggle (holdInf, "Hold Infinite", false));
    p.push_back (std::make_unique<AudioParameterFloat> (id (holdTime), "Hold Time", NormalisableRange<float> (0.0f, 20.0f, 0.1f), 2.0f, seconds));
    p.push_back (std::make_unique<AudioParameterInt>   (id (holdDecay), "Hold Decay", 1, 60, 3, dbps));
    p.push_back (std::make_unique<AudioParameterChoice>(id (holdType), "Hold Style", barTypes, 1));
    p.push_back (std::make_unique<AudioParameterInt>   (id (holdSize), "Hold Size", 1, 4, 2, px));

    p.push_back (toggle (avgOn, "Average", true));
    p.push_back (std::make_unique<AudioParameterFloat> (id (avgTime), "Average Time", NormalisableRange<float> (0.1f, 20.0f, 0.1f), 3.0f, seconds));
    p.push_back (std::make_unique<AudioParameterChoice>(id (avgType), "Average Style", barTypes, 1));
    p.push_back (std::make_unique<AudioParameterInt>   (id (avgSize), "Average Size", 1, 4, 3, px));
    p.push_back (std::make_unique<AudioParameterFloat> (id (avgSlope), "Average Slope", NormalisableRange<float> (-3.0f, 6.0f, 0.5f), 0.0f,
        AudioParameterFloatAttributes().withLabel ("dB/oct").withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

    p.push_back (std::make_unique<AudioParameterInt>    (id (ceiling), "Level Ceiling", -80, 20, 3, db));
    p.push_back (std::make_unique<AudioParameterInt>    (id (range), "Level Range", 10, 120, 51, db));
    p.push_back (std::make_unique<AudioParameterInt>    (id (grid), "Level Grid", 2, 12, 5, db));
    p.push_back (std::make_unique<AudioParameterChoice> (id (freqGrid), "Frequency Grid",
        StringArray { "Decade", "Octave", "Notes" }, 0));

    return { p.begin(), p.end() };
}

// Convenience: read the current (plain) value of a parameter.
inline float get (const juce::AudioProcessorValueTreeState& s, const char* pid)
{
    return s.getRawParameterValue (pid)->load (std::memory_order_relaxed);
}

inline void set (juce::AudioProcessorValueTreeState& s, const char* pid, float plainValue)
{
    if (auto* p = s.getParameter (pid))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

inline int bandsPerOctave (const juce::AudioProcessorValueTreeState& s)
{
    const auto idx = juce::jlimit (0, (int) kBandsPerOctaveChoices.size() - 1, (int) get (s, bpo));
    return kBandsPerOctaveChoices[(size_t) idx];
}

inline MeterOptions meterOptions (const juce::AudioProcessorValueTreeState& s)
{
    MeterOptions o;
    o.peakDecayDbPerSec = get (s, peakDecay);
    o.holdInfinite      = get (s, holdInf) > 0.5f;
    o.holdTimeMs        = 1000.0 * get (s, holdTime);
    o.holdDecayDbPerSec = get (s, holdDecay);
    o.avgTimeMs         = 1000.0 * get (s, avgTime);
    o.avgSlopeDbPerOct  = get (s, avgSlope);
    return o;
}

} // namespace sa::params
