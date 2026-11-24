// The DAW bridge (Source/bridge/Bridge.h): senders claim slots, the reader finds the master, audio and
// names arrive intact. With --send <seconds> it instead plays a sender called "Master" in real time
// (a 440 Hz tone with a 2 Hz level wobble), for checking the app's "From DAW" input by hand.
#include <JuceHeader.h>
#include "../Source/bridge/Bridge.h"
#include <thread>

static int failures = 0;
static void check (bool ok, const char* what) { std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what); if (! ok) ++failures; }

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
    std::printf (failures == 0 ? "all bridge checks passed\n" : "%d bridge check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
