#include <JuceHeader.h>
#include "MainComponent.h"
#include "SettingsDialog.h"
#include "AppLookAndFeel.h"

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
            if (auto* c = getChildComponent (0)) c->setBounds (0, 0, getWidth(), getHeight() - 1);
        }
    };

    struct MainWindow : public juce::DocumentWindow
    {
        explicit MainWindow (juce::PropertiesFile& p)
            : DocumentWindow ("Spectroscope - DigiCheck Universal", juce::Colour (0xff0b0c0e), allButtons)
        {
            setUsingNativeTitleBar (true);
            content = std::make_unique<MainComponent> (p);
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
                host->setBounds (area.withHeight (area.getHeight() + 1));  // 1 px taller: see FullscreenHost
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
            }
            content->setFullscreenState (full);
            content->resumeGpu();
        }

        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

        std::unique_ptr<MainComponent> content;
        std::unique_ptr<FullscreenHost> host;
    };

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
            mc->setView (0);
            mc->getSettings().bpm = 150.0; mc->getSettings().tempoTriplets = true; mc->getSettings().tempoDotted = false;
            mc->toggleTempoWindow();
            mc->toggleKeyWindow();
            mc->openSettings();
            juce::Timer::callAfterDelay (800, [this, out]
            {
                for (int i = 0; i < juce::TopLevelWindow::getNumTopLevelWindows(); ++i)
                    if (auto* w = dynamic_cast<juce::DocumentWindow*> (juce::TopLevelWindow::getTopLevelWindow (i)))
                        if ((w->getName() == "Keys" || w->getName() == "Tempo") && w->getContentComponent() != nullptr)
                            if (auto s = juce::FileOutputStream (out.getSiblingFile (out.getFileNameWithoutExtension() + "_" + w->getName().toLowerCase() + ".png")); s.openedOk())
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
    }

    std::unique_ptr<juce::PropertiesFile> props;
    AppLookAndFeel lnf;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (SpectroscopeApp)
