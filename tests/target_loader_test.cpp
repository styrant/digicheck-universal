// Target loader tests: the files Target Builder writes, the "Type/Value"-wrapped variant without a raw
// curve, a coarser frequency grid, and broken files. (No third-party target data is used.)
#include <JuceHeader.h>
#include "../Source/balance/TargetLibrary.h"
#include <cstdio>

static int fails = 0;
static void check (bool ok, const char* what) { std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++fails; }

static juce::var wrap (const juce::var& v)
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    if (auto* a = v.getArray())
    {
        juce::Array<juce::var> out;
        for (auto& x : *a) out.add (wrap (x));
        o->setProperty ("Type", "Array"); o->setProperty ("Value", out);
    }
    else { o->setProperty ("Type", "Float"); o->setProperty ("Value", v); }
    return o.get();
}

static double maxDiff (const std::vector<double>& a, const std::vector<double>& b)
{
    double m = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) m = std::max (m, std::abs (a[i] - b[i]));
    return a.size() == b.size() ? m : 1e9;
}

int main()
{
    // a target made the Target Builder way from 6 synthetic spectra with different tilts
    std::vector<std::vector<double>> songs;
    for (int s = 0; s < 6; ++s)
    {
        std::vector<double> db ((size_t) tonal::kBins);
        for (int k = 0; k < tonal::kBins; ++k)
            db[(size_t) k] = -40.0 - 8.0 * std::log2 (1.0 + tonal::binFrequency (k) / 100.0) * (0.8 + 0.08 * s);
        songs.push_back (db);
    }
    const auto t = tonal::combine (songs);
    const auto json = tonal::toJson (t, "Synthetic", { "a.wav", "b.wav" });

    tonal::Target back; juce::String name, err;
    const bool ok = TargetLibrary::parse (juce::JSON::parse (juce::String (json)), back, name, err);
    check (ok && name == "Synthetic", "reads a Target Builder file");
    check (maxDiff (back.mean, t.mean) < 1e-5 && maxDiff (back.raw, t.raw) < 1e-5 && maxDiff (back.low, t.low) < 1e-5 && maxDiff (back.high, t.high) < 1e-5,
           "curves round-trip exactly");
    bool regions = true;
    for (int b = 0; b < 4; ++b) regions &= std::abs (back.regions[(size_t) b][0] - t.regions[(size_t) b][0]) < 1e-5 && std::abs (back.regions[(size_t) b][1] - t.regions[(size_t) b][1]) < 1e-5;
    check (regions, "band ranges round-trip");

    // the wrapped variant: every value as {"Type", "Value"}, and no raw curve
    auto plain = juce::JSON::parse (juce::String (json));
    juce::DynamicObject::Ptr w = new juce::DynamicObject();
    for (auto& p : plain.getDynamicObject()->getProperties())
        if (p.name.toString() != "raw_normalized_mag_dB" && p.name.toString() != "name" && p.name.toString() != "tracks")
            w->setProperty (p.name, p.value.isArray() ? wrap (p.value) : p.value);
    tonal::Target wt; juce::String wname;
    check (TargetLibrary::parse (juce::var (w.get()), wt, wname, err) && maxDiff (wt.mean, t.mean) < 1e-5 && maxDiff (wt.raw, t.mean) < 1e-5,
           "reads the wrapped variant (raw curve falls back to the smoothed one)");
    check (std::abs (wt.regions[0][0] - t.regions[0][0]) < 1e-5, "wrapped band ranges");

    // a coarser grid (every 4th bin) is interpolated onto the 2049-bin grid
    juce::DynamicObject::Ptr c = new juce::DynamicObject();
    for (auto* key : { "frequencies_hz", "normalized_mag_dB", "low_normalized_mag_dB", "high_normalized_mag_dB" })
    {
        juce::Array<juce::var> a;
        auto* src = plain.getDynamicObject()->getProperty (key).getArray();
        for (int i = 0; i < src->size(); i += 4) a.add ((*src)[i]);
        c->setProperty (key, a);
    }
    tonal::Target ct;
    const bool cok = TargetLibrary::parse (juce::var (c.get()), ct, wname, err);
    double d = 0;
    for (int k = 20; k < 1800; ++k) d = std::max (d, std::abs (ct.mean[(size_t) k] - t.mean[(size_t) k]));
    std::printf ("      coarse grid: max error %.3f dB\n", d);
    check (cok && ct.mean.size() == (size_t) tonal::kBins && d < 0.5, "coarser frequency grid is interpolated");

    // broken files are rejected with a reason
    juce::DynamicObject::Ptr bad = new juce::DynamicObject();
    bad->setProperty ("frequencies_hz", juce::Array<juce::var> { 0.0, 10.0 });
    tonal::Target bt;
    check (! TargetLibrary::parse (juce::var (bad.get()), bt, wname, err) && err.isNotEmpty(), "incomplete file is rejected");
    check (! TargetLibrary::parse (juce::var ("text"), bt, wname, err), "non-object is rejected");

    std::printf (fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails ? 1 : 0;
}
