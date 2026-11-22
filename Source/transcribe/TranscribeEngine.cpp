// C++23: the only file that includes muscriptor.cpp. See TranscribeEngine.h.
#include "TranscribeEngine.h"
#include "muscriptor/muscriptor.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <span>

namespace transcribe
{
    namespace
    {
        static_assert (kSampleRate == msl::Transcriber::SAMPLE_RATE);
        static_assert (kDrumProgram == msl::DRUM_PROGRAM);

        // the device list is built once per process (ggml enumerates GPUs once)
        const std::vector<msl::Device>& deviceList()
        {
            static std::once_flag once;
            static std::vector<msl::Device> list;
            std::call_once (once, [] { list = msl::availableDevices(); });
            return list;
        }

        std::string pretty (std::string_view snake)
        {
            std::string s (snake);
            for (auto& c : s) if (c == '_') c = ' ';
            if (! s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = (char) (s[0] - 'a' + 'A');
            return s;
        }

        // General MIDI names, for programs the model decodes outside its named groups
        const char* const kGm[128] = {
            "Acoustic grand piano", "Bright piano", "Electric grand", "Honky-tonk piano", "Electric piano 1", "Electric piano 2", "Harpsichord", "Clavinet",
            "Celesta", "Glockenspiel", "Music box", "Vibraphone", "Marimba", "Xylophone", "Tubular bells", "Dulcimer",
            "Drawbar organ", "Percussive organ", "Rock organ", "Church organ", "Reed organ", "Accordion", "Harmonica", "Tango accordion",
            "Nylon guitar", "Steel guitar", "Jazz guitar", "Clean guitar", "Muted guitar", "Overdriven guitar", "Distortion guitar", "Guitar harmonics",
            "Acoustic bass", "Finger bass", "Pick bass", "Fretless bass", "Slap bass 1", "Slap bass 2", "Synth bass 1", "Synth bass 2",
            "Violin", "Viola", "Cello", "Contrabass", "Tremolo strings", "Pizzicato strings", "Harp", "Timpani",
            "String ensemble", "Slow strings", "Synth strings 1", "Synth strings 2", "Choir aahs", "Voice oohs", "Synth voice", "Orchestra hit",
            "Trumpet", "Trombone", "Tuba", "Muted trumpet", "French horn", "Brass section", "Synth brass 1", "Synth brass 2",
            "Soprano sax", "Alto sax", "Tenor sax", "Baritone sax", "Oboe", "English horn", "Bassoon", "Clarinet",
            "Piccolo", "Flute", "Recorder", "Pan flute", "Blown bottle", "Shakuhachi", "Whistle", "Ocarina",
            "Square lead", "Saw lead", "Calliope lead", "Chiff lead", "Charang lead", "Voice lead", "Fifths lead", "Bass + lead",
            "New age pad", "Warm pad", "Polysynth pad", "Choir pad", "Bowed pad", "Metallic pad", "Halo pad", "Sweep pad",
            "Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-fi",
            "Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
            "Tinkle bell", "Agogo", "Steel drums", "Woodblock", "Taiko drum", "Melodic tom", "Synth drum", "Reverse cymbal",
            "Guitar fret noise", "Breath noise", "Seashore", "Bird tweet", "Telephone ring", "Helicopter", "Applause", "Gunshot" };

        std::string describe (msl::Error e, const std::string& path)
        {
            switch (e)
            {
                case msl::Error::FileNotFound:                  return "Model file not found: " + path;
                case msl::Error::InvalidCheckpoint:
                case msl::Error::UnsupportedArch:               return "Not a transcription model this version can read: " + path;
                case msl::Error::UnsupportedCheckpointVersion:  return "The model file is for another version. Delete it and download it again";
                case msl::Error::OutOfMemory:                   return "Not enough memory for this model. Try a smaller model or the CPU";
                case msl::Error::DeviceUnavailable:             return "The chosen device can't run the model. Choose Auto or CPU";
                case msl::Error::UnsupportedCpu:                return "This processor is too old for the transcription engine (needs SSE4.2 and POPCNT)";
                default:                                        return std::string (msl::describe (e));
            }
        }
    }

    std::vector<Device> devices()
    {
        std::vector<Device> out;
        for (auto& d : deviceList()) out.push_back ({ d.name, d.backend, d.integrated, (std::uint64_t) d.memory_total });
        return out;
    }

    int autoDevice()            { return (int) msl::autoDevice (deviceList()); }
    std::string cpuVariant()    { return std::string (msl::cpuVariant()); }

    bool gpuBuild()
    {
       #if defined (MUSCRIPTOR_HAS_VULKAN) || defined (MUSCRIPTOR_HAS_METAL) || defined (DCU_TRANSCRIBE_GPU)
        return true;
       #else
        return false;
       #endif
    }

    std::vector<Instrument> instruments()
    {
        std::vector<Instrument> out;
        for (auto g : msl::allInstrumentGroups()) out.push_back ({ (int) g, pretty (msl::instrumentName (g)) });
        return out;
    }

    std::string instrumentName (int program)
    {
        if (program == kDrumProgram) return "Drums";
        if (auto g = msl::instrumentGroupFor (program)) return pretty (msl::instrumentName (*g));
        return program >= 0 && program < 128 ? kGm[program] : "Program " + std::to_string (program);
    }

    bool looksLikeModel (const std::string& path, std::string* whyNot)
    {
        std::ifstream f (std::filesystem::u8path (path), std::ios::binary);
        char magic[4] {};
        if (! f.read (magic, 4)) { if (whyNot) *whyNot = "can't read the file"; return false; }
        if (std::string_view (magic, 4) != "GGUF") { if (whyNot) *whyNot = "not a GGUF model file"; return false; }
        return true;
    }

    Outcome run (const float* mono16k, std::size_t numSamples, const Request& rq)
    {
        using clock = std::chrono::steady_clock;
        Outcome out;
        const auto& list = deviceList();

        msl::LoadOptions lo;
        if (rq.device >= 0 && (std::size_t) rq.device < list.size()) lo.device = (std::size_t) rq.device;
        lo.should_cancel = rq.cancel;
        if (rq.loadProgress) lo.on_progress = [&] (float p) { rq.loadProgress (p); };

        const auto t0 = clock::now();
        auto model = msl::Transcriber::load (std::filesystem::u8path (rq.modelPath), std::move (lo));
        out.loadSeconds = std::chrono::duration<double> (clock::now() - t0).count();
        if (! model)
        {
            out.status = model.error() == msl::Error::Cancelled ? Outcome::Cancelled : Outcome::Failed;
            out.error = describe (model.error(), rq.modelPath);
            return out;
        }
        out.deviceName = model->device().name;

        msl::TranscribeOptions opt;
        for (int id : rq.instruments) opt.instruments.push_back ((msl::InstrumentGroup) id);
        opt.n_threads = rq.threads;
        opt.should_cancel = rq.cancel;

        std::vector<Note> batch;
        const msl::NoteCallback cb = [&] (const msl::TranscriptionUpdate& u)
        {
            if (rq.chunk)
            {
                batch.clear();
                for (auto& n : u.new_notes) batch.push_back ({ n.onset, n.offset, n.pitch, n.program, n.is_drum });
                rq.chunk (batch, u.finalized_through, u.progress);
            }
            return true;
        };

        const auto t1 = clock::now();
        auto notes = model->transcribe (std::span<const float> (mono16k, numSamples), opt, cb);
        out.runSeconds = std::chrono::duration<double> (clock::now() - t1).count();
        if (! notes)
        {
            out.status = notes.error() == msl::Error::Cancelled ? Outcome::Cancelled : Outcome::Failed;
            out.error = describe (notes.error(), rq.modelPath);
            return out;
        }
        out.notes.reserve (notes->size());
        for (auto& n : *notes) out.notes.push_back ({ n.onset, n.offset, n.pitch, n.program, n.is_drum });
        out.status = Outcome::Ok;
        return out;
    }
}
