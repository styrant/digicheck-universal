// The Key page's player without a sound card: pulls blocks from TranscriptionPlayer as an audio device
// would, and checks the three Listen modes, the synth following the events, and seeking.
//   player_test [song.wav [out.wav]]   (no song: a 12 s test song is generated; out.wav: 8 s of the Mix)
#include <JuceHeader.h>
#include "../Source/transcribe/TranscriptionPlayer.h"

static int failures = 0;
static void expect (bool ok, const juce::String& what) { std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (! ok) ++failures; }

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    juce::File song = argc > 1 ? juce::File { juce::String (juce::CharPointer_UTF8 (argv[1])) }
                               : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("player_test_song.wav");
    if (argc < 2)
    {
        // 12 s of C major / A minor chords at 44.1 kHz
        const double rate = 44100;
        juce::AudioBuffer<float> b (2, (int) (12 * rate));
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const double t = i / rate;
            const int chord[2][3] = { { 60, 64, 67 }, { 57, 60, 64 } };
            float v = 0;
            for (int p : chord[(int) (t / 2) % 2]) v += 0.1f * (float) std::sin (2 * 3.14159265358979 * 440 * std::pow (2.0, (p - 69) / 12.0) * t);
            b.setSample (0, i, v); b.setSample (1, i, v);
        }
        song.deleteFile();
        juce::WavAudioFormat wav;
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (song), rate, 2, 16, {}, 0)))
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }
    TranscriptionPlayer player;
    const double sr = 48000;
    const int block = 512;
    player.prepareToPlay (block, sr);
    player.loadFile (song, formats);
    expect (player.hasAudio() && player.length() > 10, "loads the file (" + juce::String (player.length(), 2) + " s)");

    // a melody note every half second, A4 then C5 ...
    std::vector<TranscriptionPlayer::Event> ev;
    for (int i = 0; i < 16; ++i)
    {
        ev.push_back ({ i * 0.5, i % 2 ? 72 : 69, TranscriptionPlayer::Melody, true });
        ev.push_back ({ i * 0.5 + 0.4, i % 2 ? 72 : 69, TranscriptionPlayer::Melody, false });
    }
    player.setEvents (ev);

    juce::AudioBuffer<float> buf (2, block);
    auto render = [&] (double seconds, juce::AudioBuffer<float>* keep = nullptr)
    {
        double rms = 0; int n = 0;
        for (int b = 0; b < (int) (seconds * sr / block); ++b)
        {
            buf.clear();
            player.getNextAudioBlock ({ &buf, 0, block });
            for (int i = 0; i < block; ++i) { rms += (double) buf.getSample (0, i) * buf.getSample (0, i); ++n; }
            if (keep != nullptr) { const int at = keep->getNumSamples(); keep->setSize (2, at + block, true); for (int c = 0; c < 2; ++c) keep->copyFrom (c, at, buf, c, 0, block); }
            juce::Thread::sleep (1);                 // let the read-ahead thread keep up, as in real time
        }
        return 10 * std::log10 (rms / juce::jmax (1, n) + 1e-20);
    };

    const double silent = render (0.2);
    expect (silent < -150, "paused: silence (" + juce::String (silent, 1) + " dB)");

    player.setVolume (1.0f);
    for (int mode : { TranscriptionPlayer::Original, TranscriptionPlayer::Transcription, TranscriptionPlayer::Mix })
    {
        player.pause(); player.seek (0); juce::Thread::sleep (100);
        player.setListen (mode);
        player.play();
        const double db = render (1.0);
        const char* names[] = { "Original", "Mix", "Transcription" };
        expect (db > -40 && db < 0, juce::String ("listen ") + names[mode] + ": " + juce::String (db, 1) + " dB, " + juce::String (player.position(), 2) + " s");
    }

    // the synth alone follows the events: sound while a note is on, silence in the gap between notes
    player.pause(); player.setListen (TranscriptionPlayer::Transcription);
    player.seek (2.0); juce::Thread::sleep (100); player.play();
    juce::AudioBuffer<float> take (2, 0);
    render (0.5, &take);
    auto level = [&] (double from, double to) { double r = 0; int a = (int) (from * sr), b = (int) (to * sr); for (int i = a; i < b; ++i) r += take.getSample (0, i) * take.getSample (0, i); return 10 * std::log10 (r / (b - a) + 1e-20); };
    const double on = level (0.05, 0.35), gap = level (0.44, 0.47);
    if (std::getenv ("PLAYER_DEBUG")) for (double t = 0.30; t < 0.52; t += 0.01) std::printf ("  %.2f  %.1f dB\n", t, level (t, t + 0.01));
    expect (on > -35 && gap < on - 20, "synth follows the notes: " + juce::String (on, 1) + " dB during a note, " + juce::String (gap, 1) + " dB in the gap");

    // seeking: position jumps, playback continues from there
    player.seek (10.0); juce::Thread::sleep (100);
    render (0.25);
    expect (std::abs (player.position() - 10.25) < 0.05, "seek to 10 s: now at " + juce::String (player.position(), 2) + " s");

    if (argc > 2)
    {
        player.pause(); player.seek (0); juce::Thread::sleep (100);
        player.setListen (TranscriptionPlayer::Mix); player.setVolume (0.8f);
        player.play();
        juce::AudioBuffer<float> mix (2, 0);
        render (8.0, &mix);
        const juce::File out { juce::String (juce::CharPointer_UTF8 (argv[2])) };
        out.deleteFile();
        juce::WavAudioFormat wav;
        if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (new juce::FileOutputStream (out), sr, 2, 16, {}, 0)))
            w->writeFromAudioSampleBuffer (mix, 0, mix.getNumSamples());
    }
    std::printf (failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
