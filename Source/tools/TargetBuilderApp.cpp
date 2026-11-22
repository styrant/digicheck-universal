// Spectroscope Target Builder: drag songs in, check them, save them together as a tonal balance target.
// (TargetBuilder.exe --cli ... runs the command-line mode instead, see TargetBuilderCli.cpp.)
#include "TargetCore.h"
#include "../AppLookAndFeel.h"
#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <cstdio>
#endif

int runTargetBuilderCli (const juce::StringArray& args);

namespace
{
    const juce::Colour kBg { 0xff15171b }, kPanel { 0xff1b1d22 }, kLine { 0xff2c2f36 }, kText { 0xffe8e8e8 },
                       kMuted { 0xff8c93a3 }, kGreen { 0xff3ddc84 }, kBlue { 0xff4c8dff }, kAmber { 0xfff0b429 }, kRed { 0xffe5484d };

    struct Item
    {
        juce::File file;
        std::atomic<int> state { 0 };            // 0 queued, 1 analysing, 2 done (ok or skipped)
        std::atomic<bool> cancel { false };
        targets::TrackResult result;             // valid once state == 2
        double deviation = -1;
        bool outlier = false;
    };

    //==========================================================================
    // the combined target: shaded "in range" band and the target curve, log frequency axis
    class Preview : public juce::Component
    {
    public:
        void setTarget (std::shared_ptr<tonal::Target> t) { target = std::move (t); repaint(); }

        void paint (juce::Graphics& g) override
        {
            g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            auto r = getLocalBounds().reduced (12, 8);
            g.setColour (kMuted); g.setFont (juce::FontOptions (12.0f));
            g.drawText (target ? "Target preview - " + juce::String (target->tracks) + " song" + (target->tracks == 1 ? "" : "s")
                               : juce::String ("Target preview appears once songs are analysed"),
                        r.removeFromTop (16), juce::Justification::centredLeft);
            auto plot = r.withTrimmedBottom (14).toFloat();
            auto xOf = [&] (double f) { return plot.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * plot.getWidth(); };
            g.setColour (kLine);
            for (double f : { 50.0, 100.0, 250.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
            {
                const float x = xOf (f);
                g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                g.setColour (kMuted);
                g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String ((int) f), (int) x - 20, (int) plot.getBottom() + 1, 40, 12, juce::Justification::centred);
                g.setColour (kLine);
            }
            if (! target) return;

            double top = -1e9;
            for (int k = 1; k < tonal::kBins; ++k)
                if (tonal::binFrequency (k) >= 20 && tonal::binFrequency (k) <= 20000) top = std::max (top, target->high[(size_t) k]);
            top = std::ceil (top / 5) * 5 + 2;
            const double range = 60;
            auto yOf = [&] (double db) { return plot.getY() + (float) juce::jlimit (0.0, 1.0, (top - db) / range) * plot.getHeight(); };

            juce::Path band, line;
            std::vector<juce::Point<float>> lows;
            bool first = true;
            for (int k = 1; k < tonal::kBins; ++k)
            {
                const double f = tonal::binFrequency (k);
                if (f < 20 || f > 20000) continue;
                const float x = xOf (f);
                if (first) { band.startNewSubPath (x, yOf (target->high[(size_t) k])); line.startNewSubPath (x, yOf (target->mean[(size_t) k])); first = false; }
                else { band.lineTo (x, yOf (target->high[(size_t) k])); line.lineTo (x, yOf (target->mean[(size_t) k])); }
                lows.push_back ({ x, yOf (target->low[(size_t) k]) });
            }
            for (auto it = lows.rbegin(); it != lows.rend(); ++it) band.lineTo (*it);
            band.closeSubPath();
            g.setColour (kBlue.withAlpha (0.25f)); g.fillPath (band);
            g.setColour (kGreen); g.strokePath (line, juce::PathStrokeType (1.8f));

            // band shares of the target, under the curve
            static const char* names[4] = { "Low", "Low-mid", "High-mid", "High" };
            const auto sh = tonal::bandShares (target->raw);
            juce::String s;
            for (int b = 0; b < 4; ++b)
                s << names[b] << " " << juce::roundToInt (sh[(size_t) b] * 100) << "% (" << juce::roundToInt (target->regions[(size_t) b][0] * 100)
                  << "-" << juce::roundToInt (target->regions[(size_t) b][1] * 100) << ")    ";
            g.setColour (kMuted); g.setFont (juce::FontOptions (11.5f));
            g.drawText (s.trimEnd(), getLocalBounds().reduced (12, 8).removeFromTop (16), juce::Justification::centredRight);
        }

    private:
        std::shared_ptr<tonal::Target> target;
    };

    //==========================================================================
    class Panel : public juce::Component, public juce::FileDragAndDropTarget, public juce::TableListBoxModel, private juce::Timer
    {
    public:
        Panel()
        {
            targets::registerFormats (formats);
            for (auto* b : { &addFiles, &addFolder, &removeBtn, &clearBtn, &saveBtn, &openFolder }) addAndMakeVisible (b);
            addFiles.onClick  = [this] { choose (false); };
            addFolder.onClick = [this] { choose (true); };
            removeBtn.onClick = [this] { removeSelected(); };
            clearBtn.onClick  = [this] { clearAll(); };
            saveBtn.onClick   = [this] { save(); };
            openFolder.onClick = [] { auto d = targets::defaultFolder(); d.createDirectory(); d.startAsProcess(); };
            saveBtn.setColour (juce::TextButton::buttonColourId, kGreen.withAlpha (0.35f));

            addAndMakeVisible (nameLabel);
            nameLabel.setText ("Target name", juce::dontSendNotification);
            nameLabel.setColour (juce::Label::textColourId, kMuted);
            addAndMakeVisible (name);
            name.setTextToShowWhenEmpty ("e.g. Hip-hop 2025", kMuted);
            name.onReturnKey = [this] { save(); };
            name.setJustification (juce::Justification::centredLeft);
            name.setFont (juce::FontOptions (15.0f));
            name.setIndents (8, 0);

            addAndMakeVisible (rangeBox);
            rangeBox.addItem ("Range 10-90 % (default)", 1);
            rangeBox.addItem ("Range 5-95 % (wider)", 2);
            rangeBox.addItem ("Range 20-80 % (tighter)", 3);
            rangeBox.setSelectedId (1, juce::dontSendNotification);
            rangeBox.setTooltip ("How much of the songs' spread the \"in range\" band covers");
            rangeBox.onChange = [this] { dirty = true; };

            table.setModel (this);
            table.setColour (juce::ListBox::backgroundColourId, kPanel);
            table.setRowHeight (24);
            table.setMultipleSelectionEnabled (true);
            auto& h = table.getHeader();
            h.addColumn ("Song", 1, 320, 120, -1, juce::TableHeaderComponent::defaultFlags & ~juce::TableHeaderComponent::sortable);
            h.addColumn ("Length", 2, 70, 60, 90, juce::TableHeaderComponent::notSortable);
            h.addColumn ("Status", 3, 190, 100, -1, juce::TableHeaderComponent::notSortable);
            h.addColumn ("Distance", 4, 110, 80, 140, juce::TableHeaderComponent::notSortable);
            h.setStretchToFitActive (true);
            h.setColour (juce::TableHeaderComponent::backgroundColourId, juce::Colour (0xff23262c));
            h.setColour (juce::TableHeaderComponent::textColourId, kMuted);
            h.setColour (juce::TableHeaderComponent::outlineColourId, kLine);
            addChildComponent (table);
            addAndMakeVisible (preview);

            status.setColour (juce::Label::textColourId, kMuted);
            addAndMakeVisible (status);
            setSize (900, 640);
            startTimerHz (5);
            updateUi();
        }

        ~Panel() override
        {
            for (auto& it : items) it->cancel = true;
            pool.removeAllJobs (true, 10000);
        }

        void addPaths (const juce::StringArray& paths)
        {
            juce::Array<juce::File> files;
            for (auto& p : paths)
            {
                juce::File f (p);
                if (f.isDirectory()) files.addArray (targets::audioFilesIn (f, formats, true));
                else if (formats.findFormatForFileExtension (f.getFileExtension()) != nullptr) files.add (f);
            }
            int added = 0;
            for (auto& f : files)
            {
                if (std::any_of (items.begin(), items.end(), [&] (auto& it) { return it->file == f; })) continue;   // already in
                auto item = std::make_shared<Item>();
                item->file = f;
                items.push_back (item);
                pool.addJob ([this, item]
                {
                    if (item->cancel) return;
                    item->state = 1;
                    auto r = targets::analyse (formats, item->file, &item->cancel);
                    {
                        const juce::ScopedLock sl (lock);
                        item->result = std::move (r);
                    }
                    item->state = 2;
                });
                ++added;
            }
            if (added > 0 && name.getText().trim().isEmpty() && ! files.isEmpty())
                name.setText (paths.size() == 1 && juce::File (paths[0]).isDirectory() ? juce::File (paths[0]).getFileName()
                                                                                       : files[0].getParentDirectory().getFileName(), false);
            if (added == 0 && ! paths.isEmpty())
                lastMessage = files.isEmpty() ? "No audio files found there (WAV, AIFF, FLAC, OGG, MP3)" : "Those songs are already in the list";
            else lastMessage = {};
            dirty = true;
            table.updateContent();
            updateUi();
        }

        // ---- layout and painting
        void resized() override
        {
            auto r = getLocalBounds().reduced (14);
            auto top = r.removeFromTop (30);
            addFiles.setBounds (top.removeFromLeft (120)); top.removeFromLeft (8);
            addFolder.setBounds (top.removeFromLeft (120));
            clearBtn.setBounds (top.removeFromRight (80)); top.removeFromRight (8);
            removeBtn.setBounds (top.removeFromRight (90));
            r.removeFromTop (10);

            auto bottom = r.removeFromBottom (32);
            nameLabel.setBounds (bottom.removeFromLeft (86));
            openFolder.setBounds (bottom.removeFromRight (150)); bottom.removeFromRight (8);
            saveBtn.setBounds (bottom.removeFromRight (130)); bottom.removeFromRight (8);
            rangeBox.setBounds (bottom.removeFromRight (190)); bottom.removeFromRight (8);
            name.setBounds (bottom);
            r.removeFromBottom (6);
            status.setBounds (r.removeFromBottom (22));
            r.removeFromBottom (6);
            preview.setBounds (r.removeFromBottom (juce::jlimit (140, 220, r.getHeight() / 3)));
            r.removeFromBottom (10);
            listArea = r;
            table.setBounds (r);
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (kBg);
            if (items.empty())
            {
                auto z = listArea.toFloat();
                juce::Path p; p.addRoundedRectangle (z, 10.0f);
                const float dash[] = { 8.0f, 6.0f };
                juce::PathStrokeType (1.5f).createDashedStroke (p, p, dash, 2);
                g.setColour (juce::Colours::white.withAlpha (0.25f)); g.fillPath (p);
                g.setColour (kText); g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
                g.drawText ("Drag songs or folders here", listArea.withTrimmedBottom (40), juce::Justification::centred);
                g.setColour (kMuted); g.setFont (juce::FontOptions (14.0f));
                g.drawText ("WAV, AIFF, FLAC, OGG, MP3 - they're analysed as soon as you add them",
                            listArea.withTrimmedTop (30), juce::Justification::centred);
            }
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (! dragOver) return;
            g.setColour (kGreen.withAlpha (0.10f)); g.fillRect (getLocalBounds());
            g.setColour (kGreen); g.drawRect (getLocalBounds(), 3);
        }

        // ---- drag and drop
        bool isInterestedInFileDrag (const juce::StringArray&) override { return true; }
        void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dragOver = false; repaint(); }
        void filesDropped (const juce::StringArray& files, int, int) override { dragOver = false; addPaths (files); repaint(); }

        // ---- table
        int getNumRows() override { return (int) items.size(); }
        void paintRowBackground (juce::Graphics& g, int row, int, int, bool selected) override
        {
            if (selected) g.fillAll (kBlue.withAlpha (0.30f));
            else if (row % 2) g.fillAll (juce::Colours::white.withAlpha (0.025f));
        }
        void paintCell (juce::Graphics& g, int row, int col, int w, int h, bool) override
        {
            if (row < 0 || row >= (int) items.size()) return;
            const auto& it = *items[(size_t) row];
            const int st = it.state.load();
            const juce::ScopedLock sl (lock);
            const bool ok = st == 2 && it.result.ok();
            juce::String text; juce::Colour c = kText;
            switch (col)
            {
                case 1: text = it.file.getFileName(); c = (st == 2 && ! ok) ? kMuted : kText; break;
                case 2: if (st == 2 && it.result.seconds > 0) text = juce::String ((int) it.result.seconds / 60) + ":" + juce::String ((int) it.result.seconds % 60).paddedLeft ('0', 2); c = kMuted; break;
                case 3:
                    if (st == 0) { text = "Waiting"; c = kMuted; }
                    else if (st == 1) { text = "Analysing..."; c = kBlue; }
                    else if (! ok) { text = "Skipped: " + it.result.error; c = kRed; }
                    else if (it.outlier) { text = "Outlier - doesn't match the others"; c = kAmber; }
                    else { text = "Ready"; c = kGreen; }
                    break;
                case 4: if (ok && it.deviation >= 0) text = juce::String (it.deviation, 1) + " dB"; c = it.outlier ? kAmber : kMuted; break;
                default: break;
            }
            g.setColour (c); g.setFont (juce::FontOptions (14.0f));
            g.drawText (text, 6, 0, w - 10, h, col == 4 ? juce::Justification::centredRight : juce::Justification::centredLeft, true);
        }
        void deleteKeyPressed (int) override { removeSelected(); }
        juce::String getCellTooltip (int row, int col) override
        {
            if (col == 4) return "How far this song's balance is from the combined target (RMS, 40 Hz - 16 kHz)";
            return row >= 0 && row < (int) items.size() ? items[(size_t) row]->file.getFullPathName() : juce::String();
        }

    private:
        std::pair<double, double> range() const
        {
            switch (rangeBox.getSelectedId()) { case 2: return { 5, 95 }; case 3: return { 20, 80 }; default: return { 10, 90 }; }
        }

        int countState (int s) const { return (int) std::count_if (items.begin(), items.end(), [s] (auto& it) { return it->state.load() == s; }); }
        int countOk() const
        {
            const juce::ScopedLock sl (lock);
            return (int) std::count_if (items.begin(), items.end(), [] (auto& it) { return it->state.load() == 2 && it->result.ok(); });
        }

        void timerCallback() override
        {
            const int done = countState (2);
            if (done != lastDone) { lastDone = done; dirty = true; }
            if (dirty) { dirty = false; recompute(); }
            table.repaint();
            updateUi();
        }

        // combine the analysed songs into the preview target and mark outliers
        void recompute()
        {
            std::vector<std::vector<double>> spectra;
            std::vector<Item*> used;
            {
                const juce::ScopedLock sl (lock);
                for (auto& it : items)
                    if (it->state.load() == 2 && it->result.ok()) { spectra.push_back (it->result.db); used.push_back (it.get()); }
            }
            for (auto& it : items) { it->deviation = -1; it->outlier = false; }
            if (spectra.empty()) { preview.setTarget (nullptr); return; }
            const auto [lo, hi] = range();
            auto t = std::make_shared<tonal::Target> (tonal::combine (spectra, lo, hi));
            const auto d = targets::deviations (spectra, *t);
            for (size_t i = 0; i < used.size(); ++i) { used[i]->deviation = d.db[i]; used[i]->outlier = d.outlier[i]; }
            preview.setTarget (t);
        }

        void updateUi()
        {
            const bool empty = items.empty();
            table.setVisible (! empty);
            const int working = countState (0) + countState (1), ok = countOk();
            const int outliers = (int) std::count_if (items.begin(), items.end(), [] (auto& it) { return it->outlier; });
            const int skipped = countState (2) - ok;
            removeBtn.setEnabled (table.getNumSelectedRows() > 0);
            clearBtn.setEnabled (! empty);
            saveBtn.setEnabled (working == 0 && ok > 0);
            saveBtn.setButtonText (working > 0 ? "Analysing..." : "Save target");

            juce::String s;
            if (lastMessage.isNotEmpty()) s = lastMessage;
            else if (empty) s = "Targets are saved to " + targets::defaultFolder().getFullPathName();
            else
            {
                s << ok << " song" << (ok == 1 ? "" : "s") << " ready";
                if (working > 0) s << ", " << working << " analysing";
                if (skipped > 0) s << ", " << skipped << " skipped";
                if (outliers > 0) s << ", " << outliers << " outlier" << (outliers == 1 ? "" : "s") << " (select and press Delete to drop)";
                if (ok > 0 && ok < 10 && working == 0) s << " - 10 or more songs give a more reliable range";
                if (lastSaved.isNotEmpty()) s << "   |   " << lastSaved;
            }
            status.setText (s, juce::dontSendNotification);
        }

        void choose (bool folder)
        {
            chooser = std::make_unique<juce::FileChooser> (folder ? "Add a folder of songs" : "Add songs",
                juce::File::getSpecialLocation (juce::File::userMusicDirectory), folder ? juce::String() : formats.getWildcardForAllFormats());
            const int flags = juce::FileBrowserComponent::openMode
                            | (folder ? juce::FileBrowserComponent::canSelectDirectories : (juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectMultipleItems));
            juce::Component::SafePointer<Panel> safe (this);
            chooser->launchAsync (flags, [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                juce::StringArray paths;
                for (auto& f : fc.getResults()) paths.add (f.getFullPathName());
                if (! paths.isEmpty()) safe->addPaths (paths);
            });
        }

        void removeSelected()
        {
            const auto sel = table.getSelectedRows();
            for (int i = sel.size(); --i >= 0;)
            {
                const int row = sel[i];
                if (row < 0 || row >= (int) items.size()) continue;
                items[(size_t) row]->cancel = true;            // a running analysis stops; the job keeps its own reference
                items.erase (items.begin() + row);
            }
            table.deselectAllRows();
            table.updateContent();
            lastSaved = {};
            dirty = true;
            updateUi();
            repaint();
        }

        void clearAll()
        {
            for (auto& it : items) it->cancel = true;
            items.clear();
            table.updateContent();
            lastSaved = lastMessage = {};
            dirty = true;
            updateUi();
            repaint();
        }

        void save()
        {
            if (! saveBtn.isEnabled()) return;
            auto n = name.getText().trim();
            if (n.isEmpty()) { name.grabKeyboardFocus(); lastMessage = "Give the target a name first"; updateUi(); return; }
            const auto json = targets::defaultFolder().getChildFile (targets::safeName (n) + ".json");
            if (json.existsAsFile())
            {
                juce::Component::SafePointer<Panel> safe (this);
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Replace target?",
                    "A target called \"" + n + "\" already exists. Replace it?", "Replace", "Cancel", this,
                    juce::ModalCallbackFunction::create ([safe, n] (int r) { if (r == 1 && safe != nullptr) safe->writeTarget (n); }));
                return;
            }
            writeTarget (n);
        }

        void writeTarget (const juce::String& n)
        {
            std::vector<const targets::TrackResult*> tracks;
            {
                const juce::ScopedLock sl (lock);
                for (auto& it : items) if (it->state.load() == 2) tracks.push_back (&it->result);
            }
            const auto [lo, hi] = range();
            const auto w = targets::write (n, tracks, targets::defaultFolder(), lo, hi);
            lastMessage = {};
            if (w.error.isNotEmpty())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Couldn't save", w.error);
            else
                lastSaved = "Saved \"" + n + "\" (" + juce::String (w.songs) + " songs) to the targets folder";
            updateUi();
        }

        juce::AudioFormatManager formats;
        juce::ThreadPool pool { juce::jmax (1, juce::SystemStats::getNumCpus() - 1) };
        std::vector<std::shared_ptr<Item>> items;
        juce::CriticalSection lock;
        juce::TableListBox table;
        Preview preview;
        juce::TextButton addFiles { "Add songs..." }, addFolder { "Add folder..." }, removeBtn { "Remove" }, clearBtn { "Clear" },
                         saveBtn { "Save target" }, openFolder { "Open targets folder" };
        juce::Label nameLabel, status;
        juce::TextEditor name;
        juce::ComboBox rangeBox;
        juce::Rectangle<int> listArea;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String lastMessage, lastSaved;
        bool dragOver = false, dirty = false;
        int lastDone = -1;
        juce::TooltipWindow tips { this };
    };

    //==========================================================================
    class Window : public juce::DocumentWindow
    {
    public:
        explicit Window (const juce::StringArray& paths)
            : DocumentWindow ("Spectroscope Target Builder", kBg, allButtons)
        {
            setUsingNativeTitleBar (true);
            auto* p = new Panel();
            setContentOwned (p, true);
            setResizable (true, true);
            setResizeLimits (680, 480, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
            if (! paths.isEmpty()) p->addPaths (paths);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
}

//==============================================================================
class TargetBuilderApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Spectroscope Target Builder"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        const auto args = getCommandLineParameterArray();
        if (args.contains ("--cli"))
        {
           #if JUCE_WINDOWS
            if (AttachConsole (ATTACH_PARENT_PROCESS)) { std::freopen ("CONOUT$", "w", stdout); std::freopen ("CONOUT$", "w", stderr); }
           #endif
            setApplicationReturnValue (runTargetBuilderCli (args));
            quit();
            return;
        }
        juce::LookAndFeel::setDefaultLookAndFeel (&lnf);
        juce::StringArray paths;                         // files / folders dropped onto the .exe
        for (auto& a : args) if (! a.startsWith ("-")) paths.add (a.unquoted());
        window = std::make_unique<Window> (paths);
    }

    void shutdown() override { window.reset(); juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override {}

private:
    AppLookAndFeel lnf;
    std::unique_ptr<Window> window;
};

START_JUCE_APPLICATION (TargetBuilderApp)
