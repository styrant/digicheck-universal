#include "FftView.h"
#include "../UiScale.h"
#include "../ColourPicker.h"

namespace
{
    const juce::Colour kBgTop (0xff14171c), kBgBottom (0xff0b0c0f), kGrid (0xff23272e), kGridStrong (0xff313741),
                       kLabel (0xffb0b6c0), kText (0xffd6dae1), kButton (0xff2c2f35), kOn (0xff4c8dff);

    const char* kChannels[] = { "Stereo", "Left", "Right", "Mid", "Side" };
    const char* kSecondary[] = { "None", "Left", "Right", "Mid", "Side", "Sidechain" };
    const double kAvg[] = { 0, 50, 100, 300, 1000, 3000 };
    const char* kAvgNames[] = { "Real time", "50 ms", "100 ms", "300 ms", "1 s", "3 s" };
    const double kSlopes[] = { 0, 3, 4.5, 6 };
    const double kSmooth[] = { 0, 1.0 / 24, 1.0 / 12, 1.0 / 6, 1.0 / 3 };
    const char* kSmoothNames[] = { "Off", "1/24 oct", "1/12 oct", "1/6 oct", "1/3 oct" };
    const char* kStyles[] = { "Line", "Filled", "Dashed" };
    struct Preset { const char* name; const char* hex; };
    const Preset kColours[] = { { "Blue (SPAN)", "#5aa9ff" }, { "White (Pro-Q)", "#e8ebf0" }, { "Teal", "#3ec7c7" },
                                { "Green", "#4cd07d" }, { "Amber", "#ffb000" }, { "Magenta", "#ff4fa3" }, { "Orange", "#ff8a3d" } };

    juce::Colour hex (const juce::String& s)
    {
        auto h = s.trim().trimCharactersAtStart ("#");
        if (h.length() == 6) h = "ff" + h;
        return juce::Colour ((juce::uint32) h.getHexValue64());
    }
    juce::String toHex (juce::Colour c) { return "#" + c.toDisplayString (false); }

    juce::String noteName (double hz, int ref)
    {
        if (hz <= 0 || ref <= 0) return {};
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const double midi = 69 + 12 * std::log2 (hz / ref);
        const int n = (int) std::lround (midi);
        const int cents = (int) std::lround ((midi - n) * 100);
        return juce::String (names[((n % 12) + 12) % 12]) + juce::String (n / 12 - 1) + (cents >= 0 ? " +" : " ") + juce::String (cents) + "c";
    }
}

FftView::FftView (FifoTap& in, Settings& st, std::function<void()> ch) : input (in), s (st), changed (std::move (ch))
{
    setOpaque (true);
    int i = 0;
    for (auto* b : { &chanBtn, &secBtn, &sizeBtn, &avgBtn, &slopeBtn, &smoothBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, kButton);
        const int which = i++;
        b->onClick = [this, b, which] { menuFor (*b, which); };
        addAndMakeVisible (b);
    }
    for (auto* b : { &holdBtn, &freezeBtn, &colourBtn, &snapBtn })
    {
        b->setColour (juce::TextButton::buttonColourId, kButton);
        b->setColour (juce::TextButton::buttonOnColourId, kOn);
        addAndMakeVisible (b);
    }
    holdBtn.setClickingTogglesState (false);
    holdBtn.setTooltip ("Peak hold line (click the display to reset it)");
    holdBtn.onClick = [this] { s.fftHold = ! s.fftHold; engine.resetHold(); updateButtons(); if (changed) changed(); };
    freezeBtn.setTooltip ("Freeze the display (F, or Space)");
    freezeBtn.onClick = [this] { toggleFreeze(); };
    colourBtn.onClick = [this] { colourMenu(); };
    snapBtn.setClickingTogglesState (false);
    snapBtn.setTooltip ("Snapshot: keep the current spectrum as a ghost line, to compare before and after a change. Click again to clear it");
    snapBtn.onClick = [this]
    {
        if (snapPower.empty()) snapPower = engine.power (0); else snapPower.clear();
        snapBtn.setToggleState (! snapPower.empty(), juce::dontSendNotification);
        snapBtn.setButtonText (snapPower.empty() ? "Snapshot" : "Clear snap");
        repaint();
    };
    configure();
}

FftView::~FftView() { input.setActive (false); }

void FftView::configure()
{
    engine.configure (input.sampleRate(), s.fftOrder);
    applySettings();
}

void FftView::applySettings()
{
    if (engine.order() != s.fftOrder) engine.configure (input.sampleRate(), s.fftOrder);
    engine.setAveraging (s.fftAvgMs);
    engine.setSeries (0, true, 0, s.fftChannel);
    const int sec = s.fftSecondary;
    if (sec == 5) engine.setSeries (1, scAvailable, 1, fft::Stereo);
    else          engine.setSeries (1, sec > 0, 0, sec - 1);
    updateButtons();
    computeEdges();
    repaint();
}

void FftView::setActive (bool shown)
{
    active = shown;
    input.setActive (shown);
    if (shown) applySettings();
    lastT = 0;
}

void FftView::onFrame (double t)
{
    if (! active) return;
    const double dt = lastT > 0 ? juce::jlimit (0.0, 0.25, t - lastT) : 0.0;
    lastT = t;
    if (std::abs (input.sampleRate() - engine.sampleRate()) > 0.5) configure();
    if (frozen) { input.drain ([] (const float*, const float*, int) {}); return; }
    input.drainAll ([this] (const float* l, const float* r, const float* sl, const float* sr, const float*, int n)
    {
        engine.push (0, l, r, n);
        if (sl != nullptr && engine.seriesOn (1)) engine.push (1, sl, sr, n);
    });
    engine.decayHold (dt, 12.0);
    if (isShowing()) repaint (plot);
}

void FftView::updateButtons()
{
    chanBtn.setButtonText (juce::String (kChannels[s.fftChannel]) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    secBtn.setButtonText ((s.fftSecondary == 0 ? juce::String ("+ Curve") : "+ " + juce::String (kSecondary[s.fftSecondary])) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    sizeBtn.setButtonText (juce::String (1 << s.fftOrder) + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    int ai = 0; for (int i = 0; i < 6; ++i) if (std::abs (kAvg[i] - s.fftAvgMs) < 1) ai = i;
    avgBtn.setButtonText (juce::String ("Avg ") + kAvgNames[ai] + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    slopeBtn.setButtonText (juce::String (s.fftSlope, s.fftSlope == std::floor (s.fftSlope) ? 0 : 1) + " dB/oct" + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    smoothBtn.setButtonText (juce::String ("Smooth ") + kSmoothNames[s.fftSmooth] + juce::String::fromUTF8 (" \xe2\x96\xbe"));
    holdBtn.setToggleState (s.fftHold, juce::dontSendNotification);
    freezeBtn.setToggleState (frozen, juce::dontSendNotification);
    chanBtn.setTooltip ("Which channel the main curve shows");
    secBtn.setTooltip ("A second curve on top, in its own colour and style" + juce::String (scAvailable ? " (Sidechain: the plugin's sidechain input)" : ""));
    sizeBtn.setTooltip ("FFT size: larger resolves low notes better, smaller reacts faster");
    slopeBtn.setTooltip ("Tilt in dB per octave around 1 kHz (4.5 makes a balanced mix look flat)");
    avgBtn.setTooltip ("Averaging: how quickly the curve follows the music (longer is steadier)");
    smoothBtn.setTooltip ("Smoothing across frequency: wider bands give a calmer, easier-to-read curve");
    colourBtn.setTooltip ("Colours of the main and second curves");
}

void FftView::menuFor (juce::TextButton& b, int which)
{
    juce::PopupMenu m;
    auto apply = [this] { applySettings(); if (changed) changed(); };
    switch (which)
    {
        case 0: for (int i = 0; i < 5; ++i) m.addItem (kChannels[i], true, s.fftChannel == i, [this, i, apply] { s.fftChannel = i; apply(); }); break;
        case 1:
            for (int i = 0; i < (scAvailable ? 6 : 5); ++i)           // Sidechain only where there is one (VST3)
                m.addItem (i == 0 ? juce::String ("No second curve") : (i == 5 ? juce::String ("Sidechain") : juce::String (kSecondary[i])),
                           true, s.fftSecondary == i, [this, i, apply] { s.fftSecondary = i; apply(); });
            m.addSeparator();
            for (int i = 0; i < 3; ++i) m.addItem (juce::String ("Style: ") + kStyles[i], true, s.fftSecStyle == i, [this, i, apply] { s.fftSecStyle = i; apply(); });
            m.addItem ("Colour...", [this] { juce::CallOutBox::launchAsynchronously (std::make_unique<LiveColourPicker> (hex (s.fftSecColour), [this] (juce::Colour c)
                                                 { s.fftSecColour = toHex (c); repaint(); if (changed) changed(); }), secBtn.getScreenBounds(), nullptr); });
            break;
        case 2: for (int o = 11; o <= 15; ++o) m.addItem (juce::String (1 << o) + " points (" + juce::String (engine.sampleRate() / (1 << o), 1) + " Hz bins)",
                                                         true, s.fftOrder == o, [this, o, apply] { s.fftOrder = o; apply(); }); break;
        case 3: for (int i = 0; i < 6; ++i) m.addItem (kAvgNames[i], true, std::abs (kAvg[i] - s.fftAvgMs) < 1, [this, i, apply] { s.fftAvgMs = kAvg[i]; apply(); }); break;
        case 4: for (double v : kSlopes) m.addItem (juce::String (v, v == std::floor (v) ? 0 : 1) + " dB/oct" + (v == 4.5 ? "  (pink noise looks flat)" : (v == 0 ? "  (white noise looks flat)" : "")),
                                                    true, std::abs (s.fftSlope - v) < 0.01, [this, v, apply] { s.fftSlope = v; apply(); }); break;
        case 5: for (int i = 0; i < 5; ++i) m.addItem (kSmoothNames[i], true, s.fftSmooth == i, [this, i, apply] { s.fftSmooth = i; apply(); }); break;
        default: break;
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&b));
}

void FftView::colourMenu()
{
    juce::PopupMenu m, mainC, secC, style;
    for (auto& p : kColours) mainC.addItem (p.name, true, s.fftColour.equalsIgnoreCase (p.hex), [this, p] { s.fftColour = p.hex; repaint(); if (changed) changed(); });
    mainC.addItem ("Custom...", [this] { juce::CallOutBox::launchAsynchronously (std::make_unique<LiveColourPicker> (hex (s.fftColour), [this] (juce::Colour c)
                                          { s.fftColour = toHex (c); repaint(); if (changed) changed(); }), colourBtn.getScreenBounds(), nullptr); });
    for (auto& p : kColours) secC.addItem (p.name, true, s.fftSecColour.equalsIgnoreCase (p.hex), [this, p] { s.fftSecColour = p.hex; repaint(); if (changed) changed(); });
    secC.addItem ("Custom...", [this] { juce::CallOutBox::launchAsynchronously (std::make_unique<LiveColourPicker> (hex (s.fftSecColour), [this] (juce::Colour c)
                                         { s.fftSecColour = toHex (c); repaint(); if (changed) changed(); }), colourBtn.getScreenBounds(), nullptr); });
    for (int i = 0; i < 3; ++i) style.addItem (kStyles[i], true, s.fftSecStyle == i, [this, i] { s.fftSecStyle = i; repaint(); if (changed) changed(); });
    m.addSubMenu ("Main curve", mainC);
    m.addSubMenu ("Second curve / sidechain", secC);
    m.addSubMenu ("Second curve style", style);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&colourBtn));
}

void FftView::resized()
{
    auto r = getLocalBounds();
    header = r.removeFromTop (ui::px (36)).reduced (ui::px (8), ui::px (5));
    plot = r.reduced (ui::px (8), 0).withTrimmedBottom (ui::px (6)).withTrimmedLeft (ui::px (34)).withTrimmedBottom (ui::px (16));
    auto h = header.withTrimmedLeft (headerInset);
    colourBtn.setBounds (header.removeFromRight (ui::px (72)));
    h.setRight (colourBtn.getX() - ui::px (6));
    // narrow windows: the least used go first (drop order), the rest close up
    struct Item { juce::Component* c; int w, gap, drop; };
    Item items[] = { { &chanBtn, 78, 4, 0 }, { &secBtn, 104, 12, 0 }, { &sizeBtn, 74, 4, 5 }, { &avgBtn, 112, 4, 4 }, { &slopeBtn, 96, 4, 3 },
                     { &smoothBtn, 120, 12, 2 }, { &holdBtn, 52, 4, 0 }, { &freezeBtn, 60, 4, 0 }, { &snapBtn, 80, 0, 1 } };
    auto total = [&] (int level) { int t = 0; for (auto& i : items) if (i.drop == 0 || i.drop > level) t += ui::px (i.w + i.gap); return t; };
    int level = 0;
    while (level < 5 && total (level) > h.getWidth()) ++level;
    for (auto& i : items)
    {
        const bool on = i.drop == 0 || i.drop > level;
        i.c->setVisible (on);
        if (on) { i.c->setBounds (h.removeFromLeft (ui::px (i.w))); h.removeFromLeft (ui::px (i.gap)); }
    }
    computeEdges();
}

void FftView::computeEdges()
{
    const int W = juce::jmax (2, plot.getWidth());
    edges.resize ((size_t) W + 1);
    for (int x = 0; x <= W; ++x) edges[(size_t) x] = (float) hzAtX ((float) (plot.getX() + x));
}

double FftView::hzAtX (float x) const
{
    const double hi = juce::jmin (20000.0, engine.sampleRate() * 0.5);
    return 20.0 * std::pow (hi / 20.0, (double) (x - plot.getX()) / juce::jmax (1, plot.getWidth()));
}
float FftView::xOfHz (double hz) const
{
    const double hi = juce::jmin (20000.0, engine.sampleRate() * 0.5);
    return plot.getX() + (float) (std::log (hz / 20.0) / std::log (hi / 20.0)) * plot.getWidth();
}
float FftView::yOfDb (double db) const
{
    return plot.getY() + (float) ((s.fftTop - db) / (s.fftTop - s.fftBottom)) * plot.getHeight();
}

juce::Path FftView::curvePath (const std::vector<float>& db, bool closed) const
{
    juce::Path p;
    const int n = (int) db.size();
    if (n == 0) return p;
    const float bottom = (float) plot.getBottom() + 2;
    auto y = [&] (int i) { return juce::jlimit ((float) plot.getY() - 2, bottom, yOfDb (db[(size_t) i])); };
    const float x0 = (float) plot.getX();
    if (closed) { p.startNewSubPath (x0, bottom); p.lineTo (x0, y (0)); }
    else p.startNewSubPath (x0, y (0));
    // a point every column, quadratic through the midpoints: smooth without overshooting peaks
    for (int i = 1; i < n; ++i)
    {
        const float xm = x0 + i - 0.5f, ym = (y (i - 1) + y (i)) * 0.5f;
        p.quadraticTo (x0 + i - 1 + 0.5f, y (i - 1), xm, ym);
    }
    p.lineTo (x0 + n, y (n - 1));
    if (closed) { p.lineTo (x0 + n, bottom); p.closeSubPath(); }
    return p;
}

void FftView::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (kBgTop, 0, 0, kBgBottom, 0, (float) getHeight(), false));
    g.fillAll();
    g.setFont (juce::FontOptions (ui::fs (10.5f)));

    // grid: frequencies
    for (double f : { 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 200.0, 300.0, 400.0, 500.0, 600.0, 700.0, 800.0, 900.0,
                      1000.0, 2000.0, 3000.0, 4000.0, 5000.0, 6000.0, 7000.0, 8000.0, 9000.0, 10000.0, 20000.0 })
    {
        const float x = xOfHz (f);
        if (x < plot.getX() - 1 || x > plot.getRight() + 1) continue;
        const bool major = f == 100 || f == 1000 || f == 10000 || f == 20 || f == 20000;
        g.setColour (major ? kGridStrong : kGrid);
        g.drawVerticalLine ((int) x, (float) plot.getY(), (float) plot.getBottom());
        if (major || f == 50 || f == 200 || f == 500 || f == 2000 || f == 5000)
        {
            g.setColour (kLabel);
            g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String ((int) f), (int) x - 20, plot.getBottom() + 2, 40, 14, juce::Justification::centred);
        }
    }
    // grid: levels
    const double span = s.fftTop - s.fftBottom, step = span > 72 ? 12 : (span > 36 ? 6 : 3);
    for (double d = std::ceil (s.fftBottom / step) * step; d <= s.fftTop; d += step)
    {
        const float y = yOfDb (d);
        g.setColour (d == 0 ? kGridStrong : kGrid);
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
        g.setColour (kLabel);
        g.drawText (juce::String ((int) d), plot.getX() - 34, (int) y - 7, 30, 14, juce::Justification::centredRight);
    }

    const double smooth = kSmooth[s.fftSmooth];
    engine.curve (engine.power (0), edges, s.fftSlope, smooth, mainDb);
    const auto mainC = hex (s.fftColour), secC = hex (s.fftSecColour);

    g.saveState();
    g.reduceClipRegion (plot);
    // second curve behind when filled, else on top (see below)
    const bool sec = engine.seriesOn (1);
    if (sec) engine.curve (engine.power (1), edges, s.fftSlope, smooth, secDb);
    auto drawSecondary = [&]
    {
        if (! sec) return;
        if (s.fftSecStyle == 1)
        {
            g.setGradientFill (juce::ColourGradient (secC.withAlpha (0.45f), 0, (float) plot.getY(), secC.withAlpha (0.08f), 0, (float) plot.getBottom(), false));
            g.fillPath (curvePath (secDb, true));
            g.setColour (secC); g.strokePath (curvePath (secDb, false), juce::PathStrokeType (1.4f));
        }
        else
        {
            auto p = curvePath (secDb, false);
            g.setColour (kBgBottom.withAlpha (0.7f)); g.strokePath (p, juce::PathStrokeType (4.0f));   // a dark halo keeps it apart from the fill
            g.setColour (secC);
            if (s.fftSecStyle == 2)
            {
                juce::Path dashed; const float dashes[] = { 6.0f, 4.0f };
                juce::PathStrokeType (1.8f).createDashedStroke (dashed, p, dashes, 2);
                g.fillPath (dashed);
            }
            else g.strokePath (p, juce::PathStrokeType (1.8f));
        }
    };
    if (sec && s.fftSecStyle == 1) drawSecondary();

    // main: gradient fill and outline
    g.setGradientFill (juce::ColourGradient (mainC.withAlpha (0.42f), 0, (float) plot.getY(), mainC.withAlpha (0.04f), 0, (float) plot.getBottom(), false));
    g.fillPath (curvePath (mainDb, true));
    g.setColour (mainC.withAlpha (0.25f)); g.strokePath (curvePath (mainDb, false), juce::PathStrokeType (3.5f));   // soft glow
    g.setColour (mainC.brighter (0.25f)); g.strokePath (curvePath (mainDb, false), juce::PathStrokeType (1.5f));
    if (s.fftHold)
    {
        engine.curve (engine.held (0), edges, s.fftSlope, smooth, holdDb);
        g.setColour (juce::Colours::white.withAlpha (0.45f));
        g.strokePath (curvePath (holdDb, false), juce::PathStrokeType (1.0f));
    }
    if (sec && s.fftSecStyle != 1) drawSecondary();
    refDb.clear();
    if (refShown && reference != nullptr && reference->ready())
    {
        // the reference's long-term spectrum (orange): its loudness-matched level when matching is on
        const auto& pw = reference->fftPower (engine.order(), engine.binHz(), engine.bins());   // empty while computing
        if (! pw.empty())
        {
            engine.curve (pw, edges, s.fftSlope, smooth, refDb);
            if (const float gDb = reference->matchGainDb(); gDb != 0.0f) for (auto& v : refDb) v += gDb;   // matched loudness
            auto p = curvePath (refDb, false);
            g.setColour (kBgBottom.withAlpha (0.6f)); g.strokePath (p, juce::PathStrokeType (3.5f));
            g.setColour (juce::Colour (0xffff9f43)); g.strokePath (p, juce::PathStrokeType (1.6f));
        }
    }
    if (! snapPower.empty() && snapPower.size() != engine.power (0).size()) snapPower.clear();   // FFT size changed
    if (! snapPower.empty())
    {
        // the snapshot: a light dashed line with a dark halo, drawn with today's slope and smoothing
        engine.curve (snapPower, edges, s.fftSlope, smooth, snapDb);
        auto p = curvePath (snapDb, false);
        g.setColour (kBgBottom.withAlpha (0.6f)); g.strokePath (p, juce::PathStrokeType (3.5f));
        juce::Path dashed; const float dashes[] = { 3.0f, 3.0f };
        juce::PathStrokeType (1.4f).createDashedStroke (dashed, p, dashes, 2);
        g.setColour (juce::Colour (0xffe9e3c9)); g.fillPath (dashed);
    }
    else snapDb.clear();
    g.restoreState();

    // hover readout; with a pin (Alt+click) it stays at the pinned frequency and level instead
    const int hoverX = pin.has_value() ? juce::roundToInt (xOfHz (pin->first)) : this->hoverX;
    const int hoverY = pin.has_value() ? juce::roundToInt (yOfDb (pin->second)) : this->hoverY;
    if (hoverX >= plot.getX() && hoverX < plot.getRight() && ! mainDb.empty())
    {
        const int i = juce::jlimit (0, (int) mainDb.size() - 1, hoverX - plot.getX());
        const double hz = hzAtX ((float) hoverX + 0.5f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawVerticalLine (hoverX, (float) plot.getY(), (float) plot.getBottom());
        // the horizontal line: the level at the cursor height
        if (hoverY >= plot.getY() && hoverY <= plot.getBottom())
        {
            const double db = s.fftTop - (hoverY - plot.getY()) / (double) plot.getHeight() * (s.fftTop - s.fftBottom);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            for (float x = (float) plot.getX(); x < plot.getRight(); x += 6) g.drawLine (x, (float) hoverY, juce::jmin ((float) plot.getRight(), x + 3), (float) hoverY, 1.0f);
            g.setFont (juce::FontOptions (ui::fs (12.0f)));
            const auto t = juce::String (db, 1) + " dB";
            const int w = ui::px (64);
            auto box = juce::Rectangle<int> (plot.getX() + 4, juce::jlimit (plot.getY(), plot.getBottom() - ui::px (18), hoverY - ui::px (9)), w, ui::px (18));
            g.setColour (juce::Colour (0xe0101215)); g.fillRoundedRectangle (box.toFloat(), 4);
            g.setColour (kText); g.drawText (t, box, juce::Justification::centred);
        }
        juce::String t = (hz >= 1000 ? juce::String (hz / 1000, 2) + " kHz" : juce::String (hz, hz < 100 ? 1 : 0) + " Hz") + "   " + noteName (hz, s.noteRef)
                         + "   " + juce::String (mainDb[(size_t) i], 1) + " dB";
        if (sec && i < (int) secDb.size()) t << "   " << juce::String (secDb[(size_t) i], 1) << " dB";
        if (i < (int) refDb.size()) t << "   ref " << juce::String (refDb[(size_t) i], 1);
        if (i < (int) snapDb.size()) t << "   (snapshot " << (mainDb[(size_t) i] >= snapDb[(size_t) i] ? "+" : "") << juce::String (mainDb[(size_t) i] - snapDb[(size_t) i], 1) << ")";
        g.setFont (juce::FontOptions (ui::fs (12.0f)));
        const int w = ui::px (snapDb.empty() ? 360 : 460);
        auto box = juce::Rectangle<int> (juce::jlimit (plot.getX(), plot.getRight() - w, hoverX + 10), plot.getY() + 6, w, ui::px (22));
        g.setColour (juce::Colour (0xe0101215)); g.fillRoundedRectangle (box.toFloat(), 4);
        g.setColour (kText); g.drawText (t, box.reduced (8, 0), juce::Justification::centredLeft);
    }
    if (frozen) { g.setColour (kOn); g.setFont (juce::FontOptions (ui::fs (12.0f), juce::Font::bold)); g.drawText ("FROZEN", plot.withHeight (20).removeFromRight (80), juce::Justification::centred); }   // a copy: never shrink the plot itself
    // legend
    if (sec)
    {
        g.setFont (juce::FontOptions (ui::fs (11.5f)));
        auto lg = juce::Rectangle<int> (plot.getRight() - ui::px (220), plot.getBottom() - ui::px (24), ui::px (214), ui::px (18));
        g.setColour (mainC); g.fillRect (lg.removeFromLeft (14).withSizeKeepingCentre (14, 3));
        g.setColour (kText); g.drawText (" " + juce::String (kChannels[s.fftChannel]), lg.removeFromLeft (80), juce::Justification::centredLeft);
        g.setColour (secC); g.fillRect (lg.removeFromLeft (14).withSizeKeepingCentre (14, 3));
        g.setColour (kText); g.drawText (" " + juce::String (kSecondary[s.fftSecondary == 0 ? 5 : s.fftSecondary]), lg, juce::Justification::centredLeft);
    }
}

void FftView::mouseMove (const juce::MouseEvent& e) { hoverX = e.x; hoverY = e.y; repaint (plot.expanded (40, 4)); }
void FftView::mouseExit (const juce::MouseEvent&)    { hoverX = hoverY = -1; repaint(); }
void FftView::mouseDown (const juce::MouseEvent& e)
{
    // Alt+click: pin the readout here (Alt+click on the pin again removes it)
    if (e.mods.isAltDown() && ! e.mods.isPopupMenu())
    {
        if (pin.has_value() && juce::Point<float> (xOfHz (pin->first), yOfDb (pin->second)).getDistanceFrom (e.position) < 8.0f) pin.reset();
        else if (plot.contains (e.getPosition()))
        {
            const double db = s.fftTop - (e.position.y - plot.getY()) / (double) plot.getHeight() * (s.fftTop - s.fftBottom);
            pin = std::make_pair (hzAtX (e.position.x), db);
        }
        repaint();
        return;
    }
    if (plot.contains (e.getPosition()) && ! e.mods.isPopupMenu()) engine.resetHold(); if (e.mods.isPopupMenu()) colourMenu(); }
void FftView::mouseDoubleClick (const juce::MouseEvent&) { s.fftTop = 6; s.fftBottom = -90; repaint(); if (changed) changed(); }

void FftView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const double d = w.deltaY > 0 ? 1 : -1;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())        // zoom the dB range around the middle
    {
        const double mid = (s.fftTop + s.fftBottom) / 2, half = juce::jlimit (12.0, 90.0, (s.fftTop - s.fftBottom) / 2 * (d > 0 ? 0.85 : 1.18));
        s.fftTop = mid + half; s.fftBottom = mid - half;
    }
    else { s.fftTop = juce::jlimit (-60.0, 30.0, s.fftTop + d * 3); s.fftBottom = s.fftBottom + d * 3; }
    repaint();
    if (changed) changed();
}

void FftView::loadForTest (const juce::File& f, const juce::File* scFile)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto run = [&] (const juce::File& file, int inputIndex)
    {
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
        if (rd == nullptr) return;
        if (std::abs (rd->sampleRate - engine.sampleRate()) > 0.5) { engine.configure (rd->sampleRate, s.fftOrder); applySettings(); }
        const int len = (int) juce::jmin<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 20));
        juce::AudioBuffer<float> buf (2, len);
        rd->read (&buf, 0, len, 0, true, true);
        if (rd->numChannels == 1) buf.copyFrom (1, 0, buf, 0, 0, len);
        engine.push (inputIndex, buf.getReadPointer (0), buf.getReadPointer (1), len);
    };
    run (f, 0);
    if (scFile != nullptr) { engine.setSeries (1, true, 1, fft::Stereo); run (*scFile, 1); }
    frozen = true;
    updateButtons();
    repaint();
}
