#include "SpectrogramView.h"

namespace
{
    struct Stop { double x; juce::uint8 r, g, b; };
    const std::vector<std::vector<Stop>>& maps()
    {
        static const std::vector<std::vector<Stop>> m {
            { { 0, 0, 0, 0 }, { .25, 24, 32, 170 }, { .5, 200, 30, 60 }, { .75, 250, 200, 40 }, { 1, 255, 255, 255 } },
            { { 0, 0, 0, 0 }, { .33, 10, 110, 40 }, { .66, 220, 220, 40 }, { 1, 255, 255, 255 } },
            { { 0, 0, 0, 0 }, { .33, 10, 40, 140 }, { .66, 40, 200, 230 }, { 1, 255, 255, 255 } },
            { { 0, 0, 0, 0 }, { .33, 130, 10, 10 }, { .66, 250, 130, 20 }, { 1, 255, 255, 255 } },
            { { 0, 0, 0, 0 }, { .33, 70, 20, 130 }, { .66, 230, 70, 150 }, { 1, 255, 255, 255 } },
            { { 0, 0, 0, 0 }, { 1, 255, 255, 255 } } };
        return m;
    }
    const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    constexpr int kScaleW = 64;
}

const juce::StringArray& SpectrogramView::colourNames()
{
    static const juce::StringArray n { "blue-red-yellow-white", "green-yellow-white", "blue-cyan-white",
                                       "red-orange-white", "purple-pink-white", "grey-white" };
    return n;
}

juce::Colour SpectrogramView::colourAt (int map, double x)
{
    const auto& st = maps()[(size_t) juce::jlimit (0, (int) maps().size() - 1, map)];
    size_t a = 0;
    while (a < st.size() - 2 && x > st[a + 1].x) ++a;
    const auto& p = st[a]; const auto& q = st[a + 1];
    const double t = juce::jlimit (0.0, 1.0, (x - p.x) / (q.x - p.x));
    return juce::Colour ((juce::uint8) juce::roundToInt (p.r + (q.r - p.r) * t),
                         (juce::uint8) juce::roundToInt (p.g + (q.g - p.g) * t),
                         (juce::uint8) juce::roundToInt (p.b + (q.b - p.b) * t));
}

//==============================================================================
SpectrogramView::Panel::Panel()
{
    for (auto* b : { &pause, &down, &up }) addAndMakeVisible (b);
    pause.setTooltip ("Pause (Space)");
    down.setTooltip ("Reference level -1 dB (mouse wheel / Down)");
    up.setTooltip ("Reference level +1 dB (mouse wheel / Up)");
    ref.setJustificationType (juce::Justification::centred);
    readout.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.65f));
    readout.setText ("Click the display for a frequency cursor", juce::dontSendNotification);
    addAndMakeVisible (ref); addAndMakeVisible (readout);
}

void SpectrogramView::Panel::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xd0121418)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (juce::Colour (0xff2a2d33)); g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}

void SpectrogramView::Panel::resized()
{
    auto r = getLocalBounds().reduced (5, 4);
    pause.setBounds (r.removeFromLeft (34)); r.removeFromLeft (10);
    down.setBounds (r.removeFromLeft (26));
    ref.setBounds (r.removeFromLeft (64));
    up.setBounds (r.removeFromLeft (26)); r.removeFromLeft (10);
    readout.setBounds (r);
}

//==============================================================================
SpectrogramView::SpectrogramView (Settings& st, AudioEngine& e, std::function<void (Change)> n)
    : s (st), engine (e), notify (std::move (n))
{
    setOpaque (true);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    addAndMakeVisible (panel);
    panel.pause.onClick = [this] { togglePause(); };
    panel.down.onClick  = [this] { s.refLevel = juce::jmax (-80.0, s.refLevel - 1); notify (Change::Visual); };
    panel.up.onClick    = [this] { s.refLevel = juce::jmin (0.0, s.refLevel + 1);   notify (Change::Visual); };
    buildLut();
    startTimerHz (60);
}

void SpectrogramView::togglePause()
{
    paused = ! paused;
    panel.pause.setButtonText (paused ? ">" : "||");
}

void SpectrogramView::clearCursor()
{
    cursorF.reset();
    panel.readout.setText ("Click the display for a frequency cursor", juce::dontSendNotification);
    repaint();
}

std::pair<double, double> SpectrogramView::effectiveRange() const
{
    auto [lo, hi] = s.freqRange();
    const double sr = engine.getSampleRate();
    if (sr > 0) hi = juce::jmin (hi, sr / Reassigner::decimationFor (sr) / 2 * 0.999);
    return { lo, hi };
}

float SpectrogramView::freqToY (double f) const
{
    const auto [lo, hi] = effectiveRange();
    return (float) (getHeight() - std::log (f / lo) / std::log (hi / lo) * getHeight());
}

double SpectrogramView::yToFreq (float y) const
{
    const auto [lo, hi] = effectiveRange();
    return lo * std::pow (hi / lo, (getHeight() - y) / (double) getHeight());
}

void SpectrogramView::buildLut()
{
    for (size_t i = 0; i < lut.size(); ++i)
    {
        const auto c = colourAt (s.colour, (double) i / (double) (lut.size() - 1));
        lut[i] = juce::PixelARGB (255, c.getRed(), c.getGreen(), c.getBlue());
    }
}

int SpectrogramView::lutIndex (float db) const noexcept
{
    double v = (db - (s.refLevel - s.range)) / s.range;
    if (! (v > 0)) return 0;                                    // also catches NaN (handled by caller)
    if (v > 1) v = 1;
    v += s.compExp * v * (1 - v);                               // Compress/Expand: 2nd-order bend of the colour curve
    return juce::jlimit (0, (int) lut.size() - 1, (int) (v * (double) (lut.size() - 1)));
}

void SpectrogramView::resized()
{
    panel.setBounds (8, getHeight() - 38, juce::jmin (getWidth() - 16, 470), 30);
    rebuild();
}

void SpectrogramView::rebuild()
{
    const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
    plotW = juce::jmax (1, getWidth() - (s.showScale ? kScaleW : 0));
    W = plotW;
    H = juce::jlimit (1, AudioEngine::kMaxRows, juce::roundToInt (getHeight() * scale));
    img = juce::Image (juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
    img.clear (img.getBounds(), juce::Colours::black);
    hist.assign ((size_t) W * (size_t) H, -std::numeric_limits<float>::infinity());
    writeCol = 0;
    panel.setVisible (! s.hidePanel);
    panel.ref.setText (juce::String (s.refLevel, 0) + " dB", juce::dontSendNotification);
    sendConfig();
    repaint();
}

void SpectrogramView::sendConfig()
{
    Reassigner::Config c;
    c.fftSize = s.fftSize; c.pixelsPerSecond = s.speed; c.rows = H;
    std::tie (c.fmin, c.fmax) = s.freqRange();
    c.reassign = s.reassign; c.shortRange = s.shortRange; c.channel = s.channel;
    c.floorDb = s.refLevel - s.range - 20;
    engine.setConfig (c);
    std::fill (hist.begin(), hist.end(), -std::numeric_limits<float>::infinity());
    img.clear (img.getBounds(), juce::Colours::black);
    writeCol = 0;
}

void SpectrogramView::settingsChanged (Change c)
{
    panel.ref.setText (juce::String (s.refLevel, 0) + " dB", juce::dontSendNotification);
    panel.setVisible (! s.hidePanel);
    switch (c)
    {
        case Change::Visual:  engine.setFloorDb (s.refLevel - s.range - 20); buildLut(); repaintAll(); break;
        case Change::Overlay: break;
        case Change::Layout:  rebuild(); break;
        case Change::Dsp:     sendConfig(); break;
    }
    repaint();
}

void SpectrogramView::sourceStarted() { running = true; sendConfig(); repaint(); }
void SpectrogramView::sourceStopped() { running = false; repaint(); }

void SpectrogramView::repaintAll()
{
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::writeOnly);
    for (int x = 0; x < W; ++x)
    {
        const float* col = hist.data() + (size_t) x * (size_t) H;
        for (int j = 0; j < H; ++j)
        {
            auto* p = reinterpret_cast<juce::PixelARGB*> (bd.getPixelPointer (x, H - 1 - j));
            const float v = col[j];
            *p = std::isnan (v) ? juce::PixelARGB (255, 255, 255, 255) : lut[(size_t) lutIndex (v)];
        }
    }
}

void SpectrogramView::timerCallback()
{
    if (running)
    {
        std::optional<juce::Image::BitmapData> bd;
        const int got = engine.popColumns ([&] (const float* col, int rows)
        {
            if (paused || rows != H) return;
            if (! bd) bd.emplace (img, juce::Image::BitmapData::writeOnly);
            std::copy (col, col + H, hist.data() + (size_t) writeCol * (size_t) H);
            for (int j = 0; j < H; ++j)
            {
                auto* p = reinterpret_cast<juce::PixelARGB*> (bd->getPixelPointer (writeCol, H - 1 - j));
                const float v = col[j];
                *p = std::isnan (v) ? juce::PixelARGB (255, 255, 255, 255) : lut[(size_t) lutIndex (v)];
            }
            writeCol = (writeCol + 1) % W;
        });
        bd.reset();
        if (got > 0) repaint();
    }
    if (cursorF) panel.readout.setText (cursorText(), juce::dontSendNotification);
}

juce::String SpectrogramView::cursorText() const
{
    if (! cursorF) return {};
    const double f = *cursorF;
    auto hz = [] (double v) { return v >= 1000 ? juce::String (v / 1000, v >= 10000 ? 2 : 3) + " kHz" : juce::String (v, 1) + " Hz"; };
    const double ref = s.noteRef > 0 ? s.noteRef : 440.0;
    const int semis = juce::roundToInt (12 * std::log2 (f / ref)), midi = 69 + semis;
    const double cents = 1200 * std::log2 (f / (ref * std::pow (2.0, semis / 12.0)));
    const juce::String note = juce::String (noteNames[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    juce::String t = s.noteRef > 0 ? note + "  " + hz (f)
                                   : hz (f) + "  (" + note + (cents >= 0 ? " +" : " ") + juce::String (juce::roundToInt (cents)) + " ct)";
    // level at the cursor in the newest column
    const auto [lo, hi] = effectiveRange();
    const int j = (int) (std::log (f / lo) / std::log (hi / lo) * H);
    const int col = (writeCol - 1 + W) % W;
    float lvl = -std::numeric_limits<float>::infinity();
    for (int k = j - 1; k <= j + 1; ++k)
        if (k >= 0 && k < H) lvl = juce::jmax (lvl, hist[(size_t) col * (size_t) H + (size_t) k]);
    if (std::isfinite (lvl) && lvl > -150) t << "   " << juce::String (lvl, 1) << " dBFS";
    return t;
}

void SpectrogramView::setCursorFromY (float y)
{
    double f = yToFreq (juce::jlimit (0.0f, (float) getHeight(), y));
    if (s.noteRef > 0)
    {
        const int semis = juce::roundToInt (12 * std::log2 (f / s.noteRef));
        f = s.noteRef * std::pow (2.0, semis / 12.0);
    }
    cursorF = f;
    panel.readout.setText (cursorText(), juce::dontSendNotification);
    repaint();
}

void SpectrogramView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && e.x < plotW) setCursorFromY ((float) e.y);
}
void SpectrogramView::mouseDrag (const juce::MouseEvent& e)        { if (e.mods.isLeftButtonDown()) setCursorFromY ((float) e.y); }
void SpectrogramView::mouseDoubleClick (const juce::MouseEvent&)   { clearCursor(); }
void SpectrogramView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-6f) return;
    s.refLevel = juce::jlimit (-80.0, 0.0, s.refLevel + (w.deltaY > 0 ? 1 : -1));
    notify (Change::Visual);
}

//==============================================================================
void SpectrogramView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);
    const int h = getHeight();
    g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);

    if (s.stationary)
        g.drawImage (img, 0, 0, W, h, 0, 0, W, H);
    else
    {
        const int a = W - writeCol;
        if (a > 0)        g.drawImage (img, 0, 0, a, h, writeCol, 0, a, H);
        if (writeCol > 0) g.drawImage (img, a, 0, writeCol, h, 0, 0, writeCol, H);
    }

    // frequency grid and labels
    const auto [lo, hi] = effectiveRange();
    const bool hGrid = s.grid == 1 || s.grid == 3, vGrid = s.grid == 2 || s.grid == 3;
    g.setFont (juce::FontOptions (11.0f));
    for (double dec = 1; dec <= 100000; dec *= 10)
        for (int m = 1; m <= 9; ++m)
        {
            const double f = m * dec;
            if (f < lo || f > hi) continue;
            const float y = std::round (freqToY (f)) + 0.5f;
            const bool major = m == 1 || m == 2 || m == 5;
            if (hGrid) { g.setColour (juce::Colours::white.withAlpha (major ? 0.16f : 0.06f)); g.drawHorizontalLine ((int) y, 0.0f, (float) plotW); }
            if (major)
            {
                g.setColour (juce::Colours::white.withAlpha (0.6f));
                const juce::String lbl = f >= 1000 ? juce::String (f / 1000) + "k" : juce::String ((int) f);
                g.drawText (lbl, 4, juce::jlimit (0, h - 14, (int) y - 14), 60, 13, juce::Justification::left);
            }
        }
    if (vGrid && s.speed > 0)
        for (int sec = 1; sec * s.speed < plotW; ++sec)
        {
            const int x = juce::roundToInt (s.stationary ? sec * s.speed : plotW - sec * s.speed);
            g.setColour (juce::Colours::white.withAlpha (0.12f)); g.drawVerticalLine (x, 0.0f, (float) h);
            g.setColour (juce::Colours::white.withAlpha (0.45f));
            g.drawText ((s.stationary ? "" : "-") + juce::String (sec) + " s", x + 3, 3, 50, 12, juce::Justification::left);
        }

    if (s.stationary && running) { g.setColour (juce::Colours::white.withAlpha (0.6f)); g.drawVerticalLine (writeCol, 0.0f, (float) h); }

    // colour scale: 0 .. -120 dBFS
    if (s.showScale)
    {
        const int x0 = plotW + 8, top = 10, sh = h - 20;
        g.setColour (juce::Colour (0xff0b0c0e)); g.fillRect (plotW, 0, kScaleW, h);
        for (int i = 0; i < sh; ++i)
        {
            const auto& p = lut[(size_t) lutIndex ((float) (-120.0 * i / sh))];
            g.setColour (juce::Colour (p.getRed(), p.getGreen(), p.getBlue())); g.fillRect (x0, top + i, 14, 1);
        }
        g.setColour (juce::Colours::white.withAlpha (0.3f)); g.drawRect (x0, top, 14, sh);
        g.setColour (juce::Colours::white.withAlpha (0.7f));
        for (int db = 0; db >= -120; db -= 20)
            g.drawText (juce::String (db), x0 + 18, top + (int) (-db / 120.0 * sh) - 6, 40, 12, juce::Justification::left);
        const float yr = top + (float) (-s.refLevel / 120.0 * sh);
        juce::Path tri; tri.addTriangle ((float) x0 - 1, yr, (float) x0 - 6, yr - 4, (float) x0 - 6, yr + 4);
        g.setColour (juce::Colours::white); g.fillPath (tri);
    }

    // frequency cursor
    if (cursorF)
    {
        const float y = std::round (freqToY (*cursorF)) + 0.5f;
        juce::Path p; p.startNewSubPath (0, y); p.lineTo ((float) plotW, y);
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (p, p, dashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillPath (p);
        const auto txt = cursorText();
        g.setFont (juce::FontOptions (12.0f));
        const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 12;
        const int bx = juce::jmax (4, plotW - tw - 8), by = juce::jlimit (2, h - 20, (int) y - 22);
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRect (bx, by, tw, 18);
        g.setColour (juce::Colours::white); g.drawText (txt, bx + 6, by, tw - 6, 18, juce::Justification::centredLeft);
    }

    if (! running)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f)); g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Choose a source above and press Start", 0, h / 2 - 22, plotW, 20, juce::Justification::centred);
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.setFont (juce::FontOptions (12.0f));
        g.drawText ("\"Test signal\" works without an interface", 0, h / 2 + 2, plotW, 16, juce::Justification::centred);
    }
}
