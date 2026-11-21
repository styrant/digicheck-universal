#pragma once
#include <JuceHeader.h>
#include "Settings.h"

// Spectrogram view presets: a few built-in views, plus the user's own (saved in the settings file).
// A preset only changes how the spectrogram looks and analyses: input, channel, note cursor,
// LUFS, text size and so on are left alone. Startup settings are not affected.
namespace specpresets
{
    struct Preset
    {
        juce::String name;
        bool   reassign   = true;
        int    fftSize    = 4096;
        double refLevel   = -20.0;
        double range      = 60.0;
        double compExp    = 0.0;
        int    colour     = 0;
        double speed      = 300.0;
        int    freqPreset = 0;
        int    grid       = 1;
        bool   shortRange = true;
        juce::String hint;              // tooltip-style description (built-ins only)
    };

    // colours: 0 blue-red-yellow-white, 1 green-yellow-white, 2 blue-cyan-white, 3 red-orange-white, 4 purple-pink-white, 5 grey-white
    // frequency: 0 20-20k, 1 20-10k, 2 20-5k, 3 20-2k, 4 20-1k, 5 50-20k, 6 100-20k, 7 200-20k, 8 1k-20k
    inline const std::vector<Preset>& builtIn()
    {
        //                 name                       reass  fft    ref    range  comp  col  speed  freq grid short
        static const std::vector<Preset> p {
            { "Default (DigiCheck)",                   true,  4096, -20.0,  60.0,  0.0, 0,  300.0, 0,   1,  true,
              "The startup view: full range, balanced resolution" },
            { "Bass focus (20 Hz - 1 kHz)",            true, 16384, -20.0,  60.0,  0.0, 3,  200.0, 4,   3,  false,
              "Kick, bass and sub detail with a fine frequency grid" },
            { "Low end & mix body (20 Hz - 2 kHz)",    true,  8192, -20.0,  60.0,  0.0, 0,  250.0, 3,   3,  true,
              "Bass, low mids and fundamentals" },
            { "Vocals & melody (20 Hz - 5 kHz)",       true,  4096, -24.0,  60.0,  0.2, 4,  300.0, 2,   1,  true,
              "Fundamentals and the first harmonics of voices and instruments" },
            { "Hi-hats & air (1 - 20 kHz)",            true,  1024, -26.0,  60.0,  0.3, 2,  600.0, 8,   1,  true,
              "Cymbals, sibilance and the top end, with fast time resolution" },
            { "Drums & transients",                    true,  1024, -20.0,  50.0,  0.0, 0,  800.0, 0,   1,  true,
              "Very fast scrolling and short windows for sharp attacks" },
            { "Mastering overview",                    true,  8192, -18.0,  80.0,  0.0, 1,  100.0, 0,   1,  true,
              "Slow scroll over a whole song, wide dynamic range" },
            { "Noise floor & quiet detail",            true,  4096, -40.0, 100.0,  0.5, 5,  300.0, 0,   1,  false,
              "Reveals hiss, hum, reverb tails and low-level artefacts" },
            { "High contrast",                         true,  4096, -20.0,  40.0, -0.4, 0,  300.0, 0,   0,  true,
              "Only the strongest components stand out" },
            { "Plain FFT (no reassignment)",          false,  4096, -20.0,  60.0,  0.0, 0,  300.0, 0,   1,  true,
              "A conventional spectrogram, for comparison" },
        };
        return p;
    }

    inline Preset fromSettings (const Settings& s, const juce::String& name)
    {
        return { name, s.reassign, s.fftSize, s.refLevel, s.range, s.compExp, s.colour, s.speed, s.freqPreset, s.grid, s.shortRange, {} };
    }

    inline void apply (const Preset& p, Settings& s)
    {
        s.reassign = p.reassign;   s.fftSize = p.fftSize;  s.refLevel = p.refLevel; s.range = p.range;
        s.compExp = p.compExp;     s.colour = p.colour;    s.speed = p.speed;       s.freqPreset = p.freqPreset;
        s.grid = p.grid;           s.shortRange = p.shortRange;
    }

    inline bool matches (const Preset& p, const Settings& s)
    {
        auto eq = [] (double a, double b) { return std::abs (a - b) < 1e-6; };
        return p.reassign == s.reassign && p.fftSize == s.fftSize && eq (p.refLevel, s.refLevel) && eq (p.range, s.range)
            && eq (p.compExp, s.compExp) && p.colour == s.colour && eq (p.speed, s.speed) && p.freqPreset == s.freqPreset
            && p.grid == s.grid && p.shortRange == s.shortRange;
    }

    // ---- user presets, stored as XML in the properties file under "spectrogramPresets"
    inline std::vector<Preset> loadUser (juce::PropertiesFile& props)
    {
        std::vector<Preset> out;
        if (auto xml = props.getXmlValue ("spectrogramPresets"))
            for (auto* e : xml->getChildIterator())
            {
                Preset p;
                p.name       = e->getStringAttribute ("name");
                p.reassign   = e->getBoolAttribute ("reassign", true);
                p.fftSize    = e->getIntAttribute ("fftSize", 4096);
                p.refLevel   = e->getDoubleAttribute ("refLevel", -20.0);
                p.range      = e->getDoubleAttribute ("range", 60.0);
                p.compExp    = e->getDoubleAttribute ("compExp", 0.0);
                p.colour     = e->getIntAttribute ("colour", 0);
                p.speed      = e->getDoubleAttribute ("speed", 300.0);
                p.freqPreset = e->getIntAttribute ("freqPreset", 0);
                p.grid       = e->getIntAttribute ("grid", 1);
                p.shortRange = e->getBoolAttribute ("shortRange", true);
                if (p.name.isNotEmpty()) out.push_back (p);
            }
        return out;
    }

    inline void saveUser (juce::PropertiesFile& props, const std::vector<Preset>& list)
    {
        juce::XmlElement root ("Presets");
        for (auto& p : list)
        {
            auto* e = root.createNewChildElement ("Preset");
            e->setAttribute ("name", p.name);           e->setAttribute ("reassign", p.reassign);
            e->setAttribute ("fftSize", p.fftSize);     e->setAttribute ("refLevel", p.refLevel);
            e->setAttribute ("range", p.range);         e->setAttribute ("compExp", p.compExp);
            e->setAttribute ("colour", p.colour);       e->setAttribute ("speed", p.speed);
            e->setAttribute ("freqPreset", p.freqPreset); e->setAttribute ("grid", p.grid);
            e->setAttribute ("shortRange", p.shortRange);
        }
        props.setValue ("spectrogramPresets", &root);
        props.saveIfNeeded();
    }
}
