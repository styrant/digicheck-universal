#include "TargetLibrary.h"

namespace
{
    // {"Type": "...", "Value": x} -> x, recursively
    juce::var unwrap (const juce::var& v)
    {
        if (auto* o = v.getDynamicObject())
            if (o->hasProperty ("Value") && o->hasProperty ("Type")) return unwrap (o->getProperty ("Value"));
        if (auto* a = v.getArray())
        {
            juce::Array<juce::var> out;
            for (auto& x : *a) out.add (unwrap (x));
            return out;
        }
        return v;
    }

    std::vector<double> numbers (const juce::var& v)
    {
        std::vector<double> out;
        const juce::var u = unwrap (v);                      // keep the unwrapped value alive while reading it
        if (auto* a = u.getArray())
            for (auto& x : *a)
                if (x.isDouble() || x.isInt() || x.isInt64()) out.push_back ((double) x);
                else return {};
        return out;
    }

    // resample a curve given at `freqs` onto the 44.1 kHz / 4096-point grid
    std::vector<double> toGrid (const std::vector<double>& freqs, const std::vector<double>& v)
    {
        if (freqs.size() == (size_t) tonal::kBins && std::abs (freqs.back() - 22050.0) < 1.0) return v;
        std::vector<double> out ((size_t) tonal::kBins);
        size_t j = 0;
        for (int k = 0; k < tonal::kBins; ++k)
        {
            const double f = tonal::binFrequency (k);
            while (j + 1 < freqs.size() && freqs[j + 1] < f) ++j;
            if (j + 1 >= freqs.size()) { out[(size_t) k] = v.back(); continue; }
            const double t = juce::jlimit (0.0, 1.0, (f - freqs[j]) / juce::jmax (1e-9, freqs[j + 1] - freqs[j]));
            out[(size_t) k] = v[j] * (1 - t) + v[j + 1] * t;
        }
        return out;
    }
}

juce::File TargetLibrary::folder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DigiCheck Universal").getChildFile ("Targets");
}

bool TargetLibrary::parse (const juce::var& json, tonal::Target& t, juce::String& name, juce::String& error)
{
    auto* obj = json.getDynamicObject();
    if (obj == nullptr) { error = "not a JSON object"; return false; }
    auto get = [&] (const char* key) { return numbers (obj->getProperty (key)); };

    const auto freqs = get ("frequencies_hz");
    auto mean = get ("normalized_mag_dB"), low = get ("low_normalized_mag_dB"), high = get ("high_normalized_mag_dB"), raw = get ("raw_normalized_mag_dB");
    if (freqs.size() < 16 || mean.size() != freqs.size() || low.size() != freqs.size() || high.size() != freqs.size())
    {
        error = "missing or mismatched curves (frequencies_hz, normalized_mag_dB, low_/high_normalized_mag_dB)";
        return false;
    }
    if (raw.size() != freqs.size()) raw = mean;              // some files only have the smoothed curve
    t.mean = toGrid (freqs, mean); t.low = toGrid (freqs, low); t.high = toGrid (freqs, high); t.raw = toGrid (freqs, raw);
    for (int k = 0; k < tonal::kBins; ++k)                   // make sure the range contains the curve
    {
        t.low[(size_t) k]  = std::min (t.low[(size_t) k], t.mean[(size_t) k]);
        t.high[(size_t) k] = std::max (t.high[(size_t) k], t.mean[(size_t) k]);
    }

    // band ranges: from the file, or (if absent) +/-15 % around the curve's own shares
    const auto own = tonal::bandShares (t.raw);
    for (int b = 0; b < 4; ++b)
    {
        const auto bounds = numbers (obj->getProperty (juce::Identifier ("region_" + juce::String (b) + "_bounds")));
        if (bounds.size() == 2 && bounds[0] > 0 && bounds[1] >= bounds[0])
            t.regions[(size_t) b] = { bounds[0], bounds[1] };
        else
            t.regions[(size_t) b] = { own[(size_t) b] * 0.85, own[(size_t) b] * 1.15 };
    }
    t.tracks = (int) unwrap (obj->getProperty ("track_count"));
    name = unwrap (obj->getProperty ("name")).toString();
    return true;
}

bool TargetLibrary::load (const juce::File& f, Entry& out, juce::String& error)
{
    juce::var json;
    const auto r = juce::JSON::parse (f.loadFileAsString(), json);
    if (r.failed()) { error = "not valid JSON (" + r.getErrorMessage() + ")"; return false; }
    juce::String name;
    if (! parse (json, out.target, name, error)) return false;
    out.file = f;
    out.name = name.isNotEmpty() ? name : f.getFileNameWithoutExtension();
    return true;
}

juce::String TargetLibrary::currentSignature() const
{
    juce::String sig;
    for (auto& e : juce::RangedDirectoryIterator (folder(), false, "*.json", juce::File::findFiles))
        sig << e.getFile().getFileName() << ':' << e.getFileSize() << ':' << e.getModificationTime().toMilliseconds() << ';';
    return sig;
}

bool TargetLibrary::rescanIfChanged()
{
    if (scanned && currentSignature() == signature) return false;
    rescan();
    return true;
}

void TargetLibrary::rescan()
{
    scanned = true;
    signature = currentSignature();
    list.clear();
    problems.clear();
    juce::Array<juce::File> files;
    for (auto& e : juce::RangedDirectoryIterator (folder(), false, "*.json", juce::File::findFiles)) files.add (e.getFile());
    for (auto& f : files)
    {
        Entry e; juce::String err;
        if (load (f, e, err)) list.push_back (std::move (e));
        else problems.add (f.getFileName() + ": " + err);
    }
    std::sort (list.begin(), list.end(), [] (const Entry& a, const Entry& b) { return a.name.compareNatural (b.name) < 0; });
}

int TargetLibrary::indexOf (const juce::String& name) const
{
    for (size_t i = 0; i < list.size(); ++i) if (list[i].name == name) return (int) i;
    return -1;
}
