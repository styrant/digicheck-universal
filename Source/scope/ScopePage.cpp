#include "ScopePage.h"
#include "../TextFormat.h"
#include "../AppInfo.h"
#include "../UiScale.h"
#include "../FrameClock.h"
#include "../ColourPicker.h"
#include "../Core.h"

namespace
{
    const double kBars[6] = { 0.25, 0.5, 1, 2, 4, 8 };
    const char* kBarNames[6] = { "1/4", "1/2", "1", "2", "4", "8" };
    const float kGuideDb[5] = { -2, -4, -6, -12, -18 };

    juce::String ms (double s) { return juce::String (s * 1000.0, s < 1 ? 1 : 0) + "ms"; }
    juce::String db1 (double d) { return d <= -199 ? juce::String ("-inf") : juce::String (d, 1) + "dB"; }

    // a column's colour from its five band energies (sub, low, mid, high, air)
    juce::Colour spectralColour (const std::array<float, scope::kBands>& e, const ScopeTheme& t)
    {
        static const float comp[scope::kBands] = { 1.0f, 1.2f, 1.6f, 2.2f, 3.0f };   // higher bands carry less energy
        float w[scope::kBands], sum = 0;
        for (int i = 0; i < scope::kBands; ++i) { const float a = std::sqrt (std::max (0.0f, e[(size_t) i])) * comp[i]; w[i] = a * a; sum += w[i]; }
        if (sum <= 0) return t.wave;
        float r = 0, g = 0, b = 0;
        for (int i = 0; i < scope::kBands; ++i)
        {
            const auto c = t.spectral[(size_t) i];
            r += w[i] * c.getFloatRed(); g += w[i] * c.getFloatGreen(); b += w[i] * c.getFloatBlue();
        }
        return juce::Colour::fromFloatRGBA (r / sum, g / sum, b / sum, 1.0f);
    }
}

//==============================================================================
// Buttons and sliders in the theme's colours. Button property "style": "seg" (the mode switch: accent
// when on), "text" (text only: the on colour when on), anything else boxed (secondary accent when on).
struct ScopePage::Lnf : public juce::LookAndFeel_V4
{
    const ScopeTheme* t = nullptr;

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        const auto style = b.getProperties()["style"].toString();
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const float rad = 5.0f;
        if (style == "text")
        {
            if (over) { g.setColour (t->controlBg.brighter (0.15f)); g.fillRoundedRectangle (r, rad); }
            return;
        }
        juce::Colour fill = t->controlBg;
        if (on) fill = style == "seg" ? t->accent : t->secondaryAccent;
        if (b.getProperties()["rec"]) fill = on ? t->recording : t->controlBg;
        if (! b.isEnabled()) fill = t->controlBg.withMultipliedAlpha (0.6f);
        if (over && ! on) fill = fill.brighter (0.08f);
        if (down) fill = fill.darker (0.1f);
        g.setColour (fill);
        if (style == "seg")
        {
            const bool l = b.isConnectedOnLeft(), rr = b.isConnectedOnRight();
            juce::Path p;
            p.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), rad, rad, ! l, ! rr, ! l, ! rr);
            g.fillPath (p);
            g.setColour (t->buttonOutline); g.strokePath (p, juce::PathStrokeType (1.0f));
            return;
        }
        g.fillRoundedRectangle (r, rad);
        if (! on) { g.setColour (t->buttonOutline); g.drawRoundedRectangle (r, rad, 1.0f); }
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto style = b.getProperties()["style"].toString();
        const bool on = b.getToggleState();
        juce::Colour c = t->textBtnOff;
        if (style == "text") c = on ? t->textBtnOn : t->textBtnOff;
        else if (on) c = style == "seg" ? t->accentText : t->secondaryAccentText;
        if (b.getProperties()["rec"] && on) c = juce::Colours::white;
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.45f);
        g.setColour (c);
        g.setFont (juce::FontOptions (ui::fs (13.5f), on && style != "text" ? juce::Font::bold : juce::Font::plain));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const float cy = (float) y + (float) h * 0.5f;
        g.setColour (t->sliderTrack); g.fillRoundedRectangle ((float) x, cy - 2.5f, (float) w, 5.0f, 2.5f);
        // filled from the slider's "zero" (0 dB for the gain)
        const double zero = s.getProperties().contains ("zero") ? (double) s.getProperties()["zero"] : s.getMinimum();
        const float zx = (float) s.getPositionOfValue (zero) + (float) x;
        g.setColour (t->accent);
        g.fillRoundedRectangle (juce::jmin (zx, pos), cy - 2.5f, std::abs (pos - zx) + 0.01f, 5.0f, 2.5f);
        g.fillRoundedRectangle (pos - 4.5f, cy - 8.0f, 9.0f, 16.0f, 3.0f);
    }

    juce::Font getPopupMenuFont() override { return juce::Font (juce::FontOptions (ui::fs (14.0f))); }
    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        g.fillAll (t->controlBg);
        g.setColour (t->buttonOutline); g.drawRect (0, 0, w, h);
    }
};

//==============================================================================
// The band-pass frequency: drag up / right to raise it (logarithmic), double-click for 100 Hz
struct ScopePage::FreqKnob : public juce::Component, public juce::SettableTooltipClient
{
    explicit FreqKnob (ScopePage& p) : page (p)
    {
        setMouseCursor (juce::MouseCursor::UpDownLeftRightResizeCursor);
        setTooltip ("Band-pass frequency (Hz): drag to change, double-click for 100 Hz");
    }
    ScopePage& page;
    double startHz = 100;
    void paint (juce::Graphics& g) override
    {
        const auto& t = page.theme;
        g.setColour (t.controlBg); g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f);
        const double hz = page.s.scopeBPHz;
        g.setColour (page.s.scopeBP ? t.textBtnOn : t.textBtnOff);
        g.setFont (juce::FontOptions (ui::fs (13.5f)));
        g.drawText (textfmt::hz (hz), getLocalBounds(), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent&) override { startHz = page.s.scopeBPHz; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const double d = (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) / 150.0;
        page.s.scopeBPHz = juce::jlimit (20.0, 20000.0, startHz * std::pow (2.0, d * 3));
        page.engine.setBandpass (page.s.scopeBPHz);
        if (page.host != nullptr && page.host->isolation().bandpass.load()) page.host->setScopeListen (true, page.s.scopeBPHz, page.s.scopeBPType);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override { if (page.changed) page.changed(); }
    void mouseDoubleClick (const juce::MouseEvent&) override { page.s.scopeBPHz = 100; page.engine.setBandpass (100); repaint(); if (page.changed) page.changed(); }
};

//==============================================================================
// The wave icon: drag it out to export the recording (or the window's audio) as a WAV
struct ScopePage::WaveIcon : public juce::Component, public juce::SettableTooltipClient
{
    explicit WaveIcon (ScopePage& p) : page (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip ("Drag into your DAW or a folder: the recording (or, without one, the audio in the window) as a WAV. "
                    "Right-click for Normalize / Trim / Snap");
    }
    ScopePage& page;
    bool dragging = false;
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (4, 8);
        juce::Path p;
        for (int i = 0; i <= 48; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float) i / 48.0f;
            const float y = r.getCentreY() - std::sin ((float) i / 48.0f * 6.2831853f * 1.5f) * r.getHeight() * 0.5f;
            if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        const bool ready = page.engine.recState() == scope::ScopeModel::Rec::Done;
        g.setColour (ready ? page.theme.accent : page.theme.accent.withMultipliedAlpha (0.75f));
        g.strokePath (p, juce::PathStrokeType (2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu()) return;
        juce::PopupMenu m;
        m.setLookAndFeel (page.lnf.get());
        m.addItem (1, "Normalize (to 0 dB)", true, page.s.scopeRecNormalize);
        m.addItem (2, "Trim (silence at the end)", true, page.s.scopeRecTrim);
        m.addItem (3, "Snap (pad to the beat grid)", true, page.s.scopeRecSnap);
        m.addSeparator();
        m.addItem (4, "Save as...");
        m.addItem (5, "Open recordings folder");
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int id)
        {
            auto& s = page.s;
            if (id == 1) s.scopeRecNormalize = ! s.scopeRecNormalize;
            else if (id == 2) s.scopeRecTrim = ! s.scopeRecTrim;
            else if (id == 3) s.scopeRecSnap = ! s.scopeRecSnap;
            else if (id == 4)
            {
                auto fc = std::make_shared<juce::FileChooser> ("Save audio", page.nextRecordingFile(), "*.wav");
                fc->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                 [this, fc] (const juce::FileChooser& c) { if (c.getResult() != juce::File()) page.exportTo (c.getResult().withFileExtension ("wav")); });
            }
            else if (id == 5) { recordingsFolder().createDirectory(); recordingsFolder().startAsProcess(); }
            if (id >= 1 && id <= 3 && page.changed) page.changed();
        });
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.mods.isPopupMenu() || e.getDistanceFromDragStart() < 5 || ! page.engine.hasSignal()) return;
        dragging = true;
        const auto f = page.nextRecordingFile();
        page.exportTo (f);
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
    }
};

//==============================================================================
// The waveform display
struct ScopePage::Display : public juce::Component
{
    explicit Display (ScopePage& p) : page (p) { setOpaque (false); }
    ScopePage& page;
    double v0 = 0, v1 = 1;                       // visible window time
    bool selecting = false, haveSel = false, pinned = false;
    double selA = 0, selB = 0;
    int resizing = 0;                             // 1: the selection's left edge, 2: its right edge
    float rulerY = -1;
    juce::Point<int> downPos;
    double downV0 = 0, downV1 = 1, anchorT = 0;
    bool panning = false;
    std::vector<scope::Column> cols, holdCols;
    std::vector<float> sm, ss, hm, hs;

    double window() const { return page.engine.windowSeconds(); }
    float xOf (double t) const { return (float) ((t - v0) / (v1 - v0) * getWidth()); }
    double tAt (float x) const { return v0 + (double) x / juce::jmax (1, getWidth()) * (v1 - v0); }
    float gainLin() const { return (float) juce::Decibels::decibelsToGain (page.s.scopeGainDb); }
    void showAll() { v0 = 0; v1 = window(); }

    void clampView()
    {
        const double W = window(), minSpan = juce::jmax (16.0 / page.engine.sampleRate(), 1e-4);
        double span = juce::jlimit (minSpan, W, v1 - v0);
        double a = juce::jlimit (0.0, W - span, v0);
        v0 = a; v1 = a + span;
        page.repaintZoomBar();
    }

    void zoomAround (double t, double factor)
    {
        const double span = juce::jlimit (juce::jmax (16.0 / page.engine.sampleRate(), 1e-4), window(), (v1 - v0) * factor);
        const double frac = (t - v0) / (v1 - v0);
        v0 = t - frac * span; v1 = v0 + span;
        clampView();
    }

    //-------------------------------------------------------------------------- input
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const float d = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : w.deltaX;
        if (e.mods.isAltDown()) { page.setDivision (page.s.scopeDivision + (d > 0 ? 1 : -1)); return; }
        if (e.mods.isCommandDown() || e.mods.isCtrlDown())
        {
            page.s.scopeGainDb = juce::jlimit (-24.0, 24.0, page.s.scopeGainDb + (d > 0 ? 1.0 : -1.0));
            page.gain.setValue (page.s.scopeGainDb, juce::dontSendNotification);
            page.repaint (page.gainText);
            repaint();
            return;
        }
        if (e.mods.isShiftDown()) { const double sp = v1 - v0; v0 -= d * sp * 0.25; v1 = v0 + sp; clampView(); repaint(); return; }
        zoomAround (tAt ((float) e.x), std::pow (1.25, -d * 4.0));
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        downPos = e.getPosition(); downV0 = v0; downV1 = v1; anchorT = tAt ((float) e.x);
        panning = false; selecting = false; resizing = 0;
        if (e.mods.isPopupMenu()) { page.showMenu(); return; }
        if (haveSel)
        {
            const float xa = xOf (std::min (selA, selB)), xb = xOf (std::max (selA, selB));
            if (std::abs ((float) e.x - xa) < 6) { resizing = 1; return; }
            if (std::abs ((float) e.x - xb) < 6) { resizing = 2; return; }
        }
        if (e.mods.isCommandDown() || e.mods.isCtrlDown()) { selecting = true; selA = selB = anchorT; return; }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) return;
        const double t = juce::jlimit (0.0, window(), tAt ((float) e.x));
        if (resizing)
        {
            double& lo = selA < selB ? selA : selB;
            double& hi = selA < selB ? selB : selA;
            (resizing == 1 ? lo : hi) = t;
            repaint(); return;
        }
        if (selecting) { selB = t; haveSel = std::abs (selB - selA) > 0; repaint(); return; }
        if (e.getDistanceFromDragStart() < 3 && ! panning) return;
        panning = true;
        // up / down zooms around where the drag started, left / right pans
        const double factor = std::exp ((e.y - downPos.y) * -0.01);
        const double span = juce::jlimit (juce::jmax (16.0 / page.engine.sampleRate(), 1e-4), window(), (downV1 - downV0) * factor);
        const double frac = (anchorT - downV0) / (downV1 - downV0);
        v0 = anchorT - frac * span - (e.x - downPos.x) / (double) juce::jmax (1, getWidth()) * span;
        v1 = v0 + span;
        clampView();
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const bool click = e.getDistanceFromDragStart() < 3 && ! panning;
        if (selecting && click) haveSel = false;
        if (click && (e.mods.isCommandDown() || e.mods.isCtrlDown()) && ! e.mods.isPopupMenu())
        {
            pinned = ! pinned;                       // Ctrl+click pins / releases the dB ruler
            rulerY = (float) e.y;
        }
        else if (click && ! resizing && ! e.mods.isPopupMenu() && haveSel)
        {
            const double t = tAt ((float) e.x);
            if (t < std::min (selA, selB) || t > std::max (selA, selB)) haveSel = false;   // click outside deselects
        }
        selecting = false; resizing = 0; panning = false;
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { showAll(); clampView(); repaint(); }

    void mouseMove (const juce::MouseEvent& e) override
    {
        if (! pinned) rulerY = (float) e.y;
        if (haveSel && (std::abs ((float) e.x - xOf (std::min (selA, selB))) < 6 || std::abs ((float) e.x - xOf (std::max (selA, selB))) < 6))
            setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
        else setMouseCursor (juce::MouseCursor::NormalCursor);
        // no repaint here: the page redraws the display every frame (ScopePage::onFrame), and a repaint
        // per mouse event (hundreds a second) would add whole extra GPU frames outside the frame pacing
    }
    void mouseExit (const juce::MouseEvent&) override { if (! pinned) rulerY = -1; repaint(); }   // once (frozen: clears the ruler)

    //-------------------------------------------------------------------------- drawing
    // one column of the envelope: [top, bot] in image pixels, in the clipping colour beyond the 0 dBFS
    // lines. Fractional edges are anti-aliased by the renderer (AA on); snapped to whole pixels (AA off).
    static void column (juce::Graphics& g, int x, float top, float bot, juce::Colour c, juce::Colour clip, float clipTop, float clipBot, float alpha, bool aa)
    {
        if (bot < top) std::swap (top, bot);
        if (bot - top < 1.0f) { const float m = (top + bot) * 0.5f; top = m - 0.5f; bot = m + 0.5f; }
        if (! aa) { top = std::floor (top); bot = std::ceil (bot); }
        auto span = [&] (float a, float b, juce::Colour col)
        {
            if (b <= a) return;
            g.setColour (col.withMultipliedAlpha (alpha));
            g.fillRect (juce::Rectangle<float> ((float) x, a, 1.0f, b - a));
        };
        span (top, juce::jmin (bot, clipTop), clip);                                   // above +0 dBFS
        span (juce::jmax (top, clipTop), juce::jmin (bot, clipBot), c);
        span (juce::jmax (top, clipBot), bot, clip);                                   // below -0 dBFS
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = page.theme;
        auto r = getLocalBounds().toFloat();
        g.setColour (t.scopeBg); g.fillRoundedRectangle (r, 6.0f);
        const float W = r.getWidth(), H = r.getHeight(), mid = H * 0.5f, half = H * 0.5f - 4.0f;
        const float gl = gainLin();
        auto yOf = [&] (float v) { return mid - v * gl * half; };

        // ---- grid: beats, stronger on bars
        {
            const double beat = 60.0 / juce::jmax (20.0, page.s.bpm) * (page.s.scopeTriplet ? 2.0 / 3.0 : 1.0);
            const double pxPerBeat = beat / (v1 - v0) * W;
            const int barEvery = page.s.scopeTriplet ? 6 : 4;
            const double step = pxPerBeat >= 10 ? beat : beat * barEvery;
            for (double tt = std::ceil (v0 / step - 1e-9) * step; tt <= v1 + 1e-9; tt += step)
            {
                const long n = std::lround (tt / beat);
                const bool bar = n % barEvery == 0;
                g.setColour (bar ? t.grid.brighter (0.25f) : t.grid);
                g.drawVerticalLine ((int) xOf (tt), r.getY(), r.getBottom());
            }
        }
        // ---- guide lines with the dB scale on the right
        g.setColour (t.grid); g.drawHorizontalLine ((int) mid, 0, W);
        if (page.s.scopeGuides)
        {
            g.setFont (juce::FontOptions (ui::fs (10.5f)));
            for (float d : kGuideDb)
                for (int sgn : { 1, -1 })
                {
                    const float y = yOf ((float) sgn * juce::Decibels::decibelsToGain (d));
                    if (y < 6 || y > H - 6) continue;
                    g.setColour (t.grid); g.drawHorizontalLine ((int) y, 0, W);
                    g.setColour (t.text.withMultipliedAlpha (0.8f));
                    g.drawText (juce::String ((int) d), (int) W - ui::px (28), (int) y - 7, ui::px (24), 14, juce::Justification::centredRight);
                }
        }
        if (gl < 1.0f)                                     // where 0 dBFS is, when the gain shows the headroom
        {
            g.setColour (t.clipping.withAlpha (0.35f));
            for (int sgn : { 1, -1 }) { const float y = yOf ((float) sgn); for (float x = 0; x < W; x += 8) g.drawHorizontalLine ((int) y, x, x + 4); }
        }

        // ---- the waveform(s)
        const bool bpOn = page.s.scopeBP, side = page.s.scopeSide, hold = page.engine.isHolding();
        const double sr = page.engine.sampleRate();
        const float scale = juce::jmax (1.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
        const int pw = juce::jmax (1, juce::roundToInt (W * scale)), ph = juce::jmax (1, juce::roundToInt (H * scale));
        const double samplesPerPx = (v1 - v0) * sr / pw;
        const float mainAlpha = side ? 0.7f : 1.0f;        // the main wave is dimmed under Side2SC
        if (samplesPerPx >= 1.0)
        {
            // Scroll: columns on a fixed sample grid, one extra, drawn shifted by the sub-pixel remainder
            const bool smoothScroll = page.engine.getMode() == scope::Scroll && page.engine.totalWritten() > 0;
            double frac = 0;
            const int iw = pw + (smoothScroll ? 1 : 0);
            if (smoothScroll)
            {
                page.engine.columnsScroll (page.scrollNow - (window() - v1) * sr, samplesPerPx, pw, bpOn, cols, frac);
                // whole device pixels only: drawn at sub-pixel offsets, thin peaks were anti-aliased a little
                // differently every frame and shimmered. On the fixed grid each column keeps its exact content
                // and just moves over (steps alternate by at most one pixel)
                frac = 0;
            }
            else
                page.engine.columns (v0, v1, pw, bpOn, false, cols);
            if (hold) page.engine.columns (v0, v1, pw, bpOn, true, holdCols);
            // drawn straight into the context (no image to upload to the GPU every frame): one rectangle per
            // device-pixel column, in device-pixel units through a transform; the OpenGL renderer batches them
            {
                g.saveState();
                g.reduceClipRegion (getLocalBounds());
                g.addTransform (juce::AffineTransform::scale (W / (float) pw, H / (float) ph)
                                    .translated (r.getX() - (float) frac * W / (float) pw, r.getY()));
                auto& bd = g;
                const float pmid = ph * 0.5f, phalf = ph * 0.5f - 4.0f * scale;
                auto py = [&] (float v) { return pmid - v * gl * phalf; };
                const float clipTop = py (1.0f), clipBot = py (-1.0f);
                auto layer = [&] (const std::vector<scope::Column>& cs, bool sideLayer, juce::Colour fixed, bool spectral, float alpha, bool clipColour)
                {
                    for (int x = 0; x < (int) cs.size() && x < iw; ++x)
                    {
                        const auto& c = cs[(size_t) x];
                        if (! c.valid) continue;
                        float lo = sideLayer ? c.smn : c.mn, hi = sideLayer ? c.smx : c.mx;
                        if (x > 0 && cs[(size_t) x - 1].valid)        // join the neighbour so steep edges have no gaps
                        {
                            const auto& p = cs[(size_t) x - 1];
                            hi = std::max (hi, sideLayer ? p.smn : p.mn); lo = std::min (lo, sideLayer ? p.smx : p.mx);
                        }
                        const auto col = spectral ? spectralColour (c.bands, t) : fixed;
                        column (bd, x, py (hi), py (lo), col, clipColour ? t.clipping : col, clipColour ? clipTop : -1e9f, clipColour ? clipBot : 1e9f, alpha, page.s.scopeAA);
                    }
                };
                if (hold) layer (holdCols, false, t.hold, false, 0.9f, false);
                layer (cols, false, t.wave, page.s.scopeSpectral && ! bpOn, mainAlpha, true);
                if (side && page.s.scopeScStyle == 1)
                {
                    // outline: the overlay's envelope (its peak over a few columns either side) as two lines,
                    // so its shape reads clearly over the main wave; nothing where it's silent
                    const int n = (int) cols.size(), k = juce::jmax (2, (int) (3 * scale));
                    std::vector<float> e ((size_t) n, 0.0f), env ((size_t) n, 0.0f);
                    for (int x = 0; x < n; ++x) if (cols[(size_t) x].valid) e[(size_t) x] = juce::jmax (std::abs (cols[(size_t) x].smx), std::abs (cols[(size_t) x].smn));
                    for (int x = 0; x < n; ++x) { float m = 0; for (int d = -k; d <= k; ++d) if (x + d >= 0 && x + d < n) m = juce::jmax (m, e[(size_t) (x + d)]); env[(size_t) x] = m; }
                    const float lw = 2.0f * scale;
                    for (int sgn : { 1, -1 })
                    {
                        juce::Path p; bool on = false;
                        for (int x = 0; x < n; ++x)
                        {
                            if (env[(size_t) x] < 0.001f) { on = false; continue; }
                            const float y = py ((float) sgn * env[(size_t) x]);
                            if (! on) { p.startNewSubPath ((float) x, y); on = true; } else p.lineTo ((float) x, y);
                        }
                        g.setColour (t.sideChain);
                        g.strokePath (p, juce::PathStrokeType (lw, juce::PathStrokeType::curved));
                    }
                }
                else if (side) layer (cols, true, t.sideChain, false, t.sideChainAlpha, false);
                g.restoreState();
            }
        }
        else
        {
            // zoomed in to the samples: lines through them (dots when they're far apart)
            auto curve = [&] (const std::vector<float>& v, juce::Colour c, float width)
            {
                juce::Path p; bool started = false;
                const double t0 = std::floor (v0 * sr) / sr;
                for (size_t i = 0; i < v.size(); ++i)
                {
                    if (std::isnan (v[i])) { started = false; continue; }
                    const float x = xOf (t0 + (double) i / sr), y = yOf (v[i]);
                    if (! started) { p.startNewSubPath (x, y); started = true; } else p.lineTo (x, y);
                }
                g.setColour (c); g.strokePath (p, juce::PathStrokeType (width));
                if (W / juce::jmax<size_t> (1, v.size()) > 8)
                    for (size_t i = 0; i < v.size(); ++i)
                        if (! std::isnan (v[i])) g.fillEllipse (xOf (t0 + (double) i / sr) - 2.5f, yOf (v[i]) - 2.5f, 5.0f, 5.0f);
            };
            const double a = std::floor (v0 * sr) / sr;
            if (hold) { page.engine.samples (a, v1, bpOn, true, hm, hs); curve (hm, t.hold, 1.5f); }
            page.engine.samples (a, v1, bpOn, false, sm, ss);
            page.engine.columns (v0, v1, 1, bpOn, false, cols);
            const auto c = page.s.scopeSpectral && ! bpOn && ! cols.empty() && cols[0].valid ? spectralColour (cols[0].bands, t) : t.wave;
            curve (sm, c.withMultipliedAlpha (mainAlpha), 1.8f);
            if (side) curve (ss, t.sideChain.withAlpha (t.sideChainAlpha), 1.5f);
        }

        // ---- the playhead (Free): a line with a fading trail
        if (page.engine.getMode() == scope::Free && page.engine.hasSignal())
        {
            const float x = xOf (page.engine.playhead());
            if (x >= 0 && x <= W)
            {
                juce::ColourGradient grad (t.playhead.withAlpha (0.0f), x - ui::px (40), 0, t.playhead.withAlpha (page.engine.isFrozen() ? 0.05f : 0.16f), x, 0, false);
                g.setGradientFill (grad); g.fillRect (juce::Rectangle<float> (x - ui::px (40), r.getY(), (float) ui::px (40), H));
                g.setColour (t.playhead); g.drawLine (x, r.getY(), x, r.getBottom(), 1.5f);
            }
        }

        // ---- the selection and its numbers
        if (haveSel)
        {
            const double a = std::min (selA, selB), b = std::max (selA, selB);
            const float xa = xOf (a), xb = xOf (b);
            g.setColour (t.selection.withAlpha (0.16f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xa, r.getY(), xb, r.getBottom()));
            g.setColour (t.selection.withAlpha (0.8f));
            g.drawVerticalLine ((int) xa, r.getY(), r.getBottom()); g.drawVerticalLine ((int) xb, r.getY(), r.getBottom());
            const auto st = page.engine.stats (a, b, bpOn);
            const juce::String txt = "time: " + ms (b - a) + "   peak: " + (st.valid ? db1 (st.peakDb) : "-") + "   rms: "
                                   + (st.valid ? db1 (st.rmsDb) : "-") + "   crest: " + (st.valid ? juce::String (st.crestDb, 1) + "dB" : "-");
            label (g, txt, juce::jlimit (4.0f, W - 10.0f, (xa + xb) * 0.5f), ui::px (8), true);
        }

        // ---- the dB ruler
        if (rulerY >= 0 && rulerY < H)
        {
            const float v = (mid - rulerY) / (gl * half);
            const double d = juce::Decibels::gainToDecibels (std::abs (v), -120.0f);
            g.setColour (t.selection.withAlpha (pinned ? 0.9f : 0.55f)); g.drawHorizontalLine ((int) rulerY, 0, W);
            label (g, (d <= -119 ? juce::String ("-inf") : juce::String (d, 1)) + " dB" + (pinned ? "  (pinned)" : ""), W - ui::px (8), rulerY - ui::px (11), false);
        }

        // ---- nothing to show yet
        if (! page.engine.hasSignal() && ! page.engine.isHolding() && page.inputRunning)   // no input: the app's banner says so
        {
            g.setColour (t.text); g.setFont (juce::FontOptions (ui::fs (15.0f)));
            g.drawText (page.inputRunning ? (page.engine.isFrozen() ? "Frozen" : "Waiting for audio...")
                                          : "No input. Open Options (bottom left), choose an input and press Start.",
                        getLocalBounds().withTrimmedBottom (getHeight() / 3), juce::Justification::centred);
        }
        if (page.engine.recState() == scope::ScopeModel::Rec::Done)
        {
            label (g, "Recorded " + juce::String (page.engine.windowSeconds(), 2) + " s.  Drag the wave icon (bottom right) into your DAW or a folder;"
                       " right-click it for Normalize / Trim / Snap", W * 0.5f, H - ui::px (30), true);
        }
    }

    // a label box: centred on x when centred, else right-aligned to x
    void label (juce::Graphics& g, const juce::String& txt, float x, float y, bool centred)
    {
        const auto& t = page.theme;
        g.setFont (juce::FontOptions (ui::fs (12.5f)));
        const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 14.0f, h = (float) ui::px (22);
        auto box = juce::Rectangle<float> (centred ? x - w * 0.5f : x - w, y, w, h);
        box = box.withX (juce::jlimit (2.0f, juce::jmax (2.0f, (float) getWidth() - w - 2), box.getX()));
        g.setColour (t.hoverBg.withAlpha (0.92f)); g.fillRoundedRectangle (box, 4.0f);
        g.setColour (t.hoverText.withAlpha (0.5f)); g.drawRoundedRectangle (box, 4.0f, 1.0f);
        g.setColour (t.hoverText); g.drawText (txt, box, juce::Justification::centred);
    }
};

//==============================================================================
// The scrollbar under the display: drag it to pan, drag its ends to zoom from that side
struct ScopePage::ZoomBar : public juce::Component
{
    explicit ZoomBar (ScopePage& p) : page (p) {}
    ScopePage& page;
    int edge = 0;
    double grabT = 0, a0 = 0, a1 = 0;
    juce::Rectangle<float> thumb() const
    {
        auto& d = *page.display;
        const double W = d.window();
        return { (float) (d.v0 / W * getWidth()), 2.0f, juce::jmax (8.0f, (float) ((d.v1 - d.v0) / W * getWidth())), (float) getHeight() - 4.0f };
    }
    void paint (juce::Graphics& g) override
    {
        if (page.display->v1 - page.display->v0 >= page.display->window() - 1e-9) return;    // nothing to scroll
        g.setColour (page.theme.controlBg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        g.setColour (page.theme.sliderTrack.brighter (0.4f)); g.fillRoundedRectangle (thumb(), 3.0f);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto th = thumb();
        edge = std::abs (e.position.x - th.getX()) < 8 ? 1 : std::abs (e.position.x - th.getRight()) < 8 ? 2 : 0;
        grabT = e.position.x; a0 = page.display->v0; a1 = page.display->v1;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& d = *page.display;
        const double dt = (e.position.x - grabT) / juce::jmax (1, getWidth()) * d.window();
        if (edge == 1) d.v0 = juce::jmin (a0 + dt, a1 - 1e-3);
        else if (edge == 2) d.v1 = juce::jmax (a1 + dt, a0 + 1e-3);
        else { d.v0 = a0 + dt; d.v1 = a1 + dt; }
        d.clampView(); d.repaint(); repaint();
    }
};

//==============================================================================
ScopePage::ScopePage (ScopeTap& t, Settings& st, std::function<void()> ch) : tap (t), s (st), changed (std::move (ch))
{
    setOpaque (true);
    loadTheme();
    lnf = std::make_unique<Lnf>();
    lnf->t = &theme;
    display = std::make_unique<Display> (*this);
    zoomBar = std::make_unique<ZoomBar> (*this);
    waveIcon = std::make_unique<WaveIcon> (*this);
    bpFreq = std::make_unique<FreqKnob> (*this);
    addAndMakeVisible (*display); addAndMakeVisible (*zoomBar); addAndMakeVisible (*waveIcon); addAndMakeVisible (*bpFreq);

    auto style = [&] (juce::TextButton& b, const char* st)
    {
        b.getProperties().set ("style", st);
        b.setLookAndFeel (lnf.get());
        addAndMakeVisible (b);
    };
    style (syncBtn, "seg"); style (freeBtn, "seg");
    syncBtn.setConnectedEdges (juce::Button::ConnectedOnRight); freeBtn.setConnectedEdges (juce::Button::ConnectedOnLeft);
    syncBtn.setEnabled (false);
    syncBtn.setTooltip ("Sync follows the DAW's transport: in the VST3 version");
    freeBtn.setTooltip ("Free: runs whenever audio comes in");
    for (auto* b : { &freezeBtn, &scrollBtn, static_cast<juce::TextButton*> (&spectralBtn), &aaBtn }) style (*b, "text");
    style (menuBtn, "box");
    freezeBtn.setTooltip ("Freeze: hold the picture still (Space)");
    scrollBtn.setTooltip ("Scroll: the newest audio enters at the right and moves left. Off: the trace sweeps left to right and, after silence, starts again at the left");
    spectralBtn.setTooltip ("Colour the waveform by frequency: sub, low, mid, high, air. Right-click: palettes; mouse wheel or C: next palette");
    aaBtn.setTooltip ("Anti-aliasing: smoother edges");
    freezeBtn.onClick = [this] { toggleFreeze(); };
    scrollBtn.onClick = [this] { s.scopeMode = s.scopeMode == scope::Scroll ? scope::Free : scope::Scroll; s.scopeSync = false; engine.setMode (effectiveMode()); updateButtons(); if (changed) changed(); };
    syncBtn.onClick = [this] { s.scopeSync = true; engine.setMode (effectiveMode()); updateButtons(); if (changed) changed(); };
    freeBtn.onClick = [this] { s.scopeSync = false; engine.setMode (effectiveMode()); updateButtons(); if (changed) changed(); };
    spectralBtn.onClick = [this] { s.scopeSpectral = ! s.scopeSpectral; updateButtons(); if (changed) changed(); };
    spectralBtn.onWheel = [this] (int d) { cyclePalette (d); };
    spectralBtn.onRightClick = [this]
    {
        juce::PopupMenu m;
        const auto pal = ScopeTheme::paletteNames();
        for (int i = 0; i < pal.size(); ++i)
            m.addItem (pal[i], true, s.scopePalette == i, [this, i]
            {
                s.scopePalette = i; if (i > 0) s.scopeSpectral = true;
                loadTheme(); updateButtons(); display->repaint(); repaint();
                if (changed) changed();
            });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&spectralBtn));
    };
    aaBtn.onClick = [this] { s.scopeAA = ! s.scopeAA; updateButtons(); if (changed) changed(); };
    menuBtn.onClick = [this] { showMenu(); };

    for (int i = 0; i < 6; ++i)
    {
        auto& b = divBtns[(size_t) i];
        b.setButtonText (kBarNames[i]);
        style (b, "box");
        b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 5 ? juce::Button::ConnectedOnRight : 0));
        b.setTooltip ("Bars shown at the tempo (Alt+wheel over the scope)");
        b.onClick = [this, i] { setDivision (i); };
    }
    style (tripletBtn, "text");
    tripletBtn.setTooltip ("Triplets: the division and grid in triplet time");
    tripletBtn.onClick = [this] { s.scopeTriplet = ! s.scopeTriplet; configure(); updateButtons(); if (changed) changed(); };

    addAndMakeVisible (bpmLabel);
    bpmLabel.setEditable (true);
    bpmLabel.setJustificationType (juce::Justification::centred);
    bpmLabel.setTooltip ("Tempo for the divisions (shared with the Tempo window). Click to type");
    bpmLabel.onTextChange = [this]
    {
        const double v = bpmLabel.getText().retainCharacters ("0123456789.").getDoubleValue();
        if (v >= 20 && v <= 400) { s.bpm = v; configure(); if (changed) changed(); }
        updateButtons();
    };

    gain.setLookAndFeel (lnf.get());
    gain.setRange (-24.0, 24.0, 0.1);
    gain.getProperties().set ("zero", 0.0);
    gain.setValue (s.scopeGainDb, juce::dontSendNotification);
    gain.setDoubleClickReturnValue (true, 0.0);
    gain.setTooltip ("Display gain (Ctrl+wheel over the scope). Turn it down to see the headroom: anything over 0 dBFS shows in the clipping colour");
    gain.onValueChange = [this] { s.scopeGainDb = gain.getValue(); display->repaint(); repaint (gainText); };
    gain.onDragEnd = [this] { if (changed) changed(); };
    addAndMakeVisible (gain);

    for (auto* b : { static_cast<juce::TextButton*> (&bpBtn), &sideBtn, &holdBtn, &resetBtn, &recBtn }) style (*b, "box");
    recBtn.getProperties().set ("rec", true);
    bpBtn.setTooltip ("Filter the display at the frequency on the right (drag it): band-pass, or right-click for low-pass / high-pass. Only the picture changes");
    bpBtn.onRightClick = [this]
    {
        juce::PopupMenu m;
        const char* names[] = { "Band-pass (BP)", "Low-pass (LP)", "High-pass (HP)" };
        for (int i = 0; i < 3; ++i)
            m.addItem (names[i], true, s.scopeBPType == i, [this, i]
            {
                s.scopeBPType = i;
                engine.setFilterType (i);
                if (host != nullptr && host->isolation().bandpass.load()) host->setScopeListen (true, s.scopeBPHz, i);
                updateButtons(); bpFreq->repaint(); display->repaint();
                if (changed) changed();
            });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&bpBtn));
    };
    sideBtn.setTooltip ("Side2SC: the side signal (L - R) over the waveform. Loud: wide or out of phase; barely there: close to mono");
    holdBtn.setTooltip ("Hold: keep the current picture behind the live one, to compare before / after");
    resetBtn.setTooltip ("Clear the live waveform (not the Hold picture)");
    recBtn.setTooltip ("Record: starts when audio comes in and stops after one division. Then drag the wave icon out");
    bpBtn.onClick = [this] { s.scopeBP = ! s.scopeBP; updateButtons(); bpFreq->repaint(); if (changed) changed(); };
    sideBtn.onClick = [this] { s.scopeSide = ! s.scopeSide; updateButtons(); if (changed) changed(); };
    holdBtn.onClick = [this] { engine.hold (! engine.isHolding()); updateButtons(); };
    resetBtn.onClick = [this] { engine.reset(); display->repaint(); };
    recBtn.onClick = [this]
    {
        const auto r = engine.recState();
        if (r == scope::ScopeModel::Rec::Armed || r == scope::ScopeModel::Rec::Recording) engine.disarm(); else engine.arm();
        updateButtons();
    };

    style (listenBtn, "box");
    listenBtn.getProperties().set ("rec", true);
    listenBtn.setVisible (false);
    listenBtn.setTooltip ("Listen: hear only the band-pass (the plugin's output; click again to stop)");
    listenBtn.onClick = [this]
    {
        if (host == nullptr) return;
        const bool on = ! host->isolation().bandpass.load();
        if (on && ! s.scopeBP) s.scopeBP = true;
        host->setScopeListen (on, s.scopeBPHz, s.scopeBPType);
        updateButtons();
    };
    addChildComponent (listenBtn);
    engine.setSource (s.scopeSource);
    engine.setMode (effectiveMode());
    engine.setFilterType (s.scopeBPType);
    engine.setBandpass (s.scopeBPHz);
    configure();
    updateButtons();
}

ScopePage::~ScopePage()
{
    for (auto* b : { &syncBtn, &freeBtn, &freezeBtn, &scrollBtn, static_cast<juce::TextButton*> (&spectralBtn), &aaBtn, &menuBtn, &tripletBtn, static_cast<juce::TextButton*> (&bpBtn), &sideBtn, &holdBtn, &resetBtn, &recBtn, &listenBtn })
        b->setLookAndFeel (nullptr);
    for (auto& b : divBtns) b.setLookAndFeel (nullptr);
    gain.setLookAndFeel (nullptr);
}

void ScopePage::loadForTest (const juce::File& f, double gainDb, bool selection, bool side)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr) return;
    engine.configure (rd->sampleRate, windowSeconds());
    tap.setActive (false);
    engine.reset();
    const int n = (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (engine.windowSeconds() * rd->sampleRate * 0.97));
    juce::AudioBuffer<float> b (2, n);
    rd->read (&b, 0, n, 0, true, true);
    engine.push (b.getReadPointer (0), b.getReadPointer (juce::jmin (1, b.getNumChannels() - 1)), n);
    engine.setFrozen (true);
    s.scopeGainDb = gainDb; s.scopeSide = side;
    display->showAll();
    if (auto z = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_SCOPEZOOM", {}); z.isNotEmpty())   // "start,length" in seconds
    {
        display->v0 = z.upToFirstOccurrenceOf (",", false, false).getDoubleValue();
        display->v1 = display->v0 + z.fromFirstOccurrenceOf (",", false, false).getDoubleValue();
        display->clampView();
        selection = false;
    }
    if (juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_TEST_SCOPEBP", {}).isNotEmpty()) { s.scopeBP = true; s.scopeBPHz = 60; engine.setBandpass (60); }
    if (selection) { display->haveSel = true; display->selA = engine.windowSeconds() * 0.5; display->selB = engine.windowSeconds() * 0.62; display->rulerY = display->getHeight() * 0.3f; display->pinned = true; }
    updateButtons();
}

void ScopePage::repaintZoomBar() { if (zoomBar != nullptr) zoomBar->repaint(); }

juce::File ScopePage::recordingsFolder()
{
    return appinfo::documentsFolder().getChildFile ("Scope Recordings");
}

juce::File ScopePage::nextRecordingFile() const
{
    recordingsFolder().createDirectory();
    return recordingsFolder().getNonexistentChildFile ("Scope " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav", false);
}

void ScopePage::exportTo (const juce::File& wav)
{
    const double beat = 60.0 / juce::jmax (20.0, s.bpm) * (s.scopeTriplet ? 2.0 / 3.0 : 1.0);
    const auto t = engine.take (s.scopeRecNormalize, s.scopeRecTrim, s.scopeRecSnap, beat);
    if (t.l.empty()) return;
    juce::AudioBuffer<float> b (2, (int) t.l.size());
    b.copyFrom (0, 0, t.l.data(), (int) t.l.size());
    b.copyFrom (1, 0, t.r.data(), (int) t.r.size());
    wav.deleteFile();
    juce::WavAudioFormat fmt;
    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (fmt.createWriterFor (new juce::FileOutputStream (wav), t.sampleRate, 2, 24, {}, 0)))
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

int ScopePage::effectiveMode() const
{
    if (host != nullptr && s.scopeSync) return scope::Sync;
    return s.scopeMode == scope::Scroll ? scope::Scroll : scope::Free;
}

void ScopePage::setHost (HostLink* h)
{
    host = h;
    syncBtn.setEnabled (h != nullptr);
    syncBtn.setTooltip (h ? "Sync: the window starts on a bar line and follows the DAW's transport, sample-accurately"
                          : "Sync follows the DAW's transport: in the VST3 version");
    listenBtn.setVisible (h != nullptr);
    engine.setMode (effectiveMode());
    sideBtn.setButtonText (h ? "SC" : "Side2SC");
    sideBtn.setTooltip (h ? "The overlay: the sidechain input (or Side, L - R; choose in the menu), in the Side2SC / sidechain colour"
                          : sideBtn.getTooltip());
    configure(); updateButtons(); resized();
}

double ScopePage::windowSeconds() const
{
    const double beat = 60.0 / juce::jlimit (20.0, 400.0, s.bpm);
    return juce::jmin (60.0, kBars[juce::jlimit (0, 5, s.scopeDivision)] * 4.0 * beat * (s.scopeTriplet ? 2.0 / 3.0 : 1.0));
}

void ScopePage::configure()
{
    lastBpm = s.bpm;
    engine.configure (tap.sampleRate(), windowSeconds());
    if (host != nullptr) host->setScopeSyncBars (kBars[juce::jlimit (0, 5, s.scopeDivision)] * (s.scopeTriplet ? 2.0 / 3.0 : 1.0));
    display->showAll();
    display->haveSel = false;
    display->repaint();
    zoomBar->repaint();
}

void ScopePage::applySettings()
{
    if (std::abs (windowSeconds() - engine.windowSeconds()) > 1e-9) configure();
    updateButtons();
}

void ScopePage::setDivision (int d)
{
    d = juce::jlimit (0, 5, d);
    if (d == s.scopeDivision) return;
    s.scopeDivision = d;
    configure();
    updateButtons();
    if (changed) changed();
}

void ScopePage::toggleFreeze()
{
    engine.setFrozen (! engine.isFrozen());
    updateButtons();
    display->repaint();
}

void ScopePage::setActive (bool shown)
{
    active = shown;
    tap.setActive (shown);
    if (shown) { loadTheme(); applySettings(); }
}

void ScopePage::onFrame (double t)
{
    if (! active || ! isShowing()) { lastFrameT = 0; return; }
    if (s.bpm != lastBpm || std::abs (tap.sampleRate() - engine.sampleRate()) > 0.5) { configure(); updateButtons(); }
    const auto before = engine.recState();
    engine.setOverlayFromSidechain (host != nullptr && s.scopeScInput);
    tap.drainAll ([this] (const float* l, const float* r, const float* sl, const float* sr, const float* ph, int n)
    {
        engine.push (l, r, sl, sr, ph, n);
    });

    // the scroll clock: steady steps of (frame time x rate), the frame time snapped to whole refresh
    // intervals (vblank timestamps jitter). The audio arrives in chunks (~40 ms from the DSP thread), so
    // the distance to the newest sample saw-tooths; only its smoothed value pulls the clock, very gently,
    // so the speed never visibly changes.
    const double sr = engine.sampleRate(), target = (double) engine.totalWritten() - kScrollLatency * sr;
    double dt = lastFrameT > 0 ? juce::jlimit (0.0, 0.1, t - lastFrameT) : 0.0;
    lastFrameT = t;
    if (const double R = frameclock::clock().refresh; R > 0.002 && dt > 0) dt = juce::jmax (1.0, std::round (dt / R)) * R;
    if (! engine.isFrozen())
    {
        scrollNow += dt * sr;
        const double err = target - scrollNow;
        if (std::abs (err) > 0.25 * sr) { scrollNow = target; scrollErr = 0; }   // started, stalled or reset: jump
        else
        {
            scrollErr += 0.03 * (err - scrollErr);
            scrollNow += scrollErr * 0.015;
        }
        scrollNow = juce::jmin (scrollNow, (double) engine.totalWritten());
        static const bool paceLog = std::getenv ("SPECTROSCOPE_SCOPE_PACE") != nullptr;
        if (paceLog) std::fprintf (stderr, "SCOPEPACE %.5f %.1f %.1f\n", t, scrollNow, target - scrollNow);
    }
    if (engine.recState() != before || engine.recState() == scope::ScopeModel::Rec::Recording) updateButtons();
    // frozen: the picture only changes with the mouse (ruler, zoom: those repaint themselves too) or on (un)freezing
    const bool frozenNow = engine.isFrozen();
    if (! frozenNow || frozenNow != lastFrozen || display->isMouseOver (true)) display->repaint();
    lastFrozen = frozenNow;
}

void ScopePage::updateButtons()
{
    freeBtn.setToggleState (effectiveMode() != scope::Sync, juce::dontSendNotification);
    syncBtn.setToggleState (effectiveMode() == scope::Sync, juce::dontSendNotification);
    if (host != nullptr) listenBtn.setToggleState (host->isolation().bandpass.load(), juce::dontSendNotification);
    freezeBtn.setToggleState (engine.isFrozen(), juce::dontSendNotification);
    scrollBtn.setToggleState (effectiveMode() == scope::Scroll, juce::dontSendNotification);
    spectralBtn.setToggleState (s.scopeSpectral, juce::dontSendNotification);
    aaBtn.setToggleState (s.scopeAA, juce::dontSendNotification);
    for (int i = 0; i < 6; ++i) divBtns[(size_t) i].setToggleState (i == s.scopeDivision, juce::dontSendNotification);
    tripletBtn.setToggleState (s.scopeTriplet, juce::dontSendNotification);
    bpBtn.setToggleState (s.scopeBP, juce::dontSendNotification);
    bpBtn.setButtonText (s.scopeBPType == 1 ? "LP" : s.scopeBPType == 2 ? "HP" : "BP");
    sideBtn.setToggleState (s.scopeSide, juce::dontSendNotification);
    holdBtn.setToggleState (engine.isHolding(), juce::dontSendNotification);
    const auto r = engine.recState();
    recBtn.setToggleState (r == scope::ScopeModel::Rec::Armed || r == scope::ScopeModel::Rec::Recording, juce::dontSendNotification);
    recBtn.setButtonText (r == scope::ScopeModel::Rec::Armed ? juce::String ("Armed")
                          : r == scope::ScopeModel::Rec::Recording ? juce::String (juce::roundToInt (engine.recProgress() * 100)) + "%"
                          : juce::String ("Rec"));
    bpmLabel.setText (juce::String (s.bpm, s.bpm == std::floor (s.bpm) ? 0 : 1) + " BPM", juce::dontSendNotification);
    bpmLabel.setColour (juce::Label::textColourId, theme.text);
    bpmLabel.setColour (juce::Label::backgroundColourId, theme.controlBg);
    bpmLabel.setColour (juce::Label::outlineColourId, theme.buttonOutline);
    gain.setValue (s.scopeGainDb, juce::dontSendNotification);
    bpFreq->repaint(); waveIcon->repaint();
    repaint();
}

void ScopePage::cyclePalette (int dir)
{
    const auto pal = ScopeTheme::paletteNames();
    if (pal.isEmpty()) return;
    s.scopePalette = (s.scopePalette + dir + pal.size()) % pal.size();
    s.scopeSpectral = true;
    loadTheme(); updateButtons(); display->repaint(); repaint();
    if (changed) changed();
    if (onToast) onToast ("Palette: " + pal[s.scopePalette]);
}

void ScopePage::loadTheme()
{
    theme = ScopeTheme::named (s.scopeTheme);
    theme.applyOverrides (s.scopeColours);
    if (s.scopePalette > 0) theme.spectral = ScopeTheme::palette (s.scopePalette, theme);
}

namespace
{
    juce::String setOverride (const juce::String& all, const juce::String& key, const juce::String& value)
    {
        juce::StringArray out;
        for (auto& kv : juce::StringArray::fromTokens (all, ";", {}))
            if (kv.isNotEmpty() && kv.upToFirstOccurrenceOf ("=", false, false).trim() != key) out.add (kv);
        if (value.isNotEmpty()) out.add (key + "=" + value);
        return out.joinIntoString (";");
    }
}

void ScopePage::pickColour (const juce::String& key, const juce::String&, juce::Colour current)
{
    auto picker = std::make_unique<LiveColourPicker> (current, [this, key] (juce::Colour c)
    {
        s.scopeColours = setOverride (s.scopeColours, key, "#" + c.toDisplayString (true));
        loadTheme();
        updateButtons();
        display->repaint();
    });
    const auto c = display->getScreenBounds().getCentre();
    juce::CallOutBox::launchAsynchronously (std::move (picker), juce::Rectangle<int> (c.x, c.y - 150, 1, 1), nullptr);
    if (changed) changed();
}

void ScopePage::saveThemeAs()
{
    auto* w = new juce::AlertWindow ("Save theme", "Name for the theme (saved to the Scope Themes folder):", juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("name", theme.name + " (mine)", {});
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
    {
        const auto name = w->getTextEditorContents ("name").trim();
        if (r != 1 || name.isEmpty()) return;
        auto t = theme; t.name = name;
        ScopeTheme::folder().createDirectory();
        ScopeTheme::folder().getChildFile (juce::File::createLegalFileName (name) + ".json").replaceWithText (juce::JSON::toString (t.toJson()));
        s.scopeTheme = name; s.scopeColours = {}; s.scopePalette = 0;
        loadTheme(); updateButtons();
        if (changed) changed();
    }), true);
}

void ScopePage::showMenu()
{
    juce::PopupMenu m, themes, source, colours, palettes;
    m.setLookAndFeel (lnf.get());
    const char* names[] = { "Left", "Right", "Mid", "Peak" };
    for (int i = 0; i < 4; ++i) source.addItem (100 + i, names[i], true, s.scopeSource == i);
    auto list = ScopeTheme::all();
    for (int i = 0; i < list.size(); ++i) themes.addItem (200 + i, list[i].name, true, list[i].name == theme.name);
    themes.addSeparator();
    themes.addItem (301, "Save current colours as a theme...");
    themes.addItem (300, "Open themes folder");

    const auto pal = ScopeTheme::paletteNames();
    for (int i = 0; i < pal.size(); ++i) palettes.addItem (400 + i, pal[i], true, s.scopePalette == i);
    colours.addSubMenu ("Spectral palette", palettes);
    colours.addSeparator();
    struct Key { const char* key; const char* label; juce::Colour ScopeTheme::* c; };
    static const Key keys[] = { { "waveColor", "Wave (Spectral off)", &ScopeTheme::wave }, { "sideChainColor", "Side2SC / sidechain", &ScopeTheme::sideChain },
                                { "holdColor", "Hold", &ScopeTheme::hold }, { "scopeBgColor", "Background", &ScopeTheme::scopeBg },
                                { "gridColor", "Grid", &ScopeTheme::grid }, { "playheadColor", "Playhead", &ScopeTheme::playhead },
                                { "clippingColor", "Clipping", &ScopeTheme::clipping }, { "accentColor", "Accent", &ScopeTheme::accent } };
    for (int i = 0; i < (int) std::size (keys); ++i)
        colours.addItem (500 + i, juce::String (keys[i].label) + "...", true, s.scopeColours.contains (keys[i].key));
    colours.addSeparator();
    colours.addItem (600, "Reset to the theme's colours", s.scopeColours.isNotEmpty() || s.scopePalette != 0);

    m.addSubMenu ("Source", source);
    m.addSubMenu ("Theme", themes);
    m.addSubMenu ("Colours", colours);
    m.addItem (1, "Guide lines", true, s.scopeGuides);
    m.addItem (2, "Open recordings folder");
    m.addSeparator();
    if (host != nullptr)
    {
        m.addItem (700, "Overlay (SC): the sidechain input", true, s.scopeScInput);
        m.addItem (701, "Overlay (SC): Side (L - R)", true, ! s.scopeScInput);
        m.addItem (702, "Overlay style: filled", true, s.scopeScStyle == 0);
        m.addItem (703, "Overlay style: outline (the main wave stays visible underneath)", true, s.scopeScStyle == 1);
        m.addItem (704, "Sync to the DAW", true, s.scopeSync);
    }
    else
    {
        m.addItem (702, "Overlay style: filled", true, s.scopeScStyle == 0);
        m.addItem (703, "Overlay style: outline", true, s.scopeScStyle == 1);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuBtn), [this, list] (int id)
    {
        if (id >= 100 && id < 104) { s.scopeSource = id - 100; engine.setSource (s.scopeSource); engine.reset(); }
        else if (id >= 200 && id < 200 + list.size()) { s.scopeTheme = list[id - 200].name; s.scopeColours = {}; loadTheme(); }
        else if (id == 300) { ScopeTheme::folder().createDirectory(); ScopeTheme::folder().startAsProcess(); }
        else if (id == 301) { saveThemeAs(); return; }
        else if (id >= 400 && id < 410) { s.scopePalette = id - 400; if (s.scopePalette > 0) s.scopeSpectral = true; loadTheme(); }
        else if (id >= 500 && id < 510)
        {
            const auto& k = keys[id - 500];
            pickColour (k.key, k.label, theme.*(k.c));
            return;
        }
        else if (id == 600) { s.scopeColours = {}; s.scopePalette = 0; loadTheme(); }
        else if (id == 700 || id == 701) { s.scopeScInput = id == 700; s.scopeSide = true; engine.reset(); }
        else if (id == 702 || id == 703) s.scopeScStyle = id - 702;
        else if (id == 704) { s.scopeSync = ! s.scopeSync; engine.setMode (effectiveMode()); }
        else if (id == 1) s.scopeGuides = ! s.scopeGuides;
        else if (id == 2) { recordingsFolder().createDirectory(); recordingsFolder().startAsProcess(); }
        else return;
        updateButtons();
        display->repaint();
        if (changed) changed();
    });
}

//==============================================================================
void ScopePage::resized()
{
    auto r = getLocalBounds().reduced (ui::px (12), ui::px (8));
    header = r.removeFromTop (ui::px (34));
    footer = r.removeFromBottom (ui::px (36));
    r.removeFromBottom (ui::px (6));

    // left-to-right items that drop off the end (hidden) when the page is narrow, e.g. in a Custom pane
    struct Item { juce::Component* c; int w, gapAfter; };
    auto place = [] (juce::Rectangle<int>& area, std::initializer_list<Item> items)
    {
        bool fits = true;
        for (auto& i : items)
        {
            if (i.c == nullptr) continue;
            fits = fits && area.getWidth() >= ui::px (i.w);
            i.c->setVisible (fits);
            if (! fits) continue;
            i.c->setBounds (area.removeFromLeft (ui::px (i.w)).reduced (0, 3));
            area.removeFromLeft (juce::jmin (area.getWidth(), ui::px (i.gapAfter)));
        }
    };

    auto h = header;
    titleArea = {};                                   // no page title: the bottom bar already says Scope
    menuBtn.setBounds (h.removeFromRight (ui::px (70)).reduced (0, 3)); h.removeFromRight (ui::px (8));
    place (h, { { &syncBtn, 58, 0 }, { &freeBtn, 58, 12 }, { &freezeBtn, 64, 0 }, { &scrollBtn, 58, 16 }, { &spectralBtn, 74, 0 }, { &aaBtn, 40, 0 } });
    infoArea = h.getWidth() >= ui::px (120) ? h : juce::Rectangle<int>();

    zoomBar->setBounds (r.removeFromBottom (ui::px (10)));
    r.removeFromBottom (ui::px (2));
    display->setBounds (r);

    auto f = footer;
    // right: Hold, Reset, Rec, the wave icon (dropped first when narrow), then the left group fills what's left
    const bool roomRight = f.getWidth() >= ui::px (760);
    for (auto* c : std::initializer_list<juce::Component*> { waveIcon.get(), &recBtn, &resetBtn, &holdBtn }) c->setVisible (roomRight);
    if (roomRight)
    {
        waveIcon->setBounds (f.removeFromRight (ui::px (52)));
        f.removeFromRight (ui::px (6));
        recBtn.setBounds (f.removeFromRight (ui::px (66)).reduced (0, 3)); f.removeFromRight (ui::px (6));
        resetBtn.setBounds (f.removeFromRight (ui::px (64)).reduced (0, 3)); f.removeFromRight (ui::px (6));
        holdBtn.setBounds (f.removeFromRight (ui::px (64)).reduced (0, 3)); f.removeFromRight (ui::px (8));
    }
    place (f, { { &divBtns[0], 38, 0 }, { &divBtns[1], 38, 0 }, { &divBtns[2], 38, 0 }, { &divBtns[3], 38, 0 }, { &divBtns[4], 38, 0 }, { &divBtns[5], 38, 4 },
                { &tripletBtn, 30, 16 }, { &bpmLabel, 80, 18 } });
    const bool roomGain = f.getWidth() >= ui::px (130 + 92);
    gain.setVisible (roomGain);
    gainText = {};
    if (roomGain) { gain.setBounds (f.removeFromLeft (ui::px (130))); gainText = f.removeFromLeft (ui::px (92)); f.removeFromLeft (ui::px (18)); }
    place (f, { { &bpBtn, 46, 4 }, { bpFreq.get(), 72, host ? 4 : 18 }, { host != nullptr ? &listenBtn : nullptr, 60, 18 }, { &sideBtn, 86, 0 } });
    if (host == nullptr) listenBtn.setVisible (false);
}

void ScopePage::paint (juce::Graphics& g)
{
    g.fillAll (theme.bg);
    g.setColour (theme.text); g.setFont (juce::FontOptions (ui::fs (13.0f)));
    g.drawText ("Gain " + juce::String (s.scopeGainDb >= 0 ? "+" : "") + juce::String (s.scopeGainDb, 1) + " dB", gainText, juce::Justification::centred);
    // the window's length, for reference
    auto info = infoArea;
    if (! info.isEmpty()) g.drawText (juce::String (engine.windowSeconds(), engine.windowSeconds() < 10 ? 2 : 1) + " s  " + juce::String (juce::roundToInt (engine.sampleRate() / 100) / 10.0, 1) + " kHz",
                info, juce::Justification::centredRight);
}
