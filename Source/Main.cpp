#include <JuceHeader.h>
#include "MainComponent.h"
#include "SettingsDialog.h"

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

        lnf.setColourScheme (juce::LookAndFeel_V4::getDarkColourScheme());
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
    struct MainWindow : public juce::DocumentWindow
    {
        explicit MainWindow (juce::PropertiesFile& p)
            : DocumentWindow ("Spectroscope - DigiCheck Universal", juce::Colour (0xff0b0c0e), allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MainComponent (p);
            setContentOwned (content, true);
            setResizable (true, true);
            setResizeLimits (640, 360, 10000, 10000);
            if (! restoreWindowStateFromString (content->getSettings().windowState)) centreWithSize (1300, 760);
            setVisible (true);
        }
        ~MainWindow() override { content->getSettings().windowState = getWindowStateAsString(); }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MainComponent* content = nullptr;
    };

    void runSelfTest (juce::String path)
    {
        auto* mc = window->content;
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
    juce::LookAndFeel_V4 lnf;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (SpectroscopeApp)
