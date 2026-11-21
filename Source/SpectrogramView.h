#pragma once
#include <JuceHeader.h>
#include "UiScale.h"
#include "Settings.h"
#include "AudioEngine.h"
#include <optional>

// The Spectroscope display, laid out like DigiCheck NG: a black plot inside a grey frame with
// frequency labels in both side margins. The control strip lives in BottomBar (shared by both views).
//
// Frame pacing: updates are driven by the monitor's vertical blank (60/120/144 Hz...). Columns from
// the DSP thread arrive in bursts, so they go into a small jitter buffer (~30 ms) and are released
// at exactly the display speed (px/s), giving an even scroll every frame.
//
// Two renderers:
//  - GPU (OpenGL, default): the history lives in a texture; each frame uploads only the new columns
//    and the scroll is a texture offset with sub-pixel precision. The owner's OpenGLContext calls
//    glInit / glRender / glRelease on its render thread. Labels, grid and cursor are still painted as
//    a normal component on top (the plot area is left transparent).
//  - CPU: a software image, repainted on each vblank that has new columns.
// Columns are one logical pixel wide; rows use full physical (DPI-scaled) resolution.
class SpectrogramView : public juce::Component
{
public:
    SpectrogramView (Settings& s, AudioEngine& e, std::function<void (Change)> notify);

    void settingsChanged (Change c);        // called after any settings change
    void sourceStarted();
    void sourceStopped();
    void togglePause();
    bool isPaused() const noexcept { return paused; }
    void clearCursor();
    void setCursorFrequency (double hz) { cursorF = hz; repaint(); }
    std::function<void (bool paused)> onPauseChanged;

    // Renderer selection (message thread)
    void setGpuMode (bool useGpu);
    bool isGpuMode() const noexcept { return gpu; }
    double measureFps();                    // frames drawn per second since the last call

    // OpenGL hooks, called by the owner's OpenGLContext on its render thread
    void glInit();
    void glRender (float ownerWidth, float ownerHeight);   // owner (context component) size in logical px
    void glRelease();

    void paint (juce::Graphics&) override;
    void resized() override;
    void moved() override { updateGlArea(); }
    void visibilityChanged() override { updateGlArea(); }
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::Colour colourAt (int map, double x);     // 0..1 along a colour map
    static const juce::StringArray& colourNames();

private:
    void onVBlank (double timestampSec);
    void commitColumn (const float* col, std::optional<juce::Image::BitmapData>& bd);
    void rebuild();
    void sendConfig (bool clearHistory = true);
    void buildLut();
    void repaintAll();
    void updateGlArea();
    juce::uint32 rgbaFor (float db) const noexcept;
    int lutIndex (float db) const noexcept;
    std::pair<double, double> effectiveRange() const;
    float freqToY (double f) const;
    double yToFreq (float y) const;
    void setCursorFromY (float y);
    juce::String cursorText() const;

    Settings& s;
    AudioEngine& engine;
    std::function<void (Change)> notify;

    juce::Rectangle<int> plot;
    juce::Image img;                        // CPU renderer
    std::vector<float> hist;                // dB history, column-major (W columns x H rows, row 0 = lowest freq)
    int W = 1, H = 1, writeCol = 0, tick = 0;
    std::array<juce::PixelARGB, 512> lut;
    bool paused = false, running = false, gpu = false;
    std::optional<double> cursorF;

    // jitter buffer between the DSP thread and the display
    std::vector<float> pend;
    int pendCap = 0, pendHead = 0, pendCount = 0;
    double lastVBlank = 0, credit = 0;
    // Smooth scrolling: the picture moves a whole number of pixels per monitor frame (sub-pixel
    // offsets made thin lines shimmer). The scroll speed is snapped to k pixels per frame when that
    // is within 20% of the chosen speed, so every frame moves by the same amount.
    double effSpeed = 300.0, refreshHz = 60.0, lastHz = 0.0;
    std::vector<double> vblankDts;
    double effectiveSpeedFor (double refresh) const;
    void updateRefresh (double dt);
    bool needsRepaint = false;

    // shared with the GL render thread (guarded by glLock)
    juce::CriticalSection glLock;
    std::vector<juce::uint32> gpix;         // colours, column-major (one texture row per display column)
    std::vector<int> dirtyCols;
    bool fullDirty = true, glVisible = false, glStationary = false;
    int glW = 1, glH = 1;
    double glScroll = 0;                    // newest column position incl. sub-pixel fraction
    juce::Rectangle<float> glArea;          // plot area in the owner's logical coordinates

    // GL objects (render thread only)
    std::unique_ptr<juce::OpenGLShaderProgram> shader;
    juce::uint32 texId = 0, vbo = 0, vao = 0;
    int texW = 0, texH = 0;

    std::atomic<int> frames { 0 };
    double fpsLastTime = 0;

    juce::VBlankAttachment vblank { this, [this] (double t) { onVBlank (t); } };
};
