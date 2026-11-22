#include "TargetCore.h"

namespace targets
{
juce::File defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DigiCheck Universal").getChildFile ("Targets");
}

void registerFormats (juce::AudioFormatManager& fm) { fm.registerBasicFormats(); }

TrackResult analyse (juce::AudioFormatManager& fm, const juce::File& f, const std::atomic<bool>* cancel)
{
    TrackResult r;
    r.file = f;
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
    if (reader == nullptr) { r.error = "can't read this file"; return r; }
    const double sr = reader->sampleRate;
    const int nch = (int) juce::jmax (1u, reader->numChannels);
    const auto total = reader->lengthInSamples;
    if (sr < 8000 || total <= 0) { r.error = "empty or unsupported"; return r; }
    r.seconds = (double) total / sr;

    tonal::SpectrumAccumulator acc (sr);                    // measured at the file's own rate
    constexpr int kBlock = 32768;
    juce::AudioBuffer<float> buf (nch, kBlock);
    std::vector<float> mono;
    for (juce::int64 pos = 0; pos < total; pos += kBlock)
    {
        if (cancel != nullptr && cancel->load()) { r.error = "cancelled"; return r; }
        const int n = (int) juce::jmin<juce::int64> (kBlock, total - pos);
        reader->read (&buf, 0, n, pos, true, true);
        mono.assign ((size_t) n, 0.0f);
        for (int c = 0; c < nch; ++c)
        {
            const float* s = buf.getReadPointer (c);
            for (int i = 0; i < n; ++i) mono[(size_t) i] += s[i] / (float) nch;
        }
        acc.process (mono.data(), n);
    }
    r.analysed = acc.analysedSeconds();
    r.db = acc.normalizedDb();
    if (r.db.empty()) r.error = "too quiet (nothing above the loudness gate)";
    return r;
}

juce::Array<juce::File> audioFilesIn (const juce::File& dir, juce::AudioFormatManager& fm, bool recursive)
{
    juce::Array<juce::File> files;
    for (auto& e : juce::RangedDirectoryIterator (dir, recursive, "*", juce::File::findFiles))
        if (fm.findFormatForFileExtension (e.getFile().getFileExtension()) != nullptr && ! e.getFile().getFileName().startsWith ("."))
            files.add (e.getFile());
    files.sort();
    return files;
}

juce::String safeName (juce::String s) { return s.replaceCharacters ("\\/:*?\"<>|", "_________").trim(); }

Deviations deviations (const std::vector<std::vector<double>>& spectra, const tonal::Target& t)
{
    Deviations d;
    for (auto& s : spectra) d.db.push_back (tonal::deviationDb (s, t));
    auto sorted = d.db;
    std::sort (sorted.begin(), sorted.end());
    const double med = sorted.empty() ? 0 : sorted[sorted.size() / 2];
    for (double v : d.db) d.outlier.push_back (spectra.size() >= 5 && v > juce::jmax (2.0, 2.0 * med));
    return d;
}

Written write (const juce::String& name, const std::vector<const TrackResult*>& tracks, const juce::File& outDir, double pLo, double pHi)
{
    Written w;
    std::vector<std::vector<double>> spectra;
    std::vector<std::string> names;
    std::vector<const TrackResult*> used;
    for (auto* t : tracks)
        if (t != nullptr && t->ok()) { spectra.push_back (t->db); names.push_back (t->file.getFileName().toStdString()); used.push_back (t); }
    if (spectra.empty()) { w.error = "no usable songs"; return w; }
    if (! outDir.createDirectory()) { w.error = "can't create " + outDir.getFullPathName(); return w; }

    const auto target = tonal::combine (spectra, pLo, pHi);
    const auto base = safeName (name);
    w.json = outDir.getChildFile (base + ".json");
    w.report = outDir.getChildFile (base + " - report.txt");
    if (! w.json.replaceWithText (tonal::toJson (target, name.toStdString(), names))) { w.error = "can't write " + w.json.getFullPathName(); return w; }

    juce::StringArray rep;
    rep.add ("Tonal balance target \"" + name + "\" - " + juce::String ((int) spectra.size()) + " song(s), range "
             + juce::String (pLo, 0) + "-" + juce::String (pHi, 0) + " percentile");
    rep.add ("Created " + juce::Time::getCurrentTime().toString (true, true));
    rep.add ("");
    rep.add ("Band ranges (share of spectrum):  Low 0-250 Hz | Low-mid 250-2k | High-mid 2k-8k | High 8k+");
    juce::String line = "  target range:";
    for (auto& b : target.regions) line << "  " << juce::String (b[0] * 100, 1) << "-" << juce::String (b[1] * 100, 1) << "%";
    rep.add (line);
    rep.add ("");
    rep.add ("Per song: band shares, and RMS distance of its curve from the target (40 Hz - 16 kHz)");
    const auto dev = deviations (spectra, target);
    for (size_t i = 0; i < used.size(); ++i)
    {
        const auto sh = tonal::bandShares (spectra[i]);
        w.outliers += dev.outlier[i] ? 1 : 0;
        rep.add (juce::String::formatted ("  %5.1f%% %5.1f%% %5.1f%% %5.1f%%   %4.1f dB %s  %s (%d:%02d)",
                 sh[0] * 100, sh[1] * 100, sh[2] * 100, sh[3] * 100, dev.db[i], dev.outlier[i] ? "OUTLIER" : "       ",
                 used[i]->file.getFileName().toRawUTF8(), (int) used[i]->seconds / 60, (int) used[i]->seconds % 60));
    }
    for (auto* t : tracks)
        if (t != nullptr && ! t->ok()) rep.add ("  skipped: " + t->file.getFileName() + " (" + t->error + ")");
    if (w.outliers > 0)
    {
        rep.add ("");
        rep.add ("OUTLIER = more than twice the typical distance from the target. Consider removing those songs");
        rep.add ("and building again if they don't belong to this sound.");
    }
    w.report.replaceWithText (rep.joinIntoString ("\n") + "\n");
    w.songs = (int) spectra.size();
    return w;
}
}
