#include "ModelStore.h"

namespace
{
    // NeuralNote v2's pinned checkpoints (Lib/Model/ModelManifest.h): same files, sizes and digests.
    constexpr const char* kRepo = "DamRsn/muscriptor-gguf";
    constexpr const char* kRevision = "d7045f94e8b19427f4ff9542975035e66596e51c";
    constexpr const char* kRepoDir = "v1";

    const ModelStore::Entry kEntries[ModelStore::kSizes] = {
        { "Small",  "muscriptor-small-f16.gguf",  209425152,  "925f55af65a20ebc4f8b45ceaf095a12b72493d436cb112623cd0041a1af23d4",
          "210 MB. Fastest; fine for clear recordings. About real time on a CPU" },
        { "Medium", "muscriptor-medium-f16.gguf", 618442496,  "3850cc9e5b436b17a09bd25b8f2615cb3366ab96a71e7b50f73a793a917fdf03",
          "620 MB. Better on full mixes. A GPU is recommended" },
        { "Large",  "muscriptor-large-f16.gguf",  2739142176, "35a750fb1ab1e77195cdc2c0b9b4aeea2f4d59f11f729f02af9920c4854ef72e",
          "2.7 GB. Best quality; needs a GPU with 4 GB or more" },
    };

    constexpr int kTimeoutMs = 30000, kRedirects = 5, kChunk = 1 << 20;
    constexpr int kRetryMs[] = { 2000, 5000, 10000 };

    juce::URL urlFor (int size)
    {
        return juce::URL ("https://huggingface.co/" + juce::String (kRepo) + "/resolve/" + kRevision + "/" + kRepoDir + "/" + kEntries[size].fileName);
    }

    juce::File partFile (int size)
    {
        // named for the digest, so a part file from other pinned weights is never resumed
        return ModelStore::ownFolder().getChildFile (juce::String (kEntries[size].fileName) + "." + juce::String (kEntries[size].sha256).substring (0, 12) + ".part");
    }
}

const ModelStore::Entry& ModelStore::entry (int size) { return kEntries[juce::jlimit (0, kSizes - 1, size)]; }

juce::File ModelStore::ownFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("DigiCheck Universal").getChildFile ("Models");
}

juce::File ModelStore::neuralNoteFolder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/NeuralNote/models");
   #elif JUCE_LINUX
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".config/NeuralNote/models");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("NeuralNote").getChildFile ("models");
   #endif
}

juce::File ModelStore::testModelOverride()
{
    const auto p = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_MODEL", {});
    return p.isNotEmpty() && juce::File (p).existsAsFile() ? juce::File (p) : juce::File();
}

juce::File ModelStore::modelFile (int size)
{
    if (auto t = testModelOverride(); t != juce::File()) return t;
    const auto& e = entry (size);
    for (auto dir : { ownFolder(), neuralNoteFolder() })
    {
        auto f = dir.getChildFile (e.fileName);
        if (f.getSize() == e.bytes) return f;          // getSize is 0 when missing; no checkpoint is empty
    }
    return {};
}

//==============================================================================
class ModelStore::Job : public juce::Thread
{
public:
    explicit Job (int s) : juce::Thread ("Model download (" + juce::String (kEntries[s].label) + ")"), size (s) {}
    ~Job() override { requestStop(); stopThread (-1); }

    void begin()
    {
        if (isThreadRunning()) return;
        done = partFile (size).getSize();
        phase = Phase::Downloading;
        startThread (juce::Thread::Priority::low);
    }

    void requestStop()
    {
        signalThreadShouldExit();
        notify();
        const juce::ScopedLock sl (streamLock);
        if (stream != nullptr) stream->cancel();
    }

    Status getStatus() const
    {
        Status s;
        s.phase = phase.load();
        s.total = kEntries[size].bytes;
        s.done = s.busy() ? done.load() : partFile (size).getSize();
        if (s.phase == Phase::Failed) { const juce::ScopedLock sl (errorLock); s.error = error; }
        return s;
    }

private:
    struct Attempt { juce::String failure; bool canRetry = true; };

    void run() override
    {
        const auto failure = fetch();
        if (failure.isNotEmpty() && ! threadShouldExit())
        {
            const juce::ScopedLock sl (errorLock);
            error = failure;
            phase = Phase::Failed;
        }
        else phase = Phase::Idle;
    }

    juce::String fetch()
    {
        const auto& e = kEntries[size];
        if (ModelStore::isInstalled (size)) return {};

        juce::InterProcessLock lock ("DigiCheckUniversalModelDownload_" + juce::String (e.label));
        if (! lock.enter (0)) return "Another window is already downloading this model";

        const auto dir = ModelStore::ownFolder();
        if (! dir.createDirectory()) return "Could not create " + dir.getFullPathName();

        const auto part = partFile (size);
        int failedAttempts = 0;
        while (part.getSize() != e.bytes)
        {
            const auto before = part.getSize();
            const auto a = attempt (part, e);
            if (threadShouldExit()) return {};
            if (a.failure.isEmpty()) continue;
            if (! a.canRetry) return a.failure;
            if (part.getSize() > before) failedAttempts = 0;
            if (failedAttempts == (int) std::size (kRetryMs)) return a.failure;
            wait (kRetryMs[failedAttempts++]);
            if (threadShouldExit()) return {};
        }

        phase = Phase::Verifying;
        const auto digest = juce::SHA256 (part).toHexString();
        if (threadShouldExit()) return {};
        if (digest != e.sha256) { part.deleteFile(); return "The download was corrupted. Try again"; }
        if (! part.moveFileTo (dir.getChildFile (e.fileName))) return "Could not move the model into " + dir.getFullPathName();
        return {};
    }

    Attempt attempt (const juce::File& part, const Entry& e)
    {
        juce::int64 offset = part.getSize();
        if (offset > e.bytes) { part.deleteFile(); offset = 0; }
        done = offset;

        juce::WebInputStream s (urlFor (size), false);
        s.withConnectionTimeout (kTimeoutMs).withNumRedirectsToFollow (kRedirects);
        if (offset > 0) s.withExtraHeaders ("Range: bytes=" + juce::String (offset) + "-");
        { const juce::ScopedLock sl (streamLock); stream = &s; }
        const juce::ScopeGuard forget { [this] { const juce::ScopedLock sl (streamLock); stream = nullptr; } };
        if (threadShouldExit()) return {};

        if (! s.connect (nullptr)) return { "Could not reach huggingface.co. Check the internet connection" };
        const int code = s.getStatusCode();
        const auto range = s.getResponseHeaders()["Content-Range"];
        if (code == 206)
        {
            if (! range.startsWith ("bytes " + juce::String (offset) + "-") || range.fromLastOccurrenceOf ("/", false, false).getLargeIntValue() != e.bytes)
            { part.deleteFile(); return { "Unexpected response from huggingface.co" }; }
        }
        else if (code == 200)
        {
            if (offset > 0 && ! part.deleteFile()) return { "Could not write to " + part.getFullPathName(), false };
            offset = 0; done = 0;
        }
        else if (code == 416) { part.deleteFile(); return { "The partial download could not be resumed" }; }
        else if (code == 401 || code == 403) return { "huggingface.co refused the download (HTTP " + juce::String (code) + ")", false };
        else return { "huggingface.co answered HTTP " + juce::String (code), code >= 500 };

        juce::FileOutputStream out (part);
        if (out.failedToOpen()) return { "Could not write to " + part.getFullPathName(), false };
        juce::HeapBlock<char> buf (kChunk);
        while (! threadShouldExit())
        {
            const int n = s.read (buf.get(), kChunk);
            if (n <= 0) break;
            if (done.load() + n > e.bytes) return { "huggingface.co sent more than the expected size", false };
            if (! out.write (buf.get(), (size_t) n)) return { "Could not write to " + part.getFullPathName() + " (disk full?)", false };
            done += n;
        }
        out.flush();
        if (out.getStatus().failed()) return { "Could not write to " + part.getFullPathName(), false };
        if (done.load() != e.bytes && ! threadShouldExit()) return { "The connection dropped" };
        return {};
    }

    const int size;
    std::atomic<Phase> phase { Phase::Idle };
    std::atomic<juce::int64> done { 0 };
    juce::CriticalSection errorLock, streamLock;
    juce::String error;
    juce::WebInputStream* stream = nullptr;
};

ModelStore::ModelStore()
{
    for (int i = 0; i < kSizes; ++i) jobs[(size_t) i] = std::make_unique<Job> (i);
}

ModelStore::~ModelStore() = default;

void ModelStore::download (int size)              { jobs[(size_t) juce::jlimit (0, kSizes - 1, size)]->begin(); }
void ModelStore::cancel (int size)                { jobs[(size_t) juce::jlimit (0, kSizes - 1, size)]->requestStop(); }
ModelStore::Status ModelStore::status (int size) const { return jobs[(size_t) juce::jlimit (0, kSizes - 1, size)]->getStatus(); }
