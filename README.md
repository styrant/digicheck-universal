# Ultimate Audio Viewer (UAV)

Ultimate Audio Viewer (UAV; formerly DigiCheck Universal / Spectroscope) began as a native Windows recreation of the Spectroscope (reassigned spectrogram) from RME's DigiCheck NG that works with any audio interface, and grew into a set of views and meters for music. Files, folders, the exe, the VST3 and the installer use the full name; the UI says UAV (`Source/AppInfo.h`, which also moves an earlier install's folders and settings to the new names on first start). It was built for a Focusrite Scarlett 16i16 Gen 4.

**[User manual](docs/MANUAL.md)**: how to use every page. In the app, **?** (bottom right, or F1) turns on Help: hover over any control to see what it does.

## Getting it
Every push to `main` is built by GitHub Actions and published to the **Latest build** release:
- **Ultimate-Audio-Viewer-Setup.exe** installs for all users by default (so the VST3 goes to Common Files\VST3), with a Start menu entry, an optional desktop shortcut and an uninstaller; it replaces an earlier Spectroscope / DigiCheck Universal install.
- **Ultimate-Audio-Viewer.exe** is the same app as a single portable file (installed as `Ultimate Audio Viewer.exe`).

Until code signing is set up (below), Windows SmartScreen warns once when you run a freshly downloaded file: choose **More info → Run anyway**. Files the installer puts on disk don't carry the "downloaded from the internet" mark, so the installed app doesn't warn again.

## Code signing (optional, Azure Artifact Signing)
The workflow signs the exe and the installer automatically when these repository secrets exist (Settings → Secrets and variables → Actions):

| Secret | Value |
|---|---|
| `AZURE_TENANT_ID` | Directory (tenant) ID of the app registration |
| `AZURE_CLIENT_ID` | Application (client) ID of the app registration |
| `AZURE_CLIENT_SECRET` | A client secret of the app registration |
| `SIGNING_ENDPOINT` | Your signing account's endpoint, e.g. `https://eus.codesigning.azure.net/` |
| `SIGNING_ACCOUNT` | Artifact Signing account name |
| `SIGNING_PROFILE` | Certificate profile name |

Set-up in Azure: create an Artifact Signing account (Basic plan), complete identity validation as an individual, and create a Public Trust certificate profile. Then create an app registration with a client secret and give it the **Artifact Signing Certificate Profile Signer** role on the profile. Signed builds show a verified publisher. SmartScreen reputation still builds up over time, so very new builds may warn for a while.

## VST3 plugin

**Ultimate Audio Viewer.vst3** puts the same pages (Custom included) on a track in your DAW. The setup installs it (the "Install the VST3 plugin" box), or unzip **Ultimate-Audio-Viewer-VST3.zip** into `C:\Program Files\Common Files\VST3`. **Ultimate Audio Viewer Bridge.vst3** (UAV Bridge, its own installer box or **Ultimate-Audio-Viewer-Bridge-VST3.zip**) is a tiny plugin that only sends its track to the app's From DAW input. It's an analysis plugin: the audio passes through untouched (bit for bit, no latency) unless you use Listen.

What the plugin adds:
- **The project's tempo.** The Tempo window and the Scope's bar lengths follow the DAW's tempo (you can still type another one; it follows again when the project's tempo changes).
- **Scope Sync.** The window starts on a bar line and the trace follows the transport sample-accurately (it stays still while stopped). **Free** and **Scroll** work as in the app.
- **Sidechain.** Route another track to the plugin's sidechain input (in most DAWs: the plugin's sidechain / aux input selector), then:
  - **Spectrogram:** the sidechain is drawn over the track in its own colour. **SC** in the bottom bar: show or hide it, **tint** (its colour, by level) or **contour lines** (the track's picture stays clear underneath), colour and strength.
  - **Scope:** **SC** draws the sidechain over the waveform (menu: the sidechain input or Side, L - R), **filled** or **outline** (its envelope as two lines, so you can see the kick against the bass it ducks). Its colour is Menu > Colours > Side2SC / sidechain.
  - **Analyzer (FFT):** **+ Curve > Sidechain** shows its spectrum as the second curve, in its own colour and line / filled / dashed style.
- **DAW bridge** (`Source/bridge/`): a VST3 sends what it sees to the app through shared memory (`Local\UltimateAudioViewer.Bridge.1`: 16 slots, each a lock-free 65536-frame stereo ring with the track name, sample rate and a heartbeat). The app's **From DAW** input reads one slot (automatic: the master-named track, else the newest sender; or a chosen track), so it can monitor a DAW that uses ASIO with any interface and no routing. Senders: **UAV Bridge** (`Source/bridgeplugin/BridgePlugin.cpp`, *Ultimate Audio Viewer Bridge.vst3*: pass-through, no analysis or OpenGL, a status window with a meter and an on/off switch; codes Dcun/Dcub, so it sits next to the full plugin; `tests/bridge_plugin_test.cpp` loads it like a DAW) and every full UAV instance (on by default; plugin Options: *Send to the UAV app*); the reader feeds silence while nothing sends and restarts at the sender's sample rate when it changes. `tests/bridge_test.cpp` (also `--send <seconds>` to play a test sender).
- **Link** (bottom bar): another UAV instance's track as the comparison signal, with no routing. Instances in the DAW's process publish their input to a lock-free ring under their track name (VST3 track properties); the linked one is read each block (the latest block, so it can trail by one block depending on processing order) and used in place of the sidechain bus for the Spectrogram, Scope and FFT. Saved per project by track name. `Source/plugin/InstanceLink.*`.
- **Listen** (bottom bar): hear only some bands, and / or only Mid, Side, Left or Right, like Metric AB's band solo or ISOL8. Five bands (Sub < 60, Low 60-250, Low-mid 250-2k, High-mid 2k-6k, High > 6k, with movable crossovers): click a band to hear it or not, double-click to hear it alone. The crossovers are Linkwitz-Riley (24 dB/oct) with allpass alignment, so any combination sums cleanly and all five together are flat. The button turns orange and says what you're hearing; **Off** puts everything back. Changes fade over 10 ms, so they never click. On the Scope, **Listen** next to the BP knob plays the band-pass you're looking at.
- The views' settings are saved with the project; presets are shared by all instances.

Not in the plugin: the audio-input choice (the track is the input), full screen (resize the plugin window instead). The Monitor keeps measuring while the plugin window is closed.

## Sources
| Source | Use it for |
|---|---|
| **Windows Audio (shared)** | The default. Reads the Scarlett's inputs through Windows, so it works while your DAW holds ASIO. |
| **Windows Audio (exclusive)** | Bit-exact input through Windows, when nothing else is using the device. |
| **ASIO** | Bit-exact and lowest latency. It may be unavailable while your DAW is using the Focusrite ASIO driver. |
| **Windows playback (loopback)** | Analyses whatever Windows is playing to any output device (e.g. the Scarlett's outputs), with no routing needed. It can't hear a DAW that uses ASIO. |
| **From DAW** | What UAV Bridge (or the UAV VST3) sees in your DAW (put it on the master track), even when the DAW uses ASIO: the plugin hands its audio to the app through shared memory (see DAW bridge below). |
| **Test signal** | A sweep, chord, clicks and tones, for checking the display without an interface. |

**L / R** choose which two channels of the source to analyse. The spectrogram then shows **Left**, **Right**, **Mid**
((L+R)/2, what both sides share) or **Side** ((L-R)/2, the stereo difference), picked in the bottom bar or in
Settings → Channel Select. (The Analyzer has its own channel box with the same choices plus Stereo.)

## Views
Switch with the **Spectrogram | Analyzer | Balance | Key | Scope | Monitor | Custom** buttons at the bottom left (keys **1-7**, **V** cycles, **K** goes to Key). The bottom bar stays the same in every view: Options, the LUFS readout, Tempo, Reset and Full screen are always there. The controls in the middle change with the view.

- **Spectrogram:** the reassigned spectrogram described below. Bottom bar: Presets, Pause, **frequency scale**,
  **channel** (Left / Right / Mid / Side), Ref level -/+ and Settings (on narrow windows Settings, then the Ref
  buttons drop out; the mouse wheel and Up/Down still change Ref).
  - Frequency scales: **Log** (every octave the same height; the default), **Linear** (every Hz the same height:
    harmonics evenly spaced), **Ext Log** (extended log as in iZotope RX: log above ~100 Hz, compressing smoothly
    down to 1 Hz so infrasonic rumble and sub-bass are visible), **Mel**, **Bark** and **ERB** (scales of
    hearing). Linear / Mel / Bark / ERB start at 0 Hz. Presets store the scale.
  - Below 20 Hz: Ext Log shows down to 1 Hz with the normal 20 Hz-20 kHz range; for the other scales pick the
    5 Hz-20 kHz, 10 Hz-20 kHz, 2-200 Hz or 5 Hz-1 kHz range in Settings → Frequency Range. A larger FFT
    (16384) is needed to resolve very low notes: one cycle at 5 Hz lasts 200 ms.
- **Analyzer:** two styles, switched with **Bars | FFT** at the top left of the view.
  - **FFT:** a smooth spectrum curve in the style of Voxengo SPAN and FabFilter Pro-Q: a filled curve with a bright outline, a peak-hold line (click the display to reset it) and an optional **second curve** (Left, Right, Mid, Side, or in the VST3 version the sidechain) with its own colour and style (line, filled or dashed, with a dark halo so it stays readable over the main curve). The header sets the channel, FFT size (2048-32768), averaging (real time to 3 s), slope (0, 3, 4.5 or 6 dB/oct around 1 kHz; 4.5 makes pink noise and most mixes look flat), fractional-octave smoothing, hold and freeze; **Colours** has presets and a picker for both curves. Hover for frequency, note and level; the wheel scrolls the dB range, Ctrl+wheel zooms it, double-click resets. A tone reads its true level wherever it falls between FFT bins (the Hann window's scalloping is corrected exactly). `fft_test` checks the calibration on every build.
  - **Bars:** [Spectrum Analyzer 2](Source/analyzer/LICENSE), a constant-Q filter-bank analyzer (Seven Phases, MIT licence).
  - Peak bars, peak hold and RMS average, at 3–24 bands per octave.
  - Wheel zooms, drag pans, and shift+drag zooms to a selection.
  - Hover for the readout (frequency, level, note and cents). Right-click or **Settings** opens its settings panel.
  - Keys: F freeze, R reset meters, 0 full range.
  - It only runs while its view is visible, and its settings are saved with the app's.

- **Scope:** a tempo-synced oscilloscope modelled on Scyllascope's design.
  - The window is a musical length: **1/4, 1/2, 1, 2, 4 or 8 bars** at the BPM shown (click to type it; **T** = triplets). Beat and bar lines are drawn behind the wave.
  - **Free** sweeps left to right and restarts at the left edge after 0.25 s of silence; **Scroll** scrolls instead, at an even speed with sub-pixel steps (the columns sit on a fixed sample grid, and the scroll follows the audio about 40 ms behind so buffer-sized arrivals don't show); **Freeze** (or Space) stops it.
  - **Spectral** colours each slice by its frequency content: red/orange for lows (kicks, bass), yellow/green for mids, purple for highs (hats). **AA** smooths the outline.
  - **Gain** (-24 to +24 dB; Ctrl+wheel) magnifies quiet parts. Anything past 0 dBFS turns the clipping colour, and with negative gain a dashed line marks 0 dBFS.
  - **BP** and its frequency knob show only a band (two cascaded band-passes, Q 1.4), e.g. 60 Hz to see the kick alone.
  - **Side2SC** overlays the Side signal (L−R) on the main wave; **Hold** freezes a ghost copy behind the live wave for comparison.
  - Mouse: the wheel zooms, Shift+wheel scrolls, Alt+wheel changes the length, drag zooms (vertical) and pans (horizontal), double-click shows everything. **Ctrl+drag selects** a region and shows its length, peak, RMS and crest factor. Ctrl+click pins a dB ruler.
  - **Rec** arms the recorder: it starts with the next audio and records one window. Drag the wave icon into your DAW or Explorer to take it as a 24-bit WAV (right-click: Normalize, Trim silence, Snap to grid, Save as…). Recordings go to Documents\Ultimate Audio Viewer\Scope Recordings.
  - Menu: source (Stereo, Left, Right, Mid, Side, Peak), guide lines, and colour **themes**: Dark, Neon, DigiCheck, Amber, Phosphor, Ice, Sunset, Mono and Paper (light). Scyllascope-style JSON themes in Documents\Ultimate Audio Viewer\Scope Themes are loaded too.
  - **Colours** (in the menu): a **spectral palette** independent of the theme (Scyllascope, Heat, Ocean, Rainbow, Pastel, Forest, Mono), and your own colour for the wave, Side2SC / sidechain, hold, background, grid, playhead, clipping and accent (a picker that applies as you drag). **Save current colours as a theme...** writes a Scyllascope-compatible theme file.
  - Reserved for the VST3 version: Sync to the host transport, Sync to Start, sidechain from another track or instance, track name/colour, listening to the BP band, and REAPER's embedded mode. They are shown greyed out.

- **Custom:** several views on one page. **Layout ▾** in the bottom bar picks the layout (two side by side, two stacked, large left + two right, large top + two below, three columns, 2 x 2) and what each pane shows; picking a view that's already in another pane swaps them. Drag the gaps to resize the panes. Click a pane to focus it (blue outline): the bar shows its controls next to Layout and Space / P / S act on it. Layouts can be saved by name (Layout → Save layout as..., Saved layouts; stored app-wide with the presets). Each view keeps its own settings; the Monitor has a separate module layout on this page (e.g. only Loudness and Levels). The Scope's and Monitor's toolbars drop less important buttons when a pane is narrow (the Monitor's modules move to Layout → Modules).
- **Monitor:** a modular metering page in the spirit of iZotope Insight 2. **Measure file...** (or dropping a file on the page) runs a whole file through its own meters on a background thread (a 2 min song in a few seconds) and shows its results in every module while the live input keeps being measured; **Live** or Reset goes back. **Report...** writes a text report (pass / FAIL against the target for integrated loudness, true peak and LRA; maxima, PLR, PSR range, clips, lowest correlation), a PNG of the page and a CSV of the history. The chips at the top show and hide modules; each module's gear has its options (and a **Text size**, 85-200 %), the corner button maximises it (or double-click its title), and dragging a title onto another module swaps them. **Full width** (gear menu) makes a module span the row; **Layout** sets 1-3 columns or resets the layout. **Pause** freezes every module; **Reset** (here or in the bottom bar) restarts the measurements. Everything keeps measuring while the page is hidden.
  - **Loudness:** short-term, integrated, momentary (and its maximum), LRA and true peak against a target. Tall enough (or maximised) it shows Insight's three meter bars, with the LRA as a white bar beside Integrated and the target marked. Colours as in Insight: Integrated green within the tolerance and red above it, Short Term / Momentary yellow above the target, True Peak red above the peak target, LRA red outside its target range. Targets: Streaming -14, Apple Music -16, Podcast / AES TD1006 -16, EBU R128 (and DPP, South Africa), ATSC A/85, AGCOM, ARIB TR-B32, OP-59, Portaria 354, BS.1770-1 (ungated) and -2/3/4, Netflix (with program gating; Netflix asks for dialogue gating, which isn't here), Loud master -9, Custom or none. The **Targets** button (left of the list) sets the loudness target and tolerance, the peak target, the LRA range and the **gate** (Program, BS.1770-2/3/4; or Off, BS.1770-1). Gear: bar range (dB linear / non-linear, BS.1771, EBU +9, EBU +18), the scale relative to the target (LU), and (VST3) **reset when playback starts**.
  - **History:** Momentary, Short-term and Integrated loudness over 30 s to 10 min, the target as a red dashed line, Integrated red where it's above the target; hover for the values at that moment. Gear: which traces, length, relative scale.
  - **Dynamics:** **PSR** (true peak of the last 3 s minus short-term loudness: how squashed it is right now) and **PLR** (true peak since reset minus integrated loudness: the whole take), on a zoned scale (under 6 dB crushed, 6-8 dense, 8-12 punchy, 12-16 dynamic, over 16 open). The white tick is PLR; the line under the scale is the PSR range while the music is near its loudest. Below, the PSR history (30 s, 1 or 5 min) with the short-term loudness drawn over it, so you can see which sections the limiter squashes.
  - **Levels:** peak (dark) and RMS (light) bars for L and R (optionally Mid and Side), peak hold, red above -1 dBFS, clip marker, the peak target as a red dashed line, maximum peak and RMS above (click to reset). Gear: peak hold time (0.25 s to until reset: double-click the meters), RMS integration time (50 ms to 3 s), scale -40 / -60 / -90 dB, RMS +3 dB (AES17).
  - **Sound Field:** Polar Sample (every sample as a point, folded into the half disc, scaled to the recent peak), Polar Level (the energy going in each direction) or Lissajous (auto gain), with Insight's vertical correlation bar, a balance bar underneath, width (0 % mono, 100 % unrelated L/R, 200 % inverted) and balance. Clipped samples are drawn in red. The points grow with the view; gear: point brightness.
  - **Phase:** correlation (fast bar, white 3 s average, red tick for the lowest since reset), the same for the lows (< 250 Hz), mids and highs (> 2.5 kHz, Linkwitz-Riley splits), balance, width and a correlation history. Warns when the lows are out of phase (bass cancels in mono).

## Rendering
- The display is synced to the monitor's refresh rate (60/120/144 Hz…). New spectrogram columns pass through a ~30 ms buffer and are released at exactly the display speed, so the scroll is even on every frame.
- **GPU Rendering (OpenGL)** is on by default (Settings → GPU Rendering). The spectrogram history is an OpenGL texture: each frame uploads only the new columns and scrolls with sub-pixel precision. The rest of the interface, including the Analyzer, is drawn on the GPU too.
- Turn it off to use the CPU renderer, e.g. if your graphics driver has OpenGL problems.
- **HDR full screen** (in Options): on an HDR monitor, some graphics drivers switch a full-screen OpenGL window to exclusive mode, and Windows turns HDR off while it's there. **Keep HDR (1 px gap at the bottom)** (the default) makes the full-screen window 1 pixel short of the monitor, which drivers don't promote, so HDR stays on with GPU rendering. If your driver still does it, **Keep HDR (CPU rendering while full screen)** switches to CPU rendering only while full screen on a monitor with HDR on. **Exact full screen** covers the whole monitor. On NVIDIA, setting Ultimate Audio Viewer's "Vulkan/OpenGL present method" to "Prefer layered on DXGI Swapchain" in the NVIDIA Control Panel also keeps HDR.
- **Frame rate limit** (in Options): caps the whole window at a chosen rate; **60 fps by default**, which is smooth and saves the GPU redrawing at 144 or 240 Hz. Frames stay evenly spaced: well below the refresh rate the display draws every k-th refresh (60 on 60/120/240 Hz monitors, an even 72 on 144 Hz) instead of alternating gaps. Choose Off to follow the monitor. For a G-Sync / FreeSync monitor, pick about 3 below its maximum (e.g. 141 for 144 Hz) to stay inside the variable-refresh range. Vsync stays on, so frames never exceed the monitor's refresh rate.
- The Options panel shows the renderer and the measured frame rate, e.g. `GPU (OpenGL), 141 fps (limit 141)`.

## Toolbar and windows
- **Options** (bottom left, or O) opens a pop-up panel with the audio input (source, L/R channels, Start/Stop, Rescan), the status line and the display options (GPU rendering, frame rate limit). There is no top toolbar, so the analysis uses the full height. Click anywhere else or press Esc to close it.
- **Full screen**: F11 or the small button at the bottom right; Esc leaves. This is *windowed* (borderless) full screen; see HDR full screen under Rendering. In full screen, **Hide bar** (or B; Shift+F11 goes straight there) hides the bottom bar too: move the mouse to the bottom edge and it slides back while you use it.
- **Copy as image**: Ctrl+C (Custom page: Ctrl+Shift+C for the focused pane, or Options → Copy view as image) puts the page on the clipboard as a bitmap and PNG and saves it to Documents\Ultimate Audio Viewer\Screenshots. The spectrogram's picture is included with the GPU renderer too.
- **Snapshot** (Balance curve panel, FFT header): keeps the current curve as a dashed ghost for before / after comparisons; the hover readout shows the difference.
- **Options → Check for updates** (start-up check of the Latest build release; a green "x.y.z available" button appears when there's a newer one), **Back up... / Restore...** (one `.uavbackup` XML file with all settings, app-wide presets and layouts, Scope themes and Balance targets) and **Reset to defaults** (settings only; keeps the audio input; takes effect after a restart).
- **Help** (**?** left of Full screen, or F1): while on, resting the mouse on a button, box or Monitor module header shows a short description.
- **Tonal Balance view** (Balance button, or 3; V cycles the three views): your input's tonal balance against a target.
  - Left, four band meters (Low 0-250 Hz, Low-mid 250 Hz-2 kHz, High-mid 2-8 kHz, High 8 kHz+): the blue zone is the
    target's range and the marker shows where you are: green "In range", amber just outside, red further out, with how
    many dB above or below the range.
  - Right, the detailed curve (20 Hz-20 kHz): **Curve** shows your audio over the target's range (red
    where it leaves the range); **Diff** shows your audio minus the target, so 0 dB is right on target. Hover for the
    exact difference at any frequency.
  - Bottom bar: the target, the measurement window (Realtime, 1 s, 3 s, 10 s, 30 s or since Reset), the **smoothing**
    (Smooth: 1 octave, like Tonal Balance Control's broad curve, the default; Medium 1/2, Detailed 1/3, Fine 1/6
    octave; a bell-shaped weighting in the power domain, with the target's range smoothed to match), Curve/Diff and
    Reset. Realtime follows the music as it plays (about the last 0.3 s, a new measurement every 23 ms); the longer
    windows show the overall balance. The target list ends with Open targets folder, Rescan targets and Open Target
    Builder.
  - Drawing runs on the display's frame clock (vblank, obeying the frame-rate cap) and is composited by OpenGL when
    the GPU renderer is on. The curve and meters glide between measurements, so they move smoothly at the display's
    refresh rate; the grid, labels and target range are drawn once and cached. Set `SPECTROSCOPE_PAINT_LOG=1` to
    print the view's average paint time (about 2 ms at 1300x760 on a software renderer).
  - Targets are read from `Documents\Ultimate Audio Viewer\Targets` (nothing is built into the app): files made with
    Target Builder and Tonal Balance Control-style target files (including the variant that wraps values as
    `{"Type", "Value"}`). New or changed files are picked up automatically.
  - The measurement is the same as Target Builder's (gated, level independent), so a song compared with a target built
    from it sits on the target line. It only runs while the Balance view is shown.
- **Key** (Key view, 4 or K): every key-related tool on one page, sharing one audio source.
  - **Source:** **Record** (the selected input; **R**), **Browse...**, or **drag & drop** a song anywhere on the page
    (WAV, AIFF, FLAC, OGG, MP3). Nothing is analysed until you choose. **Clear** empties the page (audio, notes, chords, melody and
    key; stops playback and any transcription) to start again; loading another song or recording also replaces them.
  - **Key (left):** the key finder. For a file, the whole file's key with the two runners-up, Camelot (8A), Open Key
    (1m) and the keys that mix well. While recording, *Now* (the last 20 s, every 3 s) and *Session* (everything
    heard). The key chart below marks the key (filled) and the session key (outlined). The model is OpenKeyScan's
    KeyNet (Korzeniowski & Widmer 2018) from styrant/openkeyscan-analyzer (MIT), in native C++; `tests/key_test`
    checks it against librosa and PyTorch.
  - **Chords & melody (right):** the song is transcribed to notes by NeuralNote v2's engine (muscriptor.cpp with the
    MuScriptor model by Kyutai / Mirelo; from styrant/NeuralNote), then:
    - **Chords:** a chord lane with names spelled for the key (Bb, not A#, in F major), slash chords when the bass
      holds another chord tone, and Roman numerals (I, vi, V7, bVII...). Triads, sus, 6th and 7th chords (the
      *7th chords* box turns the 6ths and 7ths off).
    - **Melody:** the top line of the pitched, non-bass instruments, highlighted in orange in the piano roll; taken
      from the vocal part when there is one.
    - **Piano roll** with the waveform: every transcribed note coloured by instrument (*All notes* hides them). Wheel
      scrolls, Ctrl+wheel zooms, drag pans, double-click fits; hover for note, instrument and chord.
    - **Copy chords** (text with times), **Save MIDI...** or **Drag MIDI** straight into a DAW: one track per
      instrument plus Melody and Chords tracks, at 120 BPM with the key signature.
    - The key for chord names and numerals comes from the key finder, or from the notes when there's no key yet.
  - **Listen** (the row under the header): **Play** (or Space) plays the song or recording with a playhead; click the
    timeline, waveform or a chord to jump there (a chord: its start). *Listen: Original / Mix / Transcription* plays
    the source, both, or only a built-in synth playing what was transcribed, to hear where they differ. *Synth:
    Melody + chords* (melody, chord pad and bass), *All notes* (every note, drums too), *Melody* or *Chords*. Volume,
    and the output device (Windows Audio, shared; the system default unless you choose one).
  - **Sensitivity** (0-100, default 70): lower it when the transcription has notes that aren't there; raise it when
    notes are missing. The model gives no confidence per note, so each note's strength is measured from the audio
    (its fundamental and first harmonics over its first half second, relative to the strongest note around it; a
    note an octave, a twelfth or two octaves above a much stronger one is marked down as its likely overtone).
    Lowering the sensitivity drops weak and very short notes at once, for the chords, melody, MIDI and playback too;
    nothing is transcribed again. Raising it keeps everything found and makes the next transcription listen to
    quieter passages (the engine's silence gate: -39 dBFS at 0, -60 at 70, -69 at 100). Quiet audio is also brought
    up to a -1 dBFS peak (at most +24 dB) before the model listens, since it is sensitive to level.
  - **Model:** downloaded once inside the app, not part of the install: Small 210 MB (about real time on a CPU),
    Medium 620 MB or Large 2.7 GB (a GPU recommended). They're stored in `%APPDATA%\Ultimate Audio Viewer\Models`;
    models that NeuralNote has already downloaded (`%APPDATA%\NeuralNote\models`) are used in place. Each
    download is checked against NeuralNote's pinned SHA-256. The weights are CC BY-NC 4.0 (non-commercial).
  - **Device:** Auto picks the best GPU (Vulkan) or the CPU; or choose one. **Instruments:** restrict what the model
    listens for (helps when you know them). **Auto** transcribes as soon as a song is loaded or a recording stops.
  - Transcription runs in the background, chunk by chunk (notes appear as they're found); Stop keeps what's done.
    Files up to 20 minutes, recordings up to 15.
- **Presets** (bottom bar, left of the view's controls, or P):
  - Spectrogram: built-in views (Default, Bass focus, High contrast). They change only the display and analysis settings, never the input. You can save the current view under your own name, and delete your presets.
  - Analyzer: the factory presets, your own saved presets (settings and colours), and import of Spectrum Analyzer v1.x .fxp/.fxb files.
- **Smooth scrolling**: the spectrogram moves a whole number of pixels per monitor frame, so thin lines don't shimmer. The scroll speed is snapped to the nearest whole number of pixels per frame when that is within 20% of the chosen speed (e.g. 250 px/s plays at 240 px/s on 60 Hz); the time grid follows the actual speed.
- **Colour-blind-friendly meters** (in Options): colours that mean good / warning / bad switch to an Okabe-Ito based set (sky blue, yellow, vermillion; Balance's "in range" is white).
- **Ref** (reference track, bottom bar): a song to compare with. Decoded and measured once on a background thread (BS.1770 loudness, LRA, true peak, PLR; the tonal balance like Target Builder; the long-term FFT spectrum mapped onto the analyzer's bins), it's drawn in orange on Balance, the FFT analyzer (loudness-matched) and the Monitor's Loudness header. Listening: in the VST3, *Mix | Reference* replaces the plugin's output with the reference at the host's song position (10 ms crossfades; the meters keep measuring the mix); in the app, Play through Windows Audio. *Match loudness* plays it at the mix's integrated loudness. The file is remembered (per project in the plugin). `Source/reference/`.
- **Text size** (in Options): Small, Medium (default), Large, or Custom (70–200%). It applies to all text in the app; with larger sizes the bars and panels grow to fit.
- **Tempo** (or T) opens a BPM-to-milliseconds calculator: note values from 2/1 to 1/128, with optional triplet and dotted columns and a Tap button. Click a value to copy it.
- **LUFS** shows short-term, integrated (BS.1770 gated) and true-peak values, colour-coded. Loudness is white below −14 LUFS, green from −14 to −9, yellow from −9 to −6, and red from −6 up. True peak is white below −1 dBTP, yellow from −1 to 0, and red above 0. **Reset** restarts integrated loudness and the true-peak hold.

## Controls
- Click or drag on the display: frequency cursor (snaps to notes when Show Note Cursor is on). Double-click or Esc removes it.
- Mouse wheel, Up/Down, or the panel's − / + buttons: change the reference level.
- Space: pause. S: open Settings.
- Settings and the last-used source are remembered.

## How it works
- The DSP uses time-frequency reassignment (Auger, Flandrin, Chassande-Mottin), the method DigiCheck NG's manual credits. Each bin's energy is moved to its reassigned time and frequency and accumulated on the pixel grid of the chosen frequency scale (`Source/FreqScale.h`).
- Signals above 48 kHz are downsampled first, so CPU use stays constant.
- If the DSP can't keep up, the gap is drawn in white, as DigiCheck does.
- A full-scale sine reads about 0 dBFS. `dsp_test` checks this on every build.

The VST3 version: `plugin/` (processor, editor), `listen/` (band listening), `Core.h` (what the app window or the plugin processor owns). Code layout: `Source/Reassigner.*` (DSP, no dependencies), `AnalysisEngine.*` (sample ring, DSP thread, column queue, analysis taps; no audio device), `AudioEngine.*` (the standalone app's inputs: devices, loopback, test signal), `LoopbackCapture.*` (WASAPI loopback), `SpectrogramView.*` (display), `SettingsDialog.*`, `key/` (key finder, Key page), `transcribe/` (engine wrapper, chords / melody, models, MIDI), `balance/` (tonal balance), `scope/` (oscilloscope: `ScopeModel` has no JUCE dependency), `monitor/` (the Monitor page: `MonitorModel` has no JUCE dependency). The code is kept ready for a VST3 version: see [docs/VST3.md](docs/VST3.md).

## Building locally
Requires CMake 3.24+, Visual Studio 2022 or newer (Desktop C++ workload) with its Clang tools (clang-cl), Ninja, and
for GPU transcription the [Vulkan SDK](https://vulkan.lunarg.com/sdk/home). From a "x64 Native Tools" prompt:
```
set CC=clang-cl
set CXX=clang-cl
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_LINKER=lld-link -DCMAKE_AR=llvm-lib
cmake --build build
```
JUCE 9.0.6 and the transcription engine (muscriptor.cpp, which fetches ggml) are downloaded automatically. Without
the Vulkan SDK the engine builds CPU-only. The version is `VERSION` (major.minor) plus the CI build number.

Crash reports (`Source/CrashReport.*`): the app installs an unhandled-exception filter, the plugin a vectored handler limited to fatal exceptions in its own module (removed with its last instance); both write a minidump and a note to Documents\Ultimate Audio Viewer\Crashes. Release builds carry debug info (/Z7, /DEBUG) with the PDBs written to `build/symbols` and kept by CI as the `symbols` artifact (90 days).

CPU support: the key network's convolution is built twice (any x86-64, and AVX2 + FMA) and chosen at start (`KeyNetKernel.inc`); the transcription engine is built with `MUSCRIPTOR_CPU_VARIANTS` (x86-64-v2, v2 + AVX/F16C, v3, chosen at run time). Both used to need AVX(2) unconditionally.

Layout check: `"Ultimate Audio Viewer.exe" --layout-check folder` shows every page (and two Custom layouts) at 1920x1050, 1300x760 and 1024x640, saves a PNG of each, and writes `layout_report.txt`: controls that overlap (errors, exit code 1), button / box text that doesn't fit and controls outside their parent (warnings), plus `layout.json` with every control's place. CI runs it on each build, flags findings as annotations, keeps the screenshots as the `layout-check` artifact, and `tools/layout_diff.py` lists in the run summary what moved since the previous build.

Self-test: `"Ultimate Audio Viewer.exe" --test-screenshot out.png` writes a screenshot of every view. With
`SPECTROSCOPE_TEST_KEYFILE=song.wav` the Key page loads a song, with `SPECTROSCOPE_TEST_MODEL=model.gguf` it uses that
model, `SPECTROSCOPE_TEST_RECORD=10` records 10 s of the input instead, and `SPECTROSCOPE_TEST_KEYWAIT=ms` sets how
long to wait (it also writes `out_key.mid`; `SPECTROSCOPE_TEST_PLAYWAV=out.wav` renders 16 s of playback, with
`SPECTROSCOPE_TEST_LISTEN=0/1/2` for Original / Mix / Transcription). `python tools/make_tiny_model.py tiny.gguf` makes a 3 MB random model
that runs through the whole engine, for tests (CI runs `transcribe_test tiny.gguf`).

## Licensing
This is for personal use. JUCE is AGPLv3 or commercial. The ASIO SDK headers bundled with JUCE are GPLv3. Sharing builds would mean following those licences.
The chord and melody finder uses NeuralNote's engine code (Apache-2.0) and muscriptor.cpp (MIT), see `licenses/`. The
MuScriptor model weights are CC BY-NC 4.0 (non-commercial only); they are downloaded by the app, never bundled.

## TargetBuilder (tonal balance targets from your own songs)

**UAV Target Builder** (`Ultimate Audio Viewer Target Builder.exe`, released as `Ultimate-Audio-Viewer-Target-Builder.exe`: in the release, and installed next to the app with a Start menu
entry) turns songs into tonal balance targets for the upcoming Balance view.

1. Drag songs or folders into the window (or use Add songs / Add folder). WAV, AIFF, FLAC, OGG, MP3 (and WMA on
   Windows), any sample rate. Each song is analysed as soon as it's added.
2. The list shows each song's status and its distance from the combined target; songs that don't match the others
   are marked **Outlier** (select them and press Delete to drop them). Unreadable or silent files are skipped.
   The preview underneath shows the target curve, its "in range" band and the four band shares.
3. Type a name, pick the range, and press **Save target**. The default, **wide, like Tonal Balance Control**, gives
   ranges as wide as iZotope's own targets (about 11-17 dB from top to bottom, widest in the low end); the songs
   5-95 %, 10-90 % and 20-80 % options use only the spread of your songs, which is much tighter. It's written to
   `Documents\Ultimate Audio Viewer\Targets` as `<name>.json` (same layout as Tonal Balance Control target files)
   plus `<name> - report.txt`.

Dragging songs or a folder onto `Ultimate Audio Viewer Target Builder.exe` opens the window with them already added.
For batch runs there's a command-line mode: `Ultimate Audio Viewer Target Builder.exe --cli <folder>` (a folder of genre subfolders gives
one target per subfolder) or `--cli song1.wav song2.flac --name "My refs"`; options `--out DIR`, `--genres`,
`--range 10 90`, `--tight` (only the songs' spread, no widening), `--threads N`.

How it measures (shared with the app, `Source/balance/TonalAnalysis.*`, tested by `balance_test`):
- Per song: mono (mean of channels), 93 ms Hann windows at the file's own sample rate, read off at the 44.1 kHz /
  4096-point grid (so 44.1, 48 and 96 kHz files agree within ~0.1 dB and nothing above 22 kHz aliases in).
  Quiet passages are gated out (below -70 dBFS or 20 dB under the song's average). RMS magnitude per bin,
  normalised so the magnitudes add up to 1 (level independent).
- Per target: the median of the songs per bin, smoothed to 1/3 octave (power domain); the range is the 10th-90th
  percentile of the songs' smoothed curves; the 4 bands (0-250, 250-2k, 2k-8k, 8k+) store the range of each band's
  share of the spectrum. Fewer than 5 songs get a minimum range of +/-2 dB (+/-15 % per band).
- Wide (default): the range is then widened to at least the typical width of Tonal Balance Control's targets at
  each frequency (the median over 12 of its genre targets: about +7/-10 dB around the curve at 20 Hz, +5/-6 dB in
  the mids, +6/-8 dB at the top), and the band ranges likewise. It never gets narrower than the songs' own range.
- 10-20+ songs per genre give a trustworthy range. Targets stay on your computer; `Targets/` is git-ignored.
