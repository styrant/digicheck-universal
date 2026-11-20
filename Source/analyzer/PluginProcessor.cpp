#include "PluginProcessor.h"

namespace
{
    const juce::Identifier stateType   { "SpectrumAnalyzer" };
    const juce::Identifier coloursType { "Colours" };
    const juce::Identifier viewType    { "View" };
    constexpr int stateVersion = 1;

    void setPlain (juce::AudioProcessorValueTreeState& s, const char* pid, float plain)
    {
        if (auto* p = s.getParameter (pid))
            p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }
}

//==============================================================================
SpectrumAnalyzerProcessor::SpectrumAnalyzerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", sa::params::createLayout()),
      bank (std::make_unique<sa::FilterBank>()),
      ring (std::make_unique<Ring>())
{
    applyColourScheme (sa::legacy::colourSchemes().front());

    // "Save as default" (feature of the original 1.09) now lives in a
    // per-user settings file instead of the Windows registry.
    if (auto props = openUserSettings())
        if (auto xml = props->getXmlValue ("defaultState"))
            restoreFromTree (juce::ValueTree::fromXml (*xml));
}

SpectrumAnalyzerProcessor::~SpectrumAnalyzerProcessor() = default;

std::unique_ptr<juce::PropertiesFile> SpectrumAnalyzerProcessor::openUserSettings() const
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Spectrum Analyzer";
    o.folderName          = "Seven Phases";
    o.filenameSuffix      = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    return std::make_unique<juce::PropertiesFile> (o);
}

//==============================================================================
void SpectrumAnalyzerProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate.store (newSampleRate);
    publishInterval = juce::jmax<int64_t> (64, (int64_t) (newSampleRate / 100.0)); // ~10 ms
    bank->configure ({ newSampleRate, sa::params::bandsPerOctave (apvts),
                       (sa::Anchor) (int) sa::params::get (apvts, sa::params::anchor) });
}

bool SpectrumAnalyzerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SpectrumAnalyzerProcessor::setUiActive (bool active) noexcept
{
    if (active && ! uiActive.load())
        resetRequested.store (true);
    uiActive.store (active, std::memory_order_release);
}

template <typename T>
void SpectrumAnalyzerProcessor::process (juce::AudioBuffer<T>& buffer)
{
    juce::ScopedNoDenormals noDenormals;

    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int n      = buffer.getNumSamples();

    // audio passes through untouched (in-place buffer)
    for (int c = numIn; c < numOut; ++c)
        buffer.clear (c, 0, n);

    if (! uiActive.load (std::memory_order_acquire) || numIn == 0 || n == 0)
        return;

    if (resetRequested.exchange (false))
        bank->reset();

    // Layout changes are applied here, on the audio thread, so the filter
    // state is never touched concurrently (the original reset it from the UI
    // thread while process() was running).
    const sa::LayoutKey key { sampleRate.load (std::memory_order_relaxed),
                              sa::params::bandsPerOctave (apvts),
                              (sa::Anchor) (int) sa::params::get (apvts, sa::params::anchor) };
    if (key != bank->getLayout().key)
        bank->configure (key);

    const auto mode = (sa::ChannelMode) juce::jlimit (0, 4, (int) sa::params::get (apvts, sa::params::channel));
    bank->process (buffer.getArrayOfReadPointers(), juce::jmin (numIn, 2), n, mode);

    if (bank->accumulatedSamples() >= publishInterval)
    {
        if (auto* slot = ring->beginWrite())
        {
            bank->takeSnapshot (*slot);
            ring->commitWrite();
        }
        // if the ring is full the accumulators simply keep collecting
    }
}

void SpectrumAnalyzerProcessor::processStereo (const float* left, const float* right, int n) noexcept
{
    if (! uiActive.load (std::memory_order_acquire) || n <= 0)
        return;

    if (resetRequested.exchange (false))
        bank->reset();

    const sa::LayoutKey key { sampleRate.load (std::memory_order_relaxed),
                              sa::params::bandsPerOctave (apvts),
                              (sa::Anchor) (int) sa::params::get (apvts, sa::params::anchor) };
    if (key != bank->getLayout().key)
        bank->configure (key);

    const float* chans[2] = { left, right };
    const auto mode = (sa::ChannelMode) juce::jlimit (0, 4, (int) sa::params::get (apvts, sa::params::channel));
    bank->process (chans, 2, n, mode);

    if (bank->accumulatedSamples() >= publishInterval)
        if (auto* slot = ring->beginWrite())
        {
            bank->takeSnapshot (*slot);
            ring->commitWrite();
        }
}

juce::String SpectrumAnalyzerProcessor::saveStateXml()
{
    if (auto xml = createStateTree().createXml())
        return xml->toString (juce::XmlElement::TextFormat().singleLine());
    return {};
}

void SpectrumAnalyzerProcessor::restoreStateXml (const juce::String& text)
{
    if (auto xml = juce::parseXML (text))
        restoreFromTree (juce::ValueTree::fromXml (*xml));
}

void SpectrumAnalyzerProcessor::processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&)  { process (b); }
void SpectrumAnalyzerProcessor::processBlock (juce::AudioBuffer<double>& b, juce::MidiBuffer&) { process (b); }

//==============================================================================

//==============================================================================
int SpectrumAnalyzerProcessor::getNumPrograms()
{
    return (int) sa::legacy::factoryPresets().size();
}

const juce::String SpectrumAnalyzerProcessor::getProgramName (int index)
{
    const auto& p = sa::legacy::factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? juce::String (p[(size_t) index].name) : juce::String();
}

void SpectrumAnalyzerProcessor::setCurrentProgram (int index)
{
    const auto& p = sa::legacy::factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) p.size()))
        return;

    currentProgram = index;
    sa::legacy::Values v = sa::legacy::defaults;
    std::copy (p[(size_t) index].v.begin(), p[(size_t) index].v.end(), v.begin());
    applyLegacyValues (v, false); // factory presets keep the current colours
}

//==============================================================================
void SpectrumAnalyzerProcessor::applyColourScheme (const sa::legacy::ColourScheme& s)
{
    for (int i = 0; i < kNumColours; ++i)
        colours[(size_t) i].store (s.argb[(size_t) i]);
    setColourSchemeName (s.name);
}

void SpectrumAnalyzerProcessor::applyLegacyValues (const sa::legacy::Values& v, bool includeColours)
{
    using namespace sa::params;
    namespace L = sa::legacy;

    setPlain (apvts, channel,   (float) v[L::inputChannel]);      // Left, Right, Both, Mix(=Mid)
    setPlain (apvts, peakOn,    (float) v[L::peakEnable]);
    setPlain (apvts, peakDecay, (float) v[L::peakDecay]);
    setPlain (apvts, avgOn,     (float) v[L::avrgEnable]);
    setPlain (apvts, avgTime,   0.001f * (float) v[L::avrgTime]);
    setPlain (apvts, avgType,   (float) v[L::avrgBarType]);
    setPlain (apvts, avgSize,   (float) v[L::avrgBarSize]);
    setPlain (apvts, holdOn,    (float) v[L::holdEnable]);
    setPlain (apvts, holdInf,   (float) v[L::holdInfinite]);
    setPlain (apvts, holdTime,  0.001f * (float) v[L::holdTime]);
    setPlain (apvts, holdDecay, (float) v[L::holdDecay]);
    setPlain (apvts, holdType,  (float) v[L::holdBarType]);
    setPlain (apvts, holdSize,  (float) v[L::holdBarSize]);
    setPlain (apvts, ceiling,   (float) v[L::levelCeil]);
    setPlain (apvts, range,     (float) v[L::levelRange]);
    setPlain (apvts, grid,      (float) v[L::levelGrid]);
    setPlain (apvts, freqGrid,  (float) v[L::freqGridType]);      // Decade, Octave
    setPlain (apvts, bpo,       (float) v[L::bandsPerOctave]);    // index 0..2 = 3, 4, 6 (same order)
    setPlain (apvts, avgSlope,  0.5f * (float) v[L::avrgSlope]);  // was half-dB steps
    setPlain (apvts, anchor,    0.0f);

    if (includeColours)
    {
        for (int i = 0; i < kNumColours; ++i)
        {
            auto c = (juce::uint32) v[(size_t) (L::ColorsIndex + i)];
            if (i >= BkgTopColour)
                c |= 0xff000000u; // backgrounds are opaque ("rgb" in the original)
            colours[(size_t) i].store (c);
        }
        setColourSchemeName ("Imported");
    }
}

void SpectrumAnalyzerProcessor::applyLegacyPreset (const sa::legacy::ImportedPreset& p, bool includeColours)
{
    applyLegacyValues (p.values, includeColours);
    if (p.width > 0 && p.height > 0)
    {
        editorWidth.store (p.width);
        editorHeight.store (p.height);
        if (juce::MessageManager::existsAndIsCurrentThread())
            if (auto* ed = getActiveEditor())
                ed->setSize (p.width, p.height);
    }
}

bool SpectrumAnalyzerProcessor::importLegacyFile (const juce::File& file, juce::String& error)
{
    if (! file.existsAsFile() || file.getSize() > 1024 * 1024)
    {
        error = "File not found or too large.";
        return false;
    }

    juce::MemoryBlock mb;
    if (! file.loadFileAsData (mb))
    {
        error = "Could not read the file.";
        return false;
    }

    auto p = sa::legacy::parseFxFile (mb.getData(), mb.getSize());
    if (! p)
        p = sa::legacy::parseChunk (mb.getData(), mb.getSize());

    if (! p)
    {
        error = "Not a Seven Phases Spectrum Analyzer (v1.x) .fxp/.fxb preset.";
        return false;
    }

    applyLegacyPreset (*p, true);
    return true;
}

//==============================================================================
juce::ValueTree SpectrumAnalyzerProcessor::createStateTree()
{
    juce::ValueTree root (stateType);
    root.setProperty ("stateVersion", stateVersion, nullptr);
    root.setProperty ("pluginVersion", "2.0.0", nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    juce::ValueTree c (coloursType);
    c.setProperty ("scheme", getColourSchemeName(), nullptr);
    for (int i = 0; i < kNumColours; ++i)
        c.setProperty ("c" + juce::String (i), getColour (i).toString(), nullptr);
    root.appendChild (c, nullptr);

    juce::ValueTree v (viewType);
    v.setProperty ("low",       viewLowHz.load(), nullptr);
    v.setProperty ("high",      viewHighHz.load(), nullptr);
    v.setProperty ("inspector", inspectorEnabled.load(), nullptr);
    v.setProperty ("width",     editorWidth.load(), nullptr);
    v.setProperty ("height",    editorHeight.load(), nullptr);
    root.appendChild (v, nullptr);
    return root;
}

bool SpectrumAnalyzerProcessor::restoreFromTree (const juce::ValueTree& root)
{
    if (! root.hasType (stateType))
        return false;

    if ((int) root.getProperty ("stateVersion", 0) > stateVersion)
    {
        DBG ("State saved by a newer version; loading what we understand.");
    }

    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid())
        apvts.replaceState (params);

    const auto c = root.getChildWithName (coloursType);
    if (c.isValid())
    {
        setColourSchemeName (c.getProperty ("scheme", "Custom").toString());
        for (int i = 0; i < kNumColours; ++i)
        {
            const auto s = c.getProperty ("c" + juce::String (i)).toString();
            if (s.isNotEmpty())
                setColour (i, juce::Colour::fromString (s));
        }
    }

    const auto v = root.getChildWithName (viewType);
    if (v.isValid())
    {
        viewLowHz.store  ((double) v.getProperty ("low", 0.0));
        viewHighHz.store ((double) v.getProperty ("high", 0.0));
        inspectorEnabled.store ((bool) v.getProperty ("inspector", true));
        const int w = v.getProperty ("width", 0), h = v.getProperty ("height", 0);
        if (w > 0 && h > 0)
        {
            editorWidth.store (w);
            editorHeight.store (h);
        }
    }
    return true;
}

void SpectrumAnalyzerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = createStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void SpectrumAnalyzerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0)
        return;

    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (restoreFromTree (juce::ValueTree::fromXml (*xml)))
            return;

    // State from the original VST2 plug-in (passed through by the VST3 wrapper
    // when a host migrates a VST2 project). Bounds-checked parsing.
    if (auto legacy = sa::legacy::parseChunk (data, (size_t) sizeInBytes))
        applyLegacyPreset (*legacy, true);
}

void SpectrumAnalyzerProcessor::saveCurrentAsDefault()
{
    if (auto props = openUserSettings())
    {
        auto tree = createStateTree();
        if (auto xml = tree.createXml())
        {
            props->setValue ("defaultState", xml.get());
            props->saveIfNeeded();
        }
    }
}

void SpectrumAnalyzerProcessor::forgetUserDefault()
{
    if (auto props = openUserSettings())
    {
        props->removeValue ("defaultState");
        props->saveIfNeeded();
    }
}

