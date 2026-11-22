#include "BalanceView.h"
#include "../UiScale.h"

namespace
{
    const juce::Colour kBg { 0xff111214 }, kPanel { 0xff17191d }, kGrid { 0xff2a2c31 }, kText { 0xffe8e8e8 }, kMuted { 0xff8c93a3 },
                       kRange { 0xff4c8dff }, kGreen { 0xff3ddc84 }, kAmber { 0xfff0b429 }, kRed { 0xffff5252 };
    const char* kBandNames[4] = { "Low", "Low-mid", "High-mid", "High" };
    const char* kBandFreqs[4] = { "0 - 250 Hz", "250 Hz - 2 kHz", "2 - 8 kHz", "8 kHz +" };

    // how far a value is outside [lo, hi] in dB (0 inside); positive = too much
    double outsideDb (double v, double lo, double hi)
    {
        if (v > hi) return 20 * std::log10 (v / hi);
        if (v < lo) return 20 * std::log10 (v / std::max (lo, 1e-12));
        return 0;
    }
    juce::Colour judge (double out) { return std::abs (out) < 1e-9 ? kGreen : (std::abs (out) < 1.5 ? kAmber : kRed); }
}

BalanceView::BalanceView (LiveBalance& l, TargetLibrary& lib) : live (l), library (lib)
{
    setOpaque (true);
    for (auto* b : { &openFolder, &openBuilder }) addChildComponent (b);
    openFolder.onClick = [this] { if (onOpenFolder) onOpenFolder(); };
    openBuilder.onClick = [this] { if (onOpenBuilder) onOpenBuilder(); };
}

void BalanceView::setActive (bool a)
{
    if (a) { library.rescanIfChanged(); startTimerHz (15); timerCallback(); }
    else stopTimer();
}

void BalanceView::setTargetIndex (int i)
{
    targetIndex = (i >= 0 && i < (int) library.entries().size()) ? i : -1;
    repaint();
}

const tonal::Target* BalanceView::target() const
{
    return targetIndex >= 0 && targetIndex < (int) library.entries().size() ? &library.entries()[(size_t) targetIndex].target : nullptr;
}

void BalanceView::timerCallback()
{
    // notice new / changed target files about every 2 s
    static int tick = 0;
    if (++tick % 30 == 0 && library.rescanIfChanged() && onTargetsChanged) onTargetsChanged();
    const auto s = live.getSnapshot();
    if (s.serial != lastSerial) { lastSerial = s.serial; snap = s; repaint(); }
    const bool empty = library.entries().empty();
    if (openFolder.isVisible() != empty) { openFolder.setVisible (empty); openBuilder.setVisible (empty && builderAvailable); resized(); repaint(); }
}

void BalanceView::resized()
{
    auto r = getLocalBounds().reduced (ui::px (12), ui::px (10));
    const int bw = juce::jlimit (ui::px (260), ui::px (420), r.getWidth() / 3);
    bandArea = r.removeFromLeft (bw);
    r.removeFromLeft (ui::px (12));
    curveArea = r;
    auto c = curveArea.withSizeKeepingCentre (ui::px (420), ui::px (34)).translated (0, ui::px (60));
    openFolder.setBounds (c.removeFromLeft (ui::px (200)));
    c.removeFromLeft (ui::px (20));
    openBuilder.setBounds (c);
    if (! openBuilder.isVisible()) openFolder.setBounds (curveArea.withSizeKeepingCentre (ui::px (200), ui::px (34)).translated (0, ui::px (60)));
}

void BalanceView::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kPanel);
    g.fillRoundedRectangle (bandArea.toFloat(), 6.0f);
    g.fillRoundedRectangle (curveArea.toFloat(), 6.0f);
    if (library.entries().empty()) { paintEmpty (g, curveArea); paintBands (g, bandArea); return; }
    paintBands (g, bandArea);
    paintCurve (g, curveArea);
}

void BalanceView::paintEmpty (juce::Graphics& g, juce::Rectangle<int> r)
{
    g.setColour (kText); g.setFont (juce::FontOptions (ui::fs (19.0f), juce::Font::bold));
    g.drawText ("No tonal balance targets yet", r.withTrimmedBottom (ui::px (90)), juce::Justification::centred);
    g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (14.0f)));
    g.drawFittedText ("Put target files (.json) in " + TargetLibrary::folder().getFullPathName()
                      + "\nor make your own from songs with Target Builder.", r.withTrimmedBottom (ui::px (10)).withTrimmedTop (ui::px (10)),
                      juce::Justification::centred, 3);
}

void BalanceView::paintBands (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto* t = target();
    auto r = area.reduced (ui::px (14), ui::px (10));
    g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (12.0f), juce::Font::bold));
    g.drawText ("BANDS", r.removeFromTop (ui::px (18)), juce::Justification::centredLeft);
    r.removeFromTop (ui::px (6));
    auto labels = r.removeFromBottom (ui::px (44));
    const int w = r.getWidth() / 4;
    constexpr double kSpan = 12.0;                                       // dB shown above and below the range centre

    for (int b = 0; b < 4; ++b)
    {
        auto col = juce::Rectangle<int> (r.getX() + b * w, r.getY(), w, r.getHeight()).reduced (ui::px (8), 0);
        auto lab = juce::Rectangle<int> (r.getX() + b * w, labels.getY(), w, labels.getHeight());
        auto track = col.withSizeKeepingCentre (juce::jmin (col.getWidth(), ui::px (46)), col.getHeight()).withTrimmedTop (ui::px (22)).toFloat();
        g.setColour (juce::Colour (0xff202227)); g.fillRoundedRectangle (track, 4.0f);

        g.setColour (kText); g.setFont (juce::FontOptions (ui::fs (13.5f), juce::Font::bold));
        g.drawText (kBandNames[b], lab.removeFromTop (lab.getHeight() / 2), juce::Justification::centred);
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (11.0f)));
        g.drawText (kBandFreqs[b], lab, juce::Justification::centred);
        if (t == nullptr) continue;

        const double lo = t->regions[(size_t) b][0], hi = t->regions[(size_t) b][1];
        const double centre = std::sqrt (lo * hi);
        auto yOf = [&] (double share)
        {
            const double db = 20 * std::log10 (std::max (share, 1e-9) / centre);
            return track.getCentreY() - (float) juce::jlimit (-1.0, 1.0, db / kSpan) * track.getHeight() * 0.5f;
        };
        auto zone = juce::Rectangle<float>::leftTopRightBottom (track.getX(), yOf (hi), track.getRight(), yOf (lo));
        g.setColour (kRange.withAlpha (0.30f)); g.fillRect (zone);
        g.setColour (kRange.withAlpha (0.7f)); g.drawHorizontalLine ((int) zone.getY(), track.getX(), track.getRight());
        g.drawHorizontalLine ((int) zone.getBottom(), track.getX(), track.getRight());

        if (! snap.valid) continue;
        const double v = snap.shares[(size_t) b];
        const double out = outsideDb (v, lo, hi);
        const auto c = judge (out);
        const float y = yOf (v);
        // bar from the range centre to the value, and a marker line
        auto bar = juce::Rectangle<float>::leftTopRightBottom (track.getX() + 6, std::min (y, track.getCentreY()), track.getRight() - 6, std::max (y, track.getCentreY()));
        g.setColour (c.withAlpha (0.35f)); g.fillRect (bar);
        g.setColour (c); g.fillRoundedRectangle (track.getX() - 3, y - 2.5f, track.getWidth() + 6, 5.0f, 2.0f);

        juce::String txt = std::abs (out) < 1e-9 ? "In range" : (out > 0 ? "+" : "") + juce::String (out, 1) + " dB";
        g.setFont (juce::FontOptions (ui::fs (13.0f), juce::Font::bold));
        g.drawText (txt, col.withHeight (ui::px (20)), juce::Justification::centred);
    }
}

float BalanceView::xOf (double hz) const
{
    return (float) plot.getX() + (float) (std::log (hz / 20.0) / std::log (1000.0)) * (float) plot.getWidth();
}

double BalanceView::hzAt (float x) const
{
    return 20.0 * std::pow (1000.0, juce::jlimit (0.0, 1.0, (double) (x - (float) plot.getX()) / juce::jmax (1, plot.getWidth())));
}

void BalanceView::paintCurve (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto* t = target();
    auto r = area.reduced (ui::px (14), ui::px (10));
    auto head = r.removeFromTop (ui::px (18));
    g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (12.0f), juce::Font::bold));
    const auto name = targetIndex >= 0 ? library.entries()[(size_t) targetIndex].name : juce::String ("no target selected");
    g.drawText ((difference ? "DIFFERENCE FROM " : "CURVE VS ") + name.toUpperCase(), head, juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (ui::fs (11.5f)));
    g.drawText (snap.valid ? "measured over " + juce::String (juce::roundToInt (snap.seconds)) + " s of audio" : juce::String(), head, juce::Justification::centredRight);
    r.removeFromTop (ui::px (6));
    plot = r.withTrimmedLeft (ui::px (34)).withTrimmedBottom (ui::px (16));

    // vertical scale
    double top, span;
    if (difference) { top = 12; span = 24; }
    else
    {
        top = -1e9;
        if (t != nullptr)
            for (int k = 1; k < tonal::kBins; ++k) if (tonal::binFrequency (k) >= 20 && tonal::binFrequency (k) <= 20000) top = std::max (top, t->high[(size_t) k]);
        if (top < -1e8 && snap.valid) for (double v : snap.smooth) top = std::max (top, v);
        if (top < -1e8) top = -30;
        top = std::ceil (top / 5) * 5 + 5;
        span = 60;
    }
    auto yOf = [&] (double db) { return (float) plot.getY() + (float) juce::jlimit (-0.02, 1.02, (top - db) / span) * (float) plot.getHeight(); };

    g.setFont (juce::FontOptions (ui::fs (11.0f)));
    for (double f : { 30.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = xOf (f);
        g.setColour (kGrid); g.drawVerticalLine ((int) x, (float) plot.getY(), (float) plot.getBottom());
        g.setColour (kMuted);
        g.drawText (f >= 1000 ? juce::String (f / 1000, 0) + "k" : juce::String ((int) f), (int) x - 20, plot.getBottom() + 2, 40, ui::px (14), juce::Justification::centred);
    }
    for (double d = top; d >= top - span; d -= (difference ? 3 : 10))
    {
        const float y = yOf (d);
        g.setColour (difference && std::abs (d) < 0.01 ? kMuted : kGrid);
        g.drawHorizontalLine ((int) y, (float) plot.getX(), (float) plot.getRight());
        g.setColour (kMuted);
        g.drawText ((difference && d > 0 ? "+" : "") + juce::String ((int) d), plot.getX() - ui::px (36), (int) y - 7, ui::px (32), 14, juce::Justification::centredRight);
    }
    // band edges
    for (double f : { 250.0, 2000.0, 8000.0 })
    {
        const float x = xOf (f);
        g.setColour (kRange.withAlpha (0.35f));
        for (float y = (float) plot.getY(); y < (float) plot.getBottom(); y += 8) g.drawVerticalLine ((int) x, y, juce::jmin (y + 4, (float) plot.getBottom()));
    }

    auto bins = [&] (auto&& fn)
    {
        for (int k = 1; k < tonal::kBins; ++k)
        {
            const double f = tonal::binFrequency (k);
            if (f < 20) continue;
            if (f > 20000) break;
            fn (k, xOf (f));
        }
    };

    if (t != nullptr)
    {
        juce::Path band; std::vector<juce::Point<float>> lows; bool first = true;
        juce::Path mean;
        bins ([&] (int k, float x)
        {
            const double base = difference ? t->mean[(size_t) k] : 0.0;
            const float yh = yOf (t->high[(size_t) k] - base), ym = yOf (t->mean[(size_t) k] - base);
            if (first) { band.startNewSubPath (x, yh); mean.startNewSubPath (x, ym); first = false; }
            else { band.lineTo (x, yh); mean.lineTo (x, ym); }
            lows.push_back ({ x, yOf (t->low[(size_t) k] - base) });
        });
        for (auto it = lows.rbegin(); it != lows.rend(); ++it) band.lineTo (*it);
        band.closeSubPath();
        g.setColour (kRange.withAlpha (0.22f)); g.fillPath (band);
        g.setColour (kRange.withAlpha (0.8f)); g.strokePath (mean, juce::PathStrokeType (1.2f));
    }

    if (snap.valid)
    {
        // the input's curve; red where it leaves the target's range
        juce::Path in, outPath;
        bool first = true, inOut = false;
        bins ([&] (int k, float x)
        {
            const double v = snap.smooth[(size_t) k];
            const double base = (difference && t != nullptr) ? t->mean[(size_t) k] : 0.0;
            const float y = yOf (v - base);
            if (first) { in.startNewSubPath (x, y); first = false; } else in.lineTo (x, y);
            const bool outside = t != nullptr && (v > t->high[(size_t) k] || v < t->low[(size_t) k]);
            if (outside) { if (! inOut) outPath.startNewSubPath (x, y); else outPath.lineTo (x, y); }
            inOut = outside;
        });
        g.setColour (kText); g.strokePath (in, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved));
        g.setColour (kRed); g.strokePath (outPath, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved));
    }
    else
    {
        g.setColour (kMuted); g.setFont (juce::FontOptions (ui::fs (15.0f)));
        g.drawText (running ? "Waiting for audio..." : "No input. Open Options (bottom left), choose an input and press Start.",
                    plot.withTrimmedBottom (plot.getHeight() / 3), juce::Justification::centred);
    }

    // hover readout
    if (plot.contains (hover.toInt()) && (snap.valid || t != nullptr))
    {
        const double f = hzAt (hover.x);
        const int k = juce::jlimit (1, tonal::kBins - 1, (int) std::lround (f / (tonal::kSampleRate / tonal::kFftSize)));
        g.setColour (kMuted.withAlpha (0.6f)); g.drawVerticalLine ((int) hover.x, (float) plot.getY(), (float) plot.getBottom());
        juce::String txt = (f >= 1000 ? juce::String (f / 1000, 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz");
        if (snap.valid && t != nullptr)
        {
            const double d = snap.smooth[(size_t) k] - t->mean[(size_t) k];
            const double o = snap.smooth[(size_t) k] > t->high[(size_t) k] ? snap.smooth[(size_t) k] - t->high[(size_t) k]
                           : (snap.smooth[(size_t) k] < t->low[(size_t) k] ? snap.smooth[(size_t) k] - t->low[(size_t) k] : 0.0);
            txt << "   " << (d >= 0 ? "+" : "") << juce::String (d, 1) << " dB vs target"
                << (std::abs (o) > 1e-9 ? "   (" + juce::String (o > 0 ? "+" : "") + juce::String (o, 1) + " dB outside the range)" : juce::String ("   in range"));
        }
        g.setFont (juce::FontOptions (ui::fs (13.0f)));
        const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 16;
        auto box = juce::Rectangle<int> (juce::jlimit (plot.getX(), plot.getRight() - tw, (int) hover.x + 10), plot.getY() + 6, tw, ui::px (22));
        g.setColour (juce::Colour (0xe0202227)); g.fillRoundedRectangle (box.toFloat(), 4.0f);
        g.setColour (kText); g.drawText (txt, box, juce::Justification::centred);
    }
}

void BalanceView::mouseMove (const juce::MouseEvent& e) { hover = e.position; repaint (curveArea); }
void BalanceView::mouseExit (const juce::MouseEvent&)   { hover = { -1, -1 }; repaint (curveArea); }
