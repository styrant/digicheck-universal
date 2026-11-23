// The Monitor view: a modular metering page in the spirit of iZotope Insight 2.
//
//   toolbar  Monitor | a chip per module (show / hide) | Layout (columns, reset layout) | Reset
//   modules  Loudness   short-term, integrated, momentary (or its max), LRA, true peak, against a target
//            Dynamics   PSR (peak to short-term loudness) now, PLR (peak to integrated loudness) for the
//                       whole take, a zoned PSR scale and a PSR history with the short-term loudness
//            Levels     peak / RMS bars for L R (and M S), peak hold, clip markers, maxima
//            Sound Field polar sample, polar level or Lissajous, with a correlation bar, width and balance
//            Phase      correlation (fast, slow, minimum), per band (low / mid / high), balance, history
//
// Every module has a header: the gear opens its options, the corner button maximises it (double-click
// the header too), x hides it. Drag a header onto another module to swap their places. Wide modules span
// the whole row; any module can be made wide or narrow from its gear menu. The layout is saved.
//
// The measurements are monitor::MonitorModel (pure C++); the audio arrives through a FifoTap, so the
// page can be a plugin editor's content unchanged. They keep running while the page is hidden, so the
// integrated loudness, LRA and PLR cover everything since the last reset (like the bottom bar's boxes,
// which reset together with these).
#pragma once
#include <JuceHeader.h>
#include "../FifoTap.h"
#include "../Settings.h"
#include "MonitorModel.h"

class MonitorPage : public juce::Component
{
public:
    enum ModuleId { Loudness = 0, Dynamics = 1, Levels = 2, SoundField = 3, Phase = 4, NumModules = 5 };

    // sharedModel: measurements kept elsewhere (the plugin's Core, which keeps measuring while the window is
    // closed); nullptr: the page's own
    MonitorPage (FifoTap&, Settings&, std::function<void()> settingsChanged, std::function<void()> resetLoudness,
                 monitor::MonitorModel* sharedModel = nullptr);
    ~MonitorPage() override;

    void setActive (bool shown);
    void onFrame (double vblankSeconds);         // drains the input; repaints when shown
    void pump();                                 // drains the input only (from a timer, while frames may stall)
    void resetMeasurements();                    // integrated loudness, LRA, PLR, maxima, history
    void applySettings();

    void paint (juce::Graphics&) override;
    void resized() override;

    monitor::MonitorModel& model()               { return engine; }
    void loadForTest (const juce::File& f);      // self-test: runs a file through the meters and holds them

    struct Module; struct LoudnessModule; struct DynamicsModule; struct LevelsModule; struct FieldModule; struct PhaseModule;

    // layout (used by the modules)
    void toggleMax (int id);
    void setShown (int id, bool shown);
    void setWide (int id, bool wide);
    bool isWide (int id) const;
    bool isMaxed (int id) const                  { return s.monitorMax == id; }
    void dragModule (int id, juce::Point<int> pagePos);
    void dropModule (int id, juce::Point<int> pagePos);
    Settings& settings()                         { return s; }
    void changed()                               { if (onChanged) onChanged(); }

private:
    std::vector<int> order() const;
    void setOrder (const std::vector<int>&);
    void layoutModules();
    void updateChips();
    void showLayoutMenu();
    int moduleAt (juce::Point<int> pagePos, int except) const;

    FifoTap& tap;
    Settings& s;
    std::function<void()> onChanged, onResetLoudness;
    monitor::MonitorModel ownModel;
    monitor::MonitorModel& engine;
    const bool shared;
    bool active = false, held = false;
    double lastFrame = 0;

    std::array<std::unique_ptr<Module>, NumModules> modules;
    std::array<juce::TextButton, NumModules> chips;
    juce::TextButton layoutBtn { "Layout" }, resetBtn { "Reset" };
    juce::Rectangle<int> toolbar, titleArea, dropHighlight;
    int dragTarget = -1;
};
