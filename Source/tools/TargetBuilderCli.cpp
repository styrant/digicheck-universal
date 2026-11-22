// TargetBuilder command-line mode (TargetBuilder.exe --cli ...), for batch genre runs and tests.
//
//   --cli <folder>                      one target from all songs in the folder (named after it); a folder of
//                                       genre subfolders gives one target per subfolder
//   --cli <song> <song> ... --name X    one target from the listed songs
//   options: --name NAME  --out DIR  --genres  --range LO HI  --threads N
#include "TargetCore.h"
#include <atomic>
#include <cstdio>
#include <mutex>

namespace
{
    void say (const juce::String& s) { std::printf ("%s\n", s.toRawUTF8()); std::fflush (stdout); }

    struct Options
    {
        juce::Array<juce::File> inputs;
        juce::String name;
        juce::File out = targets::defaultFolder();
        bool genres = false;
        double pLo = 10, pHi = 90;
        int threads = juce::jmax (1, juce::SystemStats::getNumCpus());
    };

    bool build (const juce::String& name, const juce::Array<juce::File>& files, const Options& opt, juce::AudioFormatManager& fm)
    {
        say ("");
        say ("== " + name + ": " + juce::String (files.size()) + " file(s)");
        if (files.isEmpty()) { say ("   no audio files found"); return false; }

        std::vector<targets::TrackResult> results ((size_t) files.size());
        std::atomic<int> next { 0 }, done { 0 };
        std::mutex printLock;
        {
            juce::ThreadPool pool (juce::jmin (opt.threads, files.size()));
            for (int t = 0; t < juce::jmin (opt.threads, files.size()); ++t)
                pool.addJob ([&]
                {
                    for (int i; (i = next++) < files.size();)
                    {
                        results[(size_t) i] = targets::analyse (fm, files[i]);
                        const std::lock_guard<std::mutex> l (printLock);
                        const auto& r = results[(size_t) i];
                        say ("   [" + juce::String (++done) + "/" + juce::String (files.size()) + "] " + files[i].getFileName()
                             + (r.ok() ? juce::String() : "  -- skipped: " + r.error));
                    }
                });
            while (pool.getNumJobs() > 0) juce::Thread::sleep (50);
        }
        std::vector<const targets::TrackResult*> ptrs;
        for (auto& r : results) ptrs.push_back (&r);
        const auto w = targets::write (name, ptrs, opt.out, opt.pLo, opt.pHi);
        if (w.error.isNotEmpty()) { say ("   " + w.error + ", target not written"); return false; }
        say ("   wrote " + w.json.getFullPathName());
        say ("   " + juce::String (w.songs) + " song(s) used" + (w.outliers ? ", " + juce::String (w.outliers) + " outlier(s) - see the report" : juce::String()));
        return true;
    }
}

int runTargetBuilderCli (const juce::StringArray& args)
{
    Options opt;
    for (int i = 0; i < args.size(); ++i)
    {
        const auto& a = args[i];
        if (a == "--cli") continue;
        if (a == "--name" && i + 1 < args.size())            opt.name = args[++i];
        else if (a == "--out" && i + 1 < args.size())        opt.out = juce::File::getCurrentWorkingDirectory().getChildFile (args[++i]);
        else if (a == "--genres")                            opt.genres = true;
        else if (a == "--range" && i + 2 < args.size())      { opt.pLo = args[++i].getDoubleValue(); opt.pHi = args[++i].getDoubleValue(); }
        else if (a == "--threads" && i + 1 < args.size())    opt.threads = juce::jmax (1, args[++i].getIntValue());
        else opt.inputs.add (juce::File::getCurrentWorkingDirectory().getChildFile (a.unquoted()));
    }
    if (opt.inputs.isEmpty())
    {
        say ("TargetBuilder --cli <folder> | <song> ... [--name NAME] [--out DIR] [--genres] [--range LO HI] [--threads N]");
        return 1;
    }
    if (opt.pLo < 0 || opt.pHi > 100 || opt.pLo >= opt.pHi) { say ("--range must be two percentiles, low < high"); return 1; }

    juce::AudioFormatManager fm;
    targets::registerFormats (fm);
    int ok = 0, tried = 0;
    if (opt.inputs.size() == 1 && opt.inputs[0].isDirectory())
    {
        const auto dir = opt.inputs[0];
        juce::Array<juce::File> subs;
        for (auto& e : juce::RangedDirectoryIterator (dir, false, "*", juce::File::findDirectories)) subs.add (e.getFile());
        subs.sort();
        const bool genreMode = opt.genres || (targets::audioFilesIn (dir, fm, false).isEmpty() && subs.size() > 1);
        if (genreMode)
        {
            say ("Genre mode: one target per subfolder of " + dir.getFullPathName());
            for (auto& s : subs)
            {
                const auto files = targets::audioFilesIn (s, fm, true);
                if (files.isEmpty()) continue;
                ++tried; ok += build (s.getFileName(), files, opt, fm) ? 1 : 0;
            }
        }
        else
        {
            ++tried;
            ok += build (opt.name.isNotEmpty() ? opt.name : dir.getFileName(), targets::audioFilesIn (dir, fm, true), opt, fm) ? 1 : 0;
        }
    }
    else
    {
        juce::Array<juce::File> files;
        for (auto& f : opt.inputs)
        {
            if (f.isDirectory()) files.addArray (targets::audioFilesIn (f, fm, true));
            else if (f.existsAsFile()) files.add (f);
            else say ("not found: " + f.getFullPathName());
        }
        ++tried;
        ok += build (opt.name.isNotEmpty() ? opt.name : (files.size() == 1 ? files[0].getFileNameWithoutExtension() : juce::String ("My target")), files, opt, fm) ? 1 : 0;
    }
    say ("\n" + juce::String (ok) + " of " + juce::String (tried) + " target(s) written to " + opt.out.getFullPathName());
    return ok == tried && tried > 0 ? 0 : 1;
}
