#include "AnalyzerView.h"
#include "../../FrameClock.h"
#include "../../UiScale.h"

namespace
{
    namespace P = sa::params;

    constexpr double kMinViewRatio = 1.12;   // narrowest zoom: ~1/6 octave

    const juce::String& dashChar()     { static const juce::String s = juce::String::fromUTF8 ("\xe2\x80\x93"); return s; }
    const juce::String& infinityChar() { static const juce::String s = juce::String::fromUTF8 ("\xe2\x88\x9e"); return s; }
    const juce::String& centChar()     { static const juce::String s = juce::String::fromUTF8 ("\xc2\xa2"); return s; }

    juce::String trimNumber (double v, int decimals)
    {
        if (decimals <= 0)
            return juce::String (juce::roundToInt (v));   // String (double, 0) means "full precision"
        auto s = juce::String (v, decimals);
        if (s.containsChar ('.'))
            s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
        return s;
    }

    juce::String freqLabel (double f)
    {
        if (f >= 1000.0)
            return trimNumber (f / 1000.0, f >= 10000.0 ? 1 : 2) + "k";
        return trimNumber (f, f >= 100.0 ? 0 : 1);
    }

    juce::String freqText (double f)
    {
        if (f >= 1000.0)
            return juce::String (f / 1000.0, f >= 10000.0 ? 2 : 3) + " kHz";
        return juce::String (f, f >= 100.0 ? 1 : 2) + " Hz";
    }

    int nearestMidiNote (double f, double* cents = nullptr)
    {
        const double midi = 69.0 + 12.0 * std::log2 (f / 440.0);
        const int n = (int) std::lround (midi);
        if (cents != nullptr) *cents = (midi - n) * 100.0;
        return n;
    }

    juce::String noteName (int midi)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        const int oct = (int) std::floor (midi / 12.0) - 1;
        return juce::String (names[((midi % 12) + 12) % 12]) + juce::String (oct);
    }

    juce::String noteText (double f)
    {
        if (f < 8.0) return {};
        double cents = 0.0;
        const int n = nearestMidiNote (f, &cents);
        const int c = (int) std::lround (cents);
        return noteName (n) + (c >= 0 ? " +" : " ") + juce::String (c) + centChar();
    }

    [[maybe_unused]] juce::String dbText (double v)
    {
        return v <= sa::AnalyzerModel::kFloorDb + 1.0 ? "-" + infinityChar() : juce::String (v, 1);
    }

    struct GridLine
    {
        double f;
        int    priority;   // 0 = most important
        juce::String text;
    };
}

//==============================================================================
AnalyzerView::AnalyzerView (SpectrumAnalyzerProcessor& p)
    : processor (p),
      state (p.apvts),
      layout (std::make_unique<sa::BandLayout>()),
      model (std::make_unique<sa::AnalyzerModel>()),
      smallFont (juce::FontOptions (11.0f)),
      infoFont (juce::FontOptions (12.5f)),
      vblank (this, [this] (double ts) { advance (ts); })
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    syncLayout();
}

AnalyzerView::~AnalyzerView() = default;

//==============================================================================
void AnalyzerView::syncLayout()
{
    const double fs = processor.getAnalysisSampleRate();
    const sa::LayoutKey key { fs > 0.0 ? fs : 48000.0, P::bandsPerOctave (state),
                              (sa::Anchor) (int) P::get (state, P::anchor) };
    if (key != layout->key)
    {
        sa::computeLayout (*layout, key);
        model->setLayout (*layout);
        frozen = false;
        if (onStateChanged) onStateChanged();
    }
}

void AnalyzerView::advance (double ts)
{
    // Spectroscope: obey the app-wide frame-rate cap (skipping a vblank just makes the next dt longer)
    if (! frameclock::clock().due (ts))
        return;

    syncLayout();

    auto& ring = processor.getRing();
    while (auto* s = ring.beginRead())
    {
        model->push (*s);   // snapshots for a stale layout are ignored
        ring.commitRead();
    }

    const double dtMs = lastTimestamp > 0.0 ? (ts - lastTimestamp) * 1000.0 : 0.0;
    lastTimestamp = ts;
    model->tick (dtMs, P::meterOptions (state));
    repaint();
}

void AnalyzerView::resized() {}

//==============================================================================
juce::Rectangle<float> AnalyzerView::plotArea() const noexcept
{
    auto r = getLocalBounds().toFloat();
    r.removeFromLeft (34.0f);
    r.removeFromRight (34.0f);
    r.removeFromTop (18.0f);
    r.removeFromBottom (20.0f);
    return r;
}

juce::Range<double> AnalyzerView::getFullRange() const noexcept
{
    if (layout->numBands <= 0)
        return { 20.0, 20000.0 };
    return { layout->lower[0], layout->upper[(size_t) layout->numBands - 1] };
}

juce::Range<double> AnalyzerView::getViewRange() const noexcept
{
    const auto full = getFullRange();
    double lo = processor.viewLowHz.load(), hi = processor.viewHighHz.load();
    if (lo <= 0.0 || hi <= 0.0 || hi <= lo)
        return full;

    lo = juce::jmax (lo, full.getStart());
    hi = juce::jmin (hi, full.getEnd());
    if (hi / lo < kMinViewRatio)
    {
        const double c = std::sqrt (lo * hi);
        lo = c / std::sqrt (kMinViewRatio);
        hi = c * std::sqrt (kMinViewRatio);
    }
    return { lo, hi };
}

void AnalyzerView::setViewRange (double lo, double hi)
{
    const auto full = getFullRange();
    if (lo > hi) std::swap (lo, hi);
    lo = juce::jmax (lo, 1.0);

    double ratio = juce::jmax (hi / lo, kMinViewRatio);
    const double c = std::sqrt (lo * hi);
    lo = c / std::sqrt (ratio);
    hi = c * std::sqrt (ratio);

    if (ratio >= full.getEnd() / full.getStart() * 0.999)
    {
        processor.viewLowHz.store (0.0);
        processor.viewHighHz.store (0.0);
    }
    else
    {
        // keep the span, slide inside the full range
        if (lo < full.getStart()) { hi *= full.getStart() / lo; lo = full.getStart(); }
        if (hi > full.getEnd())   { lo *= full.getEnd() / hi;   hi = full.getEnd(); }
        processor.viewLowHz.store (lo);
        processor.viewHighHz.store (hi);
    }
    if (onStateChanged) onStateChanged();
}

void AnalyzerView::resetZoom()
{
    processor.viewLowHz.store (0.0);
    processor.viewHighHz.store (0.0);
    if (onStateChanged) onStateChanged();
}

void AnalyzerView::zoomBy (double factor, double around)
{
    const auto v = getViewRange();
    const double c = around > 0.0 ? around : std::sqrt (v.getStart() * v.getEnd());
    setViewRange (c * std::pow (v.getStart() / c, factor), c * std::pow (v.getEnd() / c, factor));
}

float AnalyzerView::freqToX (double f, juce::Range<double> v, juce::Rectangle<float> plot) const noexcept
{
    return plot.getX() + plot.getWidth() * (float) (std::log (f / v.getStart()) / std::log (v.getEnd() / v.getStart()));
}

double AnalyzerView::xToFreq (float x, juce::Range<double> v, juce::Rectangle<float> plot) const noexcept
{
    const double t = (x - plot.getX()) / juce::jmax (1.0f, plot.getWidth());
    return v.getStart() * std::pow (v.getEnd() / v.getStart(), t);
}

float AnalyzerView::levelToY (double db, juce::Rectangle<float> plot) const noexcept
{
    const double ceil  = P::get (state, P::ceiling);
    const double range = juce::jmax (1.0f, P::get (state, P::range));
    return plot.getY() + plot.getHeight() * (float) ((ceil - db) / range);
}

double AnalyzerView::yToLevel (float y, juce::Rectangle<float> plot) const noexcept
{
    const double ceil  = P::get (state, P::ceiling);
    const double range = juce::jmax (1.0f, P::get (state, P::range));
    return ceil - (y - plot.getY()) / juce::jmax (1.0f, plot.getHeight()) * range;
}

int AnalyzerView::bandAt (double f) const noexcept
{
    const int n = layout->numBands;
    if (n <= 0 || f <= 0.0) return -1;
    const double k = std::exp2 (1.0 / layout->key.bandsPerOctave);
    const int b = (int) std::lround (std::log (f / layout->centre[0]) / std::log (k));
    return juce::jlimit (0, n - 1, b);
}

//==============================================================================
void AnalyzerView::paint (juce::Graphics& g)
{
    smallFont = juce::FontOptions (ui::fs (11.0f));      // Spectroscope: follow the app's text size
    infoFont  = juce::FontOptions (ui::fs (13.5f));

    const auto plot = plotArea();
    const auto view = getViewRange();

    drawBackground (g, plot, view);

    if (plot.getWidth() < 10.0f || plot.getHeight() < 10.0f)
        return;

    drawFrequencyGrid (g, plot, view);
    drawLevelGrid (g, plot);

    const double* pk = frozen ? frozenPeak.data() : model->peak.data();
    const double* hd = frozen ? frozenHold.data() : model->hold.data();
    const double* av = frozen ? frozenAvg.data()  : model->avg.data();

    using BT = P::BarType;
    const bool peakOn = P::get (state, P::peakOn) > 0.5f;
    const bool holdOn = P::get (state, P::holdOn) > 0.5f;
    const bool avgOn  = P::get (state, P::avgOn)  > 0.5f;
    const auto holdType = (BT) (int) P::get (state, P::holdType);
    const auto avgType  = (BT) (int) P::get (state, P::avgType);
    const int holdSize = (int) P::get (state, P::holdSize);
    const int avgSize  = (int) P::get (state, P::avgSize);

    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (plot.reduced (1.0f).getSmallestIntegerContainer());

        using C = SpectrumAnalyzerProcessor::ColourIndex;
        // as in the original: peak goes on top of a filled hold curve
        if (holdType != BT::CurveFill && peakOn)
            drawBars (g, pk, true, 0, processor.getColour (C::PeakColour), plot, view);

        if (holdOn)
        {
            if (holdType == BT::Bars) drawBars  (g, hd, false, holdSize, processor.getColour (C::HoldColour), plot, view);
            else                      drawCurve (g, hd, holdType == BT::CurveFill, holdSize, processor.getColour (C::HoldColour), plot, view);
        }

        if (holdType == BT::CurveFill && peakOn)
            drawBars (g, pk, true, 0, processor.getColour (C::PeakColour), plot, view);

        if (avgOn)
        {
            if (avgType == BT::Bars) drawBars  (g, av, false, avgSize, processor.getColour (C::AvgColour), plot, view);
            else                     drawCurve (g, av, avgType == BT::CurveFill, avgSize, processor.getColour (C::AvgColour), plot, view);
        }
    }

    if (pin.has_value() && processor.inspectorEnabled.load())
    {
        // the pinned readout (Alt+click) stays at its frequency and level; the live values keep updating
        const auto saved = mousePos;
        mousePos = { freqToX (pin->first, view, plot), levelToY (pin->second, plot) };
        if (plot.contains (mousePos)) drawInspector (g, plot, view, pk, hd, av);
        mousePos = saved;
    }
    else if (hovering && drag != Drag::Select && processor.inspectorEnabled.load() && plot.contains (mousePos))
        drawInspector (g, plot, view, pk, hd, av);

    if (drag == Drag::Select)
    {
        const auto label = processor.getColour (SpectrumAnalyzerProcessor::LabelColour);
        const float x0 = juce::jmin (dragStartX, dragCurrentX), x1 = juce::jmax (dragStartX, dragCurrentX);
        juce::Rectangle<float> sel (x0, plot.getY(), x1 - x0, plot.getHeight());
        g.setColour (label.withMultipliedAlpha (0.15f));
        g.fillRect (sel);
        g.setColour (label.withMultipliedAlpha (0.6f));
        g.drawRect (sel, 1.0f);
    }

    drawStatus (g, plot, view);
}

void AnalyzerView::drawBackground (juce::Graphics& g, juce::Rectangle<float> plot, juce::Range<double>)
{
    using C = SpectrumAnalyzerProcessor::ColourIndex;
    const auto b = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (processor.getColour (C::BkgTopColour).withAlpha (1.0f), b.getTopLeft(),
                                             processor.getColour (C::BkgBottomColour).withAlpha (1.0f), b.getBottomRight(), false));
    g.fillAll();

    g.setColour (processor.getColour (C::BorderColour));
    g.drawRect (plot.expanded (0.5f), 1.0f);
}

void AnalyzerView::drawFrequencyGrid (juce::Graphics& g, juce::Rectangle<float> plot, juce::Range<double> view)
{
    using C = SpectrumAnalyzerProcessor::ColourIndex;
    const auto type = (P::FreqGrid) (int) P::get (state, P::freqGrid);
    const double logSpan = std::log (view.getEnd() / view.getStart());
    const auto spacingFor = [&] (double ratio) { return plot.getWidth() * std::log (ratio) / logSpan; };
    constexpr double minGap = 7.0;

    std::vector<GridLine> lines;

    if (type == P::FreqGrid::Decade)
    {
        // finest mantissa step (in 1/100 of a decade) whose lines are still >= minGap apart
        static constexpr int steps[] = { 100, 50, 20, 10, 5, 2, 1 };
        int step = 0; // 0 = only 1, 2, 5
        for (int s : steps)
            if (spacingFor (1000.0 / (1000.0 - s)) >= minGap) step = s;

        const int d0 = (int) std::floor (std::log10 (view.getStart()));
        const int d1 = (int) std::ceil  (std::log10 (view.getEnd()));
        for (int d = d0; d <= d1; ++d)
        {
            const double dec = std::pow (10.0, d);
            if (step == 0)
            {
                for (int m : { 1, 2, 5 })
                    lines.push_back ({ m * dec, m == 1 ? 0 : 1, {} });
                continue;
            }
            for (int m = 100; m < 1000; m += step)   // mantissa x 100
            {
                const int pr = (m == 100) ? 0 : (m == 200 || m == 500) ? 1 : (m % 100 == 0) ? 2 : 3;
                lines.push_back ({ m * 0.01 * dec, pr, {} });
            }
        }
        for (auto& l : lines) l.text = freqLabel (l.f);
    }
    else if (type == P::FreqGrid::Octave)
    {
        static constexpr int subdivisions[] = { 1, 2, 3, 6, 12, 24 };
        int div = 1;
        for (int s : subdivisions)
            if (spacingFor (std::exp2 (1.0 / s)) >= minGap) div = s;

        const int j0 = (int) std::floor (std::log2 (view.getStart() / 1000.0) * div);
        const int j1 = (int) std::ceil  (std::log2 (view.getEnd()   / 1000.0) * div);
        for (int j = j0; j <= j1; ++j)
        {
            const int pr = (j % div == 0) ? 0 : ((j * 3) % div == 0) ? 1 : 2;
            const double f = 1000.0 * std::exp2 ((double) j / div);
            lines.push_back ({ f, pr, freqLabel (f) });
        }
    }
    else // Notes
    {
        const double semitone = spacingFor (std::exp2 (1.0 / 12.0));
        const int n0 = nearestMidiNote (view.getStart()) - 1;
        const int n1 = nearestMidiNote (view.getEnd()) + 1;
        for (int n = n0; n <= n1; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            const bool natural = pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
            if (pc != 0 && semitone < 5.0)
                continue;
            const int pr = pc == 0 ? 0 : pc == 9 ? 1 : natural ? 2 : 3;
            lines.push_back ({ 440.0 * std::exp2 ((n - 69) / 12.0), pr, noteName (n) });
        }
    }

    // lines
    const auto lineColour = processor.getColour (C::LineColour);
    for (const auto& l : lines)
    {
        if (l.f < view.getStart() || l.f > view.getEnd()) continue;
        const float x = std::round (freqToX (l.f, view, plot)) + 0.5f;
        g.setColour (l.priority <= 1 ? lineColour : lineColour.withMultipliedAlpha (0.55f));
        g.drawVerticalLine ((int) x, plot.getY() + 1.0f, plot.getBottom());
    }

    // labels: place by priority without overlap
    g.setFont (smallFont);
    g.setColour (processor.getColour (C::LabelColour));
    std::vector<juce::Range<float>> placed;
    for (int pr = 0; pr <= 3; ++pr)
    {
        for (const auto& l : lines)
        {
            if (l.priority != pr || l.f < view.getStart() || l.f > view.getEnd()) continue;
            const float x = freqToX (l.f, view, plot);
            const float w = juce::GlyphArrangement::getStringWidth (smallFont, l.text) + 8.0f;
            juce::Range<float> r (x - w * 0.5f, x + w * 0.5f);
            if (r.getStart() < plot.getX() - 14.0f || r.getEnd() > plot.getRight() + 14.0f) continue;
            bool overlap = false;
            for (auto& q : placed) if (q.intersects (r)) { overlap = true; break; }
            if (overlap) continue;
            placed.push_back (r);
            g.drawText (l.text, juce::Rectangle<float> (r.getStart(), plot.getBottom() + 3.0f, w, 14.0f),
                        juce::Justification::centredTop, false);
        }
    }
}

void AnalyzerView::drawLevelGrid (juce::Graphics& g, juce::Rectangle<float> plot)
{
    using C = SpectrumAnalyzerProcessor::ColourIndex;
    const int top   = (int) P::get (state, P::ceiling);
    const int grid  = juce::jmax (1, (int) P::get (state, P::grid));
    const int range = (int) P::get (state, P::range);

    int level = top - top % grid;
    if (level > top) level -= grid;

    g.setFont (smallFont);
    while (level > top - range)
    {
        const float y = std::round (levelToY (level, plot)) + 0.5f;
        if (level < top)
        {
            g.setColour (processor.getColour (C::LineColour));
            g.drawHorizontalLine ((int) y, plot.getX() + 1.0f, plot.getRight());
        }
        g.setColour (processor.getColour (C::LabelColour));
        const auto t = juce::String (level);
        g.drawText (t, juce::Rectangle<float> (0.0f, y - 7.0f, plot.getX() - 5.0f, 14.0f), juce::Justification::centredRight, false);
        g.drawText (t, juce::Rectangle<float> (plot.getRight() + 5.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredLeft, false);
        level -= grid;
    }
}

void AnalyzerView::drawBars (juce::Graphics& g, const double* level, bool full, int size, juce::Colour colour,
                             juce::Rectangle<float> plot, juce::Range<double> view)
{
    juce::RectangleList<float> rects;
    const float bottom = plot.getBottom();

    for (int b = 0; b < layout->numBands; ++b)
    {
        const double lo = layout->lower[(size_t) b], hi = layout->upper[(size_t) b];
        if (hi < view.getStart() || lo > view.getEnd()) continue;

        float x0 = freqToX (lo, view, plot), x1 = freqToX (hi, view, plot);
        const float gap = juce::jlimit (0.0f, 2.0f, (x1 - x0) * 0.2f);
        x0 += gap * 0.5f;
        x1 -= gap * 0.5f;
        if (x1 - x0 < 1.0f) x1 = x0 + 1.0f;

        const float y = juce::jlimit (plot.getY() - 1.0f, bottom, levelToY (level[b], plot));
        if (y >= bottom) continue;

        if (full) rects.addWithoutMerging ({ x0, y, x1 - x0, bottom - y });
        else      rects.addWithoutMerging ({ x0, y, x1 - x0, (float) size });
    }

    g.setColour (colour);
    g.fillRectList (rects);
}

void AnalyzerView::drawCurve (juce::Graphics& g, const double* level, bool fill, int size, juce::Colour colour,
                              juce::Rectangle<float> plot, juce::Range<double> view)
{
    std::vector<juce::Point<float>> pts;
    pts.reserve ((size_t) layout->numBands);
    for (int b = 0; b < layout->numBands; ++b)
    {
        if (layout->upper[(size_t) b] < view.getStart() * 0.8 || layout->lower[(size_t) b] > view.getEnd() * 1.25)
            continue;
        const float y = juce::jlimit (plot.getY() - 40.0f, plot.getBottom() + 4.0f, levelToY (level[b], plot));
        pts.push_back ({ freqToX (layout->centre[(size_t) b], view, plot), y });
    }
    if (pts.size() < 2)
        return;

    // Catmull-Rom through band centres (the original used a cardinal spline)
    juce::Path path;
    path.startNewSubPath (pts[0]);
    for (size_t i = 0; i + 1 < pts.size(); ++i)
    {
        const auto p0 = pts[i > 0 ? i - 1 : i];
        const auto p1 = pts[i];
        const auto p2 = pts[i + 1];
        const auto p3 = pts[i + 2 < pts.size() ? i + 2 : i + 1];
        path.cubicTo (p1 + (p2 - p0) / 6.0f, p2 - (p3 - p1) / 6.0f, p2);
    }

    if (fill)
    {
        juce::Path area (path);
        area.lineTo (pts.back().x, plot.getBottom() + 1.0f);
        area.lineTo (pts.front().x, plot.getBottom() + 1.0f);
        area.closeSubPath();
        g.setColour (colour.withMultipliedAlpha (0.5f));
        g.fillPath (area);
    }

    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (0.667f * (0.5f + (float) size), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void AnalyzerView::drawInspector (juce::Graphics& g, juce::Rectangle<float> plot, juce::Range<double> view,
                                  const double* pk, const double* hd, const double* av)
{
    using C = SpectrumAnalyzerProcessor::ColourIndex;
    const int b = bandAt (xToFreq (mousePos.x, view, plot));
    if (b < 0) return;

    const auto label = processor.getColour (C::LabelColour);
    const double f  = xToFreq (mousePos.x, view, plot);
    const double lv = yToLevel (mousePos.y, plot);

    // highlight band
    const float bx0 = juce::jmax (plot.getX(), freqToX (layout->lower[(size_t) b], view, plot));
    const float bx1 = juce::jmin (plot.getRight(), freqToX (layout->upper[(size_t) b], view, plot));
    g.setColour (label.withMultipliedAlpha (0.10f));
    g.fillRect (juce::Rectangle<float> (bx0, plot.getY(), juce::jmax (1.0f, bx1 - bx0), plot.getHeight()));

    // crosshair
    g.setColour (label.withMultipliedAlpha (0.35f));
    g.drawVerticalLine ((int) mousePos.x, plot.getY(), plot.getBottom());
    g.drawHorizontalLine ((int) mousePos.y, plot.getX(), plot.getRight());

    // Spectroscope: just the frequency, level and note under the mouse
    juce::ignoreUnused (pk, hd, av);
    const juce::String lines[] = { freqText (f) + "   " + juce::String (lv, 1) + " dB   " + noteText (f) };
    const float lineH = infoFont.getHeight() + 4.0f;

    float w = 0.0f;
    for (auto& l : lines) w = juce::jmax (w, juce::GlyphArrangement::getStringWidth (infoFont, l));
    w += 16.0f;
    const float h = lineH + 8.0f;

    float x = mousePos.x + 14.0f;
    if (x + w > plot.getRight()) x = mousePos.x - 14.0f - w;
    x = juce::jmax (plot.getX() + 2.0f, x);
    float y = mousePos.y + 14.0f;
    if (y + h > plot.getBottom()) y = mousePos.y - 14.0f - h;
    y = juce::jmax (plot.getY() + 2.0f, y);

    const juce::Rectangle<float> box (x, y, w, h);
    g.setColour (processor.getColour (C::BkgBottomColour).withAlpha (0.88f));
    g.fillRoundedRectangle (box, 4.0f);
    g.setColour (label.withMultipliedAlpha (0.6f));
    g.drawRoundedRectangle (box, 4.0f, 1.0f);

    g.setFont (infoFont);
    g.setColour (label.withAlpha (1.0f));
    for (int i = 0; i < (int) std::size (lines); ++i)
        g.drawText (lines[i], box.reduced (8.0f, 4.0f).withHeight (lineH).translated (0.0f, i * lineH),
                    juce::Justification::centredLeft, false);
}

void AnalyzerView::drawStatus (juce::Graphics& g, juce::Rectangle<float> plot, juce::Range<double> view)
{
    using C = SpectrumAnalyzerProcessor::ColourIndex;
    g.setFont (smallFont);
    g.setColour (processor.getColour (C::LabelColour));

    const auto topRow = juce::Rectangle<float> (plot.getX(), 2.0f, plot.getWidth(), 14.0f);

    juce::String left;
    if (frozen) left << "FROZEN   ";
    if (isZoomed())
        left << "View " << freqLabel (view.getStart()) << dashChar() << freqLabel (view.getEnd())
             << " Hz  (double-click axis for full range)";
    g.drawText (left, topRow, juce::Justification::centredLeft, false);

    const auto right = juce::String (layout->numBands) + " bands  " + juce::String (layout->key.bandsPerOctave) + "/oct";
    g.drawText (right, topRow, juce::Justification::centredRight, false);
}

//==============================================================================
void AnalyzerView::resetMeters()
{
    model->reset();
}

void AnalyzerView::toggleFreeze()
{
    if (! frozen)
    {
        frozenPeak = model->peak;
        frozenHold = model->hold;
        frozenAvg  = model->avg;
    }
    frozen = ! frozen;
    if (onStateChanged) onStateChanged();
}

void AnalyzerView::mouseMove (const juce::MouseEvent& e)
{
    mousePos = e.position;
    hovering = true;
}

void AnalyzerView::mouseExit (const juce::MouseEvent&)
{
    hovering = false;
}

void AnalyzerView::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    mousePos = e.position;

    if (e.mods.isPopupMenu())
    {
        if (onSettingsRequested) onSettingsRequested();
        return;
    }

    // Alt+click: pin the readout here (Alt+click on the pin again removes it)
    if (e.mods.isAltDown())
    {
        const auto plot = plotArea();
        const auto view = getViewRange();
        if (pin.has_value() && juce::Point<float> (freqToX (pin->first, view, plot), levelToY (pin->second, plot)).getDistanceFrom (e.position) < 8.0f)
            pin.reset();
        else if (plot.contains (e.position))
            pin = std::make_pair (xToFreq (e.position.x, view, plot), yToLevel (e.position.y, plot));
        repaint();
        return;
    }

    dragStartX = dragCurrentX = e.position.x;
    dragStartView = getViewRange();
    drag = e.mods.isShiftDown() ? Drag::Select : Drag::Pan;
}

void AnalyzerView::mouseDrag (const juce::MouseEvent& e)
{
    mousePos = e.position;
    dragCurrentX = e.position.x;

    if (drag == Drag::Pan && e.mouseWasDraggedSinceMouseDown())
    {
        const auto plot = plotArea();
        const double ratio = std::pow (dragStartView.getEnd() / dragStartView.getStart(),
                                       (double) (dragStartX - e.position.x) / juce::jmax (1.0f, plot.getWidth()));
        if (isZoomed())
            setViewRange (dragStartView.getStart() * ratio, dragStartView.getEnd() * ratio);
    }
}

void AnalyzerView::mouseUp (const juce::MouseEvent&)
{
    if (drag == Drag::Select && std::abs (dragCurrentX - dragStartX) > 6.0f)
    {
        const auto plot = plotArea();
        const auto view = getViewRange();
        setViewRange (xToFreq (juce::jmin (dragStartX, dragCurrentX), view, plot),
                      xToFreq (juce::jmax (dragStartX, dragCurrentX), view, plot));
    }
    drag = Drag::None;
}

void AnalyzerView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (e.position.y > plotArea().getBottom())
        resetZoom();
    else
        resetMeters();
}

void AnalyzerView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const auto plot = plotArea();

    if (e.mods.isCommandDown() || e.mods.isAltDown())
    {
        wheelAccumulator += w.deltaY * 10.0f;
        const int steps = (int) wheelAccumulator;
        if (steps != 0)
        {
            wheelAccumulator -= (float) steps;
            if (e.mods.isCommandDown())
                P::set (state, P::ceiling, P::get (state, P::ceiling) + (float) steps);
            else
                P::set (state, P::range, P::get (state, P::range) - 2.0f * (float) steps);
        }
        return;
    }

    const auto view = getViewRange();
    if (std::abs (w.deltaX) > std::abs (w.deltaY))
    {
        if (isZoomed())
        {
            const double ratio = std::pow (view.getEnd() / view.getStart(), (double) -w.deltaX * 0.5);
            setViewRange (view.getStart() * ratio, view.getEnd() * ratio);
        }
        return;
    }

    const double factor = std::pow (2.0, (double) -w.deltaY * 2.0);
    zoomBy (factor, xToFreq (juce::jlimit (plot.getX(), plot.getRight(), e.position.x), view, plot));
}

bool AnalyzerView::keyPressed (const juce::KeyPress& k)
{
    switch (juce::CharacterFunctions::toUpperCase (k.getTextCharacter()))
    {
        case 'R': resetMeters();   return true;
        case 'F': toggleFreeze();  return true;
        case '0': resetZoom();     return true;
        case '+': case '=': zoomBy (0.7); return true;
        case '-': zoomBy (1.0 / 0.7);     return true;
        case 'I': processor.inspectorEnabled.store (! processor.inspectorEnabled.load()); if (onStateChanged) onStateChanged(); return true;
        default: break;
    }
    return false;
}
