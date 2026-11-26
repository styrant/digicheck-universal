# Ultimate Audio Viewer (UAV): User Manual

Ultimate Audio Viewer (UAV) is a set of tools for seeing and measuring music: a spectrogram, two spectrum analyzers, tonal balance against a target, a key, chord and melody finder, a tempo-synced oscilloscope and a loudness / stereo metering page. It works with any audio interface, with whatever Windows is playing, or as a VST3 plugin in your DAW.

In the app and the plugin it's called **UAV**; files, folders and the installer use the full name.

*Earlier builds were called DigiCheck Universal (the app: Spectroscope).* The installer replaces them, and on its first start UAV moves your settings, presets, layouts, targets, themes and downloaded models to the new folders (`Documents\Ultimate Audio Viewer`, `%APPDATA%\Ultimate Audio Viewer`). DAW projects that used the old plugin open with UAV in its place.

Tip: press **?** at the bottom right (or F1). While it's on, resting the mouse on any button or control shows what it does.

---

## Contents
1. [Getting started](#1-getting-started)
2. [The window](#2-the-window)
3. [Spectrogram](#3-spectrogram)
4. [Analyzer](#4-analyzer)
5. [Balance (tonal balance)](#5-balance-tonal-balance)
6. [Key, chords and melody](#6-key-chords-and-melody)
7. [Scope](#7-scope)
8. [Monitor (loudness and stereo metering)](#8-monitor-loudness-and-stereo-metering)
9. [Custom page](#9-custom-page)
10. [Loudness readout, Tempo, Reference and presets](#10-loudness-readout-tempo-reference-and-presets)
11. [Full screen, display and text size](#11-full-screen-display-and-text-size)
12. [The VST3 plugin](#12-the-vst3-plugin)
13. [Target Builder](#13-target-builder)
14. [Keyboard shortcuts](#14-keyboard-shortcuts)
15. [Files and folders](#15-files-and-folders)
16. [Troubleshooting](#16-troubleshooting)

---

## 1. Getting started

### Install
Download **Ultimate-Audio-Viewer-Setup.exe** from the repository's *Latest build* release and run it. Setup first asks who to install for:
- **Install for all users** (recommended; Windows asks for administrator rights): the VST3 plugin goes to `C:\Program Files\Common Files\VST3`, the folder every DAW scans. It also removes an earlier "for me only" install, so you have one.
- **Install for me only** (no administrator rights): the plugin goes to your own VST3 folder (`%LOCALAPPDATA%\Programs\Common\VST3`), which some DAWs don't scan. If you tick the VST3 plugin, Setup offers to restart itself for all users.

**Ultimate Audio Viewer.exe** is the same app as a single portable file.

The builds aren't code-signed yet, so Windows may warn once:
- **SmartScreen:** choose *More info → Run anyway*.
- **Is it really from Styrant Audio?** Right-click the file → *Properties* → *Digital Signatures*: it is signed by **Styrant Audio**. The signature is self-signed, so Windows still calls the publisher unknown, but it shows the file hasn't been changed since it was built.
- **Defender "Sabsik" detections** on an unsigned download are machine-learning false positives. If you see one, you can submit the file to Microsoft as a false positive.

### Choose what to analyse
1. Click **Options** (bottom left, or press **O**).
2. Under **AUDIO INPUT**, pick a source:

| Source | Use it for |
|---|---|
| Windows Audio (shared) | The default. Reads your interface's inputs through Windows, even while your DAW uses ASIO. |
| Windows Audio (exclusive) | Bit-exact input when nothing else is using the device. |
| ASIO | Bit-exact, lowest latency. May be unavailable while your DAW holds the ASIO driver. |
| Windows playback (loopback) | Whatever Windows is playing (Spotify, YouTube, a DAW using Windows Audio), with no cables or routing. It can't hear a DAW that uses ASIO. |
| From DAW (UAV Bridge) | What the UAV Bridge plugin (or the full UAV VST3) sees in your DAW, **even when the DAW uses ASIO**. See [Monitoring your DAW](#monitoring-your-daw-even-with-asio). |
| Test signal | A sweep, chord, clicks and tones, for trying the app without an interface. |

3. Pick the **L** and **R** channels (for example inputs 1/2, or your interface's loopback channels).
4. Press **Start**. The status line underneath shows the sample rate, latency, DSP load and renderer.

Click anywhere else, or press Esc, to close Options. Your source and channels are remembered.

### Monitoring your DAW, even with ASIO
A DAW that uses ASIO talks to your interface directly, past Windows' audio system, so ordinary Windows apps (and *Windows playback (loopback)*) can't hear it. A small plugin in the DAW hands the app what it hears instead:

1. Put **UAV Bridge** (listed under *Styrant Audio* in your DAW's plugin list) on your DAW's **master** track, last in the chain so it sees what you hear. Save it in your default template and it's always there.
2. In the app, choose **From DAW: automatic (the master track)** under *From your DAW* in Options, and press Start.

That's all: no routing, no driver or Windows settings, any interface. The app then measures your DAW's master like any other input, and every page works.

**Hear your DAW and everything else together:** tick **DAW (UAV Bridge)** in Options, under the Start button. The input you chose keeps playing (Windows playback, your interface's inputs...), and the DAW's master from UAV Bridge is added on top, so you don't have to switch inputs. The status line shows *+ DAW "Master"* while it's heard (or *+ DAW: waiting for UAV Bridge*). It follows the master automatically, works at any sample rate (the DAW's audio is converted to the input's rate), and runs about 40 ms behind the DAW. It isn't used when the input is *From DAW* itself. If the DAW also plays through Windows (not ASIO), you'd hear it twice; use one or the other.

**UAV Bridge** does nothing else: the audio passes through untouched with no latency, and it has no analysis or graphics of its own, so it costs next to nothing (it's a small fraction of the size of the full plugin). Its window shows:
- whether it's sending, and under which track name (green *Sending as "Master"*)
- *Ready: waiting for the DAW to process audio* when the DAW isn't playing
- a small level meter, to confirm audio is going through
- **Send to the UAV app**, to switch it off without removing it
- **Open UAV**, which starts the app (when it's installed)

You can use the full **Ultimate Audio Viewer** plugin instead: it sends too (*Send to the UAV app* in its Options, on by default). Use the full plugin when you also want its pages inside the DAW; use the Bridge when you only want the app to listen. Don't put both on the master: you'd get two senders with the same name (the app follows the newer one).

- **Automatic** follows the plugin on the track named like a master (Master, Stereo Out, Main...), else the one added most recently. To follow a particular track instead, put a Bridge there and choose **From DAW: <track name>** (press Rescan to list tracks that have just started sending).
- The Options status line says which track the app is hearing, or that it's waiting for your DAW. While nothing is sending, the meters fall to silence; they pick up again as soon as the DAW plays.
- Up to 16 tracks can send at once.
- It works between the DAW and the app on the same computer, with any DAW that loads VST3 plugins (including ones that run plugins in separate processes). Some DAWs don't tell plugins their track's name; the Bridge then sends as *UAV Bridge*, and Automatic still finds it as the newest sender.
- If your interface has loopback channels in its driver (many Focusrite, RME, MOTU and UA interfaces do), choosing those channels under *Windows Audio (shared)* also hears an ASIO DAW, without a plugin.

---

## 2. The window

Everything is controlled from the **bottom bar**:

```
[Options] [Spectrogram|Analyzer|Balance|Key|Scope|Monitor|Custom] [LUFS] [Tempo]  ...view controls...  [loudness boxes] [Reset] [?] [Full screen]
```

- **View buttons:** switch pages (keys **1-7**; **V** cycles).
- **LUFS:** shows or hides the loudness boxes (short-term, integrated, true peak).
- **Tempo:** a BPM ↔ milliseconds calculator (**T**).
- **Middle:** the current view's controls (they change with the view).
- **Reset:** restarts integrated loudness and the true-peak hold (the Monitor's measurements reset too).
- **?:** Help mode: hover over things to see what they do.
- **Full screen:** F11; in full screen a **Hide bar** button appears too (see [section 11](#11-full-screen-display-and-text-size)).

When the window is narrow, the least important controls drop out first. Most have a keyboard shortcut or are also in a menu.

---

## 3. Spectrogram

A reassigned spectrogram, like RME DigiCheck NG's Spectroscope: time runs left to right, frequency bottom to top, and brightness is level. Reassignment moves each bit of energy to where it really is in time and frequency, so notes and transients are much sharper than in an ordinary spectrogram.

**Bottom bar:** Presets · Pause (Space) · frequency scale · channel · Ref level −/+ · Settings (**S**).

- **Frequency scale:** *Log* (every octave the same height, the default), *Linear* (harmonics evenly spaced), *Ext Log* (log, compressed smoothly down to 1 Hz to show rumble and sub-bass), *Mel*, *Bark*, *ERB* (scales of hearing).
- **Channel:** Left, Right, *Mid* (what both sides share) or *Side* (the stereo difference).
- **Ref level:** the level shown as full brightness. Use the mouse wheel or Up/Down over the display.
- **Cursor:** click or drag on the display for a frequency cursor (it snaps to notes if *Show Note Cursor* is on in Settings). Double-click or Esc removes it.
- **Settings** has the FFT size, frequency range (including 5 Hz-20 kHz and other low ranges), colours, scroll speed, grid and more.

Tip: a larger FFT (16384) resolves very low notes. A single cycle at 5 Hz lasts 200 ms.

---

## 4. Analyzer

Two styles, switched with **Bars | FFT** at the top left of the view.

### FFT (SPAN / Pro-Q style)
A smooth spectrum curve with a peak-hold line.
- **Header:** channel, a **second curve** (Left / Right / Mid / Side, or the sidechain in the plugin) in its own colour and style, FFT size, averaging, slope (4.5 dB/oct makes a balanced mix look flat), smoothing, **Hold**, **Freeze**, **Colours**.
- **Snapshot:** keeps the current spectrum as a dashed ghost line (drawn with the current slope and smoothing), for before / after comparisons; the hover readout shows the difference. Click **Clear snap** to remove it.
- **Mouse:** hover for frequency, note and level (with a level line). The wheel scrolls the dB range, Ctrl+wheel zooms it, double-click resets. Click the display to reset the peak hold. **Alt+click** pins the readout to that spot (it keeps showing live levels there); **Alt+right-click** (or Alt+click on the pin) removes it. Its line thickness is under **Marker line** (FFT: the Colours menu; Bars: Settings).

### Bars (Spectrum Analyzer 2)
A constant-Q filter bank with peak bars, peak hold and RMS at 3-24 bands per octave.
- **Bottom bar:** Presets, bands/octave, channel, grid, zoom, Full, Freeze, Settings (or right-click the display).
- **Mouse:** wheel zooms, drag pans, Shift+drag zooms to a selection. **Alt+click** pins the inspector readout to that spot (live values, and it stays put while you zoom); **Alt+right-click** (or Alt+click on the pin) removes it. Its line thickness is under **Marker line** (FFT: the Colours menu; Bars: Settings).
- **Keys:** F freeze, R reset meters, 0 full range.

---

## 5. Balance (tonal balance)

Compares the tonal balance of your audio with a **target**, like iZotope Tonal Balance Control.

- **Left: four band meters:** Low (0-250 Hz), Low-mid (250 Hz-2 kHz), High-mid (2-8 kHz), High (8 kHz+).
  - The blue zone is the target's range, and the marker shows where you are.
  - Green means in range, amber just outside, red further out. The number says how many dB above or below the range you are.
- **Right: the detailed curve** (20 Hz-20 kHz).
  - *Curve* shows your audio over the target's range, red where it leaves the range.
  - *Diff* shows your audio minus the target, so 0 dB means right on target.
  - Hover for the exact difference at any frequency.

**Bottom bar:**
- **Target:** the target to compare with. The end of its list has *Open targets folder*, *Rescan targets* and *Open Target Builder*.
- **Window:** how much audio the measurement averages. *Realtime* (about 0.3 s, follows the music as it plays), 1 s, 3 s, 10 s, 30 s, or *Since reset*.
- **Smoothing:** how smooth the curve is.
  - **Smooth** (the default): 1 octave, a broad view like Tonal Balance Control's.
  - **Medium:** 1/2 octave.
  - **Detailed:** 1/3 octave.
  - **Fine:** 1/6 octave, for narrow resonances.
  - The target's range is smoothed to match.
- **Curve / Diff**, and **Reset** (start the measurement again).
- **Snapshot** (top of the curve panel): keeps the current measurement as a dashed ghost curve, to compare before and after a change (an EQ move, a different master). The hover readout adds how far you've moved from it. Click **Clear snapshot** to remove it.

**Targets** are JSON files in `Documents\Ultimate Audio Viewer\Targets`: ones made with [Target Builder](#13-target-builder) from your own reference songs, or Tonal Balance Control-style target files. New files are picked up automatically.

How to read it:
- Use a long window (10-30 s, or *Since reset* over a whole song) to judge the mix as a whole.
- Use *Realtime* to see a single section.
- A mix sitting inside the range everywhere is balanced like the target's songs. A band or curve consistently outside it shows where to look.

---

## 6. Key, chords and melody

Everything about key and harmony on one page.

**Source:** **Record** (the current input; **R**), **Browse...**, or drag and drop a song onto the page (WAV, AIFF, FLAC, OGG, MP3). **Clear** starts over.

- **Key (left):** the song's key with two runners-up, Camelot and Open Key notation, and the keys that mix well with it. While recording it shows *Now* (the last 20 s) and *Session* (everything heard).
- **Chords & melody (right):** the song is transcribed to notes, then:
  - **Chords:** chord names spelled for the key, slash chords, and Roman numerals.
  - **Melody:** the top line, highlighted in orange.
  - **Piano roll:** every note, coloured by instrument. Wheel scrolls, Ctrl+wheel zooms, drag pans, double-click fits.
  - **Copy chords**, **Save MIDI...** or **drag MIDI** straight into your DAW.
- **Listen row:** Play (Space) with a playhead; click a chord or the timeline to jump there. *Original / Mix / Transcription* lets you compare the song with a synth playing what was transcribed.
- **Sensitivity:**
  - Lower it if the transcription has notes that aren't there.
  - Raise it if notes are missing.
- **Model:** downloaded once inside the app, in three sizes:
  - Small: 210 MB, about real time on a CPU.
  - Medium: 620 MB.
  - Large: 2.7 GB; a GPU is recommended.
  - **Device** picks the GPU or the CPU. The model weights are for non-commercial use.

---

## 7. Scope

A tempo-synced oscilloscope: the window is a musical length, so drums and bass line up bar after bar.

- **Length:** 1/4, 1/2, 1, 2, 4 or 8 bars at the BPM shown (click the BPM to type one; **T** = triplets).
- **Modes:**
  - **Free** sweeps left to right.
  - **Scroll** scrolls smoothly.
  - **Sync** (plugin) locks to your DAW's bars.
  - **Freeze** (Space) stops it.
- **Spectral** colours the wave by frequency: red/orange lows, yellow/green mids, purple highs. **AA** smooths the outline.
- **Gain** (−24 to +24 dB; Ctrl+wheel) magnifies quiet parts. Anything past 0 dBFS turns the clipping colour.
- **BP** and its knob show only one band (for example 60 Hz for the kick). Right-click **BP** to switch to a low-pass (**LP**: everything below the knob) or high-pass (**HP**: everything above it).
- **Spectral palettes:** right-click **Spectral** for the list, scroll the mouse wheel over it, or press **C** (Shift+C goes back) to step through them; the name shows briefly.
- **Side2SC** overlays the Side signal (or, in the plugin, the sidechain). **Hold** keeps a ghost copy for comparison.
- **Mouse:**
  - The wheel zooms, Shift+wheel scrolls, Alt+wheel changes the length.
  - Dragging zooms and pans; double-click shows everything.
  - **Ctrl+drag** measures a region: length, peak, RMS and crest factor.
- **Rec** records the next window. Drag the wave icon into your DAW or Explorer to take it as a WAV.
- **Menu:** source (Stereo, L, R, Mid, Side, Peak), guide lines, colour themes, and **Colours** (spectral palettes and your own colours; save them as a theme).

On narrow pages (for example a [Custom](#9-custom-page) pane), the less important buttons drop out.

---

## 8. Monitor (loudness and stereo metering)

A modular metering page, like iZotope Insight 2. It keeps measuring while you look at other pages.

**Toolbar:**
- The module buttons show or hide modules (on narrow pages they move into **Layout → Modules**).
- **Measure file...** (or drop an audio file onto the page) measures a whole file faster than real time. A two-minute song takes a few seconds; a bar shows the progress. Every module then shows the file's results: integrated loudness, LRA, true peak, PLR and PSR, levels, phase, and its History (set the History length to 5 or 10 min for a whole song). The live input keeps being measured meanwhile; **Live** (or Reset) goes back to it.
- **Report...** saves the results, of the live take or the measured file: a text report with pass / FAIL against the target (integrated loudness, true peak, LRA), maxima, PLR, PSR range, clipped samples and lowest correlation, plus a PNG of the page and a CSV of the loudness history (every 100 ms). Save as `.csv` to get only the history. Reports go to `Documents\Ultimate Audio Viewer\Reports` by default.
- **Layout** has the number of columns, Show all and Reset layout.
- **Pause** freezes the display.
- **Reset** restarts the measurements.

**Each module:**
- The ⚙ gear has its options, including a **Text size**.
- The corner button maximises it (or double-click its title); × hides it.
- Drag a title onto another module to swap their places. *Full width* (gear) makes a module span the row.

### The modules
- **Loudness:** Short Term (3 s), Integrated (since Reset) and Momentary (0.4 s) in LUFS, with LRA and true peak, against a target.
  - Large enough, it shows three meter bars with a decaying peak line. Hover for a level line.
  - **Targets:** the list (Streaming −14, Apple −16, EBU R128 −23, ATSC −24 and more) sets the target, tolerance, peak limit and gate. The button left of the list opens the Targets panel for custom values.
  - Colours: Integrated is green within the tolerance and red above it. Short Term and Momentary are yellow above the target.
- **Dynamics:** how squashed the music is.
  - **PSR** (true peak minus short-term loudness) shows it right now.
  - **PLR** (peak minus integrated loudness) covers the whole take.
  - Zones: under 6 dB crushed, 6-8 dense, 8-12 punchy, 12-16 dynamic, over 16 open.
  - The history underneath (5 s to 10 min) shows which sections get squashed.
- **Levels:** sample peak and RMS for L/R (optionally Mid/Side), with peak hold, clip markers and the maximum values. Hover for a level line; double-click to clear the holds.
  - Gear: hold time, RMS time, scale, RMS +3 dB (AES17).
- **Sound Field:** the stereo image.
  - Three styles: *Polar Sample* (every sample as a point), *Polar Level* (energy per direction) or *Lissajous*.
  - A correlation bar, a balance bar, width and balance readouts. Clipped samples are drawn red.
- **Phase:** correlation overall and per band (lows, mids, highs), and its history. Below 0 the channels partly cancel in mono; it warns when the lows are out of phase.
- **History:** momentary, short-term and integrated loudness over time against the target. Hover for the values at that moment.

---

## 9. Custom page

Put several views on one page: for example the spectrogram large on the left, with the loudness and level meters and the scope on the right.

1. Click **Custom** (or press **7**).
2. Click **Layout ▾** in the bottom bar. Choose a **Layout**:
   - two side by side
   - two stacked
   - large left with two on the right
   - large top with two below
   - three columns
   - a 2 × 2 grid
3. In the same menu, choose what each **Pane** shows: Spectrogram, Analyzer, Balance, Key, Scope, Monitor, or Empty. A view can be in only one pane; picking one that's already in another pane swaps the two.
4. **Resize** the panes by dragging the gaps between them. *Equal sizes* (in the menu) puts them back.
   - **Quicker:** move the mouse to the top edge of a pane and a small header with its view's name appears there (it stays out of the way of the view's own controls). Click it to change the view, empty the pane or swap it with another. Right-clicking an empty pane, or the gap next to a pane, opens the same menu. (Right-click inside a view still opens that view's own menu.)
5. **Click a pane** to work with it: it gets a blue outline, and the bottom bar shows its controls next to Layout (the Balance target and smoothing, the Spectrogram's presets and scale, the Analyzer's controls). Keys like Space, P and S act on that pane too.
6. **Saved layouts:** *Layout ▾ → Save layout as...* stores the whole arrangement (layout, panes, sizes and the Monitor's modules) under a name, for example "Mixing" or "Mastering". Switch with *Saved layouts*, or press **Ctrl+1** to **Ctrl+9** for the first nine; *Update* and *Delete* act on the layout you loaded. Saved layouts are shared by the app and all plugin instances.

Good to know:
- Each view keeps its own settings: the Balance target, spectrogram presets, analyzer style and so on. Set them on the view's own page (or in the pane itself) and they carry over.
- **The Monitor has a separate module layout here.** For example, show only Loudness and Levels in the Custom page while the Monitor page keeps all six. Use its module buttons, or **Layout → Modules** when the pane is narrow.
- Everything shown is live at once, so a busy Custom page uses more CPU and GPU than a single view.

---

## 10. Loudness readout, Tempo, Reference and presets

- **LUFS boxes** (bottom right): short-term, integrated and true peak.
  - Loudness is white below −14 LUFS, green −14 to −9, yellow −9 to −6, red above.
  - True peak is white below −1 dBTP, yellow −1 to 0, red above 0.
  - **Reset** restarts them, together with the Monitor.
- **Tempo** (**T**): note values from 2/1 to 1/128 in milliseconds, with triplet and dotted columns and a **Tap** button. Click a value to copy it. In the plugin it follows the project's tempo.
- **Ref** (reference track): compare your mix with a finished song you like.
  1. Click **Ref**, then **Load reference...** (WAV, AIFF, FLAC, MP3...). It's measured once, in a few seconds, and comes back the next time you start (in the plugin: with the project).
  2. The panel shows its integrated loudness, LRA, true peak and PLR next to your mix's integrated loudness, with the difference in LU.
  3. It's drawn in **orange**: on the **Balance** curve (the hover readout adds "reference ±x dB"), on the **FFT analyzer** (its average spectrum, at matched loudness when *Match loudness* is on) and in the **Monitor**'s Loudness header. *Show in* turns each on or off.
  4. **Listening:**
     - **Plugin:** *Mix | Reference* switches what the plugin outputs. The reference plays in step with your DAW's transport (from the song position), with a 10 ms fade so it never clicks. The analysis keeps measuring your mix.
     - **App:** **Play** plays the reference through Windows' default output from the position on the slider. Mute your DAW to compare.
     - **Match loudness** (on by default) plays the reference at your mix's integrated loudness, so you compare the sound, not the level.
  5. **Clear** removes it. The Ref button turns brown while a reference is loaded, and orange while you're hearing it.
- **Presets** (**P**):
  - The spectrogram has built-in views (Default, Bass focus, High contrast) and your own saved views.
  - The Bars analyzer has factory presets, your own, and import of Spectrum Analyzer .fxp/.fxb files.

---

## 11. Full screen, display and text size

### Full screen
- **F11** or **Full screen** (bottom right): a borderless full-screen window. Esc or F11 leaves.
- **Hide bar** (or **B**, or **Shift+F11** to go straight there): hides the bottom bar too, so the views fill the whole screen.
  - Move the mouse to the very bottom edge and the bar slides back while you use it.
  - It hides again when you move away.
  - **Show bar** (or B) keeps it.

### HDR monitors (Options → HDR full screen)
Some graphics drivers switch a full-screen OpenGL window to *exclusive* mode. Windows then turns HDR off while it's there.
- **Keep HDR (1 px gap at the bottom)** (the default): the window stops 1 pixel short of the monitor's height, so the driver leaves it alone and HDR stays on.
- **Keep HDR (CPU rendering while full screen):** if your driver still turns HDR off, this uses CPU drawing only while full screen on a monitor with HDR on.
- **Exact full screen:** covers every pixel (HDR may turn off).
- On NVIDIA you can also set the app's *Vulkan/OpenGL present method* to *Prefer layered on DXGI Swapchain* in the NVIDIA Control Panel.

### Copy as image
**Ctrl+C** copies the page as a picture (paste it into a chat, an email or a document); on the Custom page **Ctrl+Shift+C** copies only the pane you last clicked. Every copy is also saved as a PNG in `Documents\Ultimate Audio Viewer\Screenshots`. *Options → Copy view as image* does the same (useful in a DAW that keeps Ctrl+C for itself).

### Display (Options)
- **GPU rendering (OpenGL):** on by default. Turn it off if your graphics driver has OpenGL problems.
- **Frame rate limit:** 60 fps by default (smooth, and spares the GPU). *Off* follows the monitor. On a G-Sync/FreeSync monitor, pick about 3 below its maximum (for example 141 on 144 Hz).
- **Text size:** Small, Medium, Large or Custom (70-200 %) for the whole app. Monitor modules also have their own text size in their gear menus.
- **Colour-blind-friendly meters:** where colour means good / warning / bad (loudness and level meters, phase, the dynamics zones, the Balance markers, the LUFS boxes), red and green change to sky blue, yellow and vermillion, which stay distinct with red-green colour blindness. In Balance, "in range" turns white.

### Updates and settings (Options)
- **Version:** the version you're running is shown at the bottom right of Options (e.g. *UAV 1.3.142*).
- **Check for updates** (on by default): at start-up the app asks GitHub whether there's a newer *Latest build*; if so, a small green "1.3.x available" button appears at the bottom right (and in Options). Nothing else is sent.
- **Back up...** saves everything to one `.uavbackup` file: all settings, presets, Custom layouts, Scope themes and Balance targets. **Restore...** loads one (restart the app, or reopen the project in a DAW, to use it).
- **Reset to defaults** puts every setting back to its default; presets, layouts, themes and targets are kept, and so is the audio input. Restart to use it.

---

## 12. The VST3 plugin

**Ultimate Audio Viewer.vst3** puts the same pages (Custom included) on a track in your DAW.
- The installer can install it, or unzip *Ultimate-Audio-Viewer-VST3.zip* into `C:\Program Files\Common Files\VST3`.
- The audio passes through untouched (bit for bit, no latency) unless you use Listen.
- **UAV Bridge** (*UAV Bridge.vst3*) is a separate, tiny plugin that only sends a track to the app; see [Monitoring your DAW](#monitoring-your-daw-even-with-asio). The installer has its own box for it (ticked by default), or unzip *Ultimate-Audio-Viewer-Bridge-VST3.zip* into the same folder.

What the plugin adds:
- **Send to the app:** the plugin hands what it sees to the standalone app, whose **From DAW** input then monitors your DAW even when it uses ASIO ([Monitoring your DAW](#monitoring-your-daw-even-with-asio)). On by default; *Send to the UAV app* in the plugin's Options turns it off. If that's all you want from the DAW, the much lighter **UAV Bridge** plugin does it on its own.
- **Tempo** follows the project, and the **Scope's Sync** mode locks the window to your bars.
- **Sidechain:** route another track to the plugin's sidechain input to see it together with the track:
  - over the **spectrogram** (**SC** button: tint or contour lines, colour and strength)
  - on the **Scope** (filled or outline)
  - as the **FFT** analyzer's second curve
- **Link** (bottom bar): compare with another track without routing anything. Every UAV instance in the project publishes its track's audio under the track's name; click **Link** and pick another track (say, the bass on the kick's instance). It's then used exactly like a sidechain: the Spectrogram overlay (SC), the Scope's SC overlay and the FFT's second curve (choose *Sidechain*). The button shows the linked track (teal while its audio arrives). *None* goes back to the real sidechain input. The link is saved with the project by track name. The linked audio can be up to one audio block behind, depending on the order the DAW processes the tracks; DAWs that run each plugin in its own process can't link.
- **Listen** (bottom bar): hear only some bands (Sub, Low, Low-mid, High-mid, High, with movable crossovers), and/or only Mid, Side, Left or Right.
  - Click a band to toggle it; double-click to solo it.
  - The button turns orange while you're listening to part of the signal. **Off** restores everything.
  - On the Scope, **Listen** next to the BP knob plays the filter you're looking at (BP, LP or HP).
- **Monitor:** keeps measuring while the plugin window is closed. Optionally, it resets when playback starts (Loudness gear).
- Settings are saved with the project; presets are shared by all instances.

Not in the plugin: choosing the audio input (the track is the input), and full screen (resize the plugin window instead).

---

## 13. Target Builder

**UAV Target Builder** (installed next to the app) makes Balance targets from your own reference songs.

1. Drag songs or folders into the window (WAV, AIFF, FLAC, OGG, MP3).
2. Check the list: songs that don't match the others are marked **Outlier** (select them and press Delete).
3. Name the target, pick the range, and press **Save target**.
   - **Wide, like Tonal Balance Control** (the default) gives ranges as wide as iZotope's targets.
   - The *songs 5-95 %*, *10-90 %* and *20-80 %* options use only your songs' spread, which is tighter.

10-20 or more songs per genre give a trustworthy target. Targets are saved to `Documents\Ultimate Audio Viewer\Targets` and appear in Balance right away. A command-line mode is described in the README.

---

## 14. Keyboard shortcuts

| Key | Action |
|---|---|
| 1-7 | Spectrogram, Analyzer, Balance, Key, Scope, Monitor, Custom |
| V | Next view |
| K | Key page |
| O | Options |
| T | Tempo calculator |
| P | Presets (Spectrogram, Analyzer) |
| S | Settings (Spectrogram, Analyzer) |
| Space | Pause / freeze (Spectrogram, Analyzer, Scope); play (Key) |
| R | Record (Key) |
| Up / Down | Reference level (Spectrogram) |
| F11 | Full screen |
| Shift+F11 | Full screen without the bottom bar |
| B | Hide / show the bottom bar (in full screen) |
| ? or F1 | Help mode |
| Ctrl+C | Copy the page as an image (also saved as PNG) |
| Ctrl+Shift+C | Copy the focused Custom pane as an image |
| Esc | Close Options, leave full screen, or remove the cursor |

---

## 15. Files and folders

| What | Where |
|---|---|
| Balance targets | `Documents\Ultimate Audio Viewer\Targets` |
| Scope recordings | `Documents\Ultimate Audio Viewer\Scope Recordings` |
| Scope themes | `Documents\Ultimate Audio Viewer\Scope Themes` |
| Copied images | `Documents\Ultimate Audio Viewer\Screenshots` |
| Loudness reports | `Documents\Ultimate Audio Viewer\Reports` |
| Crash reports | `Documents\Ultimate Audio Viewer\Crashes` |
| Transcription models | `%APPDATA%\Ultimate Audio Viewer\Models` |
| Settings | `%APPDATA%\Ultimate Audio Viewer\Ultimate Audio Viewer.settings` |

---

## 16. Troubleshooting

- **No picture, or "No input":** open Options, pick a source and channels, and press Start. ASIO may be busy while your DAW uses it; use *Windows Audio (shared)* instead.
- **I want to see what Spotify / YouTube is playing:** choose *Windows playback (loopback)*.
- **I want to see my DAW and Spotify / YouTube together:** pick *Windows playback (loopback)*, tick *DAW (UAV Bridge)*, and put UAV Bridge on the master track.
- **I want to see what my DAW plays, and it uses ASIO:** put UAV Bridge on the master track and choose *From DAW* in Options ([how](#monitoring-your-daw-even-with-asio)).
- **"From DAW: waiting for your DAW":** open the Bridge's window: it should say *Sending as ...*. If it says *Ready: waiting*, press play (and check the plugin isn't bypassed); if it says *Off*, tick *Send to the UAV app* (in the full UAV plugin, that's in its Options). Press Rescan to see the tracks that are sending.
- **HDR turns off in full screen:** see [HDR monitors](#hdr-monitors-options--hdr-full-screen).
- **Stutter or high GPU use:**
  - Keep the frame rate limit at 60 fps.
  - Check that the status line (Options) says *GPU (OpenGL)*.
  - Busy Custom pages cost more.
- **Text too small:** Options → Text size, or a Monitor module's gear → Text size.
- **Windows warns about the download:** the builds are self-signed, not signed by a certificate Windows trusts (see [Install](#install)).
- **Not sure what a control does:** press **?** and hover over it.
- **The plugin doesn't show up in my DAW:** check that `Ultimate Audio Viewer.vst3` (and `UAV Bridge.vst3`) is in `C:\Program Files\Common Files\VST3` (reinstall choosing *Install for all users*), then rescan plugins in the DAW.
- **It crashed:** a report is saved in `Documents\Ultimate Audio Viewer\Crashes` (a `.txt` and a `.dmp`). Send both and say what you were doing. The plugin only records crashes in its own code; the DAW's own crash reporting still works.
