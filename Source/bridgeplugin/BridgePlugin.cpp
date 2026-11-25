// Ultimate Audio Viewer Bridge: a tiny VST3 that only sends the audio it sees to the UAV app's
// "From DAW" input (bridge/Bridge.h), so the app can monitor a DAW that uses ASIO. The audio passes
// through untouched, with no latency. There's no analysis, no OpenGL and no data inside, so it costs next
// to nothing: put it last on the master track.
//
// The window shows only whether it's sending, under which track name, a small level meter, and a switch.
// Its codes (Dcun / Dcub) are its own, so it can sit next to the full UAV plugin.
#include <JuceHeader.h>
#include "../bridge/Bridge.h"

namespace
{
    constexpr const char* kName = "Ultimate Audio Viewer Bridge";

    // the installed app, if it's in one of the installer's folders (for the "Open UAV" button)
    juce::File findApp()
    {
       #if JUCE_WINDOWS
        const juce::File candidates[] = {
            juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory).getChildFile ("Ultimate Audio Viewer\\Ultimate Audio Viewer.exe"),
            juce::File (juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {})).getChildFile ("Programs\\Ultimate Audio Viewer\\Ultimate Audio Viewer.exe") };
        for (auto& f : candidates) if (f.existsAsFile()) return f;
       #endif
        return {};
    }
}

//==============================================================================
class BridgeProcessor : public juce::AudioProcessor
{
public:
    BridgeProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        sender.setName (trackName);
    }

    const juce::String getName() const override              { return kName; }
    bool acceptsMidi() const override                        { return false; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 0; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
        return in == out && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
    }

    void prepareToPlay (double sr, int) override             { rate = sr; }
    void releaseResources() override                         {}

    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const int n = buffer.getNumSamples(), nch = getTotalNumInputChannels();
        if (n == 0 || nch == 0) return;
        const float* L = buffer.getReadPointer (0);
        const float* R = nch > 1 ? buffer.getReadPointer (1) : L;
        if (sendOn.load (std::memory_order_relaxed))
            sender.write (L, R, n, rate);                    // real-time safe; the audio itself is left alone
        lastBlockMs = bridge::nowMs();
        // peaks for the window's meter (it takes and clears them)
        float pl = peakL.load (std::memory_order_relaxed), pr = peakR.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { pl = std::max (pl, std::abs (L[i])); pr = std::max (pr, std::abs (R[i])); }
        peakL.store (pl, std::memory_order_relaxed); peakR.store (pr, std::memory_order_relaxed);
    }

    // the host's track name: the app lists the track by it, and "automatic" looks for the master's name
    void updateTrackProperties (const TrackProperties& p) override
    {
        if (p.name.has_value() && p.name->isNotEmpty())
        {
            { const juce::SpinLock::ScopedLockType sl (nameLock); trackName = *p.name; }
            sender.setName (*p.name);
        }
    }

    bool hasEditor() const override                          { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        juce::XmlElement x ("UAVBridge");
        x.setAttribute ("send", sendOn.load() ? 1 : 0);
        copyXmlToBinary (x, dest);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size); x != nullptr && x->hasTagName ("UAVBridge"))
            setSending (x->getIntAttribute ("send", 1) != 0);
    }

    // for the window
    void setSending (bool on)                                { sendOn = on; sender.setEnabled (on); }
    bool sendingWanted() const                               { return sendOn.load(); }
    bool sending() const                                     { return sendOn.load() && sender.isSending(); }
    bool audioFlowing() const                                { return bridge::nowMs() - lastBlockMs.load() < 1000; }
    juce::String name() const                                { const juce::SpinLock::ScopedLockType sl (nameLock); return trackName; }
    void takePeaks (float& l, float& r)                      { l = peakL.exchange (0.0f); r = peakR.exchange (0.0f); }

private:
    bridge::Sender sender;
    std::atomic<bool> sendOn { true };
    std::atomic<int64_t> lastBlockMs { 0 };
    std::atomic<float> peakL { 0 }, peakR { 0 };
    double rate = 44100;
    mutable juce::SpinLock nameLock;
    juce::String trackName { "UAV Bridge" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BridgeProcessor)
};

//==============================================================================
class BridgeEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BridgeEditor (BridgeProcessor& p) : AudioProcessorEditor (p), proc (p), app (findApp())
    {
        sendToggle.setButtonText ("Send to the UAV app");
        sendToggle.setToggleState (proc.sendingWanted(), juce::dontSendNotification);
        sendToggle.setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        sendToggle.setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff4fd1c5));
        sendToggle.onClick = [this] { proc.setSending (sendToggle.getToggleState()); refresh(); };
        sendToggle.setTooltip ("Off: this track stops appearing in the app's From DAW list. The audio passes through either way.");
        addAndMakeVisible (sendToggle);

        openApp.setButtonText ("Open UAV");
        openApp.setTooltip ("Starts the Ultimate Audio Viewer app. There, choose From DAW in Options and press Start.");
        openApp.onClick = [this] { if (app.existsAsFile()) app.startAsProcess(); };
        openApp.setVisible (app.existsAsFile());
        addChildComponent (openApp);

        setSize (360, 150);
        startTimerHz (15);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff15171c));
        auto r = getLocalBounds().reduced (14, 10);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
        g.drawText ("UAV Bridge", r.removeFromTop (22), juce::Justification::centredLeft);

        r.removeFromTop (4);
        g.setFont (juce::FontOptions (13.5f));
        g.setColour (statusColour);
        g.drawText (status, r.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (juce::Colour (0xffb8bec9));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText (hint, r.removeFromTop (18), juce::Justification::centredLeft, true);

        // two thin meter bars, -60..0 dB
        r.removeFromTop (8);
        auto bars = r.removeFromTop (14);
        for (int ch = 0; ch < 2; ++ch)
        {
            auto b = bars.removeFromTop (6).toFloat();
            bars.removeFromTop (2);
            g.setColour (juce::Colour (0xff2a2e36));
            g.fillRoundedRectangle (b, 2.0f);
            const float db = juce::Decibels::gainToDecibels (level[ch], -60.0f);
            const float frac = juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
            g.setColour (db > -0.5f ? juce::Colour (0xffff5a5a) : juce::Colour (0xff4fd1c5));
            g.fillRoundedRectangle (b.withWidth (b.getWidth() * frac), 2.0f);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 10).removeFromBottom (26);
        openApp.setBounds (r.removeFromRight (90));
        sendToggle.setBounds (r.removeFromLeft (200));
    }

private:
    void timerCallback() override
    {
        float l, r; proc.takePeaks (l, r);
        for (int ch = 0; ch < 2; ++ch) level[ch] = std::max (ch == 0 ? l : r, level[ch] * 0.80f);   // ~ -20 dB/s fall
        if (++ticks % 4 == 0) refresh();
        repaint();
    }

    void refresh()
    {
        juce::String s, h;
        juce::Colour c;
        if (bridge::shared() == nullptr)
            { s = "The bridge isn't available on this computer"; h = "Shared memory couldn't be opened."; c = juce::Colour (0xffff8a5a); }
        else if (! proc.sendingWanted())
            { s = "Off: not sending"; h = "Tick the box to send this track to the app."; c = juce::Colour (0xffb8bec9); }
        else if (! proc.audioFlowing())
            { s = "Ready: waiting for the DAW to process audio"; h = "Press play in your DAW."; c = juce::Colour (0xffffd166); }
        else if (! proc.sending())
            { s = "Can't send: 16 tracks are already sending"; h = "Turn sending off on a track you don't need."; c = juce::Colour (0xffff8a5a); }
        else
            { s = juce::String::fromUTF8 ("Sending as “") + proc.name() + juce::String::fromUTF8 ("”");
              h = "In the UAV app: Options > From DAW, then Start."; c = juce::Colour (0xff4fd1c5); }
        if (s != status || h != hint || c != statusColour) { status = s; hint = h; statusColour = c; }
    }

    BridgeProcessor& proc;
    juce::File app;
    juce::ToggleButton sendToggle;
    juce::TextButton openApp;
    juce::TooltipWindow tips { this, 600 };
    juce::String status, hint;
    juce::Colour statusColour;
    float level[2] {};
    int ticks = 0;
};

juce::AudioProcessorEditor* BridgeProcessor::createEditor() { return new BridgeEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BridgeProcessor(); }
