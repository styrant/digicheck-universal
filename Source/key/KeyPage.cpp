#include "KeyPage.h"
#include "../UiScale.h"

namespace
{
    const juce::Colour kBg { 0xff111214 }, kPanel { 0xff17191d }, kLine { 0xff2a2c31 }, kText { 0xffe8e8e8 }, kMuted { 0xffb0b6c0 },
                       kGreen { 0xff3ddc84 }, kBlue { 0xff7fb0ff }, kRed { 0xffe5484d }, kAccent { 0xff4c8dff }, kMelody { 0xffffb347 };

    void caption (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text, const juce::String& right = {})
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f)); g.setFont (juce::FontOptions (ui::fs (12.0f), juce::Font::bold));
        g.drawText (text, area, juce::Justification::centredLeft);
        if (right.isNotEmpty())
        {
            g.setColour (juce::Colours::white.withAlpha (0.4f)); g.setFont (juce::FontOptions (ui::fs (11.5f)));
            g.drawText (right, area, juce::Justification::centredRight);
        }
    }
    void bar (juce::Graphics& g, juce::Rectangle<int> area, float p, juce::Colour c)
    {
        g.setColour (juce::Colours::white.withAlpha (0.07f)); g.fillRoundedRectangle (area.toFloat(), 3.0f);
        g.setColour (c); g.fillRoundedRectangle (area.toFloat().withWidth (juce::jmax (4.0f, (float) area.getWidth() * juce::jlimit (0.0f, 1.0f, p))), 3.0f);
    }
    int best (const std::array<float, 24>& p, int rank)
    {
        std::array<int, 24> idx;
        for (int i = 0; i < 24; ++i) idx[(size_t) i] = i;
        std::partial_sort (idx.begin(), idx.begin() + rank + 1, idx.end(), [&] (int a, int b) { return p[(size_t) a] > p[(size_t) b]; });
        return idx[(size_t) rank];
    }
    juce::String tags (int cls) { return juce::String (KeyNet::camelot (cls)) + "   " + KeyNet::openKey (cls); }
    juce::String mmss (double s) { const int t = juce::roundToInt (s); return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); }

    // a stable colour per instrument
    juce::Colour colourFor (int program)
    {
        static const juce::uint32 pal[] = { 0xff7fb0ff, 0xff3ddc84, 0xffc792ea, 0xff5fd7d7, 0xfff78c6c, 0xffe6db74, 0xffff79c6, 0xff9aa5ce, 0xff8bd5a0, 0xffd7a65f };
        return juce::Colour (pal[(size_t) ((program * 7 + 3) % (int) std::size (pal))]);
    }
    // chord colours around the circle of fifths, so related chords look related
    juce::Colour chordColour (int root)
    {
        if (root < 0) return juce::Colour (0xff2a2c31);
        const int fifths = (root * 7) % 12;
        return juce::Colour::fromHSV ((float) fifths / 12.0f, 0.45f, 0.42f, 1.0f);
    }
}

//==============================================================================
// Left column: the key finder's result over the key chart
struct KeyPage::Sidebar : public juce::Component
{
    explicit Sidebar (KeyPage& p) : page (p) {}
    KeyPage& page;
    KeyFinder::Result res;
    KeyFinder::FileResult file;

    void update()
    {
        res = page.finder.getResult();
        file = page.finder.getFileResult();
        repaint();
    }

    bool fileMode() const { return page.source == Source::File; }
    bool liveMode() const { return page.source == Source::Recording || page.source == Source::Recorded; }

    void paint (juce::Graphics& g) override
    {
        g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        auto r = getLocalBounds().reduced (ui::px (16), ui::px (12));
        caption (g, r.removeFromTop (ui::px (18)), "KEY");
        r.removeFromTop (ui::px (6));

        int nowKey = -1, sessionKey = -1;
        const int chartH = juce::jlimit (ui::px (12 * 17 + 32), ui::px (12 * 28 + 32), r.getHeight() - ui::px (250));
        auto chart = r.removeFromBottom (chartH);
        r.removeFromBottom (ui::px (8));

        if (fileMode())
        {
            if (file.state == KeyFinder::FileResult::Working)
            {
                g.setColour (kText.withAlpha (0.7f)); g.setFont (juce::FontOptions (ui::fs (15.0f)));
                g.drawText ("Analysing...  " + juce::String (juce::roundToInt (file.progress * 100)) + "%", r.removeFromTop (ui::px (26)), juce::Justification::centredLeft);
                bar (g, r.removeFromTop (ui::px (8)), (float) file.progress, kGreen);
            }
            else if (file.state == KeyFinder::FileResult::Failed)
            {
                g.setColour (kRed); g.setFont (juce::FontOptions (ui::fs (14.0f)));
                g.drawFittedText (file.error, r.removeFromTop (ui::px (80)), juce::Justification::topLeft, 4);
            }
            else if (file.state == KeyFinder::FileResult::Done)
            {
                nowKey = best (file.probs, 0);
                keyBlock (g, r, file.probs, kGreen);
                r.removeFromTop (ui::px (10));
                mixes (g, r, nowKey);
            }
        }
        else if (liveMode())
        {
            const bool rec = page.source == Source::Recording;
            caption (g, r.removeFromTop (ui::px (20)), rec ? "NOW" : "LAST HEARD", res.nowValid ? "last " + juce::String (juce::roundToInt (res.windowSeconds)) + " s" : juce::String());
            if (res.nowValid) { nowKey = best (res.now, 0); bigKey (g, r, res.now, kGreen); }
            else
            {
                g.setColour (kText.withAlpha (0.7f)); g.setFont (juce::FontOptions (ui::fs (14.0f)));
                g.drawFittedText (statusText(), r.removeFromTop (ui::px (60)), juce::Justification::topLeft, 3);
            }
            r.removeFromTop (ui::px (10));
            g.setColour (kLine); g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
            r.removeFromTop (ui::px (8));
            const int secs = (int) res.secondsHeard;
            caption (g, r.removeFromTop (ui::px (20)), "SESSION", secs > 0 ? mmss (secs) + " analysed" : juce::String());
            if (res.sessionValid)
            {
                sessionKey = best (res.session, 0);
                bigKey (g, r, res.session, kBlue);
                r.removeFromTop (ui::px (6));
                mixes (g, r, sessionKey);
            }
            else
            {
                g.setColour (kText.withAlpha (0.4f)); g.setFont (juce::FontOptions (ui::fs (13.0f)));
                g.drawText ("Builds up while recording", r.removeFromTop (ui::px (22)), juce::Justification::centredLeft);
            }
        }
        else
        {
            g.setColour (kText.withAlpha (0.55f)); g.setFont (juce::FontOptions (ui::fs (14.0f)));
            g.drawFittedText ("Record the input or load a song to find its key.", r.removeFromTop (ui::px (60)), juce::Justification::topLeft, 3);
        }
        paintChart (g, chart, nowKey, sessionKey);
    }

    juce::String statusText() const
    {
        if (! res.modelOk) return "The key model could not be loaded: " + res.error;
        if (! page.inputRunning) return "No input. Open Options (bottom left), choose an input and press Start.";
        if (res.quiet) return "Too quiet to judge. Waiting for music...";
        const int heard = (int) res.secondsHeard, need = KeyFinder::kMinFrames / 5;
        return page.source == Source::Recording ? "Listening... " + juce::String (juce::jmin (heard, need)) + " / " + juce::String (need) + " s"
                                                : "Not enough audio for a key.";
    }

    void bigKey (juce::Graphics& g, juce::Rectangle<int>& r, const std::array<float, 24>& p, juce::Colour c)
    {
        const int k = best (p, 0);
        auto line = r.removeFromTop (ui::px (36));
        g.setColour (c); g.setFont (juce::FontOptions (ui::fs (26.0f), juce::Font::bold));
        g.drawText (KeyNet::keyName (k), line, juce::Justification::centredLeft);
        g.setColour (kText.withAlpha (0.7f)); g.setFont (juce::FontOptions (ui::fs (13.5f)));
        g.drawText (juce::String (juce::roundToInt (p[(size_t) k] * 100)) + "%", line, juce::Justification::centredRight);
        g.setColour (kText); g.setFont (juce::FontOptions (ui::fs (14.0f), juce::Font::bold));
        g.drawText (tags (k), r.removeFromTop (ui::px (20)), juce::Justification::centredLeft);
    }

    void keyBlock (juce::Graphics& g, juce::Rectangle<int>& r, const std::array<float, 24>& p, juce::Colour c)
    {
        bigKey (g, r, p, c);
        r.removeFromTop (4);
        bar (g, r.removeFromTop (ui::px (6)), p[(size_t) best (p, 0)], c);
        r.removeFromTop (ui::px (10));
        caption (g, r.removeFromTop (ui::px (18)), "ALSO LIKELY");
        for (int rank = 1; rank <= 2; ++rank)
        {
            const int o = best (p, rank);
            auto l = r.removeFromTop (ui::px (22));
            g.setColour (kText.withAlpha (0.85f)); g.setFont (juce::FontOptions (ui::fs (13.5f)));
            g.drawText (KeyNet::keyName (o) + "  (" + KeyNet::camelot (o) + ")", l.removeFromLeft (ui::px (160)), juce::Justification::centredLeft);
            g.setColour (kText.withAlpha (0.6f)); g.setFont (juce::FontOptions (ui::fs (12.5f)));
            g.drawText (juce::String (juce::roundToInt (p[(size_t) o] * 100)) + "%", l.removeFromRight (ui::px (36)), juce::Justification::centredRight);
            bar (g, l.reduced (4, l.getHeight() / 2 - 3), p[(size_t) o], juce::Colour (0xff8a93a6));
        }
    }

    void mixes (juce::Graphics& g, juce::Rectangle<int>& r, int k)
    {
        caption (g, r.removeFromTop (ui::px (18)), "MIXES WELL WITH");
        const int n = k % 12, off = k < 12 ? 0 : 12;
        juce::String txt;
        for (int c : { (n + 11) % 12 + off, (n + 1) % 12 + off, n + (12 - off) })
        {
            const auto name = KeyNet::keyName (c);
            txt << KeyNet::camelot (c) << " " << name.substr (0, name.find (' ')) << (KeyNet::isMajor (c) ? "" : "m") << "    ";
        }
        g.setColour (kText.withAlpha (0.8f)); g.setFont (juce::FontOptions (ui::fs (13.5f)));
        g.drawText (txt.trimEnd(), r.removeFromTop (ui::px (22)), juce::Justification::centredLeft);
    }

    // the key chart: major keys and their relative minors; the estimate filled, the session outlined
    void paintChart (juce::Graphics& g, juce::Rectangle<int> r, int nowKey, int sessionKey)
    {
        static const char* rows[][2] = { { "C", "A" }, { "C#", "A#" }, { "D", "B" }, { "D#", "C" }, { "E", "C#" }, { "F", "D" },
                                         { "F#", "D#" }, { "G", "E" }, { "G#", "F" }, { "A", "F#" }, { "A#", "G" }, { "B", "G#" } };
        auto head = r.removeFromTop (ui::px (22));
        caption (g, head.removeFromLeft (r.getWidth() / 2), "MAJOR");
        caption (g, head, "RELATIVE MINOR");
        g.setColour (kLine); g.drawHorizontalLine (r.getY() + 2, (float) r.getX(), (float) r.getRight());
        r.removeFromTop (ui::px (6));
        const int rowH = juce::jmax (ui::px (16), r.getHeight() / 12);
        auto cell = [&] (int cls)
        {
            const int pc = KeyNet::tonicPitchClass (cls);
            const bool major = KeyNet::isMajor (cls);
            const int row = major ? pc : (pc + 3) % 12;
            auto line = juce::Rectangle<int> (r.getX(), r.getY() + row * rowH, r.getWidth(), rowH);
            return (major ? line.removeFromLeft (r.getWidth() / 2) : line.withTrimmedLeft (r.getWidth() / 2)).toFloat();
        };
        if (sessionKey >= 0) { g.setColour (kAccent); g.drawRoundedRectangle (cell (sessionKey).expanded (4, -2).withTrimmedRight (10), 5.0f, 1.6f); }
        if (nowKey >= 0)     { g.setColour (kGreen.withAlpha (0.22f)); g.fillRoundedRectangle (cell (nowKey).expanded (4, -2).withTrimmedRight (12), 5.0f); }
        g.setFont (juce::FontOptions (ui::fs (juce::jlimit (12.0f, 16.0f, (float) rowH / ui::scale() * 0.55f))));
        for (int i = 0; i < 12; ++i)
        {
            auto line = juce::Rectangle<int> (r.getX(), r.getY() + i * rowH, r.getWidth(), rowH);
            g.setColour (kText);
            g.drawText (juce::String (rows[i][0]) + " major", line.removeFromLeft (r.getWidth() / 2).withTrimmedLeft (4), juce::Justification::centredLeft);
            g.setColour (juce::Colour (0xffb8c4ff));
            g.drawText (juce::String (rows[i][1]) + " minor", line.withTrimmedLeft (4), juce::Justification::centredLeft);
        }
    }
};

//==============================================================================
// Right: chord lane, waveform, piano roll, time ruler
struct KeyPage::Roll : public juce::Component, private juce::ScrollBar::Listener
{
    explicit Roll (KeyPage& p) : page (p)
    {
        addAndMakeVisible (scroll);
        scroll.addListener (this);
        scroll.setAutoHide (true);                        // only when the piece doesn't fit
        scroll.setColour (juce::ScrollBar::thumbColourId, kMuted.withAlpha (0.45f));
    }
    KeyPage& page;
    juce::ScrollBar scroll { false };
    double viewStart = 0, pxPerSec = 40;
    bool userZoomed = false;
    int lo = 48, hi = 84;
    juce::Point<int> hover { -1, -1 };
    float level = 0;
    juce::Point<int> dragFrom { -1, -1 };            // while the mouse is down (no page-turning then)
    double dragStart = 0;

    juce::Rectangle<int> chordLane, waveLane, rollArea, ruler, gutter;

    double seconds() const { return juce::jmax (1.0, page.session.status().seconds); }
    float xOf (double t) const { return (float) rollArea.getX() + (float) ((t - viewStart) * pxPerSec); }
    double tAt (float x) const { return viewStart + (x - (float) rollArea.getX()) / pxPerSec; }
    float yOf (int pitch) const { return (float) rollArea.getY() + (float) (hi - pitch) * rowH(); }
    float rowH() const { return (float) rollArea.getHeight() / (float) (hi - lo + 1); }

    void resized() override
    {
        auto r = getLocalBounds();
        scroll.setBounds (r.removeFromBottom (ui::px (12)));
        ruler = r.removeFromBottom (ui::px (18));
        gutter = r.withWidth (ui::px (38));
        r.removeFromLeft (gutter.getWidth());
        ruler = ruler.withTrimmedLeft (gutter.getWidth());
        chordLane = r.removeFromTop (ui::px (44));
        waveLane = r.removeFromTop (ui::px (26));
        rollArea = r.withTrimmedTop (ui::px (4));
        gutter = gutter.withTop (rollArea.getY()).withBottom (rollArea.getBottom());
        fit (false);
    }

    void dataChanged()
    {
        // pitch range from the notes (at least two octaves, on whole octaves)
        int mn = 127, mx = 0;
        for (auto& n : page.notes) if (! n.drum) { mn = juce::jmin (mn, n.pitch); mx = juce::jmax (mx, n.pitch); }
        if (mn > mx) { mn = 48; mx = 84; }
        lo = (mn / 12) * 12; hi = ((mx + 12) / 12) * 12 - 1;
        if (hi - lo < 24) hi = lo + 35;
        fit (false);
        repaint();
    }

    void fit (bool force)
    {
        if (force) userZoomed = false;
        if (! userZoomed && rollArea.getWidth() > 0)
        {
            // the whole piece when it fits at 18 px/s or more, else 18 px/s and scroll
            pxPerSec = juce::jmax (18.0, rollArea.getWidth() / seconds());
            viewStart = 0;
        }
        clampView();
    }

    void clampView()
    {
        const double visible = rollArea.getWidth() / pxPerSec;
        viewStart = juce::jlimit (0.0, juce::jmax (0.0, seconds() - visible), viewStart);
        scroll.setRangeLimits (0.0, juce::jmax (seconds(), visible));
        scroll.setCurrentRange (viewStart, visible, juce::dontSendNotification);
    }

    void scrollBarMoved (juce::ScrollBar*, double start) override { viewStart = start; repaint(); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            const double t = tAt ((float) e.x);
            pxPerSec = juce::jlimit (4.0, 800.0, pxPerSec * std::pow (1.25, w.deltaY * 4));
            viewStart = t - (e.x - rollArea.getX()) / pxPerSec;
            userZoomed = true;
        }
        else viewStart -= (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY) * 300.0 / pxPerSec;
        clampView();
        repaint();
    }
    void mouseDown (const juce::MouseEvent& e) override { dragFrom = e.getPosition(); dragStart = viewStart; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        viewStart = dragStart - (e.x - dragFrom.x) / pxPerSec;
        clampView(); repaint();
    }
    void mouseDoubleClick (const juce::MouseEvent&) override { fit (true); repaint(); }
    // a click (not a drag) jumps playback there; on a chord, to the chord's start
    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool click = e.getDistanceFromDragStart() <= 3;
        dragFrom = { -1, -1 };
        if (! click || ! page.player.hasAudio() || e.x < rollArea.getX()) return;
        double t = tAt ((float) e.x);
        if (chordLane.contains (e.getPosition()))
            for (auto& c : page.chordList) if (t >= c.start && t < c.end) { t = c.start; break; }
        page.seekTo (juce::jmax (0.0, t));
    }
    void mouseMove (const juce::MouseEvent& e) override { hover = e.getPosition(); repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { hover = { -1, -1 }; repaint(); }

    void paint (juce::Graphics& g) override
    {
        g.setColour (kPanel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
        const auto st = page.session.status();
        const double t0 = viewStart, t1 = tAt ((float) rollArea.getRight());

        if (page.recorder.isRecording())                  // recording: time and level
        {
            auto r = getLocalBounds().withSizeKeepingCentre (ui::px (360), ui::px (120));
            g.setColour (kRed); g.setFont (juce::FontOptions (ui::fs (22.0f), juce::Font::bold));
            g.drawText (juce::String::fromUTF8 ("\xe2\x97\x8f  Recording  ") + mmss (page.recorder.seconds()), r.removeFromTop (ui::px (40)), juce::Justification::centred);
            level = juce::jmax (page.recorder.level(), level * 0.85f);
            bar (g, r.removeFromTop (ui::px (10)).reduced (ui::px (40), 0), juce::jlimit (0.0f, 1.0f, 1.0f + juce::Decibels::gainToDecibels (level) / 60.0f), kGreen);
            r.removeFromTop (ui::px (14));
            g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (13.0f)));
            g.drawFittedText (page.inputRunning ? "The key is found as you play. Press Stop to transcribe the chords and melody."
                                                : "No input. Open Options (bottom left), choose an input and press Start.", r, juce::Justification::centredTop, 3);
            return;
        }
        if (st.seconds <= 0)                               // nothing loaded: a drop zone
        {
            auto zone = getLocalBounds().reduced (ui::px (30)).toFloat();
            juce::Path p; p.addRoundedRectangle (zone, 10.0f);
            const float dash[] = { 8.0f, 6.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (p, p, dash, 2);
            g.setColour (juce::Colours::white.withAlpha (page.dragOver ? 0.6f : 0.22f)); g.fillPath (p);
            auto t = zone.toNearestInt().withSizeKeepingCentre (ui::px (520), ui::px (130));
            g.setColour (kText.withAlpha (0.85f)); g.setFont (juce::FontOptions (ui::fs (19.0f), juce::Font::bold));
            g.drawText (st.state == TranscriptionSession::State::Preparing ? juce::String ("Reading the audio...") : juce::String ("Drag & drop a song here"),
                        t.removeFromTop (ui::px (32)), juce::Justification::centred);
            g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (13.5f)));
            g.drawFittedText ("Finds the key, the chords (with Roman numerals) and the melody, and saves them as MIDI.\n"
                              "WAV, AIFF, FLAC, OGG, MP3.  Or press Browse, or Record to use the live input.", t, juce::Justification::centred, 3);
            return;
        }

        // ---- piano roll background: black-key rows and octave lines
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (rollArea.withLeft (gutter.getX()));
            for (int p = lo; p <= hi; ++p)
            {
                const float y = yOf (p);
                const int pc = p % 12;
                if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10) { g.setColour (juce::Colours::black.withAlpha (0.18f)); g.fillRect (juce::Rectangle<float> ((float) rollArea.getX(), y, (float) rollArea.getWidth(), rowH())); }
                if (pc == 0)
                {
                    g.setColour (kLine); g.drawHorizontalLine ((int) (y + rowH()), (float) rollArea.getX(), (float) rollArea.getRight());
                    g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (10.5f)));
                    g.drawText ("C" + juce::String (p / 12 - 1), gutter.getX(), (int) (y + rowH()) - ui::px (12), gutter.getWidth() - 4, ui::px (12), juce::Justification::centredRight);
                }
            }
        }

        // ---- time grid: every 1, 5, 10 or 30 s depending on zoom
        const double step = pxPerSec > 60 ? 1 : pxPerSec > 15 ? 5 : pxPerSec > 6 ? 10 : 30;
        g.setFont (juce::FontOptions (ui::fs (10.5f)));
        for (double t = std::ceil (t0 / step) * step; t <= t1; t += step)
        {
            const float x = xOf (t);
            g.setColour (kLine.withAlpha (0.7f)); g.drawVerticalLine ((int) x, (float) waveLane.getY(), (float) rollArea.getBottom());
            g.setColour (kMuted); g.drawText (mmss (t), (int) x - 30, ruler.getY(), 60, ruler.getHeight(), juce::Justification::centred);
        }

        // ---- waveform
        if (! page.peaks.empty())
        {
            const float mid = (float) waveLane.getCentreY(), half = waveLane.getHeight() * 0.45f;
            g.setColour (kMuted.withAlpha (0.45f));
            for (int x = waveLane.getX(); x < waveLane.getRight(); ++x)
            {
                const double a = tAt ((float) x), b = tAt ((float) x + 1);
                const int i0 = juce::jmax (0, (int) (a * 100)), i1 = juce::jmin ((int) page.peaks.size(), (int) (b * 100) + 1);
                float pk = 0;
                for (int i = i0; i < i1; ++i) pk = juce::jmax (pk, page.peaks[(size_t) i]);
                if (pk > 0) g.drawVerticalLine (x, mid - pk * half, mid + pk * half);
            }
        }

        // ---- notes (drums left out), then the melody on top
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (rollArea);
            const float h = juce::jmax (2.0f, rowH() - 1.0f);
            if (page.s.keyShowNotes)
                for (auto& n : page.notes)
                {
                    if (n.drum || n.offset < t0 || n.onset > t1 || n.pitch < lo || n.pitch > hi) continue;
                    g.setColour (colourFor (n.program).withAlpha (0.55f));
                    g.fillRoundedRectangle (xOf (n.onset), yOf (n.pitch), juce::jmax (2.0f, (float) ((n.offset - n.onset) * pxPerSec) - 1.0f), h, 2.0f);
                }
            for (auto& m : page.melodyNotes)
            {
                if (m.offset < t0 || m.onset > t1 || m.pitch < lo || m.pitch > hi) continue;
                auto rr = juce::Rectangle<float> (xOf (m.onset), yOf (m.pitch), juce::jmax (3.0f, (float) ((m.offset - m.onset) * pxPerSec) - 1.0f), h);
                g.setColour (kMelody); g.fillRoundedRectangle (rr, 2.0f);
                g.setColour (juce::Colours::white.withAlpha (0.8f)); g.drawRoundedRectangle (rr, 2.0f, 1.0f);
            }
        }

        // ---- not transcribed yet
        if (st.state == TranscriptionSession::State::Running || st.state == TranscriptionSession::State::Loading)
        {
            const float x = juce::jmax ((float) rollArea.getX(), xOf (st.progress * st.seconds));
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, (float) chordLane.getY(), (float) rollArea.getRight(), (float) rollArea.getBottom()));
            g.setColour (kAccent); g.drawVerticalLine ((int) x, (float) chordLane.getY(), (float) rollArea.getBottom());
        }

        // ---- playhead; while playing, the view turns the page to follow it
        if (page.player.hasAudio())
        {
            const double pos = page.player.position();
            if (page.player.isPlaying() && dragFrom.x < 0)
            {
                const double visible = rollArea.getWidth() / pxPerSec;
                if (pos > viewStart + visible * 0.92 || pos < viewStart) { viewStart = pos - visible * 0.08; clampView(); }
            }
            const float x = xOf (pos);
            if (x >= (float) rollArea.getX() && x <= (float) rollArea.getRight())
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.drawLine (x, (float) chordLane.getY(), x, (float) rollArea.getBottom(), 1.5f);
                juce::Path tri; tri.addTriangle (x - 5, (float) ruler.getY(), x + 5, (float) ruler.getY(), x, (float) ruler.getY() + 7);
                g.fillPath (tri);
            }
        }

        // ---- chord lane
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (chordLane);
            for (auto& c : page.chordList)
            {
                if (c.end < t0 || c.start > t1) continue;
                auto rr = juce::Rectangle<float>::leftTopRightBottom (xOf (c.start) + 1, (float) chordLane.getY() + 4, xOf (c.end) - 1, (float) chordLane.getBottom() - 4);
                g.setColour (chordColour (c.root)); g.fillRoundedRectangle (rr, 4.0f);
                if (rr.getWidth() < 14) continue;
                auto text = rr.reduced (5, 1).toNearestInt();
                g.setColour (c.root < 0 ? kMuted : kText);
                g.setFont (juce::FontOptions (ui::fs (15.0f), juce::Font::bold));
                g.drawText (c.name, text.removeFromTop (text.getHeight() * 3 / 5), juce::Justification::bottomLeft, true);
                g.setColour (kText.withAlpha (0.65f)); g.setFont (juce::FontOptions (ui::fs (11.0f)));
                g.drawText (c.roman, text, juce::Justification::topLeft, true);
            }
        }
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (10.5f), juce::Font::bold));
        g.drawFittedText ("CHORDS", gutter.withY (chordLane.getY()).withHeight (chordLane.getHeight()).reduced (2, 0), juce::Justification::centred, 1, 0.6f);

        // ---- hover readout
        if (hover.x >= rollArea.getX() && hover.y >= chordLane.getY() && hover.y < rollArea.getBottom())
        {
            const double t = tAt ((float) hover.x);
            juce::String txt = mmss (t) + "." + juce::String ((int) (std::fmod (t, 1.0) * 10));
            if (chordLane.contains (hover))
            {
                for (auto& c : page.chordList) if (t >= c.start && t < c.end)
                { txt << "   " << c.name << (! c.roman.empty() ? "  (" + juce::String (c.roman) + ")" : juce::String()) << "   " << mmss (c.start) << " - " << mmss (c.end); break; }
            }
            else if (rollArea.contains (hover))
            {
                const int p = hi - (int) ((hover.y - rollArea.getY()) / rowH());
                txt << "   " << harmony::noteName (p, page.key);
                for (auto& m : page.melodyNotes) if (m.pitch == p && t >= m.onset && t < m.offset) { txt << "   melody"; break; }
                for (auto& n : page.notes) if (! n.drum && n.pitch == p && t >= n.onset && t < n.offset) { txt << "   " << transcribe::instrumentName (n.program); break; }
            }
            g.setColour (kMuted.withAlpha (0.5f)); g.drawVerticalLine (hover.x, (float) chordLane.getY(), (float) rollArea.getBottom());
            g.setFont (juce::FontOptions (ui::fs (12.5f)));
            const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 16;
            auto box = juce::Rectangle<int> (juce::jlimit (rollArea.getX(), juce::jmax (rollArea.getX(), rollArea.getRight() - tw), hover.x + 10), rollArea.getY() + 4, tw, ui::px (22));
            g.setColour (juce::Colour (0xe0202227)); g.fillRoundedRectangle (box.toFloat(), 4.0f);
            g.setColour (kText); g.drawText (txt, box, juce::Justification::centred);
        }
    }
};

//==============================================================================
// Shown in the roll area when the chosen model isn't downloaded yet
struct KeyPage::ModelCard : public juce::Component
{
    explicit ModelCard (KeyPage& p) : page (p)
    {
        for (int i = 0; i < ModelStore::kSizes; ++i)
        {
            auto& b = buttons[(size_t) i];
            addAndMakeVisible (b);
            b.onClick = [this, i]
            {
                auto st = page.models.status (i);
                if (st.busy()) page.models.cancel (i);
                else if (ModelStore::isInstalled (i)) { page.s.keyModel = i; page.modelBox.setSelectedId (i + 1, juce::dontSendNotification); if (page.changed) page.changed(); }
                else { page.models.download (i); page.s.keyModel = i; page.modelBox.setSelectedId (i + 1, juce::dontSendNotification); if (page.changed) page.changed(); }
                update();
            };
        }
        addAndMakeVisible (folder);
        folder.onClick = [] { ModelStore::ownFolder().createDirectory(); ModelStore::ownFolder().startAsProcess(); };
        update();
    }
    KeyPage& page;
    std::array<juce::TextButton, ModelStore::kSizes> buttons;
    juce::TextButton folder { "Open models folder" };

    void update()
    {
        for (int i = 0; i < ModelStore::kSizes; ++i)
        {
            const auto st = page.models.status (i);
            auto& b = buttons[(size_t) i];
            b.setButtonText (st.busy() ? "Cancel" : ModelStore::isInstalled (i) ? "Use" : st.done > 0 ? "Resume" : "Download");
            b.setColour (juce::TextButton::buttonColourId, ! st.busy() && ! ModelStore::isInstalled (i) && i == page.s.keyModel ? kAccent : juce::Colour (0xff2a2d33));
        }
        repaint();
    }

    void resized() override
    {
        auto r = inner();
        r.removeFromTop (ui::px (92));
        for (int i = 0; i < ModelStore::kSizes; ++i)
        {
            auto row = r.removeFromTop (ui::px (58));
            buttons[(size_t) i].setBounds (row.removeFromRight (ui::px (96)).withSizeKeepingCentre (ui::px (96), ui::px (28)));
        }
        r.removeFromTop (ui::px (10));
        folder.setBounds (r.removeFromTop (ui::px (28)).removeFromLeft (ui::px (170)));
    }

    juce::Rectangle<int> inner() const { return getLocalBounds().withSizeKeepingCentre (juce::jmin (getWidth() - ui::px (40), ui::px (620)), juce::jmin (getHeight() - ui::px (20), ui::px (380))); }

    void paint (juce::Graphics& g) override
    {
        auto box = inner();
        g.setColour (juce::Colour (0xff1d2026)); g.fillRoundedRectangle (box.toFloat().expanded (ui::px (16)), 8.0f);
        auto r = box;
        g.setColour (kText); g.setFont (juce::FontOptions (ui::fs (18.0f), juce::Font::bold));
        g.drawText ("Chords & melody need a transcription model", r.removeFromTop (ui::px (28)), juce::Justification::centredLeft);
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (13.0f)));
        g.drawFittedText ("A one-time download, not part of the app. The model is NeuralNote's MuScriptor (Kyutai / Mirelo), free for "
                          "non-commercial use (CC BY-NC 4.0). Models NeuralNote has already downloaded are used too.",
                          r.removeFromTop (ui::px (56)), juce::Justification::topLeft, 3);
        r.removeFromTop (ui::px (8));
        for (int i = 0; i < ModelStore::kSizes; ++i)
        {
            auto row = r.removeFromTop (ui::px (58)).withTrimmedRight (ui::px (110));
            const auto& e = ModelStore::entry (i);
            const auto st = page.models.status (i);
            g.setColour (kText); g.setFont (juce::FontOptions (ui::fs (15.0f), juce::Font::bold));
            g.drawText (juce::String (e.label) + (ModelStore::isInstalled (i) ? "  (installed)" : ""), row.removeFromTop (ui::px (22)), juce::Justification::centredLeft);
            if (st.busy())
            {
                g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (12.5f)));
                g.drawText (st.phase == ModelStore::Phase::Verifying ? juce::String ("Checking the download...")
                                : "Downloading  " + juce::File::descriptionOfSizeInBytes (st.done) + " of " + juce::File::descriptionOfSizeInBytes (st.total),
                            row.removeFromTop (ui::px (18)), juce::Justification::centredLeft);
                bar (g, row.removeFromTop (ui::px (6)).withTrimmedRight (ui::px (20)), st.progress(), kAccent);
            }
            else
            {
                g.setColour (st.phase == ModelStore::Phase::Failed ? kRed : kMuted); g.setFont (juce::FontOptions (ui::fs (12.5f)));
                g.drawFittedText (st.phase == ModelStore::Phase::Failed ? st.error : juce::String (e.hint), row.removeFromTop (ui::px (32)), juce::Justification::topLeft, 2);
            }
        }
    }
};

//==============================================================================
// Drag the MIDI file straight into a DAW
struct KeyPage::DragMidi : public juce::TextButton
{
    explicit DragMidi (KeyPage& p) : juce::TextButton ("Drag MIDI"), page (p)
    {
        setTooltip ("Drag into your DAW: one track per instrument, plus Melody and Chords tracks");
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    KeyPage& page;
    bool dragging = false;
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || page.notes.empty()) return;
        dragging = true;
        auto base = page.session.status().sourceName.upToLastOccurrenceOf (".", false, false);
        if (base.isEmpty()) base = "Transcription";
        const auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::File::createLegalFileName (base + " (transcribed).mid"));
        page.writeMidi (f);
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    }
    void mouseUp (const juce::MouseEvent& e) override { if (! dragging) juce::TextButton::mouseUp (e); }
};

//==============================================================================
KeyPage::KeyPage (KeyFinder& kf, TranscriptionSession& ts, LiveRecorder& lr, ModelStore& ms, TranscriptionPlayer& pl, PlaybackOutput* out,
                  Settings& st, std::function<void()> ch)
    : finder (kf), session (ts), recorder (lr), models (ms), player (pl), output (out), s (st), changed (std::move (ch))
{
    setOpaque (true);
    formats.registerBasicFormats();
    sidebar = std::make_unique<Sidebar> (*this);
    roll = std::make_unique<Roll> (*this);
    card = std::make_unique<ModelCard> (*this);
    dragMidi = std::make_unique<DragMidi> (*this);
    addAndMakeVisible (*sidebar);
    addAndMakeVisible (*roll);
    addChildComponent (*card);

    for (auto* b : { &recordBtn, &browseBtn, &clearBtn, &instrumentsBtn, &transcribeBtn, &copyBtn, &saveBtn, static_cast<juce::TextButton*> (dragMidi.get()) })
    {
        addAndMakeVisible (b);
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2d33));
    }
    for (auto* t : { &autoToggle, &seventhsToggle, &notesToggle }) addAndMakeVisible (t);
    addAndMakeVisible (modelBox);
    addAndMakeVisible (deviceBox);

    recordBtn.setTooltip ("Record the live input: its key is found as it plays; Stop to transcribe the recording");
    recordBtn.onClick = [this] { toggleRecording(); };
    browseBtn.setTooltip ("Analyse an audio file (or drop one anywhere on this page)");
    browseBtn.onClick = [this] { browse(); };
    clearBtn.setTooltip ("Clear the page: stops playback and any transcription, and removes the audio, notes, chords and key, to start again");
    clearBtn.onClick = [this] { clearAll(); };
    transcribeBtn.onClick = [this]
    {
        if (session.status().busy()) session.cancel();
        else startTranscription();
    };
    instrumentsBtn.setTooltip ("Which instruments the model listens for. Choosing the right ones helps; All lets it decide");
    instrumentsBtn.onClick = [this] { showInstrumentMenu(); };

    for (int i = 0; i < ModelStore::kSizes; ++i) modelBox.addItem ("Model: " + juce::String (ModelStore::entry (i).label), i + 1);
    modelBox.setSelectedId (s.keyModel + 1, juce::dontSendNotification);
    modelBox.setTooltip ("Small is fastest; Medium and Large are better on full mixes and want a GPU");
    modelBox.onChange = [this] { s.keyModel = modelBox.getSelectedId() - 1; if (changed) changed(); card->update(); updateHeader(); };

    deviceBox.addItem ("Device: Auto", 1);
    deviceBox.setSelectedId (1, juce::dontSendNotification);
    deviceBox.setTooltip ("Where the model runs. Auto picks the best GPU, or the CPU when there is none");
    deviceBox.onChange = [this]
    {
        const int id = deviceBox.getSelectedId();
        s.keyDevice = id <= 1 || ! devicesReady ? juce::String() : juce::String (devices[(size_t) (id - 2)].name);
        if (changed) changed();
    };

    autoToggle.setToggleState (s.keyAuto, juce::dontSendNotification);
    autoToggle.setTooltip ("Transcribe as soon as a song is loaded or a recording stops");
    autoToggle.onClick = [this] { s.keyAuto = autoToggle.getToggleState(); if (changed) changed(); };
    seventhsToggle.setToggleState (s.keySevenths, juce::dontSendNotification);
    seventhsToggle.setTooltip ("Name 7th and 6th chords (off: triads and sus chords only)");
    seventhsToggle.onClick = [this] { s.keySevenths = seventhsToggle.getToggleState(); if (changed) changed(); refreshData (true); };
    notesToggle.setToggleState (s.keyShowNotes, juce::dontSendNotification);
    notesToggle.setTooltip ("Show every transcribed note (coloured by instrument); the melody is always shown");
    notesToggle.onClick = [this] { s.keyShowNotes = notesToggle.getToggleState(); if (changed) changed(); roll->repaint(); };

    copyBtn.setTooltip ("Copy the chords as text");
    copyBtn.onClick = [this] { copyChords(); };
    saveBtn.setTooltip ("Save a MIDI file: one track per instrument, plus Melody and Chords tracks");
    saveBtn.onClick = [this] { saveMidi(); };

    // ---- player
    addAndMakeVisible (playBtn);
    playBtn.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a2d33));
    playBtn.setTooltip ("Play / pause (Space). Click the timeline, waveform or a chord to jump there");
    playBtn.onClick = [this] { togglePlay(); };
    listenBox.addItemList ({ "Listen: Original", "Listen: Mix", "Listen: Transcription" }, 1);
    listenBox.setSelectedId (s.keyListen + 1, juce::dontSendNotification);
    listenBox.setTooltip ("Original: the song or recording. Transcription: only the synth playing the notes found. Mix: both, to hear where they differ");
    listenBox.onChange = [this] { s.keyListen = listenBox.getSelectedId() - 1; player.setListen (s.keyListen); if (changed) changed(); };
    synthBox.addItemList ({ "Synth: Melody + chords", "Synth: All notes", "Synth: Melody", "Synth: Chords" }, 1);
    synthBox.setSelectedId (s.keySynth + 1, juce::dontSendNotification);
    synthBox.setTooltip ("What the synth plays: the melody line with the chords (bass + pad), every transcribed note (drums too), or one of them");
    synthBox.onChange = [this] { s.keySynth = synthBox.getSelectedId() - 1; rebuildEvents(); if (changed) changed(); };
    volumeSlider.setRange (0.0, 1.0);
    volumeSlider.setValue (s.keyVolume, juce::dontSendNotification);
    volumeSlider.setTooltip ("Playback volume");
    volumeSlider.onValueChange = [this] { s.keyVolume = (float) volumeSlider.getValue(); player.setVolume (s.keyVolume); };
    volumeSlider.onDragEnd = [this] { if (changed) changed(); };
    sensSlider.setRange (0, 100, 1);
    sensSlider.setValue (s.keySensitivity, juce::dontSendNotification);
    sensSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, ui::px (34), ui::px (20));
    sensSlider.setTooltip ("Sensitivity. Lower it when the transcription has notes that aren't there (overtones, ghost notes): weak and very short "
                           "notes are dropped at once. Raise it when notes are missing: everything found is kept, and the next transcription "
                           "also listens to quieter passages (press Transcribe again)");
    sensSlider.onValueChange = [this] { s.keySensitivity = (int) sensSlider.getValue(); filterNotes(); };
    sensSlider.onDragEnd = [this] { if (changed) changed(); };
    for (auto* c : std::initializer_list<juce::Component*> { &listenBox, &synthBox, &volumeSlider, &sensSlider }) addAndMakeVisible (c);
    if (output != nullptr)
    {
        addAndMakeVisible (outputBox);
        outputBox.setTooltip ("Where playback goes (Windows Audio, shared)");
        outputBox.onChange = [this]
        {
            const int id = outputBox.getSelectedId();
            s.keyOutput = id <= 1 ? juce::String() : outputBox.getItemText (outputBox.getSelectedItemIndex());
            if (changed) changed();
            const bool wasPlaying = player.isPlaying();
            player.pause();
            output->close();                          // reopened on the next Play
            if (wasPlaying) togglePlay();
        };
        outputBox.addItem ("Output: System default", 1);
        outputBox.setSelectedId (1, juce::dontSendNotification);
        auto names = output->outputNames();
        if (s.keyOutput.isNotEmpty()) names.addIfNotAlreadyThere (s.keyOutput);
        for (int i = 0; i < names.size(); ++i)
        {
            outputBox.addItem (names[i], i + 2);
            if (names[i] == s.keyOutput) outputBox.setSelectedId (i + 2, juce::dontSendNotification);
        }
    }
    player.setListen (s.keyListen);
    player.setVolume (s.keyVolume);

    // GPU start-up can take seconds: list the devices off the message thread
    deviceThread = std::thread ([this]
    {
        auto list = transcribe::devices();
        juce::MessageManager::callAsync ([this, safe = juce::Component::SafePointer<KeyPage> (this), list]
        {
            if (safe == nullptr) return;
            devices = list;
            devicesReady = true;
            const int autoIndex = transcribe::autoDevice();
            deviceBox.clear (juce::dontSendNotification);
            deviceBox.addItem ("Device: Auto (" + juce::String (autoIndex >= 0 && autoIndex < (int) devices.size() ? devices[(size_t) autoIndex].name : "CPU") + ")", 1);
            for (size_t i = 0; i < devices.size(); ++i) deviceBox.addItem (juce::String (devices[i].name) + (devices[i].backend == "CPU" ? "" : "  (" + juce::String (devices[i].backend) + ")"), (int) i + 2);
            int sel = 1;
            for (size_t i = 0; i < devices.size(); ++i) if (s.keyDevice == juce::String (devices[i].name)) sel = (int) i + 2;
            deviceBox.setSelectedId (sel, juce::dontSendNotification);
        });
    });

    updateHeader();
    startTimerHz (15);
}

KeyPage::~KeyPage()
{
    stopTimer();
    if (deviceThread.joinable()) deviceThread.join();
}

void KeyPage::setActive (bool shown)
{
    active = shown;
    if (shown) { refreshData (true); card->update(); }
}

//==============================================================================
void KeyPage::loadSource (const juce::File& f)
{
    if (recorder.isRecording()) recorder.stop();
    finder.setActive (false);
    source = Source::File;
    lastFile = f;
    finder.analyzeFile (f);
    session.loadFile (f);
    player.loadFile (f, formats);
    autoPending = s.keyAuto;
    refreshData (true);
    updateHeader();
}

void KeyPage::toggleRecording()
{
    if (recorder.isRecording())
    {
        auto take = recorder.stop();
        finder.setActive (false);
        const double rate = recorder.sampleRate();
        source = Source::Recorded;
        if (! take.empty() && rate > 0)
        {
            session.setAudio (std::move (take), rate, "Recording, " + juce::Time::getCurrentTime().toString (false, true, false, true));
            autoPending = s.keyAuto;
        }
    }
    else
    {
        session.clear();
        player.unload();
        finder.reset();
        finder.setActive (true);
        recorder.start();
        source = Source::Recording;
    }
    refreshData (true);
    updateHeader();
}

void KeyPage::clearAll()
{
    if (recorder.isRecording()) recorder.stop();
    player.unload();                              // stops playback
    session.clear();                              // cancels a transcription and drops the audio and notes
    finder.setActive (false);
    finder.reset();
    source = Source::None;
    lastFile = juce::File();
    autoPending = false;
    playError = {};
    rawNotes.clear(); strengths.clear();
    peaks.clear();
    refreshData (true);
    roll->fit (true);
    updateHeader();
    repaint();
}

void KeyPage::browse()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a song to analyse", juce::File::getSpecialLocation (juce::File::userMusicDirectory), session.wildcard());
    juce::Component::SafePointer<KeyPage> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult().existsAsFile()) safe->loadSource (fc.getResult());
    });
}

bool KeyPage::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (session.canRead (juce::File (f))) return true;
    return false;
}

void KeyPage::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files) if (session.canRead (juce::File (f))) { loadSource (juce::File (f)); break; }
    repaint();
}

int KeyPage::selectedDevice() const
{
    if (! devicesReady || s.keyDevice.isEmpty()) return -1;
    for (size_t i = 0; i < devices.size(); ++i) if (s.keyDevice == juce::String (devices[i].name)) return (int) i;
    return -1;                                    // a device that's gone: Auto
}

void KeyPage::startTranscription()
{
    const auto model = ModelStore::modelFile (s.keyModel);
    if (model == juce::File()) { card->update(); updateHeader(); return; }
    std::vector<int> ids;
    for (auto& t : juce::StringArray::fromTokens (s.keyInstruments, ",", {})) if (t.trim().isNotEmpty()) ids.push_back (t.getIntValue());
    session.transcribe (model, selectedDevice(), ids, transcribe::Sensitivity::gateDbfs (s.keySensitivity));
    updateHeader();
}

void KeyPage::showInstrumentMenu()
{
    juce::StringArray chosen = juce::StringArray::fromTokens (s.keyInstruments, ",", {});
    chosen.removeEmptyStrings();
    juce::PopupMenu m;
    m.addItem (1, "All instruments (the model decides)", true, chosen.isEmpty());
    m.addSeparator();
    for (auto& in : transcribe::instruments())
        m.addItem (1000 + in.id, juce::String (in.name), true, chosen.contains (juce::String (in.id)));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&instrumentsBtn), [this, chosen] (int id) mutable
    {
        if (id == 0) return;
        if (id == 1) chosen.clear();
        else
        {
            const auto v = juce::String (id - 1000);
            if (chosen.contains (v)) chosen.removeString (v); else chosen.add (v);
        }
        s.keyInstruments = chosen.joinIntoString (",");
        if (changed) changed();
        updateHeader();
        if (id != 1 || chosen.isEmpty()) showInstrumentMenu();          // keep the menu open while picking
    });
}

//==============================================================================
harmony::Key KeyPage::chordKey (juce::String* from) const
{
    harmony::Key k;
    auto fromNet = [&] (const std::array<float, 24>& p, const char* what)
    {
        const int c = best (p, 0);
        k.tonic = KeyNet::tonicPitchClass (c); k.minor = ! KeyNet::isMajor (c); k.valid = true; k.confidence = p[(size_t) c];
        if (from) *from = what;
    };
    if (source == Source::File)
    {
        const auto fr = finder.getFileResult();
        if (fr.state == KeyFinder::FileResult::Done) { fromNet (fr.probs, "the key finder"); return k; }
    }
    else if (source == Source::Recorded || source == Source::Recording)
    {
        const auto r = finder.getResult();
        if (r.sessionValid) { fromNet (r.session, "the key finder"); return k; }
    }
    k = harmony::estimateKey (notes);
    if (from) *from = "the notes";
    return k;
}

harmony::Options KeyPage::harmonyOptions() const
{
    harmony::Options o;
    o.sevenths = s.keySevenths;
    return o;
}

void KeyPage::refreshData (bool force)
{
    const auto st = session.status();
    juce::String from;
    const auto k = chordKey (&from);
    const bool keyChanged = k.valid != key.valid || k.tonic != key.tonic || k.minor != key.minor;
    if (! force && st.version == lastVersion && ! keyChanged) return;
    const bool audioChanged = force || lastVersion < 0 || peaks.empty() != (st.seconds <= 0);
    lastVersion = st.version;
    rawNotes = session.notes();
    strengths = session.strengths (rawNotes);
    key = k; keySource = from;
    if (audioChanged || st.state == TranscriptionSession::State::Ready) peaks = session.peaks (100);
    filterNotes();
}

void KeyPage::filterNotes()
{
    // the sensitivity filter, then everything that depends on the notes
    notes = transcribe::filterNotes (rawNotes, strengths, s.keySensitivity);
    if (! notes.empty() && key.valid && keySource == "the notes") key = harmony::estimateKey (notes);
    melodyNotes = harmony::melody (notes, harmonyOptions());
    chordList = harmony::chords (notes, key, harmonyOptions());
    rebuildEvents();
    roll->dataChanged();
    repaint (footer);
}

void KeyPage::rebuildEvents()
{
    using P = TranscriptionPlayer;
    std::vector<P::Event> ev;
    auto add = [&] (double on, double off, int pitch, int layer, float vel = 0.8f)
    {
        ev.push_back ({ on, pitch, layer, true, vel });
        ev.push_back ({ std::max (on + 0.02, off), pitch, layer, false, vel });
    };
    const int what = s.keySynth;                     // 0 melody + chords, 1 all notes, 2 melody, 3 chords
    if (what == 0 || what == 2)
        for (auto& m : melodyNotes) add (m.onset, m.offset, m.pitch, P::Melody);
    if (what == 0 || what == 3)
        for (auto& c : chordList)
        {
            if (c.root < 0) continue;
            const auto& t = harmony::qualityTemplate (c.quality);
            for (int i = 0; i < 12; ++i)
                if (t[(size_t) i]) add (c.start, c.end, 55 + (c.root + i - 7 + 24) % 12, P::Chords);   // voiced G3..F#4
            add (c.start, c.end, 36 + (c.bass >= 0 ? c.bass : c.root), P::Bass);
        }
    if (what == 1)
        for (auto& n : notes) add (n.onset, n.offset, n.pitch, n.drum ? P::Drums : P::Notes);
    player.setEvents (std::move (ev));
}

void KeyPage::togglePlay()
{
    if (! player.hasAudio()) return;
    if (! player.isPlaying() && output != nullptr && ! output->isOpen())
    {
        juce::String err;
        if (! output->open (s.keyOutput, err) && ! output->open ({}, err))
        {
            playError = "Can't play: " + err;
            repaint (footer);
            return;
        }
    }
    playError = {};
    player.toggle();
    updateHeader();
}

void KeyPage::renderPreview (const juce::File& wav, double seconds, int listen)
{
    if (! player.hasAudio() || (output != nullptr && output->isOpen())) return;
    const double sr = 48000;
    const int block = 512;
    player.prepareToPlay (block, sr);
    player.setListen (listen);
    player.seek (0);
    juce::Thread::sleep (200);
    player.play();
    juce::AudioBuffer<float> all (2, (int) (seconds * sr)), buf (2, block);
    for (int pos = 0; pos + block <= all.getNumSamples(); pos += block)
    {
        buf.clear();
        player.getNextAudioBlock ({ &buf, 0, block });
        for (int c = 0; c < 2; ++c) all.copyFrom (c, pos, buf, c, 0, block);
        juce::Thread::sleep (1);
    }
    player.pause();
    player.setListen (s.keyListen);
    wav.deleteFile();
    juce::WavAudioFormat fmt;
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (fmt.createWriterFor (new juce::FileOutputStream (wav), sr, 2, 16, {}, 0)))
        w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
}

void KeyPage::seekTo (double t)
{
    player.seek (t);
    roll->repaint();
}

void KeyPage::updateHeader()
{
    const auto st = session.status();
    const bool rec = recorder.isRecording();
    recordBtn.setButtonText (rec ? "Stop" : juce::String::fromUTF8 ("\xe2\x97\x8f  Record"));
    recordBtn.setColour (juce::TextButton::buttonColourId, rec ? kRed.withAlpha (0.85f) : juce::Colour (0xff2a2d33));
    recordBtn.setColour (juce::TextButton::textColourOffId, rec ? juce::Colours::white : kRed.brighter (0.3f));

    playBtn.setButtonText (player.isPlaying() ? juce::String::fromUTF8 ("\xe2\x9d\x9a\xe2\x9d\x9a") : juce::String::fromUTF8 ("\xe2\x96\xb6"));
    playBtn.setEnabled (player.hasAudio() && ! rec);
    playBtn.setColour (juce::TextButton::buttonColourId, player.hasAudio() && ! rec ? kAccent : juce::Colour (0xff2a2d33));

    clearBtn.setEnabled (rec || st.seconds > 0 || st.busy() || source != Source::None);

    const bool haveModel = ModelStore::isInstalled (s.keyModel);
    const bool haveAudio = st.seconds > 0 && ! rec;
    transcribeBtn.setButtonText (st.busy() && st.state != TranscriptionSession::State::Preparing ? "Stop" : "Transcribe");
    transcribeBtn.setEnabled (st.busy() ? st.state != TranscriptionSession::State::Preparing : (haveAudio && haveModel));
    transcribeBtn.setColour (juce::TextButton::buttonColourId, transcribeBtn.isEnabled() && ! st.busy() ? kAccent : juce::Colour (0xff2a2d33));

    juce::StringArray chosen = juce::StringArray::fromTokens (s.keyInstruments, ",", {});
    chosen.removeEmptyStrings();
    instrumentsBtn.setButtonText (chosen.isEmpty() ? "All instruments" : chosen.size() == 1 ? [&] {
        for (auto& in : transcribe::instruments()) if (in.id == chosen[0].getIntValue()) return juce::String (in.name);
        return juce::String ("1 instrument"); }() : juce::String (chosen.size()) + " instruments");

    const bool showCard = haveAudio && ! haveModel && ! st.busy();
    if (card->isVisible() != showCard) { card->setVisible (showCard); if (showCard) card->update(); }
    const bool haveNotes = ! notes.empty();
    for (auto* b : { &copyBtn, &saveBtn, static_cast<juce::TextButton*> (dragMidi.get()) }) b->setEnabled (haveNotes);
    repaint (header);
    repaint (footer);
}

void KeyPage::timerCallback()
{
    const auto st = session.status();
    // start automatically once a file is ready
    if (autoPending && st.state == TranscriptionSession::State::Ready)
    {
        autoPending = false;
        if (ModelStore::isInstalled (s.keyModel)) startTranscription();
    }
    if (st.state == TranscriptionSession::State::Failed) autoPending = false;
    // a finished download: transcribe what is loaded
    for (int i = 0; i < ModelStore::kSizes; ++i)
    {
        const bool busy = models.status (i).busy();
        if (wasBusy[i] && ! busy && ModelStore::isInstalled (i) && i == s.keyModel && st.state == TranscriptionSession::State::Ready && s.keyAuto) startTranscription();
        wasBusy[i] = busy;
    }
    if (card->isVisible()) card->update();
    if (source == Source::Recorded && ! player.hasAudio())
    {
        double rate = 0;
        if (auto take = session.recording (rate)) player.loadRecording (take, rate);
    }
    if (recorder.full() && recorder.isRecording()) toggleRecording();

    if (! active) return;
    sidebar->update();
    refreshData (false);
    updateHeader();
    if (st.busy() || recorder.isRecording() || player.isPlaying()) { roll->repaint(); repaint (timeArea); }
    if (player.isPlaying() != (getTimerInterval() < 50)) startTimerHz (player.isPlaying() ? 30 : 15);
    if (playBtn.getButtonText().isEmpty() || (player.isPlaying() != (playBtn.getButtonText() == juce::String::fromUTF8 ("\xe2\x9d\x9a\xe2\x9d\x9a")))) updateHeader();
}

//==============================================================================
juce::File KeyPage::writeMidi (const juce::File& dest) const
{
    auto file = midiexport::build (notes, key, harmonyOptions());
    dest.deleteFile();
    juce::FileOutputStream out (dest);
    if (out.openedOk()) file.writeTo (out);
    return dest;
}

void KeyPage::saveMidi()
{
    auto base = session.status().sourceName.upToLastOccurrenceOf (".", false, false);
    if (base.isEmpty()) base = "Transcription";
    const auto dir = lastFile.existsAsFile() ? lastFile.getParentDirectory() : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Save MIDI", dir.getChildFile (juce::File::createLegalFileName (base + " (transcribed).mid")), "*.mid");
    juce::Component::SafePointer<KeyPage> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
    {
        if (safe != nullptr && fc.getResult() != juce::File()) safe->writeMidi (fc.getResult().withFileExtension ("mid"));
    });
}

void KeyPage::copyChords()
{
    juce::SystemClipboard::copyTextToClipboard (harmony::chordSheet (chordList, key));
}

//==============================================================================
void KeyPage::resized()
{
    auto r = getLocalBounds().reduced (ui::px (12), ui::px (8));
    header = r.removeFromTop (ui::px (36));
    r.removeFromTop (ui::px (6));
    playerRow = r.removeFromTop (ui::px (30));
    r.removeFromTop (ui::px (8));
    {
        auto p = playerRow;
        playBtn.setBounds (p.removeFromLeft (ui::px (52)).reduced (0, 1)); p.removeFromLeft (ui::px (8));
        timeArea = p.removeFromLeft (ui::px (110));
        p.removeFromLeft (ui::px (6));
        listenBox.setBounds (p.removeFromLeft (ui::px (170)).reduced (0, 1)); p.removeFromLeft (ui::px (6));
        synthBox.setBounds (p.removeFromLeft (ui::px (198)).reduced (0, 1)); p.removeFromLeft (ui::px (10));
        volumeSlider.setBounds (p.removeFromLeft (ui::px (110))); p.removeFromLeft (ui::px (10));
        if (output != nullptr)
        {
            const bool room = p.getWidth() > ui::px (300 + 210);
            outputBox.setVisible (room);
            if (room) { outputBox.setBounds (p.removeFromRight (ui::px (210)).reduced (0, 1)); p.removeFromRight (ui::px (12)); }
        }
        auto sens = p.removeFromRight (juce::jmin (p.getWidth(), ui::px (300)));
        sensLabel = sens.removeFromLeft (ui::px (80));
        sensSlider.setBounds (sens);
    }
    footer = r.removeFromBottom (ui::px (34));
    r.removeFromBottom (ui::px (6));

    auto h = header;
    recordBtn.setBounds (h.removeFromLeft (ui::px (96)).reduced (0, 2)); h.removeFromLeft (ui::px (6));
    browseBtn.setBounds (h.removeFromLeft (ui::px (90)).reduced (0, 2)); h.removeFromLeft (ui::px (6));
    clearBtn.setBounds (h.removeFromLeft (ui::px (64)).reduced (0, 2)); h.removeFromLeft (ui::px (12));
    transcribeBtn.setBounds (h.removeFromRight (ui::px (104)).reduced (0, 2)); h.removeFromRight (ui::px (6));
    autoToggle.setBounds (h.removeFromRight (ui::px (62))); h.removeFromRight (ui::px (4));
    const bool wide = h.getWidth() > ui::px (230 + 136 + 190 + 150 + 30);
    instrumentsBtn.setBounds (h.removeFromRight (ui::px (136)).reduced (0, 2)); h.removeFromRight (ui::px (6));
    deviceBox.setVisible (wide);
    if (wide) { deviceBox.setBounds (h.removeFromRight (ui::px (190)).reduced (0, 2)); h.removeFromRight (ui::px (6)); }
    modelBox.setBounds (h.removeFromRight (ui::px (130)).reduced (0, 2)); h.removeFromRight (ui::px (10));
    statusArea = h;                                                // the source name

    const int sideW = juce::jlimit (ui::px (280), ui::px (360), r.getWidth() / 4);
    sidebar->setBounds (r.removeFromLeft (sideW));
    r.removeFromLeft (ui::px (10));
    roll->setBounds (r);
    card->setBounds (r);

    auto f = footer;
    static_cast<juce::TextButton*> (dragMidi.get())->setBounds (f.removeFromRight (ui::px (92)).reduced (0, 3)); f.removeFromRight (ui::px (6));
    saveBtn.setBounds (f.removeFromRight (ui::px (100)).reduced (0, 3)); f.removeFromRight (ui::px (6));
    copyBtn.setBounds (f.removeFromRight (ui::px (100)).reduced (0, 3)); f.removeFromRight (ui::px (12));
    notesToggle.setBounds (f.removeFromRight (ui::px (90))); f.removeFromRight (ui::px (4));
    seventhsToggle.setBounds (f.removeFromRight (ui::px (100))); f.removeFromRight (ui::px (10));
    footer = footer.withRight (f.getRight());
}

void KeyPage::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    const auto st = session.status();

    // source name in the header
    {
        auto r = statusArea;
        juce::String name, detail;
        if (recorder.isRecording()) { name = "Recording the input"; detail = mmss (recorder.seconds()); }
        else if (st.sourceName.isNotEmpty()) { name = st.sourceName; detail = st.seconds > 0 ? mmss (st.seconds) : juce::String(); }
        else { name = "No audio yet"; detail = "drop a song anywhere on this page"; }
        g.setColour (kText); g.setFont (juce::FontOptions (ui::fs (14.5f), juce::Font::bold));
        const int nw = juce::jmin (r.getWidth() * 2 / 3, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 4);
        g.drawText (name, r.removeFromLeft (nw), juce::Justification::centredLeft, true);
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (12.5f)));
        g.drawText ("   " + detail, r, juce::Justification::centredLeft, true);
    }

    // player row: position / length, and the sensitivity label
    {
        g.setColour (player.hasAudio() ? kText : kMuted); g.setFont (juce::FontOptions (ui::fs (13.5f)));
        const double pos = player.position(), len = player.hasAudio() ? player.length() : st.seconds;
        g.drawText (mmss (pos) + " / " + mmss (len), timeArea, juce::Justification::centredLeft);
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (12.5f)));
        g.drawText ("Sensitivity", sensLabel, juce::Justification::centredRight);
    }

    // footer: what the transcription is doing, or what was found
    {
        auto r = footer.withTrimmedRight (ui::px (8));
        juce::String txt;
        juce::Colour c = kMuted;
        switch (st.state)
        {
            case TranscriptionSession::State::Preparing: txt = "Reading the audio...  " + juce::String (juce::roundToInt (st.progress * 100)) + "%"; break;
            case TranscriptionSession::State::Loading:   txt = "Loading the " + juce::String (ModelStore::entry (s.keyModel).label).toLowerCase() + " model...  " + juce::String (juce::roundToInt (st.progress * 100)) + "%"; break;
            case TranscriptionSession::State::Running:
                txt = "Transcribing...  " + juce::String (juce::roundToInt (st.progress * 100)) + "%   " + juce::String (st.noteCount) + " notes"
                    + (st.speed > 0 ? "   " + juce::String (st.speed, 1) + "x real time" : juce::String());
                break;
            case TranscriptionSession::State::Done:
                txt = (notes.size() == rawNotes.size() ? juce::String (st.noteCount) + " notes, "
                                                        : juce::String ((int) notes.size()) + " of " + juce::String ((int) rawNotes.size()) + " notes (sensitivity), ")
                    + juce::String ((int) std::count_if (chordList.begin(), chordList.end(), [] (auto& ch) { return ch.root >= 0; }))
                    + " chords, " + juce::String ((int) melodyNotes.size()) + " melody notes"
                    + (key.valid ? "   Key " + juce::String (key.name()) + " (from " + keySource + ")" : juce::String())
                    + (st.deviceName.isNotEmpty() ? "   on " + st.deviceName : juce::String())
                    + (st.speed > 0 ? ", " + juce::String (st.speed, 1) + "x real time" : juce::String());
                c = kText.withAlpha (0.75f);
                break;
            case TranscriptionSession::State::Cancelled: txt = st.message; break;
            case TranscriptionSession::State::Failed:    txt = st.message; c = kRed; break;
            case TranscriptionSession::State::Ready:
                txt = ModelStore::isInstalled (s.keyModel) ? "Ready. Press Transcribe to find the chords and melody" : "Download a model to find the chords and melody";
                break;
            case TranscriptionSession::State::Empty:
                if (recorder.isRecording()) txt = "Recording. Press Stop to transcribe what was played";
                break;
        }
        if (playError.isNotEmpty()) { txt = playError; c = kRed; }
        if (st.busy())
        {
            bar (g, r.removeFromLeft (ui::px (140)).withSizeKeepingCentre (ui::px (140), ui::px (6)), st.progress, kAccent);
            r.removeFromLeft (ui::px (10));
        }
        g.setColour (c); g.setFont (juce::FontOptions (ui::fs (12.5f)));
        g.drawFittedText (txt, r, juce::Justification::centredLeft, 1, 0.85f);
    }

}
