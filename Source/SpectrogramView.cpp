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

    constexpr int kGutter = 44;        // frequency labels, both sides
    constexpr int kTop = 12;           // keeps the top label (e.g. 20k) fully visible
    constexpr int kStrip = 38;         // bottom control strip
    constexpr int kStripMin = 12;      // bottom margin when the strip has nothing to show
    constexpr int kScaleW = 58;
    const juce::Colour kFrame (0xff2a2b2e), kFrameLine (0xff3a3c40), kLabel (0xffc8cacd);

    juce::String fmtLoud (double v) { return std::isfinite (v) ? juce::String (v, 1) : juce::String ("--.-"); }
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
SpectrogramView::SpectrogramView (Settings& st, AudioEngine& e, std::function<void (Change)> n)
    : s (st), engine (e), notify (std::move (n))
{
    setOpaque (true);

    for (auto* b : { &pause, &down, &up, &full, &lufsBtn, &resetBtn })
    {
        addAndMakeVisible (b);
        b->setMouseCursor (juce::MouseCursor::NormalCursor);
        b->setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3c41));
    }
    pause.setTooltip ("Pause (Space)");
    down.setTooltip ("Reference level -1 dB (mouse wheel / Down)");
    up.setTooltip ("Reference level +1 dB (mouse wheel / Up)");
    full.setTooltip ("Full screen (F11, Esc to leave)");
    lufsBtn.setTooltip ("Show or hide the loudness meter");
    lufsBtn.setClickingTogglesState (true);
    lufsBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff4c8dff));
    resetBtn.setTooltip ("Restart integrated loudness and true-peak hold");

    pause.onClick   = [this] { togglePause(); };
    down.onClick    = [this] { s.refLevel = juce::jmax (-80.0, s.refLevel - 1); notify (Change::Visual); };
    up.onClick      = [this] { s.refLevel = juce::jmin (0.0, s.refLevel + 1);   notify (Change::Visual); };
    full.onClick    = [this] { if (onToggleFullscreen) onToggleFullscreen(); };
    lufsBtn.onClick = [this] { s.showLufs = lufsBtn.getToggleState(); notify (Change::Layout); };
    resetBtn.onClick = [this] { engine.resetLoudness(); };

    ref.setJustificationType (juce::Justification::centred);
    ref.setColour (juce::Label::backgroundColourId, juce::Colour (0xff1e1f22));
    ref.setColour (juce::Label::outlineColourId, juce::Colour (0xff4a4c51));
    readout.setColour (juce::Label::textColourId, kLabel.withAlpha (0.8f));
    lufs.setJustificationType (juce::Justification::centredRight);
    lufs.setColour (juce::Label::textColourId, kLabel);
    lufs.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    for (auto* l : { &ref, &readout, &lufs }) { addAndMakeVisible (l); l->setMouseCursor (juce::MouseCursor::NormalCursor); }

    buildLut();
    startTimerHz (60);
}

void SpectrogramView::togglePause()
{
    paused = ! paused;
    pause.setButtonText (paused ? ">" : "||");
}

void SpectrogramView::clearCursor()
{
    cursorF.reset();
    readout.setText ({}, juce::dontSendNotification);
    repaint();
}

void SpectrogramView::setFullscreenState (bool fs) { full.setButtonText (fs ? "Exit full screen" : "Full screen"); }

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
    return (float) (plot.getBottom() - std::log (f / lo) / std::log (hi / lo) * plot.getHeight());
}

double SpectrogramView::yToFreq (float y) const
{
    const auto [lo, hi] = effectiveRange();
    return lo * std::pow (hi / lo, (plot.getBottom() - y) / (double) plot.getHeight());
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
    if (! (v > 0)) return 0;
    if (v > 1) v = 1;
    v += s.compExp * v * (1 - v);                               // Compress/Expand: 2nd-order bend of the colour curve
    return juce::jlimit (0, (int) lut.size() - 1, (int) (v * (double) (lut.size() - 1)));
}

void SpectrogramView::resized() { rebuild(); }

void SpectrogramView::layoutStrip()
{
    const bool controls = ! s.hidePanel;
    for (auto* c : std::initializer_list<juce::Component*> { &pause, &ref, &up, &down, &full, &lufsBtn, &readout })
        c->setVisible (controls);
    lufs.setVisible (s.showLufs); resetBtn.setVisible (s.showLufs);
    lufsBtn.setToggleState (s.showLufs, juce::dontSendNotification);
    if (strip.getHeight() < kStrip) return;

    auto r = strip.reduced (kGutter, 6);
    if (s.showLufs)
    {
        resetBtn.setBounds (r.removeFromRight (54)); r.removeFromRight (8);
        lufs.setBounds (r.removeFromRight (juce::jmin (440, r.getWidth() / 2)));
    }
    // centre group, like RME's: [||] [Ref: -20 dB] [+] [-]  [Full screen] [LUFS]
    const int groupW = 34 + 4 + 96 + 4 + 30 + 4 + 30 + 14 + 96 + 4 + 50;
    auto g = juce::Rectangle<int> (strip.getCentreX() - groupW / 2, r.getY(), groupW, r.getHeight());
    if (g.getRight() > r.getRight()) g.setX (juce::jmax (r.getX(), r.getRight() - groupW));
    pause.setBounds (g.removeFromLeft (34)); g.removeFromLeft (4);
    ref.setBounds (g.removeFromLeft (96));   g.removeFromLeft (4);
    up.setBounds (g.removeFromLeft (30));    g.removeFromLeft (4);
    down.setBounds (g.removeFromLeft (30));  g.removeFromLeft (14);
    full.setBounds (g.removeFromLeft (96));  g.removeFromLeft (4);
    lufsBtn.setBounds (g.removeFromLeft (50));
    readout.setBounds (r.withRight (pause.getX() - 8));
}

void SpectrogramView::rebuild()
{
    const bool stripUsed = ! s.hidePanel || s.showLufs;
    auto area = getLocalBounds();
    strip = area.removeFromBottom (stripUsed ? kStrip : kStripMin);
    if (s.showScale) area.removeFromRight (kScaleW);
    area.removeFromTop (kTop);
    plot = area.reduced (kGutter, 0);
    if (plot.getWidth() < 10 || plot.getHeight() < 10) plot.setSize (juce::jmax (10, plot.getWidth()), juce::jmax (10, plot.getHeight()));
    layoutStrip();

    const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
    W = plot.getWidth();
    H = juce::jlimit (1, AudioEngine::kMaxRows, juce::roundToInt (plot.getHeight() * scale));
    img = juce::Image (juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
    hist.assign ((size_t) W * (size_t) H, -std::numeric_limits<float>::infinity());
    ref.setText ("Ref: " + juce::String (s.refLevel, 0) + " dB", juce::dontSendNotification);
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
    ref.setText ("Ref: " + juce::String (s.refLevel, 0) + " dB", juce::dontSendNotification);
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

void SpectrogramView::updateLufs()
{
    if (! s.showLufs) return;
    if (! running) { lufs.setText ("Short-Term --.- LUFS   Integrated --.- LUFS   True Peak --.- dBTP", juce::dontSendNotification); return; }
    lufs.setText ("Short-Term " + fmtLoud (engine.getShortTermLufs()) + " LUFS   Integrated "
                  + fmtLoud (engine.getIntegratedLufs()) + " LUFS   True Peak " + fmtLoud (engine.getTruePeakDb()) + " dBTP",
                  juce::dontSendNotification);
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
        if (got > 0) repaint (plot);
    }
    if (++tick % 6 == 0)
    {
        updateLufs();
        if (cursorF) { readout.setText (cursorText(), juce::dontSendNotification); repaint (plot); }
    }
}

juce::String SpectrogramView::cursorText() const
{
    if (! cursorF) return {};
    const double f = *cursorF;
    auto hz = [] (double v) { return v >= 1000 ? juce::String (v / 1000, v >= 10000 ? 2 : 3) + " kHz" : juce::String (v, 1) + " Hz"; };
    const double rf = s.noteRef > 0 ? s.noteRef : 440.0;
    const int semis = juce::roundToInt (12 * std::log2 (f / rf)), midi = 69 + semis;
    const double cents = 1200 * std::log2 (f / (rf * std::pow (2.0, semis / 12.0)));
    const juce::String note = juce::String (noteNames[((midi % 12) + 12) % 12]) + juce::String (midi / 12 - 1);
    juce::String t = s.noteRef > 0 ? note + "  " + hz (f)
                                   : hz (f) + "  (" + note + (cents >= 0 ? " +" : " ") + juce::String (juce::roundToInt (cents)) + " ct)";
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
    double f = yToFreq (juce::jlimit ((float) plot.getY(), (float) plot.getBottom(), y));
    if (s.noteRef > 0)
    {
        const int semis = juce::roundToInt (12 * std::log2 (f / s.noteRef));
        f = s.noteRef * std::pow (2.0, semis / 12.0);
    }
    cursorF = f;
    readout.setText (cursorText(), juce::dontSendNotification);
    repaint (plot.expanded (2));
}

void SpectrogramView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && plot.contains (e.getPosition())) setCursorFromY ((float) e.y);
}
void SpectrogramView::mouseDrag (const juce::MouseEvent& e)
{
    if (e.mods.isLeftButtonDown() && plot.contains (e.getMouseDownPosition())) setCursorFromY ((float) e.y);
}
void SpectrogramView::mouseDoubleClick (const juce::MouseEvent& e) { if (plot.contains (e.getPosition())) clearCursor(); }
void SpectrogramView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-6f) return;
    s.refLevel = juce::jlimit (-80.0, 0.0, s.refLevel + (w.deltaY > 0 ? 1 : -1));
    notify (Change::Visual);
}

//==============================================================================
void SpectrogramView::paint (juce::Graphics& g)
{
    g.fillAll (kFrame);
    g.setColour (juce::Colours::black); g.fillRect (plot);
    g.setColour (kFrameLine); g.drawRect (plot.expanded (1));
    if (strip.getHeight() >= kStrip) g.drawHorizontalLine (strip.getY(), 0.0f, (float) getWidth());

    // spectrogram image
    {
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot);
        g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality);
        const int px = plot.getX(), py = plot.getY(), ph = plot.getHeight();
        if (s.stationary)
            g.drawImage (img, px, py, W, ph, 0, 0, W, H);
        else
        {
            const int a = W - writeCol;
            if (a > 0)        g.drawImage (img, px, py, a, ph, writeCol, 0, a, H);
            if (writeCol > 0) g.drawImage (img, px + a, py, writeCol, ph, 0, 0, writeCol, H);
        }
    }

    // frequency labels (both margins, like RME) and grid
    const auto [lo, hi] = effectiveRange();
    const bool hGrid = s.grid == 1 || s.grid == 3, vGrid = s.grid == 2 || s.grid == 3;
    g.setFont (juce::FontOptions (11.5f));
    float lastY = 1.0e9f;
    for (double dec = 1; dec <= 100000; dec *= 10)
        for (int m = 1; m <= 9; ++m)
        {
            const double f = m * dec;
            if (f < lo * 0.999 || f > hi * 1.001) continue;
            const float y = freqToY (f);
            const bool major = m == 1 || m == 2 || m == 5;
            if (hGrid)
            {
                g.setColour (juce::Colours::white.withAlpha (major ? 0.16f : 0.06f));
                g.drawHorizontalLine (juce::roundToInt (y), (float) plot.getX(), (float) plot.getRight());
            }
            if (lastY - y < 12.0f) continue;               // keep labels from overlapping
            lastY = y;
            const juce::String lbl = f >= 1000 ? juce::String (f / 1000) + "k" : juce::String ((int) f);
            const int ty = juce::roundToInt (y) - 7;
            g.setColour (kLabel);
            g.drawText (lbl, 0, ty, kGutter - 6, 14, juce::Justification::centredRight);
            g.drawText (lbl, plot.getRight() + 6, ty, kGutter - 6, 14, juce::Justification::centredLeft);
        }
    if (vGrid && s.speed > 0)
        for (int sec = 1; sec * s.speed < W; ++sec)
        {
            const int x = plot.getX() + juce::roundToInt (s.stationary ? sec * s.speed : W - sec * s.speed);
            g.setColour (juce::Colours::white.withAlpha (0.12f)); g.drawVerticalLine (x, (float) plot.getY(), (float) plot.getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.45f)); g.setFont (juce::FontOptions (11.0f));
            g.drawText ((s.stationary ? "" : "-") + juce::String (sec) + " s", x + 3, plot.getY() + 3, 50, 12, juce::Justification::left);
        }

    if (s.stationary && running)
    {
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawVerticalLine (plot.getX() + writeCol, (float) plot.getY(), (float) plot.getBottom());
    }

    // colour scale: 0 .. -120 dBFS
    if (s.showScale)
    {
        const int x0 = getWidth() - kScaleW + 4, top = plot.getY(), sh = plot.getHeight();
        for (int i = 0; i < sh; ++i)
        {
            const auto& p = lut[(size_t) lutIndex ((float) (-120.0 * i / sh))];
            g.setColour (juce::Colour (p.getRed(), p.getGreen(), p.getBlue())); g.fillRect (x0, top + i, 14, 1);
        }
        g.setColour (kFrameLine); g.drawRect (x0 - 1, top - 1, 16, sh + 2);
        g.setColour (kLabel); g.setFont (juce::FontOptions (11.0f));
        for (int db = 0; db >= -120; db -= 20)
            g.drawText (juce::String (db), x0 + 18, top + (int) (-db / 120.0 * sh) - 6, 36, 12, juce::Justification::left);
        const float yr = top + (float) (-s.refLevel / 120.0 * sh);
        juce::Path tri; tri.addTriangle ((float) x0 - 1, yr, (float) x0 - 6, yr - 4, (float) x0 - 6, yr + 4);
        g.setColour (juce::Colours::white); g.fillPath (tri);
    }

    // frequency cursor: solid line
    if (cursorF)
    {
        const int y = juce::roundToInt (freqToY (*cursorF));
        g.setColour (juce::Colours::white.withAlpha (0.9f));
        g.drawHorizontalLine (y, (float) plot.getX(), (float) plot.getRight());
        const auto txt = cursorText();
        g.setFont (juce::FontOptions (12.0f));
        const int tw = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), txt) + 12;
        const int bx = juce::jmax (plot.getX() + 4, plot.getRight() - tw - 8);
        const int by = juce::jlimit (plot.getY() + 2, plot.getBottom() - 20, y - 22);
        g.setColour (juce::Colours::black.withAlpha (0.7f)); g.fillRect (bx, by, tw, 18);
        g.setColour (juce::Colours::white); g.drawText (txt, bx + 6, by, tw - 6, 18, juce::Justification::centredLeft);
    }

    if (! running)
    {
        g.setColour (juce::Colours::white.withAlpha (0.55f)); g.setFont (juce::FontOptions (15.0f));
        g.drawText ("Choose a source above and press Start", plot.withHeight (20).withY (plot.getCentreY() - 22), juce::Justification::centred);
        g.setColour (juce::Colours::white.withAlpha (0.35f)); g.setFont (juce::FontOptions (12.0f));
        g.drawText ("\"Test signal\" works without an interface", plot.withHeight (16).withY (plot.getCentreY() + 2), juce::Justification::centred);
    }
}
