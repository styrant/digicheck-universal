#include "BalanceView.h"
#include "../UiScale.h"
#include <cstdio>
#include <utility>

namespace
{
    const juce::Colour kBg { 0xff111214 }, kPanel { 0xff17191d }, kGrid { 0xff2a2c31 }, kText { 0xffe8e8e8 }, kMuted { 0xffb0b6c0 },
                       kRange { 0xff4c8dff }, kGreen { 0xff3ddc84 }, kAmber { 0xfff0b429 }, kRed { 0xffff5252 }, kTrack { 0xff202227 };
    const char* kBandNames[4] = { "Low", "Low-mid", "High-mid", "High" };
    const char* kBandFreqs[4] = { "0 - 250 Hz", "250 Hz - 2 kHz", "2 - 8 kHz", "8 kHz +" };
    constexpr double kBandSpanDb = 12.0;                  // the band meters show +/-12 dB around the range centre

    // a band share relative to the centre of the target's range for that band, in dB
    double bandDb (double share, double lo, double hi) { return 20 * std::log10 (std::max (share, 1e-12) / std::sqrt (lo * hi)); }
    double halfRangeDb (double lo, double hi)          { return 10 * std::log10 (hi / lo); }
    juce::Colour judge (double out) { return std::abs (out) < 1e-9 ? kGreen : (std::abs (out) < 1.5 ? kAmber : kRed); }

    bool paintLogEnabled() { static const bool on = juce::SystemStats::getEnvironmentVariable ("SPECTROSCOPE_PAINT_LOG", {}).isNotEmpty(); return on; }
    int binAt (double hz) { return juce::jlimit (1, tonal::kBins - 1, (int) std::lround (hz / (tonal::kSampleRate / tonal::kFftSize))); }
    float binPos (double hz) { return (float) juce::jlimit (1.0, (double) tonal::kBins - 1.001, hz / (tonal::kSampleRate / tonal::kFftSize)); }
    // a curve value between bins (the low end has few bins per pixel; interpolating avoids steps)
    // (Catmull-Rom between bins, so the curve has no corners at the bins)
    double at (const std::vector<double>& v, float pos)
    {
        const int n = (int) v.size();
        const int i = juce::jlimit (0, n - 2, (int) pos); const double f = pos - (float) i;
        const double p0 = v[(size_t) juce::jmax (0, i - 1)], p1 = v[(size_t) i], p2 = v[(size_t) i + 1], p3 = v[(size_t) juce::jmin (n - 1, i + 2)];
        return p1 + 0.5 * f * (p2 - p0 + f * (2 * p0 - 5 * p1 + 4 * p2 - p3 + f * (3 * (p1 - p2) + p3 - p0)));
    }
}

BalanceView::BalanceView (LiveBalance& l, TargetLibrary& lib) : live (l), library (lib)
{
    setOpaque (true);
    for (auto* b : { &openFolder, &openBuilder }) addChildComponent (b);
    openFolder.onClick = [this] { if (onOpenFolder) onOpenFolder(); };
    openBuilder.onClick = [this] { if (onOpenBuilder) onOpenBuilder(); };
    shownCurve.assign ((size_t) tonal::kBins, -120.0);
}

void BalanceView::setActive (bool a)
{
    active = a;
    if (a) { if (library.rescanIfChanged()) smoothedFor = -2; startTimerHz (2); timerCallback(); lastFrameTime = 0; }
    else stopTimer();
}

void BalanceView::setTargetIndex (int i)
{
    targetIndex = (i >= 0 && i < (int) library.entries().size()) ? i : -1;
    smoothedFor = -2;
    shownValid = false;                                   // band values are relative to the target: start fresh
    invalidateStatic();
}

void BalanceView::setDifferenceMode (bool d)    { difference = d; invalidateStatic(); }
void BalanceView::setInputRunning (bool r)       { if (r != running) { running = r; repaint(); } }

void BalanceView::setSmoothing (int i)
{
    if (i == smoothing) return;
    smoothing = i; smoothedFor = -2;
    live.setSmoothing (i);
    invalidateStatic();
}

const tonal::Target* BalanceView::target() const
{
    if (targetIndex < 0 || targetIndex >= (int) library.entries().size()) return nullptr;
    const auto& t = library.entries()[(size_t) targetIndex].target;
    // target files hold a 1/3-octave curve and range; smoother settings smooth them the rest of the way
    const double extra = std::sqrt (std::max (0.0, juce::square (tonal::smoothingOctaves (smoothing)) - 1.0 / 9.0));
    if (extra < 1e-6 || t.mean.size() != (size_t) tonal::kBins) return &t;
    if (smoothedFor != targetIndex)
    {
        auto& st = const_cast<BalanceView*> (this)->smoothedTarget;
        st = t;
        st.mean = tonal::smoothDbBell (t.mean, extra);
        st.low  = tonal::smoothDbBell (t.low, extra);
        st.high = tonal::smoothDbBell (t.high, extra);
        const_cast<BalanceView*> (this)->smoothedFor = targetIndex;
    }
    return &smoothedTarget;
}

void BalanceView::timerCallback()
{
    if (++tick % 4 == 0 && library.rescanIfChanged()) { smoothedFor = -2; if (onTargetsChanged) onTargetsChanged(); }     // every 2 s
    const bool empty = library.entries().empty();
    if (openFolder.isVisible() != empty) { openFolder.setVisible (empty); openBuilder.setVisible (empty && builderAvailable); resized(); repaint(); }
}

//==============================================================================
void BalanceView::onFrame (double t)
{
    if (! active || ! isShowing()) return;
    const double dt = lastFrameTime > 0 ? juce::jlimit (0.0, 0.1, t - lastFrameTime) : 1.0;
    lastFrameTime = t;

    const auto s = live.getSnapshot();
    if (s.serial != lastSerial) { lastSerial = s.serial; latest = s; }

    bool changed = false;
    if (latest.valid && latest.smooth.size() == (size_t) tonal::kBins)
    {
        // glide: about 40 ms to settle in the fast modes, 150 ms otherwise (first values jump straight in)
        const double tau = LiveBalance::isFast (latest.window) ? 0.04 : 0.15;
        const double a = shownValid ? 1.0 - std::exp (-dt / tau) : 1.0;
        double moved = 0;
        for (int k = 0; k < tonal::kBins; ++k)
        {
            const double d = (latest.smooth[(size_t) k] - shownCurve[(size_t) k]) * a;
            shownCurve[(size_t) k] += d;
            moved = std::max (moved, std::abs (d));
        }
        if (const auto* tg = target())
            for (int b = 0; b < 4; ++b)
            {
                const double want = bandDb (latest.shares[(size_t) b], tg->regions[(size_t) b][0], tg->regions[(size_t) b][1]);
                const double d = (want - shownBandDb[(size_t) b]) * a;
                shownBandDb[(size_t) b] += d;
                moved = std::max (moved, std::abs (d));
            }
        changed = moved > 0.005 || ! shownValid || shownSeconds != latest.seconds;
        shownValid = true;
        shownSeconds = latest.seconds;
    }
    else if (shownValid && ! latest.valid)
    {
        shownValid = false;
        changed = true;
    }
    if (changed) { repaint (bandArea); repaint (curveArea); }
}

//==============================================================================
void BalanceView::resized()
{
    auto r = getLocalBounds().reduced (ui::px (12), ui::px (10));
    const int bw = juce::jlimit (ui::px (260), ui::px (420), r.getWidth() / 3);
    bandArea = r.removeFromLeft (bw);
    r.removeFromLeft (ui::px (12));
    curveArea = r;

    auto c = r.reduced (ui::px (14), ui::px (10));
    c.removeFromTop (ui::px (24));
    plot = c.withTrimmedLeft (ui::px (34)).withTrimmedBottom (ui::px (16));

    // one curve point every 2 px: the nearest spectrum bin to each column's frequency
    columns.clear();
    for (int x = plot.getX(); x <= plot.getRight(); x += 2)
        columns.push_back ({ (float) x, binPos (hzAt ((float) x)) });

    if (openBuilder.isVisible())
    {
        auto b = curveArea.withSizeKeepingCentre (ui::px (420), ui::px (34)).translated (0, ui::px (60));
        openFolder.setBounds (b.removeFromLeft (ui::px (200)));
        b.removeFromLeft (ui::px (20));
        openBuilder.setBounds (b);
    }
    else
        openFolder.setBounds (curveArea.withSizeKeepingCentre (ui::px (200), ui::px (34)).translated (0, ui::px (60)));
    staticDirty = true;
}

float BalanceView::xOf (double hz) const
{
    return (float) plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * (float) plot.getWidth();
}

double BalanceView::hzAt (float x) const
{
    return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) (x - (float) plot.getX()) / juce::jmax (1, plot.getWidth())));
}

float BalanceView::yOf (double db) const
{
    return (float) plot.getY() + (float) juce::jlimit (-0.02, 1.02, (top - db) / span) * (float) plot.getHeight();
}

double BalanceView::baseAt (float pos) const
{
    const auto* t = target();
    return (difference && t != nullptr) ? at (t->mean, pos) : 0.0;
}

//==============================================================================
void BalanceView::renderStatic()
{
    staticDirty = false;
    staticScale = juce::jmax (1.0f, (float) juce::Component::getApproximateScaleFactorForComponent (this));
    const int w = juce::jmax (1, juce::roundToInt ((float) getWidth() * staticScale)), h = juce::jmax (1, juce::roundToInt ((float) getHeight() * staticScale));
    if (staticLayer.isNull() || staticLayer.getWidth() != w || staticLayer.getHeight() != h) staticLayer = juce::Image (juce::Image::RGB, w, h, false);
    juce::Graphics g (staticLayer);
    g.addTransform (juce::AffineTransform::scale (staticScale));
    paintStatic (g);
}

void BalanceView::paintStatic (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRoundedRectangle (bandArea.toFloat(), 6.0f);
    g.fillRoundedRectangle (curveArea.toFloat(), 6.0f);
    const auto* t = target();

    // ---- band meters: tracks, labels and the target's zones
    {
        auto r = bandArea.reduced (ui::px (14), ui::px (10));
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (12.0f), juce::Font::bold));
        g.drawText ("BANDS", r.removeFromTop (ui::px (18)), juce::Justification::centredLeft);
        r.removeFromTop (ui::px (6));
        auto labels = r.removeFromBottom (ui::px (44));
        const int w = r.getWidth() / 4;
        for (int b = 0; b < 4; ++b)
        {
            auto col = juce::Rectangle<int> (r.getX() + b * w, r.getY(), w, r.getHeight()).reduced (ui::px (8), 0);
            auto lab = juce::Rectangle<int> (r.getX() + b * w, labels.getY(), w, labels.getHeight());
            auto track = col.withSizeKeepingCentre (juce::jmin (col.getWidth(), ui::px (46)), col.getHeight()).withTrimmedTop (ui::px (22)).toFloat();
            g.setColour (kTrack); g.fillRoundedRectangle (track, 4.0f);
            g.setColour (kText); g.setFont (juce::FontOptions (ui::fs (13.5f), juce::Font::bold));
            g.drawText (kBandNames[b], lab.removeFromTop (lab.getHeight() / 2), juce::Justification::centred);
            g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (11.0f)));
            g.drawText (kBandFreqs[b], lab, juce::Justification::centred);
            if (t == nullptr) continue;
            const double half = halfRangeDb (t->regions[(size_t) b][0], t->regions[(size_t) b][1]);
            const float off = (float) juce::jlimit (-1.0, 1.0, half / kBandSpanDb) * track.getHeight() * 0.5f;
            const float yTop = track.getCentreY() - off, yBot = track.getCentreY() + off;
            g.setColour (kRange.withAlpha (0.30f)); g.fillRect (juce::Rectangle<float>::leftTopRightBottom (track.getX(), yTop, track.getRight(), yBot));
            g.setColour (kRange.withAlpha (0.7f));
            g.drawHorizontalLine ((int) yTop, track.getX(), track.getRight());
            g.drawHorizontalLine ((int) yBot, track.getX(), track.getRight());
        }
    }

    // ---- curve panel: scale, grid, labels and the target's range
    if (library.entries().empty())
    {
        auto r = curveArea;
        g.setColour (kText); g.setFont (juce::FontOptions (ui::fs (19.0f), juce::Font::bold));
        g.drawText ("No tonal balance targets yet", r.withTrimmedBottom (ui::px (90)), juce::Justification::centred);
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (14.0f)));
        g.drawFittedText ("Put target files (.json) in " + TargetLibrary::folder().getFullPathName()
                          + "\nor make your own from songs with Target Builder.", r.withTrimmedBottom (ui::px (10)).withTrimmedTop (ui::px (10)),
                          juce::Justification::centred, 3);
        return;
    }

    if (difference) { top = 12; span = 24; }
    else
    {
        top = -1e9;
        if (t != nullptr)
            for (int k = 1; k < tonal::kBins; ++k)
                if (tonal::binFrequency (k) >= 20 && tonal::binFrequency (k) <= 20000) top = std::max (top, t->high[(size_t) k]);
        if (top < -1e8) top = -30;
        top = std::ceil (top / 5) * 5 + 5;
        span = 60;
    }

    auto head = curveArea.reduced (ui::px (14), ui::px (10)).removeFromTop (ui::px (18));
    g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (12.0f), juce::Font::bold));
    const auto name = targetIndex >= 0 ? library.entries()[(size_t) targetIndex].name : juce::String ("no target selected");
    g.drawText ((difference ? "DIFFERENCE FROM " : "CURVE VS ") + name.toUpperCase(), head, juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (ui::fs (11.0f)));
    for (double f : { 30.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xOf (f);
        g.setColour (kGrid); g.drawVerticalLine ((int) x, (float) plot.getY(), (float) plot.getBottom());
        g.setColour (kMuted);
        g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String ((int) f), (int) x - 20, plot.getBottom() + 2, 40, ui::px (14), juce::Justification::centred);
    }
    for (double d = top; d >= top - span - 1e-9; d -= (difference ? 3 : 10))
    {
        const float y = yOf (d);
        g.setColour (difference && std::abs (d) < 0.01 ? kMuted : kGrid);
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
        g.setColour (kMuted);
        g.drawText ((difference && d > 0 ? "+" : "") + juce::String ((int) d), plot.getX() - ui::px (36), (int) y - 7, ui::px (32), 14, juce::Justification::centredRight);
    }
    for (double f : { 250.0, 2000.0, 8000.0 })                        // band edges
    {
        const float x = xOf (f);
        g.setColour (kRange.withAlpha (0.35f));
        for (float y = (float) plot.getY(); y < (float) plot.getBottom(); y += 8) g.drawVerticalLine ((int) x, y, juce::jmin (y + 4, (float) plot.getBottom()));
    }

    if (t != nullptr)
    {
        juce::Path band, mean;
        std::vector<juce::Point<float>> lows;
        bool first = true;
        for (auto [x, k] : columns)
        {
            const double base = baseAt (k);
            const float yh = yOf (at (t->high, k) - base), ym = yOf (at (t->mean, k) - base);
            if (std::exchange (first, false)) { band.startNewSubPath (x, yh); mean.startNewSubPath (x, ym); }
            else { band.lineTo (x, yh); mean.lineTo (x, ym); }
            lows.push_back ({ x, yOf (at (t->low, k) - base) });
        }
        for (auto it = lows.rbegin(); it != lows.rend(); ++it) band.lineTo (*it);
        band.closeSubPath();
        g.setColour (kRange.withAlpha (0.22f)); g.fillPath (band);
        g.setColour (kRange.withAlpha (0.8f)); g.strokePath (mean, juce::PathStrokeType (1.2f));
    }
}

//==============================================================================
void BalanceView::paint (juce::Graphics& g)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    if (staticDirty || staticLayer.isNull()) renderStatic();
    g.drawImage (staticLayer, getLocalBounds().toFloat());
    paintBandsLive (g);
    if (! library.entries().empty()) paintCurveLive (g);

    if (paintLogEnabled())
    {
        paintMsSum += juce::Time::getMillisecondCounterHiRes() - t0;
        if (++paintCount == 120)
        {
            std::fprintf (stderr, "BALANCE paint %.3f ms avg\n", paintMsSum / paintCount);
            paintMsSum = 0; paintCount = 0;
        }
    }
}

void BalanceView::paintBandsLive (juce::Graphics& g)
{
    const auto* t = target();
    if (t == nullptr || ! shownValid) return;
    auto r = bandArea.reduced (ui::px (14), ui::px (10));
    r.removeFromTop (ui::px (24));
    r.removeFromBottom (ui::px (44));
    const int w = r.getWidth() / 4;
    for (int b = 0; b < 4; ++b)
    {
        auto col = juce::Rectangle<int> (r.getX() + b * w, r.getY(), w, r.getHeight()).reduced (ui::px (8), 0);
        auto track = col.withSizeKeepingCentre (juce::jmin (col.getWidth(), ui::px (46)), col.getHeight()).withTrimmedTop (ui::px (22)).toFloat();
        const double half = halfRangeDb (t->regions[(size_t) b][0], t->regions[(size_t) b][1]);
        const double v = shownBandDb[(size_t) b];
        const double out = v > half ? v - half : (v < -half ? v + half : 0.0);
        const auto c = judge (out);
        const float y = track.getCentreY() - (float) juce::jlimit (-1.0, 1.0, v / kBandSpanDb) * track.getHeight() * 0.5f;
        auto bar = juce::Rectangle<float>::leftTopRightBottom (track.getX() + 6, std::min (y, track.getCentreY()), track.getRight() - 6, std::max (y, track.getCentreY()));
        g.setColour (c.withAlpha (0.35f)); g.fillRect (bar);
        g.setColour (c); g.fillRoundedRectangle (track.getX() - 3, y - 2.5f, track.getWidth() + 6, 5.0f, 2.0f);
        const juce::String txt = std::abs (out) < 1e-9 ? juce::String ("In range") : (out > 0 ? "+" : "") + juce::String (out, 1) + " dB";
        g.setFont (juce::FontOptions (ui::fs (13.0f), juce::Font::bold));
        g.drawText (txt, col.withHeight (ui::px (20)), juce::Justification::centred);
    }
}

void BalanceView::paintCurveLive (juce::Graphics& g)
{
    const auto* t = target();
    auto head = curveArea.reduced (ui::px (14), ui::px (10)).removeFromTop (ui::px (18));
    g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (11.5f)));
    if (shownValid)
        g.drawText (latest.window == 0 ? juce::String ("realtime")
                                       : "measured over " + (shownSeconds < 2 ? juce::String (shownSeconds, 1) : juce::String (juce::roundToInt (shownSeconds))) + " s of audio",
                    head, juce::Justification::centredRight);

    if (shownValid)
    {
        // the input's curve; red where it leaves the target's range
        juce::Path in, outPath;
        bool wasOut = false, first = true;
        for (auto [x, k] : columns)
        {
            const double v = at (shownCurve, k);
            const float y = yOf (v - baseAt (k));
            if (std::exchange (first, false)) in.startNewSubPath (x, y); else in.lineTo (x, y);
            const bool outside = t != nullptr && (v > at (t->high, k) || v < at (t->low, k));
            if (outside) { if (! wasOut) outPath.startNewSubPath (x, y); else outPath.lineTo (x, y); }
            wasOut = outside;
        }
        g.setColour (kText); g.strokePath (in, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
        g.setColour (kRed); g.strokePath (outPath, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved));
    }
    else
    {
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (15.0f)));
        g.drawText (running ? "Waiting for audio..." : "No input. Open Options (bottom left), choose an input and press Start.",
                    plot.withTrimmedBottom (plot.getHeight() / 3), juce::Justification::centred);
    }

    if (plot.contains (hover.toInt()) && (shownValid || t != nullptr))
    {
        const double f = hzAt (hover.x);
        const int k = binAt (f);
        g.setColour (kMuted.withAlpha (0.6f)); g.drawVerticalLine ((int) hover.x, (float) plot.getY(), (float) plot.getBottom());
        juce::String txt = (f >= 1000 ? juce::String (f / 1000, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
        if (shownValid && t != nullptr)
        {
            const double v = shownCurve[(size_t) k];
            const double d = v - t->mean[(size_t) k];
            const double o = v > t->high[(size_t) k] ? v - t->high[(size_t) k] : (v < t->low[(size_t) k] ? v - t->low[(size_t) k] : 0.0);
            txt << "   " << (d >= 0 ? "+" : "") << juce::String (d, 1) << " dB vs target"
                << (std::abs (o) > 1e-9 ? "   (" + juce::String (o > 0 ? "+" : "") + juce::String (o, 1) + " dB outside the range)" : juce::String ("   in range"));
        }
        g.setFont (juce::FontOptions (ui::fs (13.0f)));
        const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 16;
        auto box = juce::Rectangle<int> (juce::jlimit (plot.getX(), juce::jmax (plot.getX(), plot.getRight() - tw), (int) hover.x + 10), plot.getY() + 6, tw, ui::px (22));
        g.setColour (juce::Colour (0xe0202227)); g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (kText); g.drawText (txt, box, juce::Justification::centred);
    }
}

void BalanceView::mouseMove (const juce::MouseEvent& e) { hover = e.position; repaint (curveArea); }
void BalanceView::mouseExit (const juce::MouseEvent&)   { hover = { -1, -1 }; repaint (curveArea); }
