// TargetBuilder: makes tonal balance targets for Spectroscope from songs.
//
//   TargetBuilder <folder>                      one target from all songs in the folder (and subfolders),
//                                               named after the folder; or, if the folder only holds
//                                               subfolders of songs, one target per subfolder (genre)
//   TargetBuilder <song> <song> ... --name X    one target from the listed songs
//
// Options:  --name NAME        target name (single target)
//           --out DIR          where to write (default: Documents\DigiCheck Universal\Targets)
//           --genres           treat each subfolder as its own target
//           --range LO HI      percentiles for the "in range" band (default 10 90)
//           --threads N        songs analysed in parallel (default: number of CPU cores)
//
// Writes <name>.json (the target) and <name> - report.txt (per-song summary, outliers flagged).
// Drag a folder onto TargetBuilder.exe to use it without a command line.
#include <JuceHeader.h>
#include "../balance/TonalAnalysis.h"
#include <atomic>
#include <cstdio>
#include <mutex>
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace
{
    struct Options
    {
        juce::Array<juce::File> inputs;
        juce::String name;
        juce::File out;
        bool genres = false;
        double pLo = 10, pHi = 90;
        int threads = juce::jmax (1, juce::SystemStats::getNumCpus());
    };

    struct TrackResult
    {
        juce::File file;
        std::vector<double> db;
        double seconds = 0, analysed = 0;
        juce::String error;
    };

    void say (const juce::String& s) { std::printf ("%s\n", s.toRawUTF8()); std::fflush (stdout); }

    // decode, mix to mono (mean of channels) and measure the spectrum
    TrackResult analyse (juce::AudioFormatManager& fm, const juce::File& f)
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

        tonal::SpectrumAccumulator acc (sr);                         // measured at the file's own rate
        constexpr int kBlock = 32768;
        juce::AudioBuffer<float> buf (nch, kBlock);
        std::vector<float> mono;
        for (juce::int64 pos = 0; pos < total; pos += kBlock)
        {
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

    juce::String safeName (juce::String s)
    {
        return s.replaceCharacters ("\\/:*?\"<>|", "_________").trim();
    }

    bool buildTarget (const juce::String& name, const juce::Array<juce::File>& files, const Options& opt, juce::AudioFormatManager& fm)
    {
        say ("");
        say ("== " + name + ": " + juce::String (files.size()) + " file(s)");
        if (files.isEmpty()) { say ("   no audio files found"); return false; }

        std::vector<TrackResult> results ((size_t) files.size());
        std::atomic<int> next { 0 }, done { 0 };
        std::mutex printLock;
        {
            juce::ThreadPool pool (juce::jmin (opt.threads, files.size()));
            for (int t = 0; t < juce::jmin (opt.threads, files.size()); ++t)
                pool.addJob ([&]
                {
                    for (int i; (i = next++) < files.size();)
                    {
                        results[(size_t) i] = analyse (fm, files[i]);
                        const std::lock_guard<std::mutex> l (printLock);
                        const auto& r = results[(size_t) i];
                        say ("   [" + juce::String (++done) + "/" + juce::String (files.size()) + "] " + files[i].getFileName()
                             + (r.error.isNotEmpty() ? "  -- skipped: " + r.error : ""));
                    }
                });
            while (pool.getNumJobs() > 0) juce::Thread::sleep (50);
        }

        std::vector<std::vector<double>> spectra;
        std::vector<std::string> names;
        std::vector<const TrackResult*> used;
        for (auto& r : results)
            if (r.error.isEmpty()) { spectra.push_back (r.db); names.push_back (r.file.getFileName().toStdString()); used.push_back (&r); }
        if (spectra.empty()) { say ("   nothing usable, target not written"); return false; }

        const auto target = tonal::combine (spectra, opt.pLo, opt.pHi);
        const auto base = safeName (name);
        const auto json = opt.out.getChildFile (base + ".json");
        if (! json.replaceWithText (tonal::toJson (target, name.toStdString(), names)))
        {
            say ("   could not write " + json.getFullPathName());
            return false;
        }

        // report: per-song band shares and distance from the target; outliers flagged
        juce::StringArray rep;
        rep.add ("Tonal balance target \"" + name + "\" - " + juce::String ((int) spectra.size()) + " song(s), range "
                 + juce::String (opt.pLo, 0) + "-" + juce::String (opt.pHi, 0) + " percentile");
        rep.add ("Created " + juce::Time::getCurrentTime().toString (true, true));
        rep.add ("");
        rep.add ("Band ranges (share of spectrum):  Low 0-250 Hz | Low-mid 250-2k | High-mid 2k-8k | High 8k+");
        juce::String line = "  target range:";
        for (auto& b : target.regions) line << "  " << juce::String (b[0] * 100, 1) << "-" << juce::String (b[1] * 100, 1) << "%";
        rep.add (line);
        rep.add ("");
        rep.add ("Per song: band shares, and RMS distance of its curve from the target (40 Hz - 16 kHz)");
        std::vector<double> devs;
        for (auto& s : spectra) devs.push_back (tonal::deviationDb (s, target));
        auto sorted = devs; std::sort (sorted.begin(), sorted.end());
        const double medDev = sorted[sorted.size() / 2];
        int outliers = 0;
        for (size_t i = 0; i < used.size(); ++i)
        {
            const auto sh = tonal::bandShares (spectra[i]);
            const bool out = spectra.size() >= 5 && devs[i] > juce::jmax (2.0, 2.0 * medDev);
            outliers += out ? 1 : 0;
            rep.add (juce::String::formatted ("  %5.1f%% %5.1f%% %5.1f%% %5.1f%%   %4.1f dB %s  %s (%d:%02d)",
                     sh[0] * 100, sh[1] * 100, sh[2] * 100, sh[3] * 100, devs[i], out ? "OUTLIER" : "       ",
                     used[i]->file.getFileName().toRawUTF8(), (int) used[i]->seconds / 60, (int) used[i]->seconds % 60));
        }
        for (auto& r : results)
            if (r.error.isNotEmpty()) rep.add ("  skipped: " + r.file.getFileName() + " (" + r.error + ")");
        if (outliers > 0)
        {
            rep.add ("");
            rep.add ("OUTLIER = more than twice the typical distance from the target. Consider removing those songs");
            rep.add ("from the folder and building again if they don't belong to this sound.");
        }
        opt.out.getChildFile (base + " - report.txt").replaceWithText (rep.joinIntoString ("\n") + "\n");

        say ("   wrote " + json.getFullPathName());
        say ("   " + juce::String ((int) spectra.size()) + " song(s) used" + (outliers ? ", " + juce::String (outliers) + " outlier(s) - see the report" : ""));
        return true;
    }

    bool hasAudioDirectly (const juce::File& dir, juce::AudioFormatManager& fm) { return ! audioFilesIn (dir, fm, false).isEmpty(); }

    void usage()
    {
        say ("TargetBuilder - makes tonal balance targets for Spectroscope from songs\n");
        say ("  TargetBuilder <folder>                     target from all songs in the folder (named after it);");
        say ("                                             a folder of genre subfolders gives one target per subfolder");
        say ("  TargetBuilder <song> [<song> ...] --name X target from the listed songs\n");
        say ("  --name NAME      target name            --out DIR     output folder");
        say ("  --genres         one target per subfolder");
        say ("  --range LO HI    percentile range (default 10 90)   --threads N   parallel songs\n");
        say ("Formats: WAV, AIFF, FLAC, OGG, MP3 (plus WMA on Windows). Default output folder:");
        say ("  " + juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DigiCheck Universal").getChildFile ("Targets").getFullPathName());
    }

    bool ownsConsole()      // started by double-click / drag & drop, so the window would close at once
    {
       #if JUCE_WINDOWS
        DWORD ids[4];
        return GetConsoleProcessList (ids, 4) == 1;
       #else
        return false;
       #endif
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;          // needed by the Windows Media audio reader
    Options opt;
    opt.out = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DigiCheck Universal").getChildFile ("Targets");
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a = juce::String::fromUTF8 (argv[i]);
        if (a == "--name" && i + 1 < argc)          opt.name = juce::String::fromUTF8 (argv[++i]);
        else if (a == "--out" && i + 1 < argc)      opt.out = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[++i]));
        else if (a == "--genres")                   opt.genres = true;
        else if (a == "--range" && i + 2 < argc)    { opt.pLo = juce::String (argv[++i]).getDoubleValue(); opt.pHi = juce::String (argv[++i]).getDoubleValue(); }
        else if (a == "--threads" && i + 1 < argc)  opt.threads = juce::jmax (1, juce::String (argv[++i]).getIntValue());
        else if (a == "--help" || a == "-h")        { usage(); return 0; }
        else opt.inputs.add (juce::File::getCurrentWorkingDirectory().getChildFile (a));
    }
    const bool pause = ownsConsole();
    auto finish = [pause] (int code) { if (pause) { say ("\nPress Enter to close."); std::getchar(); } return code; };

    if (opt.inputs.isEmpty()) { usage(); return finish (1); }
    if (opt.pLo < 0 || opt.pHi > 100 || opt.pLo >= opt.pHi) { say ("--range must be two percentiles, low < high"); return finish (1); }
    if (! opt.out.createDirectory()) { say ("Can't create " + opt.out.getFullPathName()); return finish (1); }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    int ok = 0, tried = 0;

    if (opt.inputs.size() == 1 && opt.inputs[0].isDirectory())
    {
        const auto dir = opt.inputs[0];
        juce::Array<juce::File> subs;
        for (auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findDirectories)) subs.add (e.getFile());
        subs.sort();
        const bool genreMode = opt.genres || (! hasAudioDirectly (dir, fm) && subs.size() > 1);
        if (genreMode)
        {
            say ("Genre mode: one target per subfolder of " + dir.getFullPathName());
            for (auto& s : subs)
            {
                const auto files = audioFilesIn (s, fm, true);
                if (files.isEmpty()) continue;
                ++tried; ok += buildTarget (s.getFileName(), files, opt, fm) ? 1 : 0;
            }
        }
        else
        {
            ++tried;
            ok += buildTarget (opt.name.isNotEmpty() ? opt.name : dir.getFileName(), audioFilesIn (dir, fm, true), opt, fm) ? 1 : 0;
        }
    }
    else
    {
        juce::Array<juce::File> files;
        for (auto& f : opt.inputs)
        {
            if (f.isDirectory()) files.addArray (audioFilesIn (f, fm, true));
            else if (f.existsAsFile()) files.add (f);
            else say ("not found: " + f.getFullPathName());
        }
        ++tried;
        const auto name = opt.name.isNotEmpty() ? opt.name : (files.size() == 1 ? files[0].getFileNameWithoutExtension() : juce::String ("My target"));
        ok += buildTarget (name, files, opt, fm) ? 1 : 0;
    }

    say ("\n" + juce::String (ok) + " of " + juce::String (tried) + " target(s) written to " + opt.out.getFullPathName());
    return finish (ok == tried && tried > 0 ? 0 : 1);
}
