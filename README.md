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
- **Presets** (bottom bar, left of the view's controls, or P):
  - Spectrogram: built-in views (Default, Bass focus, Low end & mix body, Vocals & melody, Hi-hats & air, Drums & transients, Mastering overview, Noise floor & quiet detail, High contrast, Plain FFT). They change only the display and analysis settings, never the input. You can save the current view under your own name, and delete your presets.
  - Analyzer: the factory presets, your own saved presets (settings and colours), and import of Spectrum Analyzer v1.x .fxp/.fxb files.
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
