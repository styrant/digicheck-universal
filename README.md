# Spectroscope — DigiCheck Universal

A native Windows recreation of the Spectroscope (reassigned spectrogram) from RME's DigiCheck NG that works with any audio interface. It was built for a Focusrite Scarlett 16i16 Gen 4.

## Getting it
Every push to `main` is built by GitHub Actions and published to the **Latest build** release:
- **SpectroscopeSetup.exe** installs per user (no admin rights), with a Start menu entry, an optional desktop shortcut and an uninstaller.
- **Spectroscope.exe** is the same app as a single portable file.

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

## Sources
| Source | Use it for |
|---|---|
| **Windows Audio (shared)** | The default. Reads the Scarlett's inputs through Windows, so it works while your DAW holds ASIO. |
| **Windows Audio (exclusive)** | Bit-exact input through Windows, when nothing else is using the device. |
| **ASIO** | Bit-exact and lowest latency. It may be unavailable while your DAW is using the Focusrite ASIO driver. |
| **Windows playback (loopback)** | Analyses whatever Windows is playing to any output device (e.g. the Scarlett's outputs), with no routing needed. |
| **Test signal** | A sweep, chord, clicks and tones, for checking the display without an interface. |

**L / R** choose which two channels of the source to analyse. The spectrogram then shows **Left**, **Right**, **Mid**
((L+R)/2, what both sides share) or **Side** ((L-R)/2, the stereo difference), picked in the bottom bar or in
Settings → Channel Select. (The Analyzer has its own channel box with the same choices plus Stereo.)

## Views
Switch with the **Spectrogram | Analyzer | Balance | Key** buttons at the bottom left (keys **1-4**, **V** cycles, **K** goes to Key). The bottom bar stays the same in every view: Options, the LUFS readout, Tempo, Reset and Full screen are always there. The controls in the middle change with the view.

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
- **Analyzer:** [Spectrum Analyzer 2](Source/analyzer/LICENSE), a constant-Q filter-bank analyzer (Seven Phases, MIT licence).
  - Peak bars, peak hold and RMS average, at 3–24 bands per octave.
  - Wheel zooms, drag pans, and shift+drag zooms to a selection.
  - Hover for the readout (frequency, level, note and cents). Right-click or **Settings** opens its settings panel.
  - Keys: F freeze, R reset meters, 0 full range.
  - It only runs while its view is visible, and its settings are saved with the app's.

## Rendering
- The display is synced to the monitor's refresh rate (60/120/144 Hz…). New spectrogram columns pass through a ~30 ms buffer and are released at exactly the display speed, so the scroll is even on every frame.
- **GPU Rendering (OpenGL)** is on by default (Settings → GPU Rendering). The spectrogram history is an OpenGL texture: each frame uploads only the new columns and scrolls with sub-pixel precision. The rest of the interface, including the Analyzer, is drawn on the GPU too.
- Turn it off to use the CPU renderer, e.g. if your graphics driver has OpenGL problems.
- **Frame rate limit** (in Options): caps the whole window, both views, at a chosen rate. For a G-Sync / FreeSync monitor, pick about 3 below its maximum (e.g. 141 for 144 Hz) to stay inside the variable-refresh range. Vsync stays on, so frames never exceed the monitor's refresh rate.
- The Options panel shows the renderer and the measured frame rate, e.g. `GPU (OpenGL), 141 fps (limit 141)`.

## Toolbar and windows
- **Options** (bottom left, or O) opens a pop-up panel with the audio input (source, L/R channels, Start/Stop, Rescan), the status line and the display options (GPU rendering, frame rate limit). There is no top toolbar, so the analysis uses the full height. Click anywhere else or press Esc to close it.
- **Full screen**: F11 or the small button at the bottom right; Esc leaves. This is *windowed* (borderless) full screen. The window is deliberately 1 px taller than the monitor, so graphics drivers don't promote the OpenGL window to exclusive full screen. That keeps HDR and the video signal undisturbed.
- **Tonal Balance view** (Balance button, or 3; V cycles the three views): your input's tonal balance against a target.
  - Left, four band meters (Low 0-250 Hz, Low-mid 250 Hz-2 kHz, High-mid 2-8 kHz, High 8 kHz+): the blue zone is the
    target's range and the marker shows where you are: green "In range", amber just outside, red further out, with how
    many dB above or below the range.
  - Right, the detailed curve (20 Hz-20 kHz, 1/3-octave): **Curve** shows your audio over the target's range (red
    where it leaves the range); **Diff** shows your audio minus the target, so 0 dB is right on target. Hover for the
    exact difference at any frequency.
  - Bottom bar: the target, the measurement window (Realtime, 1 s, 3 s, 10 s, 30 s or since Reset), Curve/Diff and
    Reset. Realtime follows the music as it plays (about the last 0.3 s, a new measurement every 23 ms); the longer
    windows show the overall balance. The target list ends with Open targets folder, Rescan targets and Open Target
    Builder.
  - Drawing runs on the display's frame clock (vblank, obeying the frame-rate cap) and is composited by OpenGL when
    the GPU renderer is on. The curve and meters glide between measurements, so they move smoothly at the display's
    refresh rate; the grid, labels and target range are drawn once and cached. Set `SPECTROSCOPE_PAINT_LOG=1` to
    print the view's average paint time (about 2 ms at 1300x760 on a software renderer).
  - Targets are read from `Documents\DigiCheck Universal\Targets` (nothing is built into the app): files made with
    Target Builder and Tonal Balance Control-style target files (including the variant that wraps values as
    `{"Type", "Value"}`). New or changed files are picked up automatically.
  - The measurement is the same as Target Builder's (gated, level independent), so a song compared with a target built
    from it sits on the target line. It only runs while the Balance view is shown.
- **Key** (Key view, 4 or K): every key-related tool on one page, sharing one audio source.
  - **Source:** **Record** (the selected input; **R**), **Browse...**, or **drag & drop** a song anywhere on the page
    (WAV, AIFF, FLAC, OGG, MP3). Nothing is analysed until you choose.
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
  - **Model:** downloaded once inside the app, not part of the install: Small 210 MB (about real time on a CPU),
    Medium 620 MB or Large 2.7 GB (a GPU recommended). They're stored in `%APPDATA%\DigiCheck Universal\Models`;
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

Code layout: `Source/Reassigner.*` (DSP, no dependencies), `AnalysisEngine.*` (sample ring, DSP thread, column queue, analysis taps; no audio device), `AudioEngine.*` (the standalone app's inputs: devices, loopback, test signal), `LoopbackCapture.*` (WASAPI loopback), `SpectrogramView.*` (display), `SettingsDialog.*`, `key/` (key finder, Key page), `transcribe/` (engine wrapper, chords / melody, models, MIDI), `balance/` (tonal balance). The code is kept ready for a VST3 version: see [docs/VST3.md](docs/VST3.md).

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

Self-test: `Spectroscope.exe --test-screenshot out.png` writes a screenshot of every view. With
`SPECTROSCOPE_TEST_KEYFILE=song.wav` the Key page loads a song, with `SPECTROSCOPE_TEST_MODEL=model.gguf` it uses that
model, `SPECTROSCOPE_TEST_RECORD=10` records 10 s of the input instead, and `SPECTROSCOPE_TEST_KEYWAIT=ms` sets how
long to wait (it also writes `out_key.mid`). `python tools/make_tiny_model.py tiny.gguf` makes a 3 MB random model
that runs through the whole engine, for tests (CI runs `transcribe_test tiny.gguf`).

## Licensing
This is for personal use. JUCE is AGPLv3 or commercial. The ASIO SDK headers bundled with JUCE are GPLv3. Sharing builds would mean following those licences.
The chord and melody finder uses NeuralNote's engine code (Apache-2.0) and muscriptor.cpp (MIT), see `licenses/`. The
MuScriptor model weights are CC BY-NC 4.0 (non-commercial only); they are downloaded by the app, never bundled.

## TargetBuilder (tonal balance targets from your own songs)

**Spectroscope Target Builder** (`TargetBuilder.exe`: in the release, and installed next to the app with a Start menu
entry) turns songs into tonal balance targets for the upcoming Balance view.

1. Drag songs or folders into the window (or use Add songs / Add folder). WAV, AIFF, FLAC, OGG, MP3 (and WMA on
   Windows), any sample rate. Each song is analysed as soon as it's added.
2. The list shows each song's status and its distance from the combined target; songs that don't match the others
   are marked **Outlier** (select them and press Delete to drop them). Unreadable or silent files are skipped.
   The preview underneath shows the target curve, its "in range" band and the four band shares.
3. Type a name, pick the range, and press **Save target**. The default, **wide, like Tonal Balance Control**, gives
   ranges as wide as iZotope's own targets (about 11-17 dB from top to bottom, widest in the low end); the songs
   5-95 %, 10-90 % and 20-80 % options use only the spread of your songs, which is much tighter. It's written to
   `Documents\DigiCheck Universal\Targets` as `<name>.json` (same layout as Tonal Balance Control target files)
   plus `<name> - report.txt`.

Dragging songs or a folder onto `TargetBuilder.exe` opens the window with them already added.
For batch runs there's a command-line mode: `TargetBuilder.exe --cli <folder>` (a folder of genre subfolders gives
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
