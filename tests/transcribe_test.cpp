// The transcription engine end to end: a synthetic progression (C - Am - F - G with a melody, piano-like
// tones at 44.1 kHz) is resampled to 16 kHz and transcribed, then chords and melody are found.
//
//   transcribe_test <model.gguf>             runs the engine (CI: tools/make_tiny_model.py's random model)
//   transcribe_test <model.gguf> --musical   also checks the musical result (needs a real model)
#include "../Source/transcribe/TranscribeEngine.h"
#include "../Source/transcribe/Harmony.h"
#include "../Source/transcribe/Resample.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: transcribe_test model.gguf [--musical]\n"); return 2; }
    const bool musical = argc > 2 && std::strcmp (argv[2], "--musical") == 0;

    const double sr = 44100;
    const int prog[4][4] = { { 48, 55, 60, 64 }, { 45, 52, 57, 60 }, { 41, 53, 57, 60 }, { 43, 55, 59, 62 } };
    const int mel[4][4]  = { { 76, 74, 72, 74 }, { 72, 71, 69, 71 }, { 69, 72, 77, 76 }, { 74, 71, 67, 74 } };
    std::vector<float> x ((size_t) (sr * 8.5));
    auto tone = [&] (int midi, double start, double dur, double amp)
    {
        const double f = 440 * std::pow (2.0, (midi - 69) / 12.0);
        for (size_t i = (size_t) (start * sr); i < (size_t) ((start + dur) * sr) && i < x.size(); ++i)
        {
            const double t = (double) i / sr - start, env = std::exp (-t * 2.5) * std::min (1.0, t * 200);
            double v = 0, a = 1;
            for (int k = 1; k <= 6; ++k, a *= 0.6) v += a * std::sin (2 * 3.14159265358979 * f * k * t);
            x[i] += (float) (amp * env * v);
        }
    };
    for (int c = 0; c < 4; ++c)
    {
        for (int m : prog[c]) tone (m, c * 2.0, 2.0, 0.12);
        for (int j = 0; j < 4; ++j) tone (mel[c][j], c * 2.0 + j * 0.5, 0.5, 0.2);
    }
    float pk = 0; for (float v : x) pk = std::max (pk, std::abs (v));
    for (float& v : x) v /= pk * 1.2f;

    const auto audio = transcribe::resample (x.data(), x.size(), sr, transcribe::kSampleRate);
    const auto devs = transcribe::devices();
    std::printf ("devices:");
    for (auto& d : devs) std::printf (" [%s, %s]", d.name.c_str(), d.backend.c_str());
    std::printf ("  auto: %d  cpu variant: %s  gpu build: %d\n", transcribe::autoDevice(), transcribe::cpuVariant().c_str(), (int) transcribe::gpuBuild());

    transcribe::Request rq;
    rq.modelPath = argv[1];
    int chunks = 0;
    rq.chunk = [&] (const std::vector<transcribe::Note>&, double, float) { ++chunks; };
    const auto out = transcribe::run (audio.data(), audio.size(), rq);
    std::printf ("status %d %s  on %s  load %.2f s  run %.2f s  %zu notes  %d chunk updates\n", (int) out.status, out.error.c_str(),
                 out.deviceName.c_str(), out.loadSeconds, out.runSeconds, out.notes.size(), chunks);
    int failures = out.status == transcribe::Outcome::Ok && chunks > 0 ? 0 : 1;

    // cancelling stops a run
    transcribe::Request cancelled = rq;
    cancelled.cancel = [] { return true; };
    const auto c = transcribe::run (audio.data(), audio.size(), cancelled);
    std::printf ("%s cancel -> status %d\n", c.status == transcribe::Outcome::Cancelled ? "PASS" : "FAIL", (int) c.status);
    failures += c.status != transcribe::Outcome::Cancelled;

    // a missing model fails cleanly
    transcribe::Request missing; missing.modelPath = "does-not-exist.gguf";
    const auto m = transcribe::run (audio.data(), audio.size(), missing);
    std::printf ("%s missing model -> %s\n", m.status == transcribe::Outcome::Failed ? "PASS" : "FAIL", m.error.c_str());
    failures += m.status != transcribe::Outcome::Failed;

    if (musical)
    {
        const auto key = harmony::estimateKey (out.notes);
        std::string names;
        for (auto& ch : harmony::chords (out.notes, key)) names += (names.empty() ? "" : " ") + ch.name;
        const auto mm = harmony::melody (out.notes);
        const bool ok = names == "C Am F G" && key.tonic == 0 && ! key.minor && mm.size() == 16;
        std::printf ("%s musical: key %s, chords '%s', %zu melody notes\n", ok ? "PASS" : "FAIL", key.name().c_str(), names.c_str(), mm.size());
        failures += ! ok;
    }
    std::printf (failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures ? 1 : 0;
}
