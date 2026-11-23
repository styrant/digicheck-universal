// Loads the built VST3 like a DAW would and checks it from the outside:
//   - the sidechain bus can be enabled (stereo main + stereo sidechain)
//   - the audio passes through bit for bit
//   - the editor follows the host's tempo (128 BPM from the play head) and the state round-trips
//   - screenshots of the views with a sidechain routed (plugin_<view>.png), for a human to look at
// usage: plugin_host_test "<path to DigiCheck Universal.vst3>" <output folder>
#include <JuceHeader.h>

namespace
{
    int failures = 0;
    void expect (bool ok, const juce::String& what) { std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (! ok) ++failures; }

    // a hosted VST3's state is <VST3PluginState><IComponent>base64 of the plugin's own state</IComponent>...
    std::unique_ptr<juce::XmlElement> pluginXml (const juce::MemoryBlock& hostState, std::unique_ptr<juce::XmlElement>* wrapper = nullptr)
    {
        auto w = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
        if (w == nullptr) return nullptr;
        auto* comp = w->getChildByName ("IComponent");
        if (comp == nullptr) return nullptr;
        juce::MemoryBlock inner;
        inner.fromBase64Encoding (comp->getAllSubText().trim());
        auto x = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
        if (wrapper != nullptr) *wrapper = std::move (w);
        return x;
    }
    juce::MemoryBlock rewrap (juce::XmlElement& wrapper, const juce::XmlElement& pluginState)
    {
        juce::MemoryBlock inner; juce::AudioProcessor::copyXmlToBinary (pluginState, inner);
        auto* comp = wrapper.getChildByName ("IComponent");
        comp->deleteAllTextElements();
        comp->addTextElement (inner.toBase64Encoding());
        juce::MemoryBlock out; juce::AudioProcessor::copyXmlToBinary (wrapper, out);
        return out;
    }

    struct PlayHead : public juce::AudioPlayHead
    {
        double ppq = 0, bpm = 128;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setBpm (bpm); p.setIsPlaying (true); p.setPpqPosition (ppq);
            p.setTimeSignature (TimeSignature { 4, 4 });
            return p;
        }
    };
}

int main (int argc, char** argv)
{
    if (argc < 3) { std::printf ("usage: plugin_host_test <plugin.vst3> <out folder>\n"); return 2; }
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out { juce::String (argv[2]) };
    out.createDirectory();

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, juce::String (argv[1]));
    expect (types.size() == 1, "the VST3 is found: " + (types.isEmpty() ? juce::String ("nothing") : types[0]->name + " " + types[0]->version + " by " + types[0]->manufacturerName));
    if (types.isEmpty()) return 1;

    juce::String err;
    auto plugin = format.createInstanceFromDescription (*types[0], 48000, 512, err);
    expect (plugin != nullptr, "instantiated " + err);
    if (plugin == nullptr) return 1;

    // stereo in / out plus a stereo sidechain
    auto layout = plugin->getBusesLayout();
    if (layout.inputBuses.size() > 1) layout.inputBuses.getReference (1) = juce::AudioChannelSet::stereo();
    const bool busOk = layout.inputBuses.size() == 2 && plugin->setBusesLayout (layout);
    expect (busOk && plugin->getTotalNumInputChannels() == 4, "sidechain bus enabled (" + juce::String (plugin->getTotalNumInputChannels()) + " input channels)");

    std::printf ("buses: in %d (%s), out %d\n", plugin->getBusCount (true), plugin->getBusesLayout().inputBuses.size() > 1 ? plugin->getBusesLayout().inputBuses[1].getDescription().toRawUTF8() : "-", plugin->getBusCount (false));
    PlayHead head;
    plugin->setPlayHead (&head);
    plugin->setRateAndBufferSizeDetails (48000, 512);
    plugin->prepareToPlay (48000, 512);

    // music-ish main signal (a chord with a kick), a sidechain of kicks on the off-beats
    const double sr = 48000, tw = juce::MathConstants<double>::twoPi;
    long long pos = 0;
    bool exact = true;
    auto block = [&] (int n)
    {
        juce::AudioBuffer<float> buf (plugin->getTotalNumInputChannels(), n);
        juce::AudioBuffer<float> ref (2, n);
        for (int i = 0; i < n; ++i, ++pos)
        {
            const double t = pos / sr, beat = std::fmod (t * 128 / 60, 1.0), off = std::fmod (beat + 0.5, 1.0);
            const double kick = std::sin (tw * (45 + 120 * std::exp (-beat * 40)) * beat * 60 / 128) * std::exp (-beat * 9);
            const double chord = 0.12 * (std::sin (tw * 220 * t) + std::sin (tw * 277.2 * t) + std::sin (tw * 329.6 * t + 0.3));
            const double hat = 0.05 * std::sin (tw * 7900 * t + 9 * std::sin (tw * 3100 * t)) * std::exp (-std::fmod (t * 128 / 30, 1.0) * 30);
            buf.setSample (0, i, (float) (0.5 * kick + chord + hat));
            buf.setSample (1, i, (float) (0.5 * kick + 0.9 * chord - hat));
            if (buf.getNumChannels() > 3)
            {
                static const bool dc = std::getenv ("PHT_SCDC") != nullptr;
                const double sck = dc ? 0.5 : (off < 0.25 ? 0.7 * std::sin (tw * 60 * t) * std::exp (-off * 8) : 0.0);   // a 60 Hz thump on the off-beats
                buf.setSample (2, i, (float) sck); buf.setSample (3, i, (float) sck);
            }
        }
        for (int c = 0; c < 2; ++c) ref.copyFrom (c, 0, buf, c, 0, n);
        juce::MidiBuffer midi;
        plugin->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i) if (buf.getSample (c, i) != ref.getSample (c, i)) exact = false;
        head.ppq += n / sr * head.bpm / 60.0;
    };
    for (int b = 0; b < 200; ++b) block (b % 3 == 0 ? 333 : 512);           // odd sizes too
    expect (exact, "audio passes through bit for bit (Listen off)");

    // state round trip without an editor (CI machines have no reliable OpenGL for the editor)
    if (std::getenv ("PHT_NO_EDITOR") != nullptr)
    {
        juce::MemoryBlock a, b;
        plugin->getStateInformation (a);
        plugin->setStateInformation (a.getData(), (int) a.getSize());
        plugin->getStateInformation (b);
        auto xa = pluginXml (a), xb = pluginXml (b);
        expect (xa != nullptr && xb != nullptr && xa->isEquivalentTo (xb.get(), true), "state round-trips (no editor)");
        plugin->releaseResources();
        plugin.reset();
        std::printf (failures ? "%d FAILURES\n" : "all plugin host checks passed\n", failures);
        return failures ? 1 : 0;
    }

    // the editor
    std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorIfNeeded());
    expect (ed != nullptr, "editor opens (" + juce::String (ed ? ed->getWidth() : 0) + " x " + juce::String (ed ? ed->getHeight() : 0) + ")");
    if (ed == nullptr) return 1;
    ed->setSize (1300, 760);
    ed->setTopLeftPosition (0, 0);
    ed->addToDesktop (0); ed->setVisible (true);

    // keep audio flowing while the message loop runs (timers, frames)
    auto run = [&] (int ms)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) ms;
        while (juce::Time::getMillisecondCounter() < end) { block (512); juce::MessageManager::getInstance()->runDispatchLoopUntil (10); }
    };
    run (800);

    // the editor took the host's tempo: it's in the saved state
    juce::MemoryBlock state;
    plugin->getStateInformation (state);
    auto xml = pluginXml (state);
    double bpmSaved = 0;
    if (xml != nullptr) for (auto* v : xml->getChildIterator()) if (v->getStringAttribute ("name") == "bpm") bpmSaved = v->getDoubleAttribute ("val");
    expect (std::abs (bpmSaved - 128) < 0.01, "the tempo follows the project: " + juce::String (bpmSaved, 2) + " BPM");

    auto shot = [&] (const juce::String& name)
    {
        run (1500);
        // the plugin draws in its own native window inside the host's: grab it from the screen
        const auto b = ed->getScreenBounds();
        const auto cmd = "import -window root -crop " + juce::String (b.getWidth()) + "x" + juce::String (b.getHeight()) + "+" + juce::String (b.getX()) + "+" + juce::String (b.getY())
                         + " \"" + out.getChildFile ("plugin_" + name + ".png").getFullPathName() + "\"";
        if (std::system (cmd.toRawUTF8()) != 0) std::printf ("(no screenshot: ImageMagick's import is needed)\n");
    };
    // views through the state (as a host would restore a project)
    auto setState = [&] (std::function<void (juce::XmlElement&)> edit)
    {
        juce::MemoryBlock st; plugin->getStateInformation (st);
        std::unique_ptr<juce::XmlElement> wrapper;
        auto x = pluginXml (st, &wrapper);
        if (x == nullptr) { expect (false, "state readable"); return; }
        edit (*x);
        auto nb = rewrap (*wrapper, *x);
        plugin->setStateInformation (nb.getData(), (int) nb.getSize());
    };
    auto setValue = [] (juce::XmlElement& x, const juce::String& key, const juce::String& val)
    {
        for (auto* v : x.getChildIterator()) if (v->getStringAttribute ("name") == key) { v->setAttribute ("val", val); return; }
        auto* v = x.createNewChildElement ("VALUE"); v->setAttribute ("name", key); v->setAttribute ("val", val);
    };
    setState ([&] (juce::XmlElement& x) { setValue (x, "viewMode", "0"); setValue (x, "specSc", "1"); setValue (x, "specScStyle", "0"); });
    shot ("spectrogram_tint");
    setState ([&] (juce::XmlElement& x) { setValue (x, "specScStyle", "1"); });
    shot ("spectrogram_contour");
    setState ([&] (juce::XmlElement& x) { setValue (x, "viewMode", "4"); setValue (x, "scopeSync", "1"); setValue (x, "scopeSide", "1");
                                         setValue (x, "scopeScInput", "1"); setValue (x, "scopeScStyle", "1"); setValue (x, "scopeDivision", "2"); });
    shot ("scope_sync_sc");
    setState ([&] (juce::XmlElement& x) { setValue (x, "viewMode", "1"); setValue (x, "anaStyle", "1"); setValue (x, "fftSecondary", "5"); setValue (x, "fftSecStyle", "2"); });
    shot ("fft_sidechain");
    setState ([&] (juce::XmlElement& x) { setValue (x, "viewMode", "5"); });
    shot ("monitor");

    // state round trip: what was saved comes back
    juce::MemoryBlock a, b;
    plugin->getStateInformation (a);
    plugin->setStateInformation (a.getData(), (int) a.getSize());
    run (300);
    plugin->getStateInformation (b);
    auto xa = pluginXml (a), xb = pluginXml (b);
    expect (xa != nullptr && xb != nullptr && xa->isEquivalentTo (xb.get(), true), "state round-trips");

    ed.reset();                                  // as a host closes a plugin window
    plugin->releaseResources();
    plugin.reset();
    std::printf (failures ? "%d FAILURES\n" : "all plugin host checks passed\n", failures);
    return failures ? 1 : 0;
}
