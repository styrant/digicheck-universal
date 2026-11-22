#include "TranscriptionSession.h"
#include "Resample.h"
#include <map>

TranscriptionSession::TranscriptionSession() : juce::Thread ("Transcription")
{
    formats.registerBasicFormats();
}

TranscriptionSession::~TranscriptionSession()
{
    cancelRequest = true;
    stopThread (30000);
}

bool TranscriptionSession::canRead (const juce::File& f) const   { return formats.findFormatForFileExtension (f.getFileExtension()) != nullptr; }
juce::String TranscriptionSession::wildcard() const               { return formats.getWildcardForAllFormats(); }

void TranscriptionSession::setState (State s, const juce::String& message)
{
    const std::lock_guard<std::mutex> g (lock);
    st.state = s;
    st.message = message;
    if (s != State::Running && s != State::Loading && s != State::Preparing) st.progress = 0;
    ++st.version;
}

TranscriptionSession::Status TranscriptionSession::status() const
{
    const std::lock_guard<std::mutex> g (lock);
    return st;
}

std::vector<transcribe::Note> TranscriptionSession::notes() const
{
    const std::lock_guard<std::mutex> g (lock);
    return noteList;
}

juce::File TranscriptionSession::sourceFile() const
{
    const std::lock_guard<std::mutex> g (lock);
    return srcFile;
}

std::vector<float> TranscriptionSession::peaks (int perSecond) const
{
    const std::lock_guard<std::mutex> g (lock);
    const int step = juce::jmax (1, transcribe::kSampleRate / juce::jmax (1, perSecond));
    std::vector<float> p ((audio.size() + (size_t) step - 1) / (size_t) step);
    for (size_t i = 0; i < audio.size(); ++i) p[i / (size_t) step] = std::max (p[i / (size_t) step], std::abs (audio[i]));
    return p;
}

//==============================================================================
void TranscriptionSession::clear()
{
    cancel();
    stopThread (30000);
    const std::lock_guard<std::mutex> g (lock);
    audio.clear(); noteList.clear(); srcFile = juce::File();
    const int v = st.version;
    st = {};
    st.version = v + 1;
}

void TranscriptionSession::cancel() { cancelRequest = true; }

void TranscriptionSession::loadFile (const juce::File& f)
{
    clear();
    {
        const std::lock_guard<std::mutex> g (lock);
        pendingFile = f;
        task = Task::Decode;
        st.sourceName = f.getFileName();
    }
    cancelRequest = false;
    setState (State::Preparing, "Reading the file...");
    startThread (juce::Thread::Priority::low);
}

void TranscriptionSession::setAudio (std::vector<float> mono, double sampleRate, const juce::String& name)
{
    clear();
    {
        const std::lock_guard<std::mutex> g (lock);
        pendingRaw = std::move (mono);
        pendingRate = sampleRate;
        task = Task::Resample;
        st.sourceName = name;
    }
    cancelRequest = false;
    setState (State::Preparing, "Preparing the recording...");
    startThread (juce::Thread::Priority::low);                 // resampling minutes of audio takes a moment
}

void TranscriptionSession::transcribe (const juce::File& model, int device, std::vector<int> instruments)
{
    if (isThreadRunning()) return;
    {
        const std::lock_guard<std::mutex> g (lock);
        if (audio.empty()) return;
        modelFile = model; deviceIndex = device; instrumentIds = std::move (instruments);
        noteList.clear();
        st.noteCount = 0; st.speed = 0; st.deviceName = {};
        task = Task::Transcribe;
    }
    cancelRequest = false;
    setState (State::Loading, "Loading the model...");
    startThread (juce::Thread::Priority::low);
}

void TranscriptionSession::run()
{
    Task t;
    { const std::lock_guard<std::mutex> g (lock); t = task; task = Task::None; }
    if (t == Task::Resample)
    {
        std::vector<float> raw;
        double rate;
        { const std::lock_guard<std::mutex> g (lock); raw = std::move (pendingRaw); rate = pendingRate; }
        auto a = transcribe::resample (raw.data(), raw.size(), rate, transcribe::kSampleRate);
        {
            const std::lock_guard<std::mutex> g (lock);
            audio = std::move (a);
            st.seconds = (double) audio.size() / transcribe::kSampleRate;
        }
        setState (audio.empty() ? State::Empty : State::Ready);
    }
    else if (t == Task::Decode)     decode (pendingFile);
    else if (t == Task::Transcribe) runEngine();
}

void TranscriptionSession::decode (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr) { setState (State::Failed, "Can't read " + f.getFileName()); return; }
    const double sr = reader->sampleRate;
    const auto total = reader->lengthInSamples;
    if (sr <= 0 || total <= 0) { setState (State::Failed, f.getFileName() + " has no audio"); return; }
    if ((double) total / sr > 20 * 60) { setState (State::Failed, "The file is longer than 20 minutes"); return; }

    // mono mix (averaged, so a hot master can't clip), read in blocks
    const int chans = (int) reader->numChannels;
    std::vector<float> mono ((size_t) total);
    juce::AudioBuffer<float> buf (chans, 65536);
    for (juce::int64 pos = 0; pos < total && ! threadShouldExit() && ! cancelRequest; pos += 65536)
    {
        const int n = (int) juce::jmin<juce::int64> (65536, total - pos);
        reader->read (&buf, 0, n, pos, true, true);
        for (int i = 0; i < n; ++i)
        {
            float s = 0;
            for (int c = 0; c < chans; ++c) s += buf.getSample (c, i);
            mono[(size_t) (pos + i)] = s / (float) chans;
        }
        const std::lock_guard<std::mutex> g (lock);
        st.progress = 0.8f * (float) ((double) pos / (double) total);
    }
    if (cancelRequest) { setState (State::Empty); return; }
    auto a = transcribe::resample (mono.data(), mono.size(), sr, transcribe::kSampleRate);
    {
        const std::lock_guard<std::mutex> g (lock);
        audio = std::move (a);
        srcFile = f;
        st.seconds = (double) audio.size() / transcribe::kSampleRate;
    }
    setState (State::Ready);
}

void TranscriptionSession::runEngine()
{
    transcribe::Request rq;
    std::vector<float> a;
    {
        const std::lock_guard<std::mutex> g (lock);
        a = audio;                                  // the engine reads its own copy; the UI keeps drawing ours
        rq.modelPath = modelFile.getFullPathName().toStdString();
        rq.device = deviceIndex;
        rq.instruments = instrumentIds;
    }
    rq.cancel = [this] { return cancelRequest.load() || threadShouldExit(); };
    const double audioSeconds = (double) a.size() / transcribe::kSampleRate;
    const double t0 = juce::Time::getMillisecondCounterHiRes();
    bool running = false;
    double runStart = 0;
    rq.chunk = [&] (const std::vector<transcribe::Note>& fresh, double, float progress)
    {
        const std::lock_guard<std::mutex> g (lock);
        if (! running) { running = true; st.state = State::Running; st.message = "Transcribing..."; }
        noteList.insert (noteList.end(), fresh.begin(), fresh.end());
        st.noteCount = (int) noteList.size();
        st.progress = progress;
        const double el = (juce::Time::getMillisecondCounterHiRes() - runStart) / 1000.0;
        if (el > 0.5) st.speed = audioSeconds * progress / el;
        ++st.version;
    };
    // the first chunk callback comes after the first 5 s are decoded: switch to Running as soon as
    // the weights are in, from the load progress reaching 1
    rq.loadProgress = [&] (float p)
    {
        const std::lock_guard<std::mutex> g (lock);
        st.progress = p;
        if (p >= 1.0f && ! running) { running = true; runStart = juce::Time::getMillisecondCounterHiRes(); st.state = State::Running; st.message = "Transcribing..."; st.progress = 0; ++st.version; }
    };
    runStart = t0;

    auto out = transcribe::run (a.data(), a.size(), rq);
    {
        const std::lock_guard<std::mutex> g (lock);
        st.deviceName = out.deviceName;
        if (out.status == transcribe::Outcome::Ok)
        {
            noteList = std::move (out.notes);
            st.noteCount = (int) noteList.size();
            if (out.runSeconds > 0) st.speed = audioSeconds / out.runSeconds;
        }
    }
    if (out.status == transcribe::Outcome::Ok)             setState (State::Done);
    else if (out.status == transcribe::Outcome::Cancelled) setState (State::Cancelled, "Stopped. The notes so far are kept");
    else                                                   setState (State::Failed, out.error);
}

//==============================================================================
namespace midiexport
{
    juce::MidiFile build (const std::vector<transcribe::Note>& notes, const harmony::Key& key, const harmony::Options& opt,
                          bool withNotes, bool withMelody, bool withChords)
    {
        constexpr int tpq = 960;
        constexpr double ticksPerSecond = tpq * 2.0;            // 120 BPM
        auto tick = [] (double s) { return std::round (s * ticksPerSecond); };

        juce::MidiFile file;
        file.setTicksPerQuarterNote (tpq);

        juce::MidiMessageSequence conductor;
        conductor.addEvent (juce::MidiMessage::textMetaEvent (3, "Transcription"), 0);
        conductor.addEvent (juce::MidiMessage::tempoMetaEvent (500000), 0);
        conductor.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
        if (key.valid)
        {
            // sharps (+) or flats (-) of the major key with the same notes
            static const int sharpsForMajor[12] = { 0, -5, 2, -3, 4, -1, 6, 1, -4, 3, -2, 5 };
            const int major = key.minor ? (key.tonic + 3) % 12 : key.tonic;
            conductor.addEvent (juce::MidiMessage::keySignatureMetaEvent (sharpsForMajor[major], key.minor), 0);
        }
        file.addTrack (conductor);

        int nextChannel = 1;
        auto channelFor = [&] (bool drum) { if (drum) return 10; int c = nextChannel++; if (c == 10) c = nextChannel++; return ((c - 1) % 16) + 1; };
        auto addNotes = [&] (const juce::String& name, int program, bool drum, auto&& each)
        {
            juce::MidiMessageSequence seq;
            const int ch = channelFor (drum);
            seq.addEvent (juce::MidiMessage::textMetaEvent (3, name), 0);
            if (! drum && program >= 0 && program < 128) seq.addEvent (juce::MidiMessage::programChange (ch, program), 0);
            each ([&] (double on, double off, int pitch, int velocity)
            {
                seq.addEvent (juce::MidiMessage::noteOn (ch, juce::jlimit (0, 127, pitch), (juce::uint8) velocity), tick (on));
                seq.addEvent (juce::MidiMessage::noteOff (ch, juce::jlimit (0, 127, pitch)), std::max (tick (on) + 1, tick (off)));
            });
            seq.updateMatchedPairs();
            file.addTrack (seq);
        };

        if (withNotes)
        {
            std::map<int, std::vector<const transcribe::Note*>> byProgram;
            for (auto& n : notes) byProgram[n.drum ? transcribe::kDrumProgram : n.program].push_back (&n);
            for (auto& [program, list] : byProgram)
                addNotes (juce::String (transcribe::instrumentName (program)), program, program == transcribe::kDrumProgram, [&] (auto&& add)
                          { for (auto* n : list) add (n->onset, n->offset, n->pitch, 100); });
        }
        if (withMelody)
        {
            const auto mel = harmony::melody (notes, opt);
            if (! mel.empty())
                addNotes ("Melody", mel.front().program, false, [&] (auto&& add) { for (auto& m : mel) add (m.onset, m.offset, m.pitch, 110); });
        }
        if (withChords)
        {
            const auto cs = harmony::chords (notes, key, opt);
            addNotes ("Chords", 0, false, [&] (auto&& add)
            {
                for (auto& c : cs)
                {
                    if (c.root < 0) continue;
                    const auto& t = harmony::qualityTemplate (c.quality);
                    for (int i = 0; i < 12; ++i)
                        if (t[(size_t) i]) add (c.start, c.end, 60 + (c.root + i) % 12 + ((c.root + i) % 12 < 4 ? 12 : 0), 80);
                    add (c.start, c.end, 36 + (c.bass >= 0 ? c.bass : c.root), 90);
                }
            });
        }
        return file;
    }
}
