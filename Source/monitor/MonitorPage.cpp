#include "MonitorPage.h"
#include "../UiScale.h"

using monitor::MonitorModel;

namespace
{
    const juce::Colour kBg (0xff0b0c0e), kPanel (0xff15171b), kPanelLine (0xff2a2d33), kHeaderText (0xffe6e8eb),
                       kDim (0xff8a8f98), kFaint (0xff5a5f68), kGrid (0xff24272d), kButton (0xff3a3c41), kOn (0xff4c8dff);
    const juce::Colour kGood (0xff4cd07d), kWarn (0xffffd23f), kBad (0xffff4d6a), kQuiet (0xffbfc5cf), kTeal (0xff3ec7c7);

    // loudness targets: the common streaming ones, then Insight 2's broadcast list (public standards)
    struct Target { const char* name; double lufs, tol, tp; bool programGate; };
    const Target kTargets[] = {
        { "Streaming (Spotify, YouTube, Tidal)", -14, 1.0, -1.0, true },
        { "Apple Music", -16, 1.0, -1.0, true },
        { "Podcast / AES TD1006 (OTT)", -16, 2.0, -1.0, true },
        { "EBU R128 (Europe)", -23, 0.5, -1.0, true },
        { "ATSC A/85 (USA, Canada)", -24, 2.0, -2.0, true },
        { "Loud master (club, CD)", -9, 1.0, -0.3, true },
        { "EBU R128 DPP (UK)", -23, 0.5, -3.0, true },
        { "EBU R128 (South Africa)", -23, 1.0, -2.0, true },
        { "AGCOM 219/09/CSP (Italy)", -24, 0.5, -2.0, true },
        { "ARIB TR-B32 (Japan)", -24, 2.0, -1.0, true },
        { "OP-59 (Australia, New Zealand)", -24, 1.0, -2.0, true },
        { "Portaria 354 (Brazil)", -23, 0.5, -2.0, true },
        { "BS.1770-1 (ungated)", -24, 2.0, -2.0, false },
        { "BS.1770-2/3/4", -24, 2.0, -2.0, true },
        { "Netflix (program gate; Netflix asks for dialogue gating)", -27, 0.5, -2.0, true } };
    constexpr int kNumTargets = (int) std::size (kTargets), kCustomTarget = kNumTargets, kNoTarget = kNumTargets + 1;
    juce::String targetName (int i)
    {
        return i == kCustomTarget ? juce::String ("Custom") : (i >= kNoTarget || i < 0 ? juce::String ("No target") : juce::String (kTargets[i].name));
    }

    // the loudness bars' scale: 0 dB linear, 1 dB non-linear, 2 BS.1771, 3 EBU +9, 4 EBU +18
    struct LoudScale { double lo, hi; bool nonLinear; double relLo, relHi; };
    // the Targets panel (Insight's): loudness target with its tolerance, peak target, LRA range, gate.
    // Any change makes the preset "Custom".
    struct TargetsPanel : public juce::Component
    {
        TargetsPanel (Settings& st, std::function<void()> ch) : s (st), changed (std::move (ch))
        {
            auto setup = [this] (juce::Slider& sl, double lo, double hi, double step, double v, const juce::String& suffix)
            {
                sl.setSliderStyle (juce::Slider::LinearHorizontal);
                sl.setTextBoxStyle (juce::Slider::TextBoxRight, false, 78, 22);
                sl.setRange (lo, hi, step); sl.setValue (v, juce::dontSendNotification); sl.setTextValueSuffix (suffix);
                sl.onValueChange = [this] { edited(); };
                addAndMakeVisible (sl);
            };
            setup (loud, -40, -5, 0.5, s.monitorTargetLufs, " LUFS");
            setup (tol, 0, 5, 0.5, s.monitorTolerance, " LU");
            setup (peak, -12, 0, 0.1, s.monitorPeakTarget, " dBTP");
            lra.setSliderStyle (juce::Slider::TwoValueHorizontal);
            lra.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            lra.setRange (0, 30, 1);
            lra.setMinAndMaxValues (s.monitorLraLo, s.monitorLraHi, juce::dontSendNotification);
            lra.onValueChange = [this] { edited(); };
            addAndMakeVisible (lra);
            for (int i = 0; i < 2; ++i)
            {
                auto& b = gate[(size_t) i];
                b.setButtonText (i == 0 ? "Off (BS.1770-1)" : "Program (BS.1770-2/3/4)");
                b.setClickingTogglesState (false);
                b.setConnectedEdges (i == 0 ? juce::Button::ConnectedOnRight : juce::Button::ConnectedOnLeft);
                b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4c8dff));
                b.onClick = [this, i] { s.monitorGate = i; edited(); };
                addAndMakeVisible (b);
            }
            update();
            setSize (440, 236);
        }
        void edited()
        {
            s.monitorTargetLufs = loud.getValue(); s.monitorTolerance = tol.getValue(); s.monitorPeakTarget = peak.getValue();
            s.monitorLraLo = lra.getMinValue(); s.monitorLraHi = lra.getMaxValue();
            s.monitorTarget = kCustomTarget;
            update();
            if (changed) changed();
        }
        void update() { for (int i = 0; i < 2; ++i) gate[(size_t) i].setToggleState (s.monitorGate == i, juce::dontSendNotification); repaint(); }
        void paint (juce::Graphics& g) override
        {
            g.fillAll (juce::Colour (0xff1b1d22));
            g.setColour (juce::Colours::white.withAlpha (0.75f)); g.setFont (juce::FontOptions (13.0f));
            for (auto& [y, t] : rows) g.drawText (t, 12, y, 110, 24, juce::Justification::centredLeft);
            g.setColour (juce::Colours::white);
            g.drawText (juce::String (s.monitorLraLo, 0) + " - " + juce::String (s.monitorLraHi, 0) + " LU", getWidth() - 90, rows[3].first, 80, 24, juce::Justification::centredRight);
            g.setColour (juce::Colours::white.withAlpha (0.45f)); g.setFont (juce::FontOptions (11.5f));
            g.drawText ("Any change makes the preset Custom", 12, getHeight() - 22, getWidth() - 24, 18, juce::Justification::centredLeft);
        }
        void resized() override
        {
            int y = 10; rows.clear();
            auto row = [&] (const char* name, juce::Component& c, int rightPad = 0) { rows.push_back ({ y, name }); c.setBounds (120, y, getWidth() - 132 - rightPad, 24); y += 32; };
            row ("Loudness target", loud); row ("Tolerance", tol); row ("Peak target", peak); row ("LRA range", lra, 92);
            rows.push_back ({ y, "Gate" });
            gate[0].setBounds (120, y, (getWidth() - 132) / 2, 26); gate[1].setBounds (120 + (getWidth() - 132) / 2, y, (getWidth() - 132) / 2, 26);
        }
        Settings& s;
        std::function<void()> changed;
        juce::Slider loud, tol, peak, lra;
        std::array<juce::TextButton, 2> gate;
        std::vector<std::pair<int, const char*>> rows;
    };

    const LoudScale kLoudScales[] = { { -60, 0, false, 0, 0 }, { -60, 0, true, 0, 0 }, { -45, -14, false, -21, 9 }, { -41, -14, false, -18, 9 }, { -59, -5, false, -36, 18 } };
    const char* kModuleNames[MonitorPage::NumModules] = { "Loudness", "Dynamics", "Levels", "Sound Field", "Phase", "History" };
    const float kModuleWeight[MonitorPage::NumModules] = { 0.62f, 1.0f, 1.0f, 1.35f, 1.0f, 0.9f };

    juce::String num (double v, int dec = 1) { return v <= -199 || ! std::isfinite (v) ? juce::String ("-inf") : (v > 0 && dec > 0 ? "+" : "") + juce::String (v, dec); }
    juce::String plain (double v, int dec = 1) { return v <= -199 || ! std::isfinite (v) ? juce::String ("-inf") : juce::String (v, dec); }

    // PSR zones: how squashed the loud parts are
    struct Zone { double from; const char* name; juce::Colour c; };
    const Zone kZones[] = { { 0, "Crushed", juce::Colour (0xffff4d4f) }, { 6, "Dense", juce::Colour (0xffff9f40) },
                            { 8, "Punchy", juce::Colour (0xff4cd07d) }, { 12, "Dynamic", juce::Colour (0xff3ec7c7) },
                            { 16, "Open", juce::Colour (0xff4c8dff) } };
    const Zone& zoneOf (double psr) { int z = 0; for (int i = 0; i < 5; ++i) if (psr >= kZones[i].from) z = i; return kZones[z]; }

    juce::Colour corrColour (double c) { return c < 0 ? kBad : (c < 0.3 ? kWarn : kGood); }

    // text size of the module being painted: 15 % above the app's text size by default, times the
    // module's own Text size (its gear menu). Set by Module::paint.
    float gTextMul = 1.15f;
    constexpr float kTextSizes[] = { 0.85f, 1.0f, 1.15f, 1.3f, 1.5f, 1.75f, 2.0f };

    void text (juce::Graphics& g, const juce::String& t, juce::Rectangle<float> r, float size, juce::Colour c,
               juce::Justification j = juce::Justification::centred, bool bold = false)
    {
        g.setColour (c);
        // labels grow freely; big readouts (already sized to their panel) stay inside their own box
        size = juce::jmin (size * gTextMul, juce::jmax (size, r.getHeight() * (size > 18.0f ? 1.0f : 1.6f)));
        g.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
        g.drawText (t, r, j, false);
    }

    double historySeconds (int h) { return h == 0 ? 30.0 : (h == 1 ? 60.0 : 300.0); }
}

//==============================================================================
// A module: a panel with a header (gear | title | maximise, hide) and a body
struct MonitorPage::Module : public juce::Component
{
    Module (MonitorPage& p, int i) : page (p), id (i) {}
    MonitorPage& page;
    const int id;
    juce::Rectangle<int> header, body, gearR, maxR, closeR;
    bool dragging = false;

    virtual void paintBody (juce::Graphics&) = 0;
    virtual void layoutBody() {}
    virtual void addMenuItems (juce::PopupMenu&) {}
    virtual void advance (double /*dt*/) {}
    Settings& st() { return page.settings(); }
    const MonitorModel& m() { return page.model(); }

    float textScale() { return 1.15f * page.textScaleFor (id); }

    void paint (juce::Graphics& g) override
    {
        gTextMul = textScale();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (kPanel); g.fillRoundedRectangle (r, 6.0f);
        g.setColour (page.dragTarget == id ? kOn : kPanelLine); g.drawRoundedRectangle (r, 6.0f, page.dragTarget == id ? 2.0f : 1.0f);
        text (g, kModuleNames[id], header.toFloat(), ui::fs (14.5f), kHeaderText);
        // gear
        {
            auto c = gearR.toFloat().getCentre(); const float R = gearR.getHeight() * 0.32f;
            juce::Path p;
            for (int k = 0; k < 8; ++k)            // eight teeth: add one at the top, turn the lot by 45 degrees
            {
                p.addRectangle (-R * 0.2f, -R * 1.25f, R * 0.4f, R * 0.5f);
                p.applyTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi / 8));
            }
            p.addEllipse (-R * 0.85f, -R * 0.85f, R * 1.7f, R * 1.7f);
            p.applyTransform (juce::AffineTransform::translation (c));
            g.setColour (kDim); g.fillPath (p);
            g.setColour (kPanel); g.fillEllipse (c.x - R * 0.38f, c.y - R * 0.38f, R * 0.76f, R * 0.76f);
        }
        // maximise / restore: two corner arrows
        {
            auto b = maxR.toFloat().reduced (maxR.getHeight() * 0.28f);
            g.setColour (kDim);
            const bool maxed = page.isMaxed (id);
            juce::Path p;
            auto tri = [&] (juce::Point<float> corner, float dx, float dy)
            { const float s = b.getWidth() * 0.5f; p.addTriangle (corner, corner.translated (dx * s, 0), corner.translated (0, dy * s)); };
            if (! maxed) { tri (b.getTopRight(), -1, 1); tri (b.getBottomLeft(), 1, -1); }
            else         { tri (b.getCentre().translated (2, -2), 1, -1); tri (b.getCentre().translated (-2, 2), -1, 1); }
            g.fillPath (p);
            g.drawLine ({ b.getBottomLeft(), b.getTopRight() }, 1.4f);
        }
        // hide
        {
            auto b = closeR.toFloat().reduced (closeR.getHeight() * 0.33f);
            g.setColour (kFaint);
            g.drawLine ({ b.getTopLeft(), b.getBottomRight() }, 1.4f); g.drawLine ({ b.getBottomLeft(), b.getTopRight() }, 1.4f);
        }
        g.setColour (kPanelLine); g.drawHorizontalLine (header.getBottom(), 8.0f, (float) getWidth() - 8);
        g.saveState();
        g.reduceClipRegion (body);
        paintBody (g);
        g.restoreState();
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header = r.removeFromTop (juce::roundToInt (ui::px (28) * juce::jmax (1.0f, textScale() * 0.9f)));
        auto h = header.reduced (6, 3);
        gearR = h.removeFromLeft (h.getHeight());
        closeR = h.removeFromRight (h.getHeight());
        maxR = h.removeFromRight (h.getHeight());
        body = r.reduced (ui::px (10), ui::px (8));
        layoutBody();
    }

    void showMenu()
    {
        juce::PopupMenu menu;
        addMenuItems (menu);
        if (menu.getNumItems() > 0) menu.addSeparator();
        juce::PopupMenu sizes;
        for (float k : kTextSizes)
            sizes.addItem (juce::String (juce::roundToInt (k * 100)) + " %" + (k == 1.0f ? "  (default)" : ""), true, std::abs (page.textScaleFor (id) - k) < 0.01f,
                           [this, k] { page.setTextScale (id, k); });
        menu.addSubMenu ("Text size", sizes);
        menu.addItem ("Full width", ! page.isMaxed (id), page.isWide (id), [this] { page.setWide (id, ! page.isWide (id)); });
        menu.addItem (page.isMaxed (id) ? "Restore" : "Maximise", [this] { page.toggleMax (id); });
        menu.addItem ("Hide", [this] { page.setShown (id, false); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (gearR)));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        if (e.mods.isPopupMenu() || gearR.contains (e.getPosition())) { showMenu(); return; }
        if (maxR.contains (e.getPosition()))   { page.toggleMax (id); return; }
        if (closeR.contains (e.getPosition())) { page.setShown (id, false); return; }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! header.contains (e.getMouseDownPosition()) || page.isMaxed (id)) return;
        if (e.getDistanceFromDragStart() < 6 && ! dragging) return;
        dragging = true;
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        page.dragModule (id, e.getEventRelativeTo (&page).getPosition());
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (dragging) { page.dropModule (id, e.getEventRelativeTo (&page).getPosition()); setMouseCursor (juce::MouseCursor::NormalCursor); }
        dragging = false;
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override { if (header.contains (e.getPosition()) && ! gearR.contains (e.getPosition())) page.toggleMax (id); }
};

//==============================================================================
// Loudness, after Insight 2. Two layouts by height: compact (the readouts only) and, when there's room
// (or maximised), three vertical bars (Short Term, Integrated with the LRA bar beside it, Momentary)
// with the readouts under them. Colours: Integrated green within the target's tolerance, red above it;
// Short Term / Momentary yellow above the target; True Peak red above the peak target; LRA red outside
// the LRA target range. The Targets panel (button by the preset list) sets all of them and the gate.
struct MonitorPage::LoudnessModule : public MonitorPage::Module
{
    using Module::Module;
    juce::Rectangle<int> momLabel, targetR, targetsBtnR;

    bool hasTarget() { return st().monitorTarget != kNoTarget; }
    double target()  { return st().monitorTargetLufs; }
    juce::Colour integratedColour (double v)
    {
        if (v <= -70) return kFaint;
        if (! hasTarget()) return kHeaderText;
        const double d = v - target(), tol = st().monitorTolerance;
        return d > tol ? kBad : (d >= -tol ? kGood : kQuiet);
    }
    juce::Colour levelColour (double v)            // short-term, momentary
    {
        if (v <= -70) return kFaint;
        if (! hasTarget()) return kHeaderText;
        return v > target() + st().monitorTolerance ? kWarn : kHeaderText;
    }
    // the bar scale (absolute LUFS, or LU relative to the target)
    const LoudScale& scale() { return kLoudScales[juce::jlimit (0, 4, st().monitorLoudRange)]; }
    bool relative() { return st().monitorRelative && hasTarget(); }
    double lo() { const auto& c = scale(); return relative() && c.relHi > c.relLo ? target() + c.relLo : c.lo; }
    double hi() { const auto& c = scale(); return relative() && c.relHi > c.relLo ? target() + c.relHi : c.hi; }
    float frac (double v)
    {
        const double l = lo(), h = hi();
        const double f = juce::jlimit (0.0, 1.0, (v - l) / (h - l));
        return (float) (scale().nonLinear ? std::pow (f, 1.6) : f);
    }

    void paintBody (juce::Graphics& g) override
    {
        if (body.getHeight() >= 240 * gTextMul / 1.15f) paintBars (g); else paintCompact (g);
    }

    // the readout row: LRA | True Peak | [Targets] preset
    void paintFooter (juce::Graphics& g, juce::Rectangle<float> row, float f)
    {
        const auto& L = m().loudness();
        const float w = row.getWidth() / 3;
        auto pair = [&] (juce::Rectangle<float> b, const juce::String& name, const juce::String& v, const juce::String& unit, juce::Colour c)
        {
            auto half = b.removeFromLeft (b.getWidth() * 0.45f);
            text (g, name, half, f * 0.8f, kDim, juce::Justification::centredRight);
            text (g, "  " + v, b.removeFromLeft (f * 3.6f * gTextMul), f * 1.15f, c, juce::Justification::centredLeft, true);
            text (g, unit, b, f * 0.75f, kFaint, juce::Justification::centredLeft);
        };
        const double lra = L.getLoudnessRange();
        const bool lraOut = hasTarget() && (lra < st().monitorLraLo || lra > st().monitorLraHi) && L.blocksSeen() > 30;
        pair (row.removeFromLeft (w), "LRA", juce::String (lra, 1), "LU", lraOut ? kBad : kHeaderText);
        const double tp = L.getTruePeak();
        pair (row.removeFromLeft (w), "True Peak", num (tp, 1), "dBTP", hasTarget() && tp > st().monitorPeakTarget ? kBad : kHeaderText);
        auto t = row.reduced (4, juce::jmax (1.0f, (row.getHeight() - f * 1.9f * gTextMul) * 0.5f)).toNearestInt();
        targetsBtnR = t.removeFromLeft (t.getHeight());
        t.removeFromLeft (3);
        targetR = t;
        g.setColour (kButton.withAlpha (0.6f));
        g.fillRoundedRectangle (targetsBtnR.toFloat(), 4); g.fillRoundedRectangle (targetR.toFloat(), 4);
        {   // the Targets button: three sliders
            auto b = targetsBtnR.toFloat().reduced (targetsBtnR.getHeight() * 0.26f);
            g.setColour (kHeaderText);
            for (int k = 0; k < 3; ++k)
            {
                const float x = b.getX() + b.getWidth() * (0.15f + 0.35f * k);
                g.drawLine (x, b.getY(), x, b.getBottom(), 1.2f);
                const float y = b.getY() + b.getHeight() * (k == 1 ? 0.7f : 0.3f);
                g.fillRect (x - 2.5f, y - 1.5f, 5.0f, 3.0f);
            }
        }
        const auto name = targetName (st().monitorTarget).upToFirstOccurrenceOf (" (", false, false);
        const auto tt = hasTarget() ? name + "  " + juce::String (target(), 1) + " LUFS" + (st().monitorGate == 0 ? "  (ungated)" : "")
                                    : juce::String ("No target");
        text (g, tt + juce::String::fromUTF8 ("  \xe2\x96\xbe"), targetR.toFloat(), f * 0.82f, kHeaderText);
    }

    void paintCompact (juce::Graphics& g)
    {
        const auto& L = m().loudness();
        auto r = body.toFloat();
        const float H = r.getHeight();
        const float big = juce::jlimit (18.0f, 72.0f, H * 0.46f), mid = big * 0.7f, label = juce::jlimit (10.5f, 14.0f, H * 0.13f);
        auto top = r.removeFromTop (H * 0.70f);
        const float w3 = top.getWidth() / 3;
        auto block = [&] (juce::Rectangle<float> b, const juce::String& title, double v, float size, const juce::String& unit, juce::Colour c)
        {
            text (g, title, b.removeFromTop (label * 1.5f * gTextMul), label, kDim);
            auto vr = b.withSizeKeepingCentre (b.getWidth(), size * 1.15f);
            const auto s = plain (v, 1);
            g.setFont (juce::FontOptions (juce::jmin (size * gTextMul, juce::jmax (size, vr.getHeight())))); const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), s);
            text (g, s, vr, size, c);
            text (g, unit, { vr.getCentreX() + tw * 0.5f + 3, vr.getBottom() - label * 2.2f, 80, label * 1.4f }, label * 0.9f, kFaint, juce::Justification::left);
        };
        block (top.removeFromLeft (w3), "Short Term", L.getShortTerm(), mid, "LUFS", levelColour (L.getShortTerm()));
        auto right = top.removeFromRight (w3);
        block (top, "Integrated", L.getIntegrated(), big, "LUFS", integratedColour (L.getIntegrated()));
        const double mom = st().monitorMomMax ? L.getMomentaryMax() : L.getMomentary();
        block (right, st().monitorMomMax ? "Momentary Max" : "Momentary", mom, mid, "LUFS", levelColour (mom));
        momLabel = right.removeFromTop (label * 1.5f * gTextMul).toNearestInt();
        paintFooter (g, r.reduced (0, 2), juce::jlimit (12.0f, 18.0f, H * 0.17f));
    }

    void paintBars (juce::Graphics& g)
    {
        const auto& L = m().loudness();
        auto r = body.toFloat();
        const float f = 14.0f;
        auto footer = r.removeFromBottom (f * 2.4f * gTextMul);
        g.setColour (kPanelLine); g.drawHorizontalLine ((int) footer.getY(), r.getX(), r.getRight());
        paintFooter (g, footer.reduced (0, 4), f);
        const float numH = juce::jlimit (34.0f, 70.0f, r.getHeight() * 0.16f);
        auto numbers = r.removeFromBottom (numH * gTextMul / 1.15f + f * 1.6f * gTextMul);
        const float colW = r.getWidth() / 3;
        const float barW = juce::jlimit (28.0f, 90.0f, colW * 0.24f);
        const double tgt = target();
        auto lab = [&] (double v) { const double x = relative() ? v - tgt : v; return (relative() && x > 0 ? "+" : "") + juce::String ((int) std::lround (x)); };
        // scale marks: every 3 dB near the top, then wider
        std::vector<double> marks;
        if (scale().nonLinear || st().monitorLoudRange == 0) marks = { 0, -3, -6, -10, -15, -20, -30, -40, -50 };
        else for (double v = std::ceil (hi() / 3) * 3; v >= lo(); v -= (hi() - lo() > 40 ? 6 : 3)) marks.push_back (v);

        struct Bar { const char* name; double v; juce::Colour over; double shown; const char* unit; };
        const double mom = L.getMomentary(), momMax = L.getMomentaryMax();
        const Bar bars[3] = { { "Short Term", L.getShortTerm(), kWarn, L.getShortTerm(), "LUFS" },
                              { "Integrated", L.getIntegrated(), kBad, L.getIntegrated(), "LUFS" },
                              { "Momentary", mom, kWarn, momMax, "Max\nLUFS" } };
        for (int i = 0; i < 3; ++i)
        {
            auto col = juce::Rectangle<float> (r.getX() + i * colW, r.getY(), colW, r.getHeight());
            auto area = col.withSizeKeepingCentre (barW + 50 * gTextMul, col.getHeight());
            auto bar = area.removeFromLeft (barW).withTrimmedTop (6).withTrimmedBottom (4);
            auto sc = area.withTrimmedLeft (6);
            auto yOf = [&] (double v) { return bar.getBottom() - frac (v) * bar.getHeight(); };
            // background: dark, tinted above the target
            g.setColour (juce::Colour (0xff1d2024)); g.fillRect (bar);
            if (hasTarget()) { g.setColour (bars[i].over.withAlpha (0.08f)); g.fillRect (bar.withBottom (yOf (tgt))); }
            const double v = bars[i].v;
            if (v > lo() && v > -70)
            {
                const float y = yOf (v);
                const float yt = hasTarget() ? yOf (tgt + (i == 1 ? st().monitorTolerance : 0)) : bar.getY();
                g.setColour (kGood); g.fillRect (bar.withTop (juce::jmax (y, yt)));
                if (y < yt) { g.setColour (bars[i].over); g.fillRect (bar.withTop (y).withBottom (yt)); }
            }
            // target tick and the LRA bar (on the Integrated meter)
            if (hasTarget()) { g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect (bar.getX() - 5, yOf (tgt) - 1, 4.0f, 2.0f); }
            if (i == 1 && L.getRangeLow() > -70 && L.getRangeHigh() > L.getRangeLow())
            {
                g.setColour (juce::Colours::white);
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (bar.getX() - 4, yOf (L.getRangeHigh()), bar.getX() - 1, yOf (L.getRangeLow())));
            }
            for (double mk : marks)
            {
                if (mk > hi() + 0.01 || mk < lo() - 0.01) continue;
                text (g, lab (mk), { sc.getX(), yOf (mk) - 7, sc.getWidth(), 14 }, 10.5f, kDim, juce::Justification::left);
            }
            text (g, relative() ? "LU" : "-Inf", { sc.getX(), bar.getBottom() - 7, sc.getWidth(), 14 }, 10.5f, kFaint, juce::Justification::left);

            // the readout under the bar
            auto nb = juce::Rectangle<float> (col.getX(), numbers.getY(), colW, numbers.getHeight());
            text (g, bars[i].name, nb.removeFromTop (f * 1.4f * gTextMul), 12.0f, kDim);
            const double shown = bars[i].shown;
            const auto c = i == 1 ? integratedColour (shown) : levelColour (shown);
            const auto str = relative() && shown > -70 ? num (shown - tgt, 1) : plain (shown, 1);
            g.setFont (juce::FontOptions (numH * gTextMul / 1.15f));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), str);
            text (g, str, nb, numH, c);
            const auto unit = relative() ? juce::String (i == 2 ? "Max\nLU" : "LU") : juce::String (bars[i].unit);
            g.setColour (kFaint); g.setFont (juce::FontOptions (11.0f * gTextMul));
            g.drawFittedText (unit, juce::Rectangle<float> (nb.getCentreX() + tw * 0.5f + 3, nb.getCentreY() - 2, 60, nb.getHeight() * 0.45f).toNearestInt(),
                              juce::Justification::topLeft, 2);
            if (i == 2) momLabel = nb.toNearestInt();
        }
    }

    void targetMenu()
    {
        juce::PopupMenu menu;
        for (int i = 0; i < kNumTargets; ++i)
        {
            const auto& t = kTargets[i];
            menu.addItem (juce::String (t.name) + "   " + juce::String (t.lufs, 0) + " LUFS, " + juce::String (t.tp, 1) + " dBTP", true, st().monitorTarget == i,
                          [this, i] { page.applyTargetPreset (i); });
        }
        menu.addSeparator();
        menu.addItem ("Custom (Targets panel)", true, st().monitorTarget == kCustomTarget, [this] { page.showTargetsPanel (targetsBtnR, this); });
        menu.addItem ("No target", true, st().monitorTarget == kNoTarget, [this] { page.applyTargetPreset (kNoTarget); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (targetR)));
    }

    void addMenuItems (juce::PopupMenu& menu) override
    {
        menu.addItem ("Targets...", [this] { page.showTargetsPanel (targetsBtnR, this); });
        menu.addItem ("Momentary shows its maximum (compact view)", true, st().monitorMomMax, [this] { st().monitorMomMax = ! st().monitorMomMax; page.changed(); });
        juce::PopupMenu range;
        const char* names[] = { "dB (linear)", "dB (non-linear)", "BS.1771", "EBU +9", "EBU +18" };
        for (int i = 0; i < 5; ++i) range.addItem (names[i], true, st().monitorLoudRange == i, [this, i] { st().monitorLoudRange = i; page.changed(); });
        menu.addSubMenu ("Bar range", range);
        menu.addItem ("Scale relative to the target (LU)", hasTarget(), st().monitorRelative, [this] { st().monitorRelative = ! st().monitorRelative; page.changed(); });
        menu.addItem ("Reset when playback starts (VST3)", true, st().monitorResetOnPlay, [this] { st().monitorResetOnPlay = ! st().monitorResetOnPlay; page.changed(); });
        menu.addSectionHeader ("Tall enough (or maximised): the meter bars");
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && targetsBtnR.contains (e.getPosition())) { page.showTargetsPanel (targetsBtnR, this); return; }
        if (! e.mods.isPopupMenu() && targetR.contains (e.getPosition())) { targetMenu(); return; }
        if (! e.mods.isPopupMenu() && momLabel.contains (e.getPosition())) { st().monitorMomMax = ! st().monitorMomMax; page.changed(); return; }
        Module::mouseDown (e);
    }
};

//==============================================================================
// Dynamics: PSR now, PLR for the take, a zoned PSR scale, and the PSR history
struct MonitorPage::DynamicsModule : public MonitorPage::Module
{
    using Module::Module;
    static constexpr double kScaleMax = 20.0;

    void paintBody (juce::Graphics& g) override
    {
        const auto& L = m().loudness();
        auto r = body.toFloat();
        const double psr = L.getPSR(), plr = L.getPLR();
        const bool havePsr = psr > MonitorModel::kNone, havePlr = plr > MonitorModel::kNone;

        // left: the two numbers
        auto left = r.removeFromLeft (juce::jlimit (150.0f, 380.0f, r.getWidth() * 0.26f * juce::jmax (1.0f, gTextMul / 1.15f)));
        r.removeFromLeft (ui::pxf (12));
        const float H = left.getHeight();
        const float big = juce::jlimit (18.0f, 54.0f, H * 0.30f), small = juce::jlimit (10.0f, 13.5f, H * 0.09f);
        auto readout = [&] (juce::Rectangle<float> b, const char* name, const char* what, double v, bool have)
        {
            auto head = b.removeFromTop (small * 1.6f * gTextMul);
            text (g, name, head.removeFromLeft (juce::jmax (big * 1.5f, small * 3.2f * gTextMul)), small * 1.15f, kHeaderText, juce::Justification::centredLeft, true);
            text (g, what, head, small * 0.9f, kDim, juce::Justification::centredLeft);
            auto vr = b.removeFromTop (big * 1.1f);
            const auto& z = zoneOf (have ? v : 0);
            text (g, have ? juce::String (v, 1) : juce::String ("--"), vr.removeFromLeft (big * 2.4f), big, have ? z.c : kFaint, juce::Justification::centredLeft);
            text (g, "dB", vr.removeFromLeft (small * 2.2f * gTextMul).withTrimmedTop (big * 0.35f), small, kFaint, juce::Justification::centredLeft);
            if (have) text (g, z.name, vr.withTrimmedTop (big * 0.3f), small * 1.05f, z.c, juce::Justification::centredLeft, true);
        };
        readout (left.removeFromTop (H * 0.46f), "PSR", "peak to short-term", psr, havePsr);
        readout (left.removeFromTop (H * 0.40f), "PLR", "peak to integrated", plr, havePlr);
        if (m().psrMin() > MonitorModel::kNone)
            text (g, "Loud parts: " + juce::String (m().psrMin(), 1) + juce::String::fromUTF8 ("\xe2\x80\x93") + juce::String (m().psrMax(), 1) + " dB",
                  left, small * 0.9f, kDim, juce::Justification::centredLeft);

        // right top: the zoned scale with PSR, the loud-parts range and PLR
        auto scale = r.removeFromTop (juce::jlimit (26.0f, 44.0f, r.getHeight() * 0.22f) * juce::jmax (1.0f, gTextMul / 1.15f));
        r.removeFromTop (ui::pxf (8));
        auto bar = scale.removeFromTop (scale.getHeight() * 0.55f);
        auto xOf = [&] (double v) { return bar.getX() + (float) (juce::jlimit (0.0, kScaleMax, v) / kScaleMax) * bar.getWidth(); };
        for (int i = 0; i < 5; ++i)
        {
            const double to = i < 4 ? kZones[i + 1].from : kScaleMax;
            auto zr = juce::Rectangle<float>::leftTopRightBottom (xOf (kZones[i].from), bar.getY(), xOf (to), bar.getBottom());
            g.setColour (kZones[i].c.withAlpha (0.16f)); g.fillRect (zr);
            if (havePsr && psr > kZones[i].from)
            {
                g.setColour (kZones[i].c.withAlpha (0.85f));
                g.fillRect (zr.withRight (juce::jmin (zr.getRight(), xOf (psr))));
            }
        }
        if (m().psrMin() > MonitorModel::kNone)
        {
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            const float y = bar.getBottom() + 3;
            g.drawLine (xOf (m().psrMin()), y, xOf (m().psrMax()), y, 2.0f);
        }
        if (havePlr)
        {
            const float x = xOf (plr);
            g.setColour (juce::Colours::white);
            g.drawLine (x, bar.getY() - 3, x, bar.getBottom() + 3, 2.0f);
            juce::Path tri; tri.addTriangle (x - 5, bar.getY() - 8, x + 5, bar.getY() - 8, x, bar.getY() - 2);
            g.fillPath (tri);
        }
        for (int v = 0; v <= 20; v += 2)
        {
            const float x = juce::jlimit (bar.getX() + 12, bar.getRight() - 12, xOf (v));
            text (g, juce::String (v), { x - 12, scale.getY() + 3, 24, scale.getHeight() - 3 }, ui::fs (10.5f), v == 6 || v == 8 || v == 12 || v == 16 ? kDim : kFaint);
        }

        // right bottom: history (newest on the right), PSR filled in zone colours, short-term loudness as a line
        auto hist = r;
        g.setColour (kBg.withAlpha (0.5f)); g.fillRoundedRectangle (hist, 4);
        const double secs = historySeconds (st().monitorHistory);
        const int entries = juce::jmin (m().historySize(), (int) (secs * 10));
        auto yOf = [&] (double v) { return hist.getBottom() - (float) (juce::jlimit (0.0, kScaleMax, v) / kScaleMax) * hist.getHeight(); };
        g.setColour (kGrid);
        for (double v : { 6.0, 8.0, 12.0, 16.0 }) g.drawHorizontalLine ((int) yOf (v), hist.getX(), hist.getRight());
        for (double v : { 6.0, 12.0 }) text (g, juce::String ((int) v), { hist.getX() + 2, yOf (v) - 13, 20, 12 }, ui::fs (10), kFaint, juce::Justification::left);
        const float dx = hist.getWidth() / (float) (secs * 10);
        for (int k = 0; k < entries; ++k)
        {
            const auto& e = m().history (k);
            if (e.psr < 0) continue;
            const float x = hist.getRight() - (k + 1) * dx;
            g.setColour (zoneOf (e.psr).c.withAlpha (0.75f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x, yOf (e.psr), x + dx + 0.6f, hist.getBottom()));
        }
        if (st().monitorDynLoud && entries > 1)
        {
            // short-term loudness on its own scale: -40 .. 0 LUFS
            auto yL = [&] (double v) { return hist.getBottom() - (float) (juce::jlimit (0.0, 1.0, (v + 40.0) / 40.0)) * hist.getHeight(); };
            juce::Path p; bool started = false;
            for (int k = 0; k < entries; ++k)
            {
                const auto& e = m().history (k);
                const float x = hist.getRight() - (k + 0.5f) * dx;
                if (e.shortTerm < -60) { started = false; continue; }
                if (! started) { p.startNewSubPath (x, yL (e.shortTerm)); started = true; } else p.lineTo (x, yL (e.shortTerm));
            }
            g.setColour (juce::Colours::white.withAlpha (0.7f)); g.strokePath (p, juce::PathStrokeType (1.3f));
            text (g, "short-term LUFS", { hist.getRight() - 220, hist.getY() + 2, 216, 13 }, ui::fs (10), kDim, juce::Justification::right);
        }
        text (g, juce::String (juce::roundToInt (secs < 60 ? secs : secs / 60)) + (secs < 60 ? " s" : " min"),
              { hist.getX() + 4, hist.getY() + 2, 120, 13 }, ui::fs (10), kFaint, juce::Justification::left);
        text (g, "PSR dB", { hist.getX() + 4, hist.getBottom() - 15, 120, 13 }, ui::fs (10), kFaint, juce::Justification::left);
    }

    void addMenuItems (juce::PopupMenu& menu) override
    {
        juce::PopupMenu h;
        const char* names[] = { "30 seconds", "1 minute", "5 minutes" };
        for (int i = 0; i < 3; ++i) h.addItem (names[i], true, st().monitorHistory == i, [this, i] { st().monitorHistory = i; page.changed(); });
        menu.addSubMenu ("History length", h);
        menu.addItem ("Short-term loudness line", true, st().monitorDynLoud, [this] { st().monitorDynLoud = ! st().monitorDynLoud; page.changed(); });
        menu.addSeparator();
        menu.addSectionHeader ("PSR: true peak (last 3 s) - short-term loudness");
        menu.addSectionHeader ("PLR: true peak (since reset) - integrated loudness");
        menu.addSectionHeader ("Under 6 crushed, 8-12 punchy, over 16 open");
    }
};

//==============================================================================
// Levels: peak and RMS bars for L R (and M S)
struct MonitorPage::LevelsModule : public MonitorPage::Module
{
    using Module::Module;
    std::array<juce::Rectangle<int>, 4> numbersR;

    double lo() { return st().monitorRange == 1 ? -40.0 : (st().monitorRange == 2 ? -90.0 : -60.0); }
    float pos (double db) { const double l = lo(); return (float) std::pow (juce::jlimit (0.0, 1.0, (db - l) / -l), l < -60 ? 2.0 : 1.6); }

    void paintBody (juce::Graphics& g) override
    {
        const int n = st().monitorMS ? 4 : 2;
        const char* names[] = { "L", "R", "M", "S" };
        auto r = body.toFloat();
        const float numbersH = juce::jlimit (26.0f, 36.0f, r.getHeight() * 0.18f) * gTextMul;
        auto numbers = r.removeFromTop (numbersH);
        auto labels = r.removeFromBottom (ui::pxf (18) * gTextMul);
        const float scaleW = ui::pxf (34) * gTextMul;
        auto meters = r.reduced (scaleW, 0);
        const float gap = juce::jmin (meters.getWidth() * 0.08f, ui::pxf (40));
        const float bw = juce::jmin ((meters.getWidth() - gap * (n - 1)) / n, ui::pxf (140));
        const float total = bw * n + gap * (n - 1);
        float x0 = meters.getCentreX() - total * 0.5f;
        auto yOf = [&] (double db) { return meters.getBottom() - pos (db) * meters.getHeight(); };

        // scale on both sides
        const double marks60[] = { 0, -3, -6, -10, -20, -30, -40, -60 }, marks40[] = { 0, -3, -6, -10, -20, -30, -40 }, marks90[] = { 0, -6, -12, -20, -40, -60, -90 };
        const double* marks = st().monitorRange == 1 ? marks40 : (st().monitorRange == 2 ? marks90 : marks60);
        const int nm = st().monitorRange == 1 ? 7 : (st().monitorRange == 2 ? 7 : 8);
        float lastLabel = -1000;
        for (int i = 0; i < nm; ++i)
        {
            const float y = yOf (marks[i]);
            g.setColour (kGrid); g.drawHorizontalLine ((int) y, x0 - 6, x0 + total + 6);
            if (y - lastLabel < 13) continue;           // short meters: skip labels that would collide
            lastLabel = y;
            const auto s = marks[i] == 0 ? juce::String ("0") : juce::String ((int) marks[i]);
            text (g, s, { x0 - scaleW - 6, y - 7, scaleW - 2, 14 }, ui::fs (11), kDim, juce::Justification::right);
            text (g, s, { x0 + total + 8, y - 7, scaleW - 2, 14 }, ui::fs (11), kDim, juce::Justification::left);
        }
        text (g, "Peak", { x0 - scaleW - 6, numbers.getY(), scaleW + 2, numbersH * 0.5f }, ui::fs (11), kHeaderText, juce::Justification::right);
        text (g, "RMS",  { x0 - scaleW - 6, numbers.getCentreY(), scaleW + 2, numbersH * 0.5f }, ui::fs (11), kDim, juce::Justification::right);

        const double aes = st().monitorAes ? 3.0103 : 0.0;
        for (int c = 0; c < n; ++c)
        {
            const auto& lv = m().level (c);
            auto b = juce::Rectangle<float> (x0 + c * (bw + gap), meters.getY(), bw, meters.getHeight());
            g.setColour (kBg); g.fillRect (b);
            const double pk = MonitorModel::toDb (lv.peak), rms = MonitorModel::toDb (lv.rms) + aes, hold = MonitorModel::toDb (lv.hold);
            // peak (darker) behind RMS (lighter); the part above -1 dBFS turns red
            auto fillTo = [&] (double db, juce::Colour c1)
            {
                if (db <= lo()) return;
                const float y = yOf (db);
                g.setColour (c1); g.fillRect (b.withTop (y));
                if (db > -1) { g.setColour (kBad.withAlpha (c1.getFloatAlpha())); g.fillRect (b.withTop (y).withBottom (yOf (-1))); }
            };
            fillTo (pk, juce::Colour (0xff6d727b));
            fillTo (rms, juce::Colour (0xffb8bdc6));
            if (hold > lo()) { g.setColour (juce::Colours::white); g.fillRect (b.withTop (yOf (hold) - 1).withHeight (2)); }
            if (lv.clips > 0) { g.setColour (kBad); g.fillRect (b.withHeight (4)); }
            text (g, names[c], labels.withX (b.getX()).withWidth (bw), ui::fs (13), kDim);
            auto nr = juce::Rectangle<float> (b.getX(), numbers.getY(), bw, numbersH);
            numbersR[(size_t) c] = nr.toNearestInt();
            const double maxPk = MonitorModel::toDb (lv.maxPeak);
            text (g, plain (maxPk, 1), nr.removeFromTop (numbersH * 0.5f), ui::fs (12.5f), maxPk > -0.1 ? kBad : kHeaderText, juce::Justification::centred, true);
            text (g, plain (rms, 1), nr, ui::fs (12), kDim);
        }
        // the peak target (Loudness > Targets) as a red dashed line across the meters
        if (st().monitorTarget != kNoTarget && st().monitorPeakTarget < 0 && st().monitorPeakTarget > lo())
        {
            const float y = yOf (st().monitorPeakTarget);
            g.setColour (kBad.withAlpha (0.85f));
            for (float x = x0 - 4; x < x0 + total + 4; x += 7) g.drawLine (x, y, juce::jmin (x0 + total + 4, x + 4), y, 1.2f);
        }
    }

    void addMenuItems (juce::PopupMenu& menu) override
    {
        menu.addItem ("Show Mid and Side", true, st().monitorMS, [this] { st().monitorMS = ! st().monitorMS; page.changed(); });
        juce::PopupMenu hold, rms;
        const char* holds[] = { "0.25 s", "0.5 s", "1.5 s", "5 s", "Until reset (double-click the meters)" };
        for (int i = 0; i < 5; ++i) hold.addItem (holds[i], true, st().monitorHoldTime == i, [this, i] { st().monitorHoldTime = i; page.applyLevelOptions(); page.changed(); });
        const char* rmsN[] = { "50 ms", "300 ms (usual)", "1 s", "3 s" };
        for (int i = 0; i < 4; ++i) rms.addItem (rmsN[i], true, st().monitorRmsTime == i, [this, i] { st().monitorRmsTime = i; page.applyLevelOptions(); page.changed(); });
        menu.addSubMenu ("Peak hold time", hold);
        menu.addSubMenu ("RMS integration time", rms);
        menu.addItem ("RMS +3 dB (AES17: a sine reads its peak)", true, st().monitorAes, [this] { st().monitorAes = ! st().monitorAes; page.changed(); });
        juce::PopupMenu range;
        const char* names[] = { "-60 dB", "-40 dB", "-90 dB" };
        for (int i = 0; i < 3; ++i) range.addItem (names[i], true, st().monitorRange == i, [this, i] { st().monitorRange = i; page.changed(); });
        menu.addSubMenu ("Scale", range);
        menu.addSectionHeader ("Click the numbers to reset the maxima");
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (body.contains (e.getPosition())) { page.model().resetPeakHold(); return; }
        Module::mouseDoubleClick (e);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        for (auto& r : numbersR) if (! e.mods.isPopupMenu() && r.contains (e.getPosition())) { page.resetMeasurements(); return; }
        Module::mouseDown (e);
    }
};

//==============================================================================
// Sound Field: polar sample / polar level / Lissajous, a correlation bar, width and balance
struct MonitorPage::FieldModule : public MonitorPage::Module
{
    FieldModule (MonitorPage& p, int i) : Module (p, i)
    {
        const char* names[] = { "Polar Sample", "Polar Level", "Lissajous" };
        for (int k = 0; k < 3; ++k)
        {
            auto& b = modeBtns[(size_t) k];
            b.setButtonText (names[k]);
            b.setClickingTogglesState (false);
            b.setColour (juce::TextButton::buttonColourId, kButton);
            b.setColour (juce::TextButton::buttonOnColourId, kOn);
            b.setConnectedEdges ((k > 0 ? juce::Button::ConnectedOnLeft : 0) | (k < 2 ? juce::Button::ConnectedOnRight : 0));
            b.onClick = [this, k] { st().monitorField = k; page.changed(); updateButtons(); clearTrail(); };
            addAndMakeVisible (b);
        }
        updateButtons();
    }
    std::array<juce::TextButton, 3> modeBtns;
    juce::Rectangle<int> plot, corrBar, balanceBar;
    std::vector<float> inten, clipInt;
    juce::Image img;
    long long lastPoint = 0;
    double autoGain = 1.0;
    std::array<juce::PixelARGB, 256> lut {};
    struct Tap { int dx, dy; float w; };
    std::vector<Tap> kernel;
    int kernelRadius = 0, kernelFor = -1;
    void rebuildKernel()
    {
        // radius from the view's size: about 1 px per 250 px of view, at least a 2 x 2 dot
        const float size = (float) juce::jmin (plot.getWidth(), plot.getHeight());
        const float r = juce::jmax (0.75f, size / 250.0f);
        const float gain = st().monitorPoints == 0 ? 0.6f : (st().monitorPoints == 2 ? 1.7f : 1.0f);
        const int key = juce::roundToInt (r * 100) * 10 + st().monitorPoints;
        if (key == kernelFor) return;
        kernelFor = key;
        kernelRadius = (int) std::ceil (r);
        kernel.clear();
        for (int dy = -kernelRadius; dy <= kernelRadius; ++dy)
            for (int dx = -kernelRadius; dx <= kernelRadius; ++dx)
            {
                const float d = std::sqrt ((float) (dx * dx + dy * dy));
                const float w = juce::jlimit (0.0f, 1.0f, r + 0.5f - d);          // an anti-aliased disc
                if (w > 0) kernel.push_back ({ dx, dy, w * 0.5f * gain });
            }
    }

    void updateButtons() { for (int k = 0; k < 3; ++k) modeBtns[(size_t) k].setToggleState (st().monitorField == k, juce::dontSendNotification); }
    void clearTrail() { std::fill (inten.begin(), inten.end(), 0.0f); std::fill (clipInt.begin(), clipInt.end(), 0.0f); }

    void layoutBody() override
    {
        auto r = body;
        auto btns = r.removeFromBottom (ui::px (24));
        const int bw = juce::jmin (ui::px (110), btns.getWidth() / 3);
        auto row = btns.withSizeKeepingCentre (bw * 3, btns.getHeight());
        for (auto& b : modeBtns) b.setBounds (row.removeFromLeft (bw));
        r.removeFromBottom (ui::px (4));
        balanceBar = r.removeFromBottom (ui::px (14));
        r.removeFromBottom (ui::px (4));
        corrBar = r.removeFromRight (ui::px (34));
        r.removeFromRight (ui::px (6));
        plot = r;
        if (plot.getWidth() > 0 && plot.getHeight() > 0)
        {
            img = juce::Image (juce::Image::ARGB, plot.getWidth(), plot.getHeight(), true, juce::SoftwareImageType());   // written a row at a time
            inten.assign ((size_t) (plot.getWidth() * plot.getHeight()), 0.0f);
        }
        lastPoint = m().pointsWritten();
    }

    // the half disc (polar) or the square (Lissajous) the points live in
    juce::Rectangle<float> polarArea() const
    {
        auto p = plot.toFloat().reduced (4, 14);
        const float R = juce::jmin (p.getWidth() * 0.5f, p.getHeight() - 4);
        return { p.getCentreX() - R, p.getBottom() - R, 2 * R, R };          // origin at the bottom centre
    }
    juce::Rectangle<float> lissArea() const
    {
        auto p = plot.toFloat().reduced (6, 14);
        const float s = juce::jmin (p.getWidth(), p.getHeight());
        return p.withSizeKeepingCentre (s, s);
    }
    // radius from a magnitude relative to the reference (the recent loudest): 0 dB at the rim, -30 dB at the centre
    static float dbRadius (double mag, double ref) { return (float) juce::jlimit (0.0, 1.0, (20.0 * std::log10 (std::max (mag, 1e-9) / std::max (ref, 1e-6)) + 30.0) / 30.0); }
    double refMag = 0.1;

    void advance (double dt) override
    {
        if (img.isNull()) return;
        const int W = img.getWidth(), H = img.getHeight();
        const float fade = (float) std::exp (-dt / 0.14);
        for (auto& v : inten) v *= fade;
        if (clipInt.size() != inten.size()) clipInt.assign (inten.size(), 0.0f);
        for (auto& v : clipInt) v *= fade;

        const long long now = m().pointsWritten();
        if (lastPoint > now) lastPoint = 0;                       // the model was reset or reconfigured
        long long from = juce::jmax (lastPoint, now - (long long) juce::jmin (MonitorModel::kPoints, 6000));
        lastPoint = now;
        const auto& PL = m().pointsL(); const auto& PR = m().pointsR();
        const int mode = st().monitorField;
        {   // the reference for the polar views: the loudest point lately (fast up, slow down)
            double mx = 0;
            for (long long i = from; i < now; ++i) { const auto k = (size_t) (i & (MonitorModel::kPoints - 1)); mx = juce::jmax (mx, (double) PL[k] * PL[k] + (double) PR[k] * PR[k]); }
            mx = std::sqrt (mx);
            refMag = mx > refMag ? mx : juce::jmax (1e-4, refMag * std::exp (-dt / 2.0));
        }

        // Lissajous: automatic gain from the recent peak (fills about 80 % of the square)
        if (mode == 2)
        {
            const double pk = juce::jmax (m().level (MonitorModel::L).peak, m().level (MonitorModel::R).peak, 1e-4);
            const double want = juce::jlimit (1.0, 40.0, 0.8 / pk);
            autoGain += (want - autoGain) * juce::jmin (1.0, dt * (want < autoGain ? 8.0 : 1.5));
        }
        // the points grow with the view (one pixel looks faint on a big display), a little brighter each
        rebuildKernel();
        const int R = kernelRadius;
        const auto pa = polarArea().translated ((float) -plot.getX(), (float) -plot.getY());
        const auto la = lissArea().translated ((float) -plot.getX(), (float) -plot.getY());
        if (mode != 1)
            for (long long i = from; i < now; ++i)
            {
                const auto k = (size_t) (i & (MonitorModel::kPoints - 1));
                const double l = PL[k], r = PR[k];
                double x = (r - l) * 0.70710678, y = (l + r) * 0.70710678;
                float px, py;
                if (mode == 0)
                {
                    if (y < 0) { x = -x; y = -y; }
                    const double mag = std::sqrt (x * x + y * y);
                    if (mag < 1e-6) continue;
                    const float rr = dbRadius (mag, refMag) * pa.getWidth() * 0.5f;
                    px = pa.getCentreX() + (float) (x / mag) * rr; py = pa.getBottom() - (float) (y / mag) * rr;
                }
                else
                {
                    const float s = la.getWidth() * 0.5f * (float) autoGain;
                    px = la.getCentreX() + (float) x * s; py = la.getCentreY() - (float) y * s;
                }
                const int ix = (int) px, iy = (int) py;
                if (ix < R || iy < R || ix + R + 1 >= W || iy + R + 1 >= H) continue;
                auto& buf = (std::abs (l) >= 0.999 || std::abs (r) >= 0.999) ? clipInt : inten;   // clipped samples: red
                for (const auto& k : kernel) { auto& v = buf[(size_t) ((iy + k.dy) * W + ix + k.dx)]; v = juce::jmin (1.0f, v + k.w); }
            }

        // intensity -> teal-white pixels through a 256-entry table of premultiplied pixels, a row at a time
        if (lut[255].getAlpha() == 0)
            for (int i = 0; i < 256; ++i)
            {
                const float v = i / 255.0f, a = std::pow (v, 0.6f);                     // lift the faint ones
                lut[(size_t) i] = juce::Colour::fromRGBA ((juce::uint8) (160 + 95 * v), 235, (juce::uint8) (220 + 35 * v), (juce::uint8) juce::roundToInt (a * 255)).getPixelARGB();
                lut[(size_t) i].premultiply();
            }
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
        for (int y = 0; y < H; ++y)
        {
            auto* row = reinterpret_cast<juce::PixelARGB*> (bd.getLinePointer (y));
            const float* src = inten.data() + (size_t) y * (size_t) W;
            const float* cl = clipInt.data() + (size_t) y * (size_t) W;
            for (int x = 0; x < W; ++x)
            {
                const float v = src[x], c = cl[x];
                if (c >= 0.02f && c >= v * 0.5f)
                {
                    const auto a = (juce::uint8) juce::jmin (255, (int) (std::pow (c, 0.6f) * 255.0f));
                    juce::PixelARGB px (a, 255, 70, 90); px.premultiply(); row[x] = px;
                }
                else row[x] = v < 0.004f ? juce::PixelARGB (0, 0, 0, 0) : lut[(size_t) juce::jmin (255, (int) (v * 255.0f))];
            }
        }
    }

    void paintBody (juce::Graphics& g) override
    {
        const int mode = st().monitorField;
        // grid
        g.setColour (kGrid);
        if (mode == 2)
        {
            auto a = lissArea();
            juce::Path d;
            d.startNewSubPath (a.getCentreX(), a.getY()); d.lineTo (a.getRight(), a.getCentreY()); d.lineTo (a.getCentreX(), a.getBottom());
            d.lineTo (a.getX(), a.getCentreY()); d.closeSubPath();
            g.strokePath (d, juce::PathStrokeType (1.0f));
            g.drawLine (a.getCentreX(), a.getY(), a.getCentreX(), a.getBottom());
            g.drawLine (a.getX(), a.getCentreY(), a.getRight(), a.getCentreY());
            g.setColour (kPanelLine);
            g.drawLine ({ a.getCentre(), { a.getX() + a.getWidth() * 0.15f, a.getY() + a.getHeight() * 0.15f } });
            g.drawLine ({ a.getCentre(), { a.getRight() - a.getWidth() * 0.15f, a.getY() + a.getHeight() * 0.15f } });
            text (g, "L", { a.getX() + a.getWidth() * 0.12f - 10, a.getY() + a.getHeight() * 0.12f - 16, 20, 14 }, ui::fs (12), kDim);
            text (g, "R", { a.getRight() - a.getWidth() * 0.12f - 10, a.getY() + a.getHeight() * 0.12f - 16, 20, 14 }, ui::fs (12), kDim);
            text (g, "M", { a.getCentreX() - 10, a.getY() - 14, 20, 13 }, ui::fs (11), kFaint);
            text (g, "x" + juce::String (autoGain, autoGain < 10 ? 1 : 0), { a.getRight() - 40, a.getBottom() - 14, 40, 13 }, ui::fs (10), kFaint, juce::Justification::right);
        }
        else
        {
            auto a = polarArea();
            const auto o = juce::Point<float> (a.getCentreX(), a.getBottom());
            const float R = a.getWidth() * 0.5f;
            g.setColour (kPanelLine.brighter (0.1f));
            juce::Path arc; arc.addCentredArc (o.x, o.y, R, R, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
            g.strokePath (arc, juce::PathStrokeType (1.2f));
            g.setColour (kGrid);
            for (float db : { -10.0f, -20.0f })
            {
                const float rr = (db + 30) / 30 * R;
                juce::Path p; p.addCentredArc (o.x, o.y, rr, rr, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true);
                g.strokePath (p, juce::PathStrokeType (0.8f));
            }
            for (float deg : { -45.0f, 45.0f })
            {
                const float a2 = juce::degreesToRadians (deg);
                g.setColour (kPanelLine.brighter (0.1f));
                g.drawLine ({ o, o + juce::Point<float> (std::sin (a2), -std::cos (a2)) * R });
            }
            g.setColour (kGrid); g.drawLine ({ o, o + juce::Point<float> (0, -R) });
            g.drawLine (o.x - R, o.y, o.x + R, o.y);
            text (g, "L", { o.x - R - 4, o.y + 1, 16, 13 }, ui::fs (12), kDim);
            text (g, "R", { o.x + R - 12, o.y + 1, 16, 13 }, ui::fs (12), kDim);

            if (mode == 1)
            {
                juce::Path shape; shape.startNewSubPath (o);
                double ref = 1e-6;
                for (int b = 0; b < MonitorModel::kPolarBins; ++b) ref = juce::jmax (ref, m().polarLevel (b));
                for (int b = 0; b < MonitorModel::kPolarBins; ++b)
                {
                    const float ang = juce::MathConstants<float>::pi * (1.0f - (float) b / (MonitorModel::kPolarBins - 1));   // left edge first
                    const float rr = dbRadius (m().polarLevel (b), ref) * R;
                    const auto p = o + juce::Point<float> (std::cos (ang), -std::sin (ang)) * rr;
                    shape.lineTo (p);
                    g.setColour (kTeal.withAlpha (0.55f)); g.drawLine ({ o, p }, 1.6f);
                }
                shape.closeSubPath();
                g.setColour (kTeal.withAlpha (0.16f)); g.fillPath (shape);
                g.setColour (kTeal.brighter (0.4f)); g.strokePath (shape, juce::PathStrokeType (1.4f));
            }
        }
        if (mode != 1 && img.isValid()) { g.setOpacity (1.0f); g.drawImageAt (img, plot.getX(), plot.getY()); }

        // width and balance
        const double bal = m().balanceDb();
        const auto balS = std::abs (bal) < 0.05 ? juce::String ("C") : (bal < 0 ? "L " : "R ") + juce::String (std::abs (bal), 1) + " dB";
        text (g, "Width " + juce::String (juce::roundToInt (m().width() * 100)) + "%", { (float) plot.getX(), (float) plot.getY(), 200, 14 }, ui::fs (11), kDim, juce::Justification::left);
        text (g, "Balance " + balS, { (float) plot.getRight() - 220, (float) plot.getY(), 220, 14 }, ui::fs (11), kDim, juce::Justification::right);

        // balance bar (Insight's, under the scope): where the level sits between L and R, +-12 dB
        {
            auto b = balanceBar.toFloat().withTrimmedRight ((float) corrBar.getWidth() + 6);
            g.setColour (kBg); g.fillRoundedRectangle (b, 3);
            text (g, "L", b.removeFromLeft (12), 9.5f, kFaint); text (g, "R", b.removeFromRight (12), 9.5f, kFaint);
            const float cx = b.getCentreX();
            g.setColour (kGrid); g.drawVerticalLine ((int) cx, b.getY(), b.getBottom());
            const float x = cx + (float) juce::jlimit (-1.0, 1.0, m().balanceDb() / 12.0) * b.getWidth() * 0.5f;
            g.setColour (kTeal);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (cx, x), b.getY() + 3, juce::jmax (cx, x) + 1, b.getBottom() - 3));
            g.fillRect (x - 1.5f, b.getY(), 3.0f, b.getHeight());
        }

        // correlation bar, Insight-style: +1 at the top, -1 at the bottom
        auto cb = corrBar.toFloat();
        text (g, "+1", cb.removeFromTop (14), ui::fs (10.5f), kDim);
        text (g, "-1", cb.removeFromBottom (14), ui::fs (10.5f), kDim);
        auto track = cb.withSizeKeepingCentre (4, cb.getHeight());
        g.setColour (kGrid); g.fillRect (track);
        const float y0 = cb.getCentreY();
        text (g, "0", { cb.getX() - 4, y0 - 7, 12, 14 }, ui::fs (10), kFaint, juce::Justification::left);
        const double c = m().correlation();
        const float yc = cb.getCentreY() - (float) c * cb.getHeight() * 0.5f;
        g.setColour (corrColour (c));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (track.getX(), juce::jmin (y0, yc), track.getRight(), juce::jmax (y0, yc)));
        g.fillRect (track.getX() - 3, yc - 1.5f, track.getWidth() + 6, 3.0f);
    }

    void addMenuItems (juce::PopupMenu& menu) override
    {
        const char* names[] = { "Polar sample", "Polar level", "Lissajous" };
        for (int k = 0; k < 3; ++k) menu.addItem (names[k], true, st().monitorField == k, [this, k] { st().monitorField = k; page.changed(); updateButtons(); clearTrail(); });
        juce::PopupMenu pts;
        const char* bright[] = { "Subtle", "Normal", "Bright" };
        for (int k = 0; k < 3; ++k) pts.addItem (bright[k], true, st().monitorPoints == k, [this, k] { st().monitorPoints = k; page.changed(); });
        menu.addSubMenu ("Point brightness", pts);
    }
};

//==============================================================================
// Phase: correlation (fast bar, slow marker, minimum), per band, balance, history
struct MonitorPage::PhaseModule : public MonitorPage::Module
{
    using Module::Module;

    void meter (juce::Graphics& g, juce::Rectangle<float> r, double v, double slow, double minV, bool active, bool ticks)
    {
        auto xOf = [&] (double c) { return r.getX() + (float) ((c + 1) * 0.5) * r.getWidth(); };
        g.setColour (kBg); g.fillRoundedRectangle (r, 3);
        g.setColour (kBad.withAlpha (0.10f)); g.fillRect (r.withRight (xOf (0)));
        g.setColour (kGrid);
        for (double t : { -0.5, 0.0, 0.5 }) g.drawVerticalLine ((int) xOf (t), r.getY(), r.getBottom());
        if (active)
        {
            const float x0 = xOf (0), x = xOf (v);
            g.setColour (corrColour (v).withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmin (x0, x), r.getY() + 2, juce::jmax (x0, x), r.getBottom() - 2));
            g.setColour (juce::Colours::white);
            if (slow > -2) g.fillRect (xOf (slow) - 1.0f, r.getY(), 2.0f, r.getHeight());
            if (minV > -2 && minV < 0.999) { g.setColour (kBad); g.fillRect (xOf (minV) - 1.0f, r.getY() - 2, 2.0f, r.getHeight() + 4); }
        }
        if (ticks)
            for (double t : { -1.0, -0.5, 0.0, 0.5, 1.0 })
                text (g, t == 0 ? juce::String ("0") : (t > 0 ? "+" : "") + juce::String (t, t == (int) t ? 0 : 1),
                      { juce::jlimit (r.getX() + 16, r.getRight() - 16, xOf (t)) - 16, r.getBottom() + 1, 32, 13 }, ui::fs (10.5f), kDim);
    }

    void paintBody (juce::Graphics& g) override
    {
        auto r = body.toFloat();
        const float H = r.getHeight();
        const float numW = juce::jlimit (70.0f, 120.0f, r.getWidth() * 0.16f) * gTextMul;
        const double c = m().correlation();
        const bool live = m().level (0).rms + m().level (1).rms > 1e-5;

        // main meter
        auto top = r.removeFromTop (juce::jlimit (30.0f, 58.0f, H * 0.26f));
        auto numR = top.removeFromLeft (numW);
        text (g, live ? num (c, 2) : juce::String ("--"), numR.removeFromTop (numR.getHeight() * 0.72f), juce::jlimit (16.0f, 30.0f, H * 0.17f), live ? corrColour (c) : kFaint, juce::Justification::centredLeft, true);
        text (g, "correlation", numR, ui::fs (10.5f), kDim, juce::Justification::centredLeft);
        meter (g, top.withTrimmedBottom (14), c, m().correlationSlow(), m().correlationMin(), live, true);
        r.removeFromTop (ui::pxf (8));

        // bands
        auto bands = r.removeFromTop (juce::jlimit (48.0f, 96.0f, H * 0.36f));
        const char* names[] = { "Low", "Mid", "High" };
        const char* ranges[] = { "< 250", "250-2.5k", "> 2.5k" };
        const float rh = bands.getHeight() / 3;
        for (int b = 0; b < 3; ++b)
        {
            auto row = bands.removeFromTop (rh).reduced (0, 2);
            const bool on = m().bandActive (b);
            const double v = m().bandCorrelation (b);
            auto lab = row.removeFromLeft (numW);
            text (g, names[b], lab.removeFromLeft (lab.getWidth() * 0.42f), ui::fs (12), on ? kHeaderText : kFaint, juce::Justification::centredLeft, true);
            text (g, ranges[b], lab, ui::fs (9.5f), kFaint, juce::Justification::centredLeft);
            auto val = row.removeFromRight (ui::pxf (44) * gTextMul);
            text (g, on ? num (v, 2) : juce::String ("--"), val, ui::fs (12), on ? corrColour (v) : kFaint, juce::Justification::centredRight);
            meter (g, row.reduced (6, 1), v, -3, -3, on, false);
        }
        r.removeFromTop (ui::pxf (6));

        // warning / balance and width line
        auto info = r.removeFromTop (ui::pxf (18) * gTextMul);
        juce::String warn;
        if (live && m().bandActive (0) && m().bandCorrelation (0) < 0) warn = "Lows out of phase: the bass will cancel in mono";
        else if (live && c < 0)                                          warn = "Negative correlation: check mono compatibility";
        if (warn.isNotEmpty()) text (g, warn, info, ui::fs (11.5f), kBad, juce::Justification::centredLeft, true);
        else
        {
            const double bal = m().balanceDb();
            text (g, "Balance " + (std::abs (bal) < 0.05 ? juce::String ("centre") : (bal < 0 ? "L " : "R ") + juce::String (std::abs (bal), 1) + " dB")
                     + "     Width " + juce::String (juce::roundToInt (m().width() * 100)) + "%"
                     + "     Min " + (m().correlationMin() < 0.999 ? num (m().correlationMin(), 2) : juce::String ("--")),
                  info, ui::fs (11), kDim, juce::Justification::centredLeft);
        }
        r.removeFromTop (ui::pxf (4));

        // history
        if (r.getHeight() < 20) return;
        g.setColour (kBg.withAlpha (0.5f)); g.fillRoundedRectangle (r, 4);
        const double secs = historySeconds (st().monitorHistory);
        const int entries = juce::jmin (m().historySize(), (int) (secs * 10));
        auto yOf = [&] (double v) { return r.getCentreY() - (float) v * r.getHeight() * 0.45f; };
        g.setColour (kGrid); g.drawHorizontalLine ((int) yOf (0), r.getX(), r.getRight());
        const float dx = r.getWidth() / (float) (secs * 10);
        juce::Path p;
        for (int k = 0; k < entries; ++k)
        {
            const auto& e = m().history (k);
            const float x = r.getRight() - (k + 0.5f) * dx;
            if (k == 0) p.startNewSubPath (x, yOf (e.correlation)); else p.lineTo (x, yOf (e.correlation));
            if (e.correlation < 0) { g.setColour (kBad.withAlpha (0.5f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x - dx * 0.5f, yOf (0), x + dx * 0.5f + 0.5f, yOf (e.correlation))); }
        }
        g.setColour (kGood.withAlpha (0.85f)); g.strokePath (p, juce::PathStrokeType (1.3f));
        text (g, "+1", { r.getX() + 3, r.getY() + 1, 20, 12 }, ui::fs (9.5f), kFaint, juce::Justification::left);
        text (g, "-1", { r.getX() + 3, r.getBottom() - 13, 20, 12 }, ui::fs (9.5f), kFaint, juce::Justification::left);
    }

    void addMenuItems (juce::PopupMenu& menu) override
    {
        juce::PopupMenu h;
        const char* names[] = { "30 seconds", "1 minute", "5 minutes" };
        for (int i = 0; i < 3; ++i) h.addItem (names[i], true, st().monitorHistory == i, [this, i] { st().monitorHistory = i; page.changed(); });
        menu.addSubMenu ("History length", h);
        menu.addSectionHeader ("+1 mono, 0 unrelated L/R, -1 inverted polarity");
        menu.addSectionHeader ("White: 3 s average. Red tick: lowest since reset");
    }
};

//==============================================================================
// History (Insight's): Momentary, Short-term and Integrated loudness over time, the target as a red dashed
// line, the Integrated trace red where it's above the target; hover for the values at that moment
struct MonitorPage::HistoryModule : public MonitorPage::Module
{
    using Module::Module;
    int hoverX = -1;
    juce::Rectangle<float> plotR;

    double seconds() { const double l[] = { 30, 60, 300, 600 }; return l[juce::jlimit (0, 3, st().monitorHistLen)]; }
    bool hasTarget() { return st().monitorTarget != kNoTarget; }

    void paintBody (juce::Graphics& g) override
    {
        auto r = body.toFloat();
        const float axisW = 34 * gTextMul;
        auto axis = r.removeFromLeft (axisW);
        auto bottom = r.removeFromBottom (14 * gTextMul);
        plotR = r;
        g.setColour (kBg.withAlpha (0.5f)); g.fillRoundedRectangle (r, 4);
        const bool rel = st().monitorRelative && hasTarget();
        const double tgt = st().monitorTargetLufs;
        const double hi = rel ? tgt + 9 : 0, lo = rel ? tgt - 27 : -54;
        auto yOf = [&] (double v) { return r.getBottom() - (float) juce::jlimit (0.0, 1.0, (v - lo) / (hi - lo)) * r.getHeight(); };
        for (double v = hi; v >= lo - 0.01; v -= (hi - lo) / 6)
        {
            g.setColour (kGrid); g.drawHorizontalLine ((int) yOf (v), r.getX(), r.getRight());
            const double shown = rel ? v - tgt : v;
            text (g, (rel && shown > 0 ? "+" : "") + juce::String ((int) std::lround (shown)), { axis.getX(), yOf (v) - 7, axisW - 4, 14 }, 10.5f, kDim, juce::Justification::right);
        }
        text (g, rel ? "LU" : "LUFS", { axis.getX(), r.getY(), axisW - 4, 12 }, 9.5f, kFaint, juce::Justification::right);
        const double secs = seconds();
        for (int k = 0; k <= 6; ++k)
        {
            const float x = r.getRight() - r.getWidth() * k / 6.0f;
            const double ago = secs * k / 6.0;
            g.setColour (kGrid.withAlpha (0.6f)); g.drawVerticalLine ((int) x, r.getY(), r.getBottom());
            const auto t = ago >= 60 ? juce::String ((int) ago / 60) + ":" + juce::String ((int) ago % 60).paddedLeft ('0', 2) : juce::String ((int) ago) + " s";
            text (g, k == 0 ? juce::String ("now") : "-" + t, { x - 30, bottom.getY(), 60, bottom.getHeight() }, 10.0f, kFaint);
        }
        if (hasTarget())
        {
            const float y = yOf (tgt);
            g.setColour (kBad.withAlpha (0.8f));
            for (float x = r.getX(); x < r.getRight(); x += 9) g.drawLine (x, y, juce::jmin (r.getRight(), x + 5), y, 1.2f);
        }
        const int entries = juce::jmin (m().historySize(), (int) (secs * 10));
        const float dx = r.getWidth() / (float) (secs * 10);
        auto trace = [&] (int which, juce::Colour c, float width)
        {
            juce::Path p, over; bool on = false, onOver = false;
            for (int k = entries - 1; k >= 0; --k)
            {
                const auto& e = m().history (k);
                const float v = which == 0 ? e.momentary : (which == 1 ? e.shortTerm : e.integrated);
                const float x = r.getRight() - (k + 0.5f) * dx;
                if (v < -100) { on = onOver = false; continue; }
                const float y = yOf (v);
                if (! on) { p.startNewSubPath (x, y); on = true; } else p.lineTo (x, y);
                if (which == 2 && hasTarget() && v > tgt + st().monitorTolerance)
                { if (! onOver) { over.startNewSubPath (x, y); onOver = true; } else over.lineTo (x, y); }
                else onOver = false;
            }
            g.setColour (c); g.strokePath (p, juce::PathStrokeType (width));
            if (which == 2) { g.setColour (kBad); g.strokePath (over, juce::PathStrokeType (width + 0.5f)); }
        };
        g.saveState(); g.reduceClipRegion (r.toNearestInt());
        if (st().monitorHistTraces & 1) trace (0, juce::Colour (0xff8a8f98).withAlpha (0.7f), 1.0f);
        if (st().monitorHistTraces & 2) trace (1, kHeaderText, 1.6f);
        if (st().monitorHistTraces & 4) trace (2, kOn, 2.2f);
        g.restoreState();
        // legend
        auto lg = juce::Rectangle<float> (r.getX() + 8, r.getY() + 4, 330 * gTextMul, 14 * gTextMul);
        auto item = [&] (juce::Colour c, const char* t, bool on)
        {
            if (! on) return;
            g.setColour (c); g.fillRect (lg.removeFromLeft (14).withSizeKeepingCentre (14, 3));
            text (g, t, lg.removeFromLeft (92 * gTextMul), 11.0f, kDim, juce::Justification::centredLeft);
        };
        item (juce::Colour (0xff8a8f98), " Momentary", st().monitorHistTraces & 1);
        item (kHeaderText, " Short-term", st().monitorHistTraces & 2);
        item (kOn, " Integrated", st().monitorHistTraces & 4);
        // hover: the values then
        if (hoverX >= r.getX() && hoverX < r.getRight())
        {
            const int k = (int) ((r.getRight() - hoverX) / dx);
            if (k >= 0 && k < entries)
            {
                const auto& e = m().history (k);
                g.setColour (juce::Colours::white.withAlpha (0.3f)); g.drawVerticalLine (hoverX, r.getY(), r.getBottom());
                auto v = [&] (float x) { return x < -100 ? juce::String ("-inf") : (rel ? num (x - tgt, 1) : juce::String (x, 1)); };
                const auto t = "-" + juce::String (k / 10.0, 1) + " s    M " + v (e.momentary) + "   S " + v (e.shortTerm) + "   I " + v (e.integrated);
                const float w = 300 * gTextMul;
                auto box = juce::Rectangle<float> (juce::jlimit (r.getX(), r.getRight() - w, (float) hoverX + 10), r.getY() + 22 * gTextMul, w, 20 * gTextMul);
                g.setColour (juce::Colour (0xe0101215)); g.fillRoundedRectangle (box, 4);
                text (g, t, box.reduced (8, 0), 11.5f, kHeaderText, juce::Justification::centredLeft);
            }
        }
    }
    void mouseMove (const juce::MouseEvent& e) override { hoverX = e.x; repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { hoverX = -1; repaint(); }

    void addMenuItems (juce::PopupMenu& menu) override
    {
        const char* traces[] = { "Momentary trace", "Short-term trace", "Integrated trace" };
        for (int i = 0; i < 3; ++i)
            menu.addItem (traces[i], true, (st().monitorHistTraces >> i) & 1, [this, i] { st().monitorHistTraces ^= 1 << i; page.changed(); });
        juce::PopupMenu len;
        const char* names[] = { "30 seconds", "1 minute", "5 minutes", "10 minutes" };
        for (int i = 0; i < 4; ++i) len.addItem (names[i], true, st().monitorHistLen == i, [this, i] { st().monitorHistLen = i; page.changed(); });
        menu.addSubMenu ("Length", len);
        menu.addItem ("Relative to the target (LU)", st().monitorTarget != kNoTarget, st().monitorRelative, [this] { st().monitorRelative = ! st().monitorRelative; page.changed(); });
    }
};

//==============================================================================
MonitorPage::MonitorPage (FifoTap& t, Settings& st, std::function<void()> settingsChanged, std::function<void()> resetLoudness,
                          monitor::MonitorModel* sharedModel)
    : tap (t), s (st), onChanged (std::move (settingsChanged)), onResetLoudness (std::move (resetLoudness)),
      engine (sharedModel != nullptr ? *sharedModel : ownModel), shared (sharedModel != nullptr)
{
    if (! shared) engine.configure (tap.sampleRate());
    tap.setActive (true);                       // always measuring: integrated loudness covers everything since reset

    modules[Loudness]   = std::make_unique<LoudnessModule> (*this, Loudness);
    modules[Dynamics]   = std::make_unique<DynamicsModule> (*this, Dynamics);
    modules[Levels]     = std::make_unique<LevelsModule> (*this, Levels);
    modules[SoundField] = std::make_unique<FieldModule> (*this, SoundField);
    modules[Phase]      = std::make_unique<PhaseModule> (*this, Phase);
    modules[History]    = std::make_unique<HistoryModule> (*this, History);
    if (s.monitorTarget >= 0 && s.monitorTarget < kNumTargets)        // keep the values in step with the preset
    {
        const auto& t = kTargets[s.monitorTarget];
        s.monitorTargetLufs = t.lufs; s.monitorTolerance = t.tol; s.monitorPeakTarget = t.tp; s.monitorGate = t.programGate ? 1 : 0;
    }
    applyGate();
    applyLevelOptions();
    for (auto& m : modules) addChildComponent (*m);

    for (int i = 0; i < NumModules; ++i)
    {
        auto& c = chips[(size_t) i];
        c.setButtonText (kModuleNames[i]);
        c.setClickingTogglesState (false);
        c.setColour (juce::TextButton::buttonColourId, kButton);
        c.setColour (juce::TextButton::buttonOnColourId, kOn);
        c.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < NumModules - 1 ? juce::Button::ConnectedOnRight : 0));
        c.setTooltip ("Show or hide the " + juce::String (kModuleNames[i]) + " module");
        c.onClick = [this, i] { setShown (i, ! (s.monitorShown & (1 << i))); };
        addAndMakeVisible (c);
    }
    pauseBtn.setClickingTogglesState (true);
    pauseBtn.setColour (juce::TextButton::buttonOnColourId, kWarn.darker (0.2f));
    pauseBtn.setTooltip ("Pause: freeze every module (the measurements keep their values; audio that comes in meanwhile is skipped)");
    pauseBtn.onClick = [this] { paused = pauseBtn.getToggleState(); };
    for (auto* b : { &layoutBtn, &resetBtn, &pauseBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, kButton);
        addAndMakeVisible (b);
    }
    layoutBtn.setTooltip ("Columns, and resetting the layout");
    layoutBtn.onClick = [this] { showLayoutMenu(); };
    resetBtn.setTooltip ("Restart the integrated loudness, LRA, PLR, maxima and history (the bottom bar's too)");
    resetBtn.onClick = [this] { resetMeasurements(); };
    updateChips();
}

MonitorPage::~MonitorPage() { if (! shared) tap.setActive (false); }   // a shared model keeps being fed

void MonitorPage::setActive (bool shown)
{
    active = shown;
    if (shown) { layoutModules(); repaint(); }
}

void MonitorPage::pump()
{
    if (hostPlaying)                                  // VST3: Reset when playback starts
    {
        const bool p = hostPlaying();
        if (p && ! lastPlaying && s.monitorResetOnPlay) resetMeasurements();
        lastPlaying = p;
    }
    if (held || paused) { tap.drain ([] (const float*, const float*, int) {}); return; }
    const double sr = tap.sampleRate();
    if (std::abs (sr - engine.sampleRate()) > 0.5) engine.configure (sr);
    tap.drain ([this] (const float* l, const float* r, int n) { engine.push (l, r, n); });
}

void MonitorPage::onFrame (double t)
{
    pump();
    const double dt = lastFrame > 0 ? juce::jlimit (0.0, 0.25, t - lastFrame) : 1.0 / 60;
    lastFrame = t;
    if (! active || ! isShowing()) return;
    for (auto& m : modules)
        if (m->isVisible()) { if (! held && ! paused) m->advance (dt); m->repaint(); }
}

void MonitorPage::resetMeasurements()
{
    engine.reset();
    if (onResetLoudness) onResetLoudness();
    repaint();
}

void MonitorPage::applySettings()
{
    updateChips();
    if (auto* f = dynamic_cast<FieldModule*> (modules[SoundField].get())) f->updateButtons();
    layoutModules();
    repaint();
}

void MonitorPage::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    text (g, "Monitor", titleArea.toFloat(), ui::fs (20), kHeaderText, juce::Justification::centredLeft, true);
    if ((s.monitorShown & 63) == 0)
        text (g, "All modules are hidden. Turn them on with the buttons above.", getLocalBounds().withTrimmedTop (toolbar.getBottom()).toFloat(), ui::fs (14), kDim);
}

void MonitorPage::resized()
{
    auto r = getLocalBounds();
    toolbar = r.removeFromTop (ui::px (44)).reduced (ui::px (10), ui::px (8));
    auto tb = toolbar;
    titleArea = tb.removeFromLeft (ui::px (110));
    resetBtn.setBounds (tb.removeFromRight (ui::px (64)));
    tb.removeFromRight (ui::px (6));
    pauseBtn.setBounds (tb.removeFromRight (ui::px (64)));
    tb.removeFromRight (ui::px (6));
    layoutBtn.setBounds (tb.removeFromRight (ui::px (72)));
    const int cw = juce::jmin (ui::px (104), (tb.getWidth() - ui::px (12)) / NumModules);
    auto chipsR = tb.withSizeKeepingCentre (cw * NumModules, tb.getHeight());
    if (chipsR.getX() < titleArea.getRight()) chipsR.setX (titleArea.getRight());
    for (auto& c : chips) c.setBounds (chipsR.removeFromLeft (cw));
    layoutModules();
}

//==============================================================================
// Layout: rows of modules; a wide module takes a whole row, narrow ones share rows of up to `cols`.
// Row heights follow the tallest module weight in the row.

std::vector<int> MonitorPage::order() const
{
    std::vector<int> o;
    for (auto& t : juce::StringArray::fromTokens (s.monitorOrder, ",", {}))
    {
        const int v = t.getIntValue();
        if (v >= 0 && v < NumModules && std::find (o.begin(), o.end(), v) == o.end() && t.trim().isNotEmpty()) o.push_back (v);
    }
    for (int i = 0; i < NumModules; ++i) if (std::find (o.begin(), o.end(), i) == o.end()) o.push_back (i);
    return o;
}

void MonitorPage::setOrder (const std::vector<int>& o)
{
    juce::StringArray a; for (int v : o) a.add (juce::String (v));
    s.monitorOrder = a.joinIntoString (",");
}

void MonitorPage::applyTargetPreset (int i)
{
    s.monitorTarget = i;
    if (i >= 0 && i < kNumTargets)
    {
        const auto& t = kTargets[i];
        s.monitorTargetLufs = t.lufs; s.monitorTolerance = t.tol; s.monitorPeakTarget = t.tp; s.monitorGate = t.programGate ? 1 : 0;
        s.monitorLraLo = 0; s.monitorLraHi = i == kNumTargets - 1 ? 7 : 30;      // Netflix: LRA 7 LU or less
    }
    applyGate();
    repaint();
    for (auto& m : modules) m->repaint();
    changed();
}

void MonitorPage::applyGate() { engine.setProgramGate (s.monitorGate == 1); }

void MonitorPage::applyLevelOptions()
{
    const double holds[] = { 0.25, 0.5, 1.5, 5.0, -1.0 }, rms[] = { 0.05, 0.3, 1.0, 3.0 };
    engine.setHoldTime (holds[juce::jlimit (0, 4, s.monitorHoldTime)]);
    engine.setRmsTime (rms[juce::jlimit (0, 3, s.monitorRmsTime)]);
}

void MonitorPage::showTargetsPanel (juce::Rectangle<int> anchor, juce::Component* from)
{
    auto panel = std::make_unique<TargetsPanel> (s, [this] { applyGate(); for (auto& m : modules) m->repaint(); changed(); });
    juce::CallOutBox::launchAsynchronously (std::move (panel), from != nullptr ? from->localAreaToGlobal (anchor) : getScreenBounds(), nullptr);
}

float MonitorPage::textScaleFor (int id) const
{
    auto t = juce::StringArray::fromTokens (s.monitorText, ",", {});
    return id < t.size() ? juce::jlimit (0.5f, 3.0f, t[id].getFloatValue() > 0 ? t[id].getFloatValue() : 1.0f) : 1.0f;
}

void MonitorPage::setTextScale (int id, float k)
{
    auto t = juce::StringArray::fromTokens (s.monitorText, ",", {});
    while (t.size() < NumModules) t.add ("1");
    t.set (id, juce::String (k, 2));
    s.monitorText = t.joinIntoString (",");
    modules[(size_t) id]->resized();
    modules[(size_t) id]->repaint();
    changed();
}

bool MonitorPage::isWide (int id) const { return (s.monitorWide >> id) & 1; }

void MonitorPage::layoutModules()
{
    auto area = getLocalBounds().withTrimmedTop (toolbar.getBottom() + ui::px (6)).reduced (ui::px (10), 0).withTrimmedBottom (ui::px (10));
    for (auto& m : modules) m->setVisible (false);
    if (area.isEmpty()) return;
    const int gap = ui::px (8);

    if (s.monitorMax >= 0 && ((s.monitorShown >> s.monitorMax) & 1))
    {
        modules[(size_t) s.monitorMax]->setBounds (area);
        modules[(size_t) s.monitorMax]->setVisible (active);
        return;
    }
    const int cols = s.monitorCols > 0 ? s.monitorCols : (area.getWidth() >= ui::px (1150) ? 3 : (area.getWidth() >= ui::px (760) ? 2 : 1));
    std::vector<std::vector<int>> rows;
    for (int id : order())
    {
        if (! ((s.monitorShown >> id) & 1)) continue;
        if (isWide (id) || cols == 1) { rows.push_back ({ id }); continue; }
        if (rows.empty() || isWide (rows.back().front()) || (int) rows.back().size() >= cols) rows.push_back ({});
        rows.back().push_back (id);
    }
    if (rows.empty()) return;
    float total = 0;
    std::vector<float> wts;
    for (auto& row : rows) { float w = 0; for (int id : row) w = juce::jmax (w, kModuleWeight[id]); wts.push_back (w); total += w; }
    const int avail = area.getHeight() - gap * ((int) rows.size() - 1);
    int y = area.getY();
    for (size_t i = 0; i < rows.size(); ++i)
    {
        const int h = i + 1 == rows.size() ? area.getBottom() - y : juce::roundToInt (avail * wts[i] / total);
        auto rowR = juce::Rectangle<int> (area.getX(), y, area.getWidth(), h);
        const int n = (int) rows[i].size();
        const int w = (rowR.getWidth() - gap * (n - 1)) / n;
        for (int k = 0; k < n; ++k)
        {
            auto& m = *modules[(size_t) rows[i][(size_t) k]];
            m.setBounds (k + 1 == n ? rowR : rowR.removeFromLeft (w));
            if (k + 1 < n) rowR.removeFromLeft (gap);
            m.setVisible (active);
        }
        y += h + gap;
    }
}

void MonitorPage::updateChips()
{
    for (int i = 0; i < NumModules; ++i) chips[(size_t) i].setToggleState ((s.monitorShown >> i) & 1, juce::dontSendNotification);
}

void MonitorPage::setShown (int id, bool shown)
{
    if (shown) s.monitorShown |= (1 << id); else s.monitorShown &= ~(1 << id);
    if (! shown && s.monitorMax == id) s.monitorMax = -1;
    if (shown && s.monitorMax >= 0 && s.monitorMax != id) s.monitorMax = -1;   // showing another module restores the grid
    updateChips(); layoutModules(); repaint(); changed();
}

void MonitorPage::setWide (int id, bool wide)
{
    if (wide) s.monitorWide |= (1 << id); else s.monitorWide &= ~(1 << id);
    layoutModules(); changed();
}

void MonitorPage::toggleMax (int id)
{
    s.monitorMax = s.monitorMax == id ? -1 : id;
    layoutModules();
    for (auto& m : modules) m->repaint();
    changed();
}

int MonitorPage::moduleAt (juce::Point<int> p, int except) const
{
    for (int i = 0; i < NumModules; ++i)
        if (i != except && modules[(size_t) i]->isVisible() && modules[(size_t) i]->getBounds().contains (p)) return i;
    return -1;
}

void MonitorPage::dragModule (int id, juce::Point<int> p)
{
    const int t = moduleAt (p, id);
    if (t != dragTarget)
    {
        if (dragTarget >= 0) modules[(size_t) dragTarget]->repaint();
        dragTarget = t;
        if (t >= 0) modules[(size_t) t]->repaint();
    }
}

void MonitorPage::dropModule (int id, juce::Point<int> p)
{
    const int t = moduleAt (p, id);
    if (dragTarget >= 0) modules[(size_t) dragTarget]->repaint();
    dragTarget = -1;
    if (t < 0) return;
    auto o = order();                           // swap the two modules' places
    auto a = std::find (o.begin(), o.end(), id), b = std::find (o.begin(), o.end(), t);
    std::iter_swap (a, b);
    setOrder (o);
    layoutModules(); changed();
}

void MonitorPage::showLayoutMenu()
{
    juce::PopupMenu menu, cols;
    const char* names[] = { "Automatic", "1", "2", "3" };
    for (int i = 0; i < 4; ++i) cols.addItem (names[i], true, s.monitorCols == i, [this, i] { s.monitorCols = i; layoutModules(); changed(); });
    menu.addSubMenu ("Columns", cols);
    menu.addItem ("Show all modules", (s.monitorShown & 63) != 63, false, [this] { s.monitorShown = 63; s.monitorMax = -1; updateChips(); layoutModules(); repaint(); changed(); });
    menu.addItem ("Reset layout", [this]
    {
        s.monitorOrder = "0,1,2,3,4,5"; s.monitorShown = 63; s.monitorWide = 35; s.monitorMax = -1; s.monitorCols = 0;
        updateChips(); layoutModules(); repaint(); changed();
    });
    menu.addSeparator();
    menu.addSectionHeader ("Drag a module's title onto another to swap them");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&layoutBtn));
}

void MonitorPage::loadForTest (const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return;
    held = true;
    engine.configure (rd->sampleRate);
    const int len = (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 300));
    juce::AudioBuffer<float> buf (2, len);
    rd->read (&buf, 0, len, 0, true, true);
    if (rd->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, len);
    // in display-frame-sized pieces for the last second, so the sound field has a trail
    const int tail = juce::jmin (len, (int) rd->sampleRate);
    engine.push (buf.getReadPointer (0), buf.getReadPointer (1), len - tail);
    const int step = (int) (rd->sampleRate / 60);
    for (int p = len - tail; p < len; p += step)
    {
        engine.push (buf.getReadPointer (0) + p, buf.getReadPointer (1) + p, juce::jmin (step, len - p));
        for (auto& m : modules) if (m->isVisible()) m->advance (1.0 / 60);
    }
    for (auto& m : modules) m->repaint();
}
