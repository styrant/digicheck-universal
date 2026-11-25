// The DAW bridge (Source/bridge/Bridge.h): senders claim slots, the reader finds the master, audio and
// names arrive intact. With --send <seconds> it instead plays a sender called "Master" in real time
// (a 440 Hz tone with a 2 Hz level wobble), for checking the app's "From DAW" input by hand.
#include <JuceHeader.h>
#include "../Source/bridge/Bridge.h"
#include <thread>

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }
static void check (bool ok, const juce::String& what) { check (ok, what.toRawUTF8()); }

int main (int argc, char** argv)
{
    if (argc >= 3 && juce::String (argv[1]) == "--send")
    {
        bridge::Sender s; s.setName ("Master");
        const double sr = 48000; const int block = 480; std::vector<float> L (block), R (block);
        double ph = 0; long long pos = 0;
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds ((int) (std::atof (argv[2]) * 1000));
        auto next = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() < end)
        {
            for (int i = 0; i < block; ++i, ++pos)
            {
                const float a = (float) (0.25 * (0.6 + 0.4 * std::sin (2 * juce::MathConstants<double>::pi * 2.0 * pos / sr)));
                ph += 2 * juce::MathConstants<double>::pi * 440.0 / sr;
                L[(size_t) i] = a * (float) std::sin (ph); R[(size_t) i] = 0.5f * L[(size_t) i];
            }
            s.write (L.data(), R.data(), block, sr);
            next += std::chrono::milliseconds (10);
            std::this_thread::sleep_until (next);
        }
        return 0;
    }

    check (bridge::shared() != nullptr, "shared memory opens");
    bridge::Sender a, b;
    a.setName ("Kick"); b.setName ("Master");
    std::vector<float> L (256), R (256);
    for (int i = 0; i < 256; ++i) { L[(size_t) i] = (float) i; R[(size_t) i] = (float) -i; }
    a.write (L.data(), R.data(), 256, 44100);
    b.write (L.data(), R.data(), 256, 48000);
    check (a.isSending() && b.isSending(), "two senders claim slots");
    const auto list = bridge::listSenders();
    int master = -1, kick = -1;
    for (auto& s : list) { if (s.name == "Master") master = s.slot; if (s.name == "Kick") kick = s.slot; }
    check (master >= 0 && kick >= 0 && master != kick, "names arrive (Master, Kick)");
    check (bridge::chooseSlot ({}) == master, "automatic picks the master");
    check (bridge::chooseSlot ("Kick") == kick, "a named track is found");
    auto& sl = bridge::shared()->slots[kick];
    check (sl.written.load() == 256 && sl.l[100] == 100.0f && sl.r[100] == -100.0f && sl.rateMilliHz.load() == 44100000, "audio and rate arrive");
    check (bridge::looksLikeMaster ("Stereo Out") && bridge::looksLikeMaster ("MASTER") && ! bridge::looksLikeMaster ("Bass"), "master names");
    a.setEnabled (false);
    bool gone = true; for (auto& s : bridge::listSenders()) if (s.name == "Kick") gone = false;
    check (gone, "turning a sender off frees its slot");
    // the app's "Also hear your DAW" mixer: a 1 kHz sine sent at 44.1 kHz (its clock 0.1 % fast), mixed into a
    // silent 48 kHz input for 30 simulated seconds, 10 ms blocks on both sides
    {
        bridge::Sender snd; snd.setName ("MixTest"); snd.setEnabled (true);
        const double tw = 6.283185307179586;
        std::vector<float> wl (8192), wr (8192), zero (480, 0.0f), ol (480), orr (480);
        double acc = 0; long long wpos = 0;
        auto writeMs = [&] (double ms)
        {
            acc += 44100 * 1.001 * ms / 1000.0;
            const int m = (int) acc; acc -= m;
            for (int i = 0; i < m; ++i, ++wpos) wl[(size_t) i] = wr[(size_t) i] = (float) (0.5 * std::sin (tw * 1000.0 * wpos / (44100 * 1.001)));
            snd.write (wl.data(), wr.data(), m, 44100);
        };
        writeMs (100);
        int slot = -1; for (auto& v : bridge::listSenders()) if (v.name == "MixTest") slot = v.slot;
        check (slot >= 0, "mix sender found");
        bridge::Mixer mx;
        long long crossings = 0, zeroRun = 0, maxZeroRun = 0, outN = 0; float prev = 0, peak = 0;
        double lagMs = 0;
        for (int step = 0; step < 3000 && slot >= 0; ++step)
        {
            writeMs (10);
            mx.process (slot, 48000, zero.data(), zero.data(), ol.data(), orr.data(), 480, step == 0);
            if (step < 100) continue;                    // the first second settles
            for (int i = 0; i < 480; ++i, ++outN)
            {
                const float v = ol[(size_t) i];
                if ((prev < 0) != (v < 0)) ++crossings;
                prev = v; peak = std::max (peak, std::abs (v));
                zeroRun = v == 0.0f ? zeroRun + 1 : 0; maxZeroRun = std::max (maxZeroRun, zeroRun);
            }
            lagMs = (bridge::shared()->slots[slot].written.load() - mx.pos) / 44100.0 * 1000.0;
        }
        const double hz = crossings / 2.0 / (outN / 48000.0);
        check (std::abs (hz - 1000.0) < 3.0, "mixed DAW audio keeps its pitch through resampling and drift (" + juce::String (hz, 2) + " Hz)");
        check (maxZeroRun < 3, "no gaps in the mixed audio (longest zero run " + juce::String ((int) maxZeroRun) + ")");
        check (peak > 0.48f && peak < 0.52f, "level unchanged (" + juce::String (peak, 3) + ")");
        check (lagMs > 25 && lagMs < 60, "stays about 40 ms behind the DAW after 30 s of 0.1 % drift (" + juce::String (lagMs, 1) + " ms)");
        snd.setEnabled (false);
    }

    std::printf (failures == 0 ? "all bridge checks passed\n" : "%d bridge check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
