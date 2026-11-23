#include <JuceHeader.h>
#include "MainComponent.h"
#include "SettingsDialog.h"
#include "AppLookAndFeel.h"
#include "HdrInfo.h"
#include "LayoutCheck.h"

class SpectroscopeApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Spectroscope"; }
    const juce::String getApplicationVersion() override { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String& commandLine) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Spectroscope";
        o.folderName = "DigiCheck Universal";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        props = std::make_unique<juce::PropertiesFile> (o);

        juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
        window = std::make_unique<MainWindow> (*props);

        // Self-test used by the build: "--test-screenshot out.png" runs the test signal,
        // saves screenshots of the main window and the settings dialog, then quits.
        const auto args = juce::StringArray::fromTokens (commandLine, true);
        const int i = args.indexOf ("--test-screenshot");
        if (i >= 0 && i + 1 < args.size()) runSelfTest (args[i + 1].unquoted());
        if (const int lc = args.indexOf ("--layout-check"); lc >= 0 && lc + 1 < args.size()) runLayoutCheck (args[lc + 1].unquoted());
        // checking by hand: SPECTROSCOPE_MEASURE_FILE=song.wav opens the Monitor measuring that file
        if (auto f = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_MEASURE_FILE", {}); f.isNotEmpty())
            juce::Timer::callAfterDelay (1500, [this, f] { window->content->setView (5); window->content->getMonitorPage().measureFile (juce::File (f)); });
        if (auto out = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_REPORT_OUT", {}); out.isNotEmpty())
            juce::Timer::callAfterDelay (9000, [this, out] { juce::File (out).replaceWithText (window->content->getMonitorPage().reportText (false)); });
    }

    void shutdown() override
    {
        window = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        props = nullptr;
    }

    void systemRequestedQuit() override { quit(); }

private:
    // Borderless window used for full screen. It is made 1 px taller than the monitor on purpose:
    // graphics drivers (NVIDIA in particular) treat an OpenGL window that exactly covers a monitor as
    // exclusive full screen and take it over, which switches an HDR display back to SDR. A borderless
    // window that is slightly larger stays an ordinary desktop window, so HDR and the video signal are
    // left alone, and it looks exactly the same.
    struct FullscreenHost : public juce::Component
    {
        FullscreenHost() { setOpaque (true); }
        void paint (juce::Graphics& g) override { g.fillAll (juce::Colour (0xff0b0c0e)); }
        void resized() override
        {
            if (auto* c = getChildComponent (0)) c->setBounds (0, 0, getWidth(), getHeight() - extra);
        }
        int extra = 0;                 // the 1 px hidden below the monitor (exact mode)
    };

    struct MainWindow : public juce::DocumentWindow
    {
        explicit MainWindow (juce::PropertiesFile& p)
            : DocumentWindow ("Spectroscope - DigiCheck Universal", juce::Colour (0xff0b0c0e), allButtons)
        {
            setUsingNativeTitleBar (true);
            // the analysis core and the audio input outlive the window's content; the input engine
            // (and its DSP thread) stops before the core's taps go
            core = std::make_unique<Core> (p, false);
            device = std::make_unique<AudioEngine>();
            core->registerWith (*device);
            content = std::make_unique<MainComponent> (p, *core, *device, device.get(), nullptr);
            content->onFullscreenRequest = [this] (bool full) { setAppFullscreen (full); };
            setContentNonOwned (content.get(), true);
            setResizable (true, true);
            setResizeLimits (640, 360, 10000, 10000);
            if (! restoreWindowStateFromString (content->getSettings().windowState)) centreWithSize (1300, 760);
            setVisible (true);
        }

        ~MainWindow() override
        {
            setAppFullscreen (false);
            content->getSettings().windowState = getWindowStateAsString();
            clearContentComponent();
        }

        void setAppFullscreen (bool full)
        {
            if (full == (host != nullptr)) return;
            content->suspendGpu();
            if (full)
            {
                const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (getScreenBounds());
                if (display == nullptr) { content->resumeGpu(); return; }
                const auto area = display->logicalBounds.getSmallestIntegerContainer();
                clearContentComponent();                                   // content is not owned: just detached
                host = std::make_unique<FullscreenHost>();
                host->setName (getName());
                host->addAndMakeVisible (content.get());
                // HDR monitors: a GPU (OpenGL) window covering the whole monitor can be switched by the driver to
                // exclusive / independent-flip presentation, and Windows then drops the monitor out of HDR.
                //  0 (default): 1 px short of the monitor's height, so it never covers it: HDR stays on
                //  1: on a monitor with HDR on, CPU rendering while full screen (GPU again on leaving)
                //  2: exact full screen, 1 px taller than the monitor (some drivers still promote it)
                const int mode = content->getSettings().hdrFullscreen;
                const bool hdrOn = hdr::isOnAt (juce::Desktop::getInstance().getDisplays().logicalToPhysical (area.getCentre()));
                if (mode == 1 && hdrOn) content->setCpuOverride (true);
                host->extra = mode == 2 ? 1 : 0;
                host->setBounds (mode == 0 ? area.withHeight (area.getHeight() - 1) : area.withHeight (area.getHeight() + host->extra));
                host->addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
                host->setVisible (true);
                host->toFront (true);
                setVisible (false);
            }
            else
            {
                host->removeChildComponent (content.get());
                setContentNonOwned (content.get(), false);
                setVisible (true);
                toFront (true);
                host.reset();
                content->setCpuOverride (false);
            }
            content->setFullscreenState (full);
            content->resumeGpu();
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        std::unique_ptr<Core> core;
        std::unique_ptr<AudioEngine> device;
        std::unique_ptr<MainComponent> content;
        std::unique_ptr<FullscreenHost> host;
    };

    // SPECTROSCOPE_TEST_BENCH=1: after the self-test's views have data, time 30 repaints of each view
    // (software renderer, so it measures the CPU side of the drawing) and print the averages
    static void benchViews (MainComponent* mc)
    {
        const char* names[] = { "Spectrogram", "Analyzer (bars)", "Balance", "Key", "Scope", "Monitor", "Analyzer (FFT)" };
        juce::String report;
        for (int v = 0; v < 7; ++v)
        {
            const int view = v == 6 ? 1 : v;
            if (v == 6) mc->getAnalyzerPanel().setStyle (1);
            mc->setView (view);
            juce::Image img (juce::Image::ARGB, mc->getWidth(), mc->getHeight(), true);
            for (int warm = 0; warm < 3; ++warm) { juce::Graphics g (img); mc->paintEntireComponent (g, true); }
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 30; ++i)
            {
                if (v == 4) mc->getScopePage().onFrame (i / 60.0 + 1000);
                if (v == 5) mc->getMonitorPage().onFrame (i / 60.0 + 1000);
                juce::Graphics g (img); mc->paintEntireComponent (g, true);
            }
            report << names[v] << ": " << juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 30.0, 2) << " ms per frame\n";
            if (v == 5)
                for (auto* c : mc->getMonitorPage().getChildren())
                    if (c->isVisible() && c->getWidth() > 200)
                    {
                        juce::Image ci (juce::Image::ARGB, c->getWidth(), c->getHeight(), true);
                        const double c0 = juce::Time::getMillisecondCounterHiRes();
                        for (int i = 0; i < 30; ++i) { juce::Graphics g (ci); c->paintEntireComponent (g, true); }
                        report << "   module " << c->getWidth() << "x" << c->getHeight() << ": " << juce::String ((juce::Time::getMillisecondCounterHiRes() - c0) / 30.0, 2) << " ms\n";
                    }
            if (v == 6) mc->getAnalyzerPanel().setStyle (0);
        }
        std::fputs (report.toRawUTF8(), stdout); std::fflush (stdout);
    }

    // CI: every page at several window sizes (see LayoutCheck.h)
    void runLayoutCheck (juce::String folder)
    {
        juce::MessageManager::callAsync ([this, folder]
        {
            auto* mc = window->content.get();
            mc->blockSavingForTest();
            mc->getSettings().checkUpdates = false;                   // the user's window size, page and settings stay as they were
            auto& st = mc->getSettings();
            st.gpu = false; mc->applyRendererForTest();
            st.showLufs = true; st.hidePanel = false; st.helpMode = false; st.textSize = 1;
            mc->startSourceByKey ("test");
            const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (folder);
            dir.createDirectory();
            auto log = [dir] (const juce::String& t) { dir.getChildFile ("progress.log").appendText (t + "\n"); };
            log ("start " + juce::String (ProjectInfo::versionString));
            layoutcheck::Result res;
            struct Size { int w, h; };
            const Size sizes[] = { { 1920, 1050 }, { 1300, 760 }, { 1024, 640 } };
            const char* pages[] = { "Spectrogram", "Analyzer-Bars", "Balance", "Key", "Scope", "Monitor", "Custom-2x2", "Analyzer-FFT", "Custom-large-left" };
            for (auto sz : sizes)
                for (int v = 0; v < 9; ++v)
                {
                    window->setSize (sz.w, sz.h);
                    if (v == 7) mc->getAnalyzerPanel().setStyle (1);
                    if (v == 6) mc->setCustomForTest (5, "0,5,4,2");
                    else if (v == 8) mc->setCustomForTest (2, "1,5,4");
                    else mc->setView (v == 7 ? 1 : v);
                    mc->resized();
                    for (auto* c : mc->getChildren()) c->resized();
                    const auto name = juce::String (pages[v]) + " " + juce::String (sz.w) + "x" + juce::String (sz.h);
                    log (name);
                    layoutcheck::inspect (*mc, name, res);
                    if (auto s = dir.getChildFile (name + ".png").createOutputStream())
                        juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), *s);
                    if (v == 7) mc->getAnalyzerPanel().setStyle (0);
                }
            dir.getChildFile ("layout.json").replaceWithText (juce::JSON::toString (res.fingerprint, false));
            juce::String rep;
            rep << "Layout check: " << res.errors.size() << " overlap(s), " << res.warnings.size() << " warning(s)\n\n";
            if (! res.errors.isEmpty()) rep << "OVERLAPS (errors)\n" << res.errors.joinIntoString ("\n") << "\n\n";
            if (! res.warnings.isEmpty()) rep << "Warnings\n" << res.warnings.joinIntoString ("\n") << "\n";
            dir.getChildFile ("layout_report.txt").replaceWithText (rep);
            std::fputs (rep.toRawUTF8(), stdout); std::fflush (stdout);
            juce::JUCEApplicationBase::getInstance()->setApplicationReturnValue (res.errors.isEmpty() ? 0 : 1);
            juce::JUCEApplicationBase::quit();
        });
    }

    void runSelfTest (juce::String path)
    {
        auto* mc = window->content.get();
        auto& st = mc->getSettings();
        st.grid = 1; st.showScale = false; st.showLufs = true; st.hidePanel = false;
        if (! juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_GPU", {}).isNotEmpty()) { st.gpu = false; mc->applyRendererForTest(); }
        mc->getSettings().noteRef = 440;
        mc->startSourceByKey ("test");
        juce::Timer::callAfterDelay (4000, [mc] { mc->setCursorForTest (440.0); });
        juce::Timer::callAfterDelay (9500, [this, path, mc]
        {
            juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (path));
            if (auto s = juce::FileOutputStream (out); s.openedOk())
            { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), s); }
            mc->setView (1);   // Analyzer view
            juce::Timer::callAfterDelay (3000, [this, out, mc]
            {
            if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_analyzer.png")); s.openedOk())
            { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), s); }
            // the Analyzer's FFT style with a file (SPECTROSCOPE_TEST_FFTFILE) and optionally a second one as
            // the sidechain (SPECTROSCOPE_TEST_FFTSC)
            if (auto f = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_FFTFILE", {}); f.isNotEmpty())
            {
                const int prevStyle = mc->getSettings().anaStyle;
                mc->getAnalyzerPanel().setStyle (1);
                mc->refreshBottomBarForTest();
                const juce::File sc (juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_FFTSC", "/nonexistent"));
                mc->getAnalyzerPanel().getFftView().loadForTest (juce::File (f), sc.existsAsFile() ? &sc : nullptr);
                if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_fft.png")); s.openedOk())
                { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), s); }
                mc->getAnalyzerPanel().getFftView().toggleFreeze();
                mc->getAnalyzerPanel().setStyle (prevStyle);
            }
            mc->setView (2);   // Balance view
            juce::Timer::callAfterDelay (3500, [this, out, mc]
            {
            if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_balance.png")); s.openedOk())
            { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), s); }
            // Key page; optionally with a song (SPECTROSCOPE_TEST_KEYFILE=/path/song.wav) and, for the chords and
            // melody, a model (SPECTROSCOPE_TEST_MODEL=/path/model.gguf); SPECTROSCOPE_TEST_KEYWAIT=ms to wait for it
            // Scope page (after the test signal has run through it for a while)
            mc->setView (4);
            if (auto f = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_SCOPEFILE", {}); f.isNotEmpty())
                juce::Timer::callAfterDelay (300, [mc, f]
                {
                    mc->getScopePage().loadForTest (juce::File (f), juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_SCOPEGAIN", "0").getDoubleValue(),
                                                    true, juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_SCOPESIDE", {}).isNotEmpty());
                });
            juce::Timer::callAfterDelay (juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_SCOPEWAIT", "2500").getIntValue(), [out, mc]
            {
                if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_scope.png")); s.openedOk())
                { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), s); }
                // Monitor page: the live test signal, or a file (SPECTROSCOPE_TEST_MONITORFILE) run through it and held
                if (auto v = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_MONITORFIELD", {}); v.isNotEmpty()) mc->getSettings().monitorField = v.getIntValue();
                if (auto v = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_MONITORMAX", {}); v.isNotEmpty()) mc->getSettings().monitorMax = v.getIntValue();
                if (auto v = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_MONITORSHOWN", {}); v.isNotEmpty()) mc->getSettings().monitorShown = v.getIntValue();
                if (auto v = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_MONITORCOLS", {}); v.isNotEmpty()) mc->getSettings().monitorCols = v.getIntValue();
                mc->getMonitorPage().applySettings();
                mc->setView (5);
                if (auto f = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_MONITORFILE", {}); f.isNotEmpty())
                    juce::Timer::callAfterDelay (200, [mc, f] { mc->getMonitorPage().loadForTest (juce::File (f)); });
                juce::Timer::callAfterDelay (1800, [out, mc]
                {
                    if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_monitor.png")); s.openedOk())
                    { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), s); }
                    mc->setView (3);
                });
            });
            juce::Timer::callAfterDelay (juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_SCOPEWAIT", "2500").getIntValue() + 2200, [this, out, mc]
            {
            mc->setView (3);
            const auto keyFile = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_KEYFILE", {});
            if (keyFile.isNotEmpty()) mc->getKeyPage().loadSource (juce::File (keyFile));
            // or record the (test signal) input for SPECTROSCOPE_TEST_RECORD seconds, then stop: transcribes the take
            const int recordSeconds = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_RECORD", "0").getIntValue();
            if (recordSeconds > 0)
            {
                mc->getKeyPage().toggleRecording();
                juce::Timer::callAfterDelay (recordSeconds * 1000, [mc] { mc->getKeyPage().toggleRecording(); });
            }
            const int keyWait = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_KEYWAIT", keyFile.isNotEmpty() ? "15000" : "1500").getIntValue();
            juce::Timer::callAfterDelay (keyWait, [this, out, mc]
            {
            if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_key.png")); s.openedOk())
            { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), s); }
            mc->getKeyPage().writeMidi (out.getSiblingFile (out.getFileNameWithoutExtension() + "_key.mid"));
            if (juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_CLEAR", {}).isNotEmpty())
            {
                mc->getKeyPage().clearAll();
                if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_key_cleared.png")); s.openedOk())
                { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (mc->createComponentSnapshot (mc->getLocalBounds()), s); }
            }
            if (auto playWav = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_PLAYWAV", {}); playWav.isNotEmpty())
                mc->getKeyPage().renderPreview (juce::File (playWav), 16.0, juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_LISTEN", "1").getIntValue());
            if (juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_BENCH", {}).isNotEmpty()) benchViews (mc);
            mc->setView (0);
            mc->getSettings().bpm = 150.0; mc->getSettings().tempoTriplets = true; mc->getSettings().tempoDotted = false;
            mc->toggleTempoWindow();
            mc->openSettings();
            juce::Timer::callAfterDelay (800, [this, out]
            {
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* w = dynamic_cast<juce::DocumentWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if (w->getName() == "Tempo" && w->getContentComponent() != nullptr)
                            if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_tempo.png")); s.openedOk())
                            { s.setPosition (0); s.truncate(); auto* c = w->getContentComponent(); juce::PNGImageFormat().writeImageToStream (c->createComponentSnapshot (c->getLocalBounds()), s); }
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* dw = dynamic_cast<juce::DialogWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if (auto* c = dw->getContentComponent())
                        {
                            auto f = out.getSiblingFile (out.getFileNameWithoutExtension() + "_settings.png");
                            if (auto s = juce::FileOutputStream (f); s.openedOk())
                            { s.setPosition (0); s.truncate(); juce::PNGImageFormat().writeImageToStream (c->createComponentSnapshot (c->getLocalBounds()), s); }
                            dw->exitModalState (0);
                        }
                quit();
            });
            });
            });
            });
            });
        });
    }

    std::unique_ptr<juce::PropertiesFile> props;
    AppLookAndFeel lnf;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (SpectroscopeApp)
