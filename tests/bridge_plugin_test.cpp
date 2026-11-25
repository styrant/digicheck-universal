// Loads the built UAV Bridge VST3 like a DAW would and checks it from the outside:
//   - stereo and mono layouts; the audio passes through bit for bit, with no latency
//   - it claims a bridge slot, under the host's track name, at the host's sample rate, and the newest
//     audio in shared memory is exactly the block it just processed
//   - "send" off (saved state) frees the slot; on again sends again; the state round-trips
//   - its window opens, and unloading the plugin frees the slot
// usage: bridge_plugin_test "<path to UAV Bridge.vst3>"
#include <JuceHeader.h>
#include "../Source/bridge/Bridge.h"

namespace
{
    int failures = 0;
    void expect (bool ok, const juce::String& what) { std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); if (! ok) ++failures; }

    std::unique_ptr<juce::XmlElement> pluginXml (const juce::MemoryBlock& hostState, std::unique_ptr<juce::XmlElement>& wrapper)
    {
        wrapper = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize());
        if (wrapper == nullptr) return nullptr;
        auto* comp = wrapper->getChildByName ("IComponent");
        if (comp == nullptr) return nullptr;
        juce::MemoryBlock inner;
        inner.fromBase64Encoding (comp->getAllSubText().trim());
        return juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
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

    // live senders at 48 kHz (other tests or a DAW on this machine may have their own)
    std::vector<bridge::SenderInfo> live()
    {
        std::vector<bridge::SenderInfo> v;
        for (auto& s : bridge::listSenders()) if (s.alive && std::abs (s.sampleRate - 48000) < 1) v.push_back (s);
        return v;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2) { std::printf ("usage: bridge_plugin_test <plugin.vst3>\n"); return 2; }
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI gui;

    expect (bridge::shared() != nullptr, "shared memory opens");
    const auto before = bridge::listSenders().size();

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, juce::String (argv[1]));
    expect (types.size() == 1, "the VST3 is found: " + (types.isEmpty() ? juce::String ("nothing") : types[0]->name + " " + types[0]->version + " by " + types[0]->manufacturerName));
    if (types.isEmpty()) return 1;

    juce::String err;
    auto plugin = format.createInstanceFromDescription (*types[0], 48000, 512, err);
    expect (plugin != nullptr, "instantiated " + err);
    if (plugin == nullptr) return 1;
    expect (plugin->getLatencySamples() == 0, "no latency");

    juce::AudioProcessor::TrackProperties tp; tp.name = juce::String ("Master");
    plugin->updateTrackProperties (tp);
    plugin->setRateAndBufferSizeDetails (48000, 512);
    plugin->prepareToPlay (48000, 512);

    const double tw = juce::MathConstants<double>::twoPi;
    long long pos = 0;
    juce::AudioBuffer<float> lastIn;
    auto run = [&] (int blocks, int channels)
    {
        bool exact = true;
        for (int b = 0; b < blocks; ++b)
        {
            const int n = b % 3 == 0 ? 333 : 512;
            juce::AudioBuffer<float> buf (channels, n);
            for (int i = 0; i < n; ++i, ++pos)
            {
                buf.setSample (0, i, (float) (0.5 * std::sin (tw * 220 * pos / 48000.0)));
                if (channels > 1) buf.setSample (1, i, (float) (0.3 * std::sin (tw * 331 * pos / 48000.0 + 1.0)));
            }
            lastIn.makeCopyOf (buf);
            juce::MidiBuffer midi;
            plugin->processBlock (buf, midi);
            for (int c = 0; c < channels; ++c)
                for (int i = 0; i < n; ++i) if (buf.getSample (c, i) != lastIn.getSample (c, i)) exact = false;
        }
        return exact;
    };
    // the newest frames in a slot equal the last block processed
    auto ringMatches = [&] (int slot)
    {
        auto& sl = bridge::shared()->slots[slot];
        const int64_t w = sl.written.load();
        const int n = lastIn.getNumSamples();
        if (w < n) return false;
        for (int i = 0; i < n; ++i)
        {
            const int k = (int) ((w - n + i) & (bridge::kFrames - 1));
            const float L = lastIn.getSample (0, i), R = lastIn.getSample (lastIn.getNumChannels() > 1 ? 1 : 0, i);
            if (sl.l[k] != L || sl.r[k] != R) return false;
        }
        return true;
    };

    expect (run (100, 2), "stereo audio passes through bit for bit");
    auto s = live();
    expect (s.size() == 1, "one sender at 48 kHz (" + juce::String ((int) s.size()) + ")");
    if (s.size() == 1)
    {
        if (s[0].name == "Master") expect (true, "it sends under the track's name (Master)");
        else std::printf ("NOTE the host didn't pass the track name (sending as \"%s\")\n", s[0].name.toRawUTF8());
        expect (ringMatches (s[0].slot), "the newest audio in shared memory is the block just processed");
        expect (bridge::chooseSlot ({}) == s[0].slot || s[0].name != "Master", "automatic picks it");
    }

    // send off through the saved state, then on again
    juce::MemoryBlock st; plugin->getStateInformation (st);
    std::unique_ptr<juce::XmlElement> wrapper;
    auto x = pluginXml (st, wrapper);
    expect (x != nullptr && x->hasTagName ("UAVBridge") && x->getIntAttribute ("send") == 1, "state holds send=1");
    if (x != nullptr && wrapper != nullptr)
    {
        x->setAttribute ("send", 0);
        auto off = rewrap (*wrapper, *x);
        plugin->setStateInformation (off.getData(), (int) off.getSize());
        expect (run (20, 2), "audio still passes with sending off");
        bool none = true;
        for (auto& v : bridge::listSenders()) if (std::abs (v.sampleRate - 48000) < 1 && v.name == (s.empty() ? juce::String() : s[0].name) && v.alive) none = false;
        expect (none, "sending off frees the slot");
        juce::MemoryBlock st2; plugin->getStateInformation (st2);
        std::unique_ptr<juce::XmlElement> w2;
        auto x2 = pluginXml (st2, w2);
        expect (x2 != nullptr && x2->getIntAttribute ("send", 1) == 0, "state round-trips (send=0)");
        x->setAttribute ("send", 1);
        auto on = rewrap (*wrapper, *x);
        plugin->setStateInformation (on.getData(), (int) on.getSize());
        run (20, 2);
        s = live();
        expect (s.size() == 1 && ringMatches (s[0].slot), "sending on again sends again");
    }

    // mono track
    plugin->releaseResources();
    auto layout = plugin->getBusesLayout();
    layout.inputBuses.getReference (0) = juce::AudioChannelSet::mono();
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    if (plugin->setBusesLayout (layout))
    {
        plugin->prepareToPlay (48000, 512);
        expect (run (40, 1), "mono audio passes through bit for bit");
        s = live();
        expect (s.size() == 1 && ringMatches (s[0].slot), "mono is sent to both channels");
    }
    else expect (false, "mono layout accepted");

    // the window (plain JUCE drawing, no OpenGL)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (plugin->createEditorAndMakeActive());
        expect (ed != nullptr && ed->getWidth() > 100, "window opens (" + juce::String (ed ? ed->getWidth() : 0) + " x " + juce::String (ed ? ed->getHeight() : 0) + ")");
    }

    plugin->releaseResources();
    plugin.reset();
    expect (bridge::listSenders().size() == before, "unloading frees the slot");

    std::printf (failures ? "%d FAILURES\n" : "all bridge plugin checks passed\n", failures);
    return failures ? 1 : 0;
}
