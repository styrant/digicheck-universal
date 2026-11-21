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
    constexpr int kBottom = 12;        // keeps the bottom label (e.g. 20) fully visible
    constexpr int kScaleW = 58;
    const juce::Colour kFrame (0xff2a2b2e), kFrameLine (0xff3a3c40), kLabel (0xffc8cacd);

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
    buildLut();
}

//==============================================================================
// Renderer selection and FPS
void SpectrogramView::setGpuMode (bool useGpu)
{
    if (gpu == useGpu) return;
    gpu = useGpu;
    setOpaque (! gpu);                      // GPU: the plot is left transparent so the GL texture shows through
    {
        const juce::ScopedLock sl (glLock);
        fullDirty = true;
    }
    repaintAll();
    updateGlArea();
    repaint();
}

double SpectrogramView::measureFps()
{
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const int n = frames.exchange (0);
    const double dt = now - fpsLastTime;
    fpsLastTime = now;
    return dt > 0 && dt < 10 ? n / dt : 0.0;
}

void SpectrogramView::updateGlArea()
{
    const juce::ScopedLock sl (glLock);
    glArea = plot.toFloat() + getPosition().toFloat();      // the owner (OpenGL context component) is our parent
    glVisible = isVisible() && gpu;
    glStationary = s.stationary;
}

juce::uint32 SpectrogramView::rgbaFor (float db) const noexcept
{
    if (std::isnan (db)) return 0xffffffffu;
    const auto& p = lut[(size_t) lutIndex (db)];
    return (juce::uint32) p.getRed() | ((juce::uint32) p.getGreen() << 8) | ((juce::uint32) p.getBlue() << 16) | 0xff000000u;
}

void SpectrogramView::togglePause()
{
    paused = ! paused;
    if (onPauseChanged) onPauseChanged (paused);
}

void SpectrogramView::clearCursor()
{
    cursorF.reset();
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

void SpectrogramView::rebuild()
{
    auto area = getLocalBounds();
    area.removeFromBottom (kBottom);
    if (s.showScale) area.removeFromRight (kScaleW);
    area.removeFromTop (kTop);
    plot = area.reduced (kGutter, 0);
    if (plot.getWidth() < 10 || plot.getHeight() < 10) plot.setSize (juce::jmax (10, plot.getWidth()), juce::jmax (10, plot.getHeight()));

    // Keep what is on screen across resizes / layout changes: carry the newest columns over and
    // rescale them to the new number of rows (same log-frequency axis, so rows map proportionally).
    const int oldW = W, oldH = H, oldWrite = writeCol;
    std::vector<float> old;
    old.swap (hist);

    const float scale = juce::Component::getApproximateScaleFactorForComponent (this);
    W = plot.getWidth();
    H = juce::jlimit (1, AudioEngine::kMaxRows, juce::roundToInt (plot.getHeight() * scale));
    img = juce::Image (juce::Image::ARGB, W, H, true, juce::SoftwareImageType());
    hist.assign ((size_t) W * (size_t) H, -std::numeric_limits<float>::infinity());
    pendCap = 512; pendHead = pendCount = 0;
    pend.assign ((size_t) pendCap * (size_t) H, 0.0f);
    {
        const juce::ScopedLock sl (glLock);
        gpix.assign ((size_t) W * (size_t) H, 0xff000000u);
        dirtyCols.clear(); fullDirty = true; glW = W; glH = H; glScroll = 0;
    }

    int keep = 0;
    if (old.size() == (size_t) oldW * (size_t) oldH && oldW > 1 && oldH > 1)
    {
        keep = juce::jmin (oldW, W);
        for (int k = 0; k < keep; ++k)
        {
            const float* src = old.data() + (size_t) ((oldWrite - keep + k + oldW) % oldW) * (size_t) oldH;
            float* dst = hist.data() + (size_t) k * (size_t) H;
            for (int j = 0; j < H; ++j)
                dst[j] = src[juce::jmin (oldH - 1, (int) ((j + 0.5) * oldH / H))];
        }
    }
    sendConfig (keep == 0);
    if (keep > 0) { writeCol = keep % W; repaintAll(); }
    updateGlArea();
    repaint();
}

void SpectrogramView::sendConfig (bool clearHistory)
{
    Reassigner::Config c;
    c.fftSize = s.fftSize; c.pixelsPerSecond = s.speed; c.rows = H;
    std::tie (c.fmin, c.fmax) = s.freqRange();
    c.reassign = s.reassign; c.shortRange = s.shortRange; c.channel = s.channel;
    c.floorDb = s.refLevel - s.range - 20;
    engine.setConfig (c);
    if (! clearHistory) return;
    std::fill (hist.begin(), hist.end(), -std::numeric_limits<float>::infinity());
    img.clear (img.getBounds(), juce::Colours::black);
    writeCol = 0; pendHead = pendCount = 0; credit = 0;
    const juce::ScopedLock sl (glLock);
    std::fill (gpix.begin(), gpix.end(), 0xff000000u);
    fullDirty = true; glScroll = 0;
}

void SpectrogramView::settingsChanged (Change c)
{
    switch (c)
    {
        case Change::Visual:  engine.setFloorDb (s.refLevel - s.range - 20); buildLut(); repaintAll(); break;
        case Change::Overlay: updateGlArea(); break;
        case Change::Layout:  rebuild(); break;
        case Change::Dsp:     sendConfig(); break;
        case Change::Bar:     break;
    }
    repaint();
}

void SpectrogramView::sourceStarted() { running = true; sendConfig(); repaint(); }
void SpectrogramView::sourceStopped() { running = false; pendHead = pendCount = 0; repaint(); }

void SpectrogramView::repaintAll()
{
    if (gpu)
    {
        const juce::ScopedLock sl (glLock);
        for (size_t i = 0; i < hist.size() && i < gpix.size(); ++i) gpix[i] = rgbaFor (hist[i]);
        fullDirty = true;
        return;
    }
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

void SpectrogramView::commitColumn (const float* col, std::optional<juce::Image::BitmapData>& bd)
{
    std::copy (col, col + H, hist.data() + (size_t) writeCol * (size_t) H);
    if (gpu)
    {
        auto* dst = gpix.data() + (size_t) writeCol * (size_t) H;      // caller holds glLock
        for (int j = 0; j < H; ++j) dst[j] = rgbaFor (col[j]);
        dirtyCols.push_back (writeCol);
    }
    else
    {
        if (! bd) bd.emplace (img, juce::Image::BitmapData::writeOnly);
        for (int j = 0; j < H; ++j)
        {
            auto* p = reinterpret_cast<juce::PixelARGB*> (bd->getPixelPointer (writeCol, H - 1 - j));
            const float v = col[j];
            *p = std::isnan (v) ? juce::PixelARGB (255, 255, 255, 255) : lut[(size_t) lutIndex (v)];
        }
    }
    writeCol = (writeCol + 1) % W;
}

void SpectrogramView::onVBlank (double t)
{
    const double dt = lastVBlank > 0 ? juce::jlimit (0.0, 0.1, t - lastVBlank) : 1.0 / 60.0;
    lastVBlank = t;

    // 1. take everything the DSP thread has produced into the jitter buffer
    if (running)
        engine.popColumns ([&] (const float* col, int rows)
        {
            if (paused || rows != H || pendCap == 0) return;
            if (pendCount == pendCap) { pendHead = (pendHead + 1) % pendCap; --pendCount; }   // overflow: drop oldest
            const int slot = (pendHead + pendCount) % pendCap;
            std::copy (col, col + H, pend.data() + (size_t) slot * (size_t) H);
            ++pendCount;
        });

    // 2. release columns at the display speed, nudged gently to keep ~30 ms buffered
    const double rate = s.speed;
    const double target = rate * 0.03 + 2.0;
    if (pendCount > target * 4 + rate * 0.25)                 // far behind (e.g. after a stall): catch up
    {
        const int drop = pendCount - (int) target;
        pendHead = (pendHead + drop) % pendCap; pendCount -= drop;
    }
    const double corr = juce::jlimit (0.85, 1.2, 1.0 + (pendCount - target) / (4.0 * target));
    credit += rate * dt * corr;
    int n = juce::jmin ((int) credit, pendCount);
    credit -= n;
    if (pendCount - n == 0) credit = juce::jmin (credit, 0.999);   // don't build up credit while starved

    std::optional<juce::Image::BitmapData> bd;
    {
        const juce::ScopedLock sl (glLock);
        for (; n > 0; --n)
        {
            commitColumn (pend.data() + (size_t) pendHead * (size_t) H, bd);
            pendHead = (pendHead + 1) % pendCap; --pendCount;
            if (! gpu) ++tick;
        }
        // sub-pixel scroll: how far the next (already buffered) column has moved in
        glScroll = writeCol + (pendCount > 0 && ! s.stationary ? credit : 0.0);
    }
    const bool changed = bd.has_value() || (gpu && running);
    bd.reset();

    if (! gpu && changed) { repaint (plot); }
    else if (gpu && s.stationary && running) repaint (plot);   // moving write line
    if (++tick % 6 == 0 && cursorF) repaint (plot);             // keep the cursor's level readout current
}

//==============================================================================
// OpenGL renderer (render thread)
void SpectrogramView::glInit()
{
    using namespace juce::gl;
    auto* ctx = juce::OpenGLContext::getCurrentContext();
    if (ctx == nullptr) return;

    const juce::String vs = R"(
        attribute vec2 position;
        attribute vec2 uv;
        varying vec2 vUv;
        void main() { vUv = uv; gl_Position = vec4 (position, 0.0, 1.0); })";
    const juce::String fs = R"(
        varying vec2 vUv;
        uniform sampler2D tex;
        void main() { gl_FragColor = texture2D (tex, vec2 (vUv.y, vUv.x)); })";   // texture: x = frequency, y = time

    shader = std::make_unique<juce::OpenGLShaderProgram> (*ctx);
    if (! shader->addVertexShader (juce::OpenGLHelpers::translateVertexShaderToV3 (vs))
        || ! shader->addFragmentShader (juce::OpenGLHelpers::translateFragmentShaderToV3 (fs))
        || ! shader->link())
    {
        DBG ("Spectrogram shader failed: " << shader->getLastError());
        shader.reset();
        return;
    }
    glGenTextures (1, &texId);
    glGenBuffers (1, &vbo);
    glGenVertexArrays (1, &vao);
    texW = texH = 0;
    const juce::ScopedLock sl (glLock);
    fullDirty = true;
}

void SpectrogramView::glRelease()
{
    using namespace juce::gl;
    if (texId != 0) glDeleteTextures (1, &texId);
    if (vbo != 0)   glDeleteBuffers (1, &vbo);
    if (vao != 0)   glDeleteVertexArrays (1, &vao);
    texId = vbo = vao = 0;
    shader.reset();
}

void SpectrogramView::glRender (float ownerW, float ownerH)
{
    using namespace juce::gl;
    if (shader == nullptr || texId == 0 || ownerW <= 0 || ownerH <= 0) return;

    juce::Rectangle<float> area;
    double scroll; bool stationary; int w, h;
    glBindTexture (GL_TEXTURE_2D, texId);
    {
        const juce::ScopedLock sl (glLock);
        if (! glVisible) { glBindTexture (GL_TEXTURE_2D, 0); return; }
        area = glArea; scroll = glScroll; stationary = glStationary; w = glW; h = glH;
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        if (fullDirty || texW != h || texH != w)
        {
            // texture is (frequency x time): one texture row per display column, uploaded as-is
            glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, h, w, 0, GL_RGBA, GL_UNSIGNED_BYTE, gpix.data());
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            texW = h; texH = w;
            fullDirty = false;
        }
        else
            for (int x : dirtyCols)                               // only the new columns
                if (x >= 0 && x < w)
                    glTexSubImage2D (GL_TEXTURE_2D, 0, 0, x, h, 1, GL_RGBA, GL_UNSIGNED_BYTE, gpix.data() + (size_t) x * (size_t) h);
        dirtyCols.clear();
    }

    // time coordinate: scrolling shows the newest column at the right edge; stationary shows 0..1
    const float tRight = stationary ? 1.0f : (float) ((scroll - 0.5) / w);
    const float tLeft  = stationary ? 0.0f : tRight - 1.0f;
    auto nx = [&] (float x) { return x / ownerW * 2.0f - 1.0f; };
    auto ny = [&] (float y) { return 1.0f - y / ownerH * 2.0f; };
    const float x0 = nx (area.getX()), x1 = nx (area.getRight()), y0 = ny (area.getBottom()), y1 = ny (area.getY());
    const float verts[] = { x0, y0, tLeft, 0.0f,   x1, y0, tRight, 0.0f,   x0, y1, tLeft, 1.0f,   x1, y1, tRight, 1.0f };

    glBindVertexArray (vao);
    glBindBuffer (GL_ARRAY_BUFFER, vbo);
    glBufferData (GL_ARRAY_BUFFER, sizeof (verts), verts, GL_STREAM_DRAW);
    shader->use();
    shader->setUniform ("tex", 0);
    const auto pos = (GLuint) glGetAttribLocation (shader->getProgramID(), "position");
    const auto uv  = (GLuint) glGetAttribLocation (shader->getProgramID(), "uv");
    glEnableVertexAttribArray (pos);
    glVertexAttribPointer (pos, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), nullptr);
    glEnableVertexAttribArray (uv);
    glVertexAttribPointer (uv, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float), (void*) (2 * sizeof (float)));
    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D, texId);
    glDisable (GL_BLEND);
    glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
    glDisableVertexAttribArray (pos);
    glDisableVertexAttribArray (uv);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    glBindVertexArray (0);
    glBindTexture (GL_TEXTURE_2D, 0);
    frames.fetch_add (1);
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
void SpectrogramView::mouseUp (const juce::MouseEvent&)            { clearCursor(); }     // cursor shows only while held
void SpectrogramView::mouseDoubleClick (const juce::MouseEvent&)   {}
void SpectrogramView::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (std::abs (w.deltaY) < 1.0e-6f) return;
    s.refLevel = juce::jlimit (-80.0, 0.0, s.refLevel + (w.deltaY > 0 ? 1 : -1));
    notify (Change::Visual);
}

//==============================================================================
void SpectrogramView::paint (juce::Graphics& g)
{
    if (gpu)
    {
        // the GL texture is drawn underneath: paint only the frame around the plot
        juce::Graphics::ScopedSaveState ss (g);
        g.excludeClipRegion (plot);
        g.fillAll (kFrame);
    }
    else
    {
        g.fillAll (kFrame);
        g.setColour (juce::Colours::black); g.fillRect (plot);
        frames.fetch_add (running ? 1 : 0);
    }
    g.setColour (kFrameLine); g.drawRect (plot.expanded (1));

    // spectrogram image (CPU renderer)
    if (! gpu)
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
