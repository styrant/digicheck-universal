# The VST3 version

DigiCheck Universal is built twice from the same sources, with the same version number: the
standalone app (`Spectroscope`) and the VST3 plugin (`DigiCheckUniversal`, **DigiCheck Universal.vst3**).
What the plugin does for the user is in the README ("VST3 plugin"); this page is how it's put together
and what is still open.

## How the code is split

| Layer | Files | In a plugin |
|---|---|---|
| Analysis, no audio device | `AnalysisEngine` (ring buffer, DSP thread, spectrogram, loudness, analyzer, taps), `Reassigner`, `LoudnessMeter`, `analyzer/*`, `key/KeyNet`, `key/KeyCqt`, `key/KeyFinder`, `balance/TonalAnalysis`, `balance/LiveBalance`, `transcribe/*` | Shared unchanged. The processor calls `AnalysisEngine::prepare` / `push` / `release` from `prepareToPlay` / `processBlock` / `releaseResources` |
| Live analysers | `AudioTap` (interface): `KeyFinder`, `LiveBalance`, `LiveRecorder` | Shared. Registered with `AnalysisEngine::addTap`, fed from the DSP thread, never from the audio thread |
| Views | `SpectrogramView`, `BottomBar`, `AnalyzerPanel`, `BalanceView`, `key/KeyPage` | Shared. They take an `AnalysisEngine&`, not the device engine. The Key page takes files and live input only through `TranscriptionSession` and taps |
| Standalone shell | `Main.cpp`, `MainComponent` (window, Options panel with the audio input, fullscreen, OpenGL attach), `AudioEngine` (devices, ASIO, WASAPI loopback, test signal), `LoopbackCapture` | Not used. A plugin editor composes the shared views instead |
| Transcription engine | `dcu_transcribe` (C++23 static library around muscriptor.cpp / ggml) with a plain C++20 header, `TranscribeEngine.h` | Linked the same way. Models are downloaded at run time, never bundled |

CMake lists the shared files once, in `DCU_SHARED_SOURCES`; a plugin target adds only its processor
and editor.

## How the plugin is put together (Nov 2026)

- **Target.** `juce_add_plugin` (`FORMATS VST3`, category Analyzer | Tools). The codes are fixed for good
  (hosts store them in projects): manufacturer `Dcun`, plugin `Dcu1`, bundle ID
  `com.digicheckuniversal.plugin`. ASIO is off in the plugin (`JUCE_ASIO=0`).
- **Buses.** Main stereo or mono in = out; an optional stereo "Sidechain" input, off until the host
  routes something to it (mono works too).
- **Core** (`Core.h`): settings, the Spectrum Analyzer processor, key finder, live balance, recorder,
  transcription, and the Scope / Monitor / FFT taps. The app window owns one; the processor owns one per
  instance and lends it to the editor, so the analysis outlives the plugin window. `Core::registerWith`
  hooks the taps to an `AnalysisEngine`.
- **MainComponent** is the main view in both: `MainComponent (globalProps, core, engine, device, host)`.
  The standalone passes its `AudioEngine` as `device`; the plugin passes `nullptr` and itself as `host`
  (`HostLink`: processing?, host tempo, transport, sidechain connected, the sidechain's engine, Scope
  sync bars, Scope listen, band listening, a status line).
- **Processor** (`plugin/PluginProcessor`): `processBlock` feeds the shared `AnalysisEngine`
  (spectrogram, loudness, analyzer, key, balance, recorder, Monitor), a second `AnalysisEngine` with the
  sidechain (its spectrogram), and pushes the Scope and FFT taps itself with the sidechain and the
  Scope's sync phase per sample (`FifoTap` extras: sample-aligned with the main signal). It reads the
  play head (tempo, PPQ, time signature, playing). Band listening (`listen/BandIsolator`) runs last on
  the output and returns immediately, untouched, when nothing is active.
- **Monitor.** Its `MonitorModel` lives in `Core` in the plugin; a processor timer (message thread) feeds
  it while no editor is open, so loudness, LRA and PLR cover everything since the last reset.
- **Editor** (`plugin/PluginEditor`): its own `AppLookAndFeel` (never a static one), resizable, the size
  saved per instance; it rebuilds the views when the host loads another state.
- **State.** The instance's `PropertySet` (all view settings) as XML in the project; app-wide things
  (presets) in "DigiCheck Universal Plugin.settings".
- **Tests.** `isolator_test` (band listening), `plugin_host_test` (loads the built .vst3 like a DAW:
  sidechain bus, bit-exact pass-through, state round trip; with a display also the editor, the project
  tempo and screenshots of the views with a sidechain), pluginval in CI (strictness 5, no GUI).

## Still open

1. **Frame clock and text size are process-wide** (`frameclock::clock()`, `ui::textScale`). Fine for
   one plugin window; several open editors share one frame-rate cap and text size.
2. **Shared services per process.** `ModelStore` should become a `juce::SharedResourcePointer` (one
   download for all instances); allow one transcription at a time per process (0.3-3 GB each).
3. **Idle threads.** Each instance has two DSP threads that poll every 10 ms while idle; with dozens of
   instances they should wait on an event.
4. **Sidechain from another instance** (without routing a bus), **Sync to Start**, **track name and
   colour** (`updateTrackProperties`) and **REAPER's embedded UI** are not done.
5. **Mono-in / stereo-out** layouts are refused (in must equal out).
6. The bars analyzer (Spectrum Analyzer 2) has no sidechain; the FFT style does.

## Licences relevant to a plugin

- VST3 SDK: MIT licensed since 2025 (bundled with JUCE).
- JUCE: personal / non-commercial use under JUCE's licence.
- ASIO (standalone only): Steinberg also released ASIO under an open-source licence in Oct 2025; check its terms before distributing a build with ASIO.
- MuScriptor model weights: CC BY-NC 4.0 (non-commercial). They are downloaded by the user, never
  distributed with the app or plugin.
- NeuralNote (Apache-2.0) and muscriptor.cpp (MIT): notices in `licenses/`.


## Instance linking and the reference track
- **Link:** `instancelink::Hub` (process-wide) lists every instance with its track name (`updateTrackProperties`) and a single-writer ring of its input. A linked instance's latest block replaces the sidechain pointers in `processBlock`, so every sidechain view works unchanged. The pointer to the linked ring is swapped on the message thread under a spin lock that the audio thread only try-locks.
- **Reference:** `ReferenceTrack` (in `Core`) can replace the plugin's output with the reference at the host's song position (`renderSynced`, Hermite resampling, 10 ms crossfades), after the analysis has seen the mix and before band listening.

## Closed window, memory
- With the editor closed only the Monitor (and a running Key recording, Link and the Reference) keeps working; the spectrogram, analyzer, scope, FFT and balance analysis stop until the window opens (MainComponent's destructor; setView turns them on again).
- References are decoded once per process per file, as 16-bit, and shared by every instance that loads the same file (ReferenceTrack::Data); the FFT averages are computed on a background job per FFT size.
