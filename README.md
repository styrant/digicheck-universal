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

**L / R** choose which two channels of the source to analyse. **Channel Select** in Settings then picks left, right, the sum or the difference.

## Views
Switch with the **Spectrogram | Analyzer** buttons at the bottom left (or **V**). The bottom bar stays the same in both views: Options, the LUFS readout, Key, Tempo, Reset and Full screen are always there. The controls in the middle change with the view.

- **Spectrogram:** the reassigned spectrogram described below.
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
- **Key finder** (Key button or K): a window that finds the musical key of the live input or of an audio file, with the key chart beside it. Nothing is analysed until you choose:
  - **Record**: analyses the selected input until Stop (or until the window closes).
  - **Browse...** or **drag & drop** a file (WAV, AIFF, FLAC, OGG, MP3; also WMA on Windows): the whole file is analysed, as the original OpenKeyScan does, with a progress bar.
  - *Now*: the key of the last 20 s, refreshed every 3 s, with the two next most likely keys.
  - *Session*: the average over everything heard since Record or Reset (silence is ignored); also lists the Camelot-compatible keys.
  - Keys are shown as names, Camelot (8A) and Open Key (1m). The chart marks the current key (filled) and the session key (outlined).
  - Recording uses about 7% of one CPU core; a 3-minute file takes a few seconds.
  - The model is OpenKeyScan's KeyNet (Korzeniowski & Widmer 2018, trained on GiantSteps), from styrant/openkeyscan-analyzer (MIT, see licenses/). It runs natively in C++: a constant-Q front end matching librosa's and the 9-layer CNN; tests/key_test checks both against librosa and PyTorch. tools/export_keynet.py regenerates Resources/keynet.bin from a checkpoint.
- **Presets** (bottom bar, left of the view's controls, or P):
  - Spectrogram: built-in views (Default, Bass focus, High contrast). They change only the display and analysis settings, never the input. You can save the current view under your own name, and delete your presets.
  - Analyzer: the factory presets, your own saved presets (settings and colours), and import of Spectrum Analyzer v1.x .fxp/.fxb files.
- **Smooth scrolling**: the spectrogram moves a whole number of pixels per monitor frame, so thin lines don't shimmer. The scroll speed is snapped to the nearest whole number of pixels per frame when that is within 20% of the chosen speed (e.g. 250 px/s plays at 240 px/s on 60 Hz); the time grid follows the actual speed.
- **Text size** (in Options): Small, Medium (default), Large, or Custom (70–200%). It applies to all text in the app; with larger sizes the bars and panels grow to fit.
- **Key** (or K) opens a small always-on-top chart of major keys and their relative minors.
- **Tempo** (or T) opens a BPM-to-milliseconds calculator: note values from 2/1 to 1/128, with optional triplet and dotted columns and a Tap button. Click a value to copy it.
- **LUFS** shows short-term, integrated (BS.1770 gated) and true-peak values, colour-coded. Loudness is white below −14 LUFS, green from −14 to −9, yellow from −9 to −6, and red from −6 up. True peak is white below −1 dBTP, yellow from −1 to 0, and red above 0. **Reset** restarts integrated loudness and the true-peak hold.

## Controls
- Click or drag on the display: frequency cursor (snaps to notes when Show Note Cursor is on). Double-click or Esc removes it.
- Mouse wheel, Up/Down, or the panel's − / + buttons: change the reference level.
- Space: pause. S: open Settings.
- Settings and the last-used source are remembered.

## How it works
- The DSP uses time-frequency reassignment (Auger, Flandrin, Chassande-Mottin), the method DigiCheck NG's manual credits. Each bin's energy is moved to its reassigned time and frequency and accumulated on a log-frequency pixel grid.
- Signals above 48 kHz are downsampled first, so CPU use stays constant.
- If the DSP can't keep up, the gap is drawn in white, as DigiCheck does.
- A full-scale sine reads about 0 dBFS. `dsp_test` checks this on every build.

Code layout: `Source/Reassigner.*` (DSP, no dependencies), `AudioEngine.*` (sources, threads, lock-free queues), `LoopbackCapture.*` (WASAPI loopback), `SpectrogramView.*` (display), `SettingsDialog.*`.

## Building locally
Requires CMake 3.22+ and Visual Studio 2022 or newer (Desktop C++ workload):
```
cmake -B build -A x64
cmake --build build --config Release
```
JUCE 9.0.6 is downloaded automatically. Run `Spectroscope.exe --test-screenshot out.png` to self-test.

## Licensing
This is for personal use. JUCE is AGPLv3 or commercial. The ASIO SDK headers bundled with JUCE are GPLv3. Sharing builds would mean following those licences.

## TargetBuilder (tonal balance targets from your own songs)

`TargetBuilder.exe` (in the release, and installed next to the app with a Start menu entry) turns songs into
tonal balance targets for the upcoming Balance view. Easiest use: **drag a folder onto TargetBuilder.exe**.

- A folder of songs gives one target named after the folder.
- A folder of genre subfolders (`Hip-hop\`, `Rock\`, ...) gives one target per subfolder.
- Command line: `TargetBuilder <folder>` or `TargetBuilder song1.wav song2.flac --name "My refs"`;
  options `--out DIR`, `--genres`, `--range 10 90` (percentiles for the "in range" band), `--threads N`.
- Formats: WAV, AIFF, FLAC, OGG, MP3 (and WMA on Windows); any sample rate.
- Output (default `Documents\DigiCheck Universal\Targets`): `<name>.json` in the same layout as Tonal Balance
  Control target files, plus `<name> - report.txt` listing every song's band balance and how far it is from the
  target, with outliers flagged so you can drop songs that don't fit and build again.

How it measures (shared with the app, `Source/balance/TonalAnalysis.*`, tested by `balance_test`):
- Per song: mono (mean of channels), 93 ms Hann windows at the file's own sample rate, read off at the 44.1 kHz /
  4096-point grid (so 44.1, 48 and 96 kHz files agree within ~0.1 dB and nothing above 22 kHz aliases in).
  Quiet passages are gated out (below -70 dBFS or 20 dB under the song's average). RMS magnitude per bin,
  normalised so the magnitudes add up to 1 (level independent).
- Per target: the median of the songs per bin, smoothed to 1/3 octave (power domain); the range is the 10th-90th
  percentile of the songs' smoothed curves; the 4 bands (0-250, 250-2k, 2k-8k, 8k+) store the range of each band's
  share of the spectrum. Fewer than 5 songs get a minimum range of +/-2 dB (+/-15 % per band).
- 10-20+ songs per genre give a trustworthy range. Targets stay on your computer; `Targets/` is git-ignored.
