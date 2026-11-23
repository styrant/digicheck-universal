#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    std::unique_ptr<juce::PropertiesFile> openGlobal()
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "DigiCheck Universal Plugin";
        o.folderName = "DigiCheck Universal";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        o.processLock = nullptr;
        return std::make_unique<juce::PropertiesFile> (o);
    }
    double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
}

DcuProcessor::DcuProcessor()
    : juce::AudioProcessor (BusesProperties()
                              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                              .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      global (openGlobal())
{
    core.registerWith (engine);
    core.monitorTap.setActive (true);
    core.monitorModel.configure (48000);
    startTimerHz (10);
    engine.setChannels (0, 1);
    scEngine.setChannels (0, 1);
    // band listening: the crossover points the user chose last (the bands themselves always start on)
    auto splits = juce::StringArray::fromTokens (core.settings.listenSplits, ",", {});
    for (int k = 0; k < 4 && k < splits.size(); ++k) iso.splits[(size_t) k] = splits[k].getFloatValue();
}

DcuProcessor::~DcuProcessor()
{
    instancelink::Hub::get().remove (hubId);
    { const juce::SpinLock::ScopedLockType sl (linkLock); linked.reset(); }
    stopTimer();
    engine.setAnalyzer (nullptr);
}

bool DcuProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (in != out) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    if (l.inputBuses.size() > 1)
    {
        const auto sc = l.inputBuses[1];
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

void DcuProcessor::prepareToPlay (double sampleRate, int maxBlock)
{
    sr = sampleRate;
    linkL.assign ((size_t) juce::jmax (maxBlock, 8192), 0.0f); linkR.assign (linkL.size(), 0.0f);
    engine.prepare (sampleRate);
    scEngine.prepare (sampleRate);
    isolator.prepare (sampleRate);
    const auto n = (size_t) juce::jmax (maxBlock, 4096);
    phase.assign (n, -1.0f); monoL.assign (n, 0.0f); monoR.assign (n, 0.0f);
}

void DcuProcessor::releaseResources()
{
    engine.release();
    scEngine.release();
}

void DcuProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    if (total <= 0) return;
    lastBlockMs = nowMs();

    auto main = getBusBuffer (buffer, true, 0);
    const int nch = main.getNumChannels();
    if (nch == 0) return;

    // the sidechain, if the host routes something to it
    const float* scL = nullptr; const float* scR = nullptr;
    if (getBusCount (true) > 1)
        if (auto* bus = getBus (true, 1); bus != nullptr && bus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            if (sc.getNumChannels() > 0)
            {
                scL = sc.getReadPointer (0); scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL;
                if (sc.getMagnitude (0, total) > 1.0e-5f) lastScSignalMs = nowMs();
            }
        }
    scLive = scL != nullptr && nowMs() - lastScSignalMs.load() < 2000.0;

    // this track's audio for other instances; and the linked instance's audio, used like the sidechain
    {
        const float* L = main.getReadPointer (0); const float* R = nch > 1 ? main.getReadPointer (1) : L;
        ownRing->write (L, R, total);
        const juce::SpinLock::ScopedTryLockType sl (linkLock);
        bool ok = false;
        if (sl.isLocked() && linked != nullptr && (int) linkL.size() >= total)
            ok = linked->readLatest (linkL.data(), linkR.data(), total);
        linkOk = ok;
        if (ok) { scL = linkL.data(); scR = linkR.data(); scLive = true; }
    }

    // the host's tempo and position
    double ppq = 0, bpmNow = 0, barQn = 4, hostSeconds = 0;
    bool isPlaying = false, havePpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpmNow = *b;
            isPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { ppq = *p; havePpq = true; }
            if (auto t = pos->getTimeInSeconds()) hostSeconds = *t;
            if (auto ts = pos->getTimeSignature(); ts && ts->denominator > 0) barQn = 4.0 * ts->numerator / ts->denominator;
        }
    bpm = bpmNow; playing = isPlaying;

    // in pieces no longer than the work buffers (hosts may exceed the announced block size)
    const int piece = (int) phase.size();
    for (int off = 0; off < total; off += piece)
    {
        const int n = juce::jmin (piece, total - off);
        const float* L = main.getReadPointer (0) + off;
        const float* R = (nch > 1 ? main.getReadPointer (1) : main.getReadPointer (0)) + off;
        const float* in[2] = { L, R };
        engine.push (in, 2, n);
        const float* sl = scL ? scL + off : nullptr; const float* sr2 = scR ? scR + off : nullptr;
        if (sl != nullptr) { const float* sc[2] = { sl, sr2 }; scEngine.push (sc, 2, n); }

        // the Scope's sync phase: where each sample falls in its window, which starts on a bar line
        const double windowQn = juce::jmax (0.25, syncBars.load() * barQn);
        if (isPlaying && havePpq && bpmNow > 0)
        {
            const double qnPerSample = bpmNow / 60.0 / sr, start = ppq + off * qnPerSample;
            for (int i = 0; i < n; ++i)
            {
                double p = std::fmod (start + i * qnPerSample, windowQn) / windowQn;
                if (p < 0) p += 1.0;
                phase[(size_t) i] = (float) p;
            }
        }
        else std::fill (phase.begin(), phase.begin() + n, -1.0f);

        core.scopeTap.pushAll (L, R, sl, sr2, phase.data(), n, sr);
        core.fftTap.pushAll (L, R, sl, sr2, nullptr, n, sr);
    }

    // the reference track instead of the mix (Ref > Reference), in step with the transport, after the analysis
    // has seen the mix; band listening below then applies to whichever is heard
    if (nch > 1) core.reference.renderSynced (main.getWritePointer (0), main.getWritePointer (1), total, sr, hostSeconds, isPlaying);
    else
    {
        float* m = main.getWritePointer (0);
        core.reference.renderSynced (m, m, total, sr, hostSeconds, isPlaying);
    }

    // band listening on the output (main bus; the input and output share these channels). The isolator
    // returns at once, leaving the audio untouched, when nothing is active and its fade-out has finished.
    if (nch > 1) isolator.process (main.getWritePointer (0), main.getWritePointer (1), total, iso);
    else if (iso.active())
        for (int off = 0; off < total; off += piece)
        {
            const int n = juce::jmin (piece, total - off);
            float* m = main.getWritePointer (0) + off;
            std::copy (m, m + n, monoL.begin()); std::copy (m, m + n, monoR.begin());
            isolator.process (monoL.data(), monoR.data(), n, iso);
            for (int i = 0; i < n; ++i) m[i] = 0.5f * (monoL[(size_t) i] + monoR[(size_t) i]);
        }
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c) buffer.clear (c, 0, total);
}

bool DcuProcessor::isProcessing() const { return nowMs() - lastBlockMs.load() < 1000.0; }

juce::String DcuProcessor::describe() const
{
    juce::String t = "VST3";
    if (auto type = juce::PluginHostType(); type.type != juce::PluginHostType::UnknownHost) t << " in " << type.getHostDescription();
    t << "  |  " << (isProcessing() ? juce::String (sr / 1000.0, 1) + " kHz" : juce::String ("no audio"));
    t << "  |  sidechain: " << (sidechainConnected() ? "connected" : (getBusCount (true) > 1 && getBus (true, 1)->isEnabled() ? "silent" : "not routed"));
    if (bpm.load() > 0) t << "  |  " << juce::String (bpm.load(), 1) << " BPM" << (playing.load() ? ", playing" : "");
    return t;
}

void DcuProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::StringArray splits;
    for (auto& f : iso.splits) splits.add (juce::String (f.load(), 1));
    core.settings.listenSplits = splits.joinIntoString (",");
    core.save();
    if (auto xml = state.createXml ("DigiCheckUniversal")) copyXmlToBinary (*xml, dest);
}

void DcuProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        state.restoreFromXml (*xml);
        core.settings.load (state);
        if (core.settings.analyzerState.isNotEmpty()) core.analyzerProc.restoreStateXml (core.settings.analyzerState);
        auto splits = juce::StringArray::fromTokens (core.settings.listenSplits, ",", {});
        for (int k = 0; k < 4 && k < splits.size(); ++k) iso.splits[(size_t) k] = splits[k].getFloatValue();
        stateRestored = true;                        // the editor (if open) picks this up and rebuilds
    }
}

juce::AudioProcessorEditor* DcuProcessor::createEditor() { return new DcuEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new DcuProcessor(); }


//==============================================================================
// Instance linking
void DcuProcessor::updateTrackProperties (const TrackProperties& p)
{
    if (p.name.has_value()) instancelink::Hub::get().rename (hubId, *p.name);
}

juce::StringArray DcuProcessor::otherInstances() const
{
    juce::StringArray names;
    for (auto& i : instancelink::Hub::get().list()) if (i.id != hubId) names.addIfNotAlreadyThere (i.name);
    return names;
}

void DcuProcessor::setLink (const juce::String& n)
{
    core.settings.linkName = n;
    core.save();
    resolveLink();
}

void DcuProcessor::resolveLink()
{
    // by track name, so a project reopened (new instance ids) links again
    const auto want = core.settings.linkName;
    std::shared_ptr<instancelink::Ring> ring;
    if (want.isNotEmpty())
        for (auto& i : instancelink::Hub::get().list()) if (i.id != hubId && i.name == want) { ring = i.ring; break; }
    if (ring == linked) return;
    auto old = linked;                          // freed here (message thread), not on the audio thread
    { const juce::SpinLock::ScopedLockType sl (linkLock); linked = ring; }
    linkedTo = want;
    old.reset();
}
