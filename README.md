# Spectroscope — DigiCheck Universal

A native Windows recreation of the Spectroscope (reassigned spectrogram) from RME's DigiCheck NG that works with any audio interface. It was built for a Focusrite Scarlett 16i16 Gen 4.

## Getting it
Every push to `main` is built by GitHub Actions. Download `Spectroscope.exe` from the **Latest build** release, or from the run's artifacts. It's a single file with nothing to install. Windows SmartScreen may warn the first time because the file isn't signed; choose **More info → Run anyway**.

## Sources
| Source | Use it for |
|---|---|
| **Windows Audio (shared)** | The default. Reads the Scarlett's inputs through Windows, so it works while your DAW holds ASIO. |
| **Windows Audio (exclusive)** | Bit-exact input through Windows, when nothing else is using the device. |
| **ASIO** | Bit-exact and lowest latency. It may be unavailable while your DAW is using the Focusrite ASIO driver. |
| **Windows playback (loopback)** | Analyses whatever Windows is playing to any output device (e.g. the Scarlett's outputs), with no routing needed. |
| **Test signal** | A sweep, chord, clicks and tones, for checking the display without an interface. |

**L / R** choose which two channels of the source to analyse. **Channel Select** in Settings then picks left, right, the sum or the difference.

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
