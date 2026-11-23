# VST3 readiness

DigiCheck Universal is **standalone only** for now. This page records how the code is kept ready for a
VST3 version, so that it can be added as a second build target from the same sources and released with
the same version number, without forking the code.

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

## Already done (Nov 2026)

- **AnalysisEngine split from AudioEngine.** The audio-device code (`AudioEngine`) only feeds an
  `AnalysisEngine` through `push()`, which is real-time safe (a lock-free ring, no allocation, no locks).
- **AudioTap.** Live analysers no longer hang off the engine as special cases; anything that analyses
  the stream implements `AudioTap::push`.
- **Views depend on `AnalysisEngine`, not `AudioEngine`.**
- **Settings serialise to any `juce::PropertySet`.** The app uses its settings file; a plugin can keep
  a `PropertySet` per instance and store it in the host's project with `createXml` / `restoreFromXml`.
- **No shared static UI state.** The bottom bar's look-and-feel is owned per instance (a static
  look-and-feel destroyed at DLL unload is a classic plugin crash); the Key page's download watcher is
  per instance.
- **One version number.** Major.minor in `VERSION`; CMake and CI read it, and the patch is the CI build
  number. A plugin target gets the same version automatically.
- **Fixed bundle IDs** (`com.digicheckuniversal.spectroscope`, `.targetbuilder`). Hosts identify
  plugins by IDs like these, so they are set explicitly now rather than derived from names.
- **The transcription engine is isolated.** Its C++23 headers stay inside `dcu_transcribe`; the
  2.7 GB of model weights are never part of a binary.

## To do when the VST3 work starts

1. **Plugin target.** `juce_add_plugin` with `FORMATS VST3` (Standalone stays the existing app),
   `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT FALSE`, the audio passed through unchanged, latency 0. Choose the
   4-character `PLUGIN_MANUFACTURER_CODE` and `PLUGIN_CODE` once and never change them: hosts store
   them in projects.
2. **Editor shell.** Split `MainComponent` into a reusable "main view" (the four views and the bottom
   bar) and the standalone shell. In the plugin the Options panel shows display settings only (there's
   no device to choose), and fullscreen becomes a resizable editor.
3. **Frame clock.** `frameclock::clock()` and `frameclock::capHz` are process-wide. Fine for one
   window, but each plugin editor should own its own clock (several editors can be on different
   monitors).
4. **Look-and-feel.** `Main.cpp` sets the app-wide default look-and-feel. A plugin must set it on its
   editor instead, because the default is shared with the host and other plugins.
5. **Shared services per process.** `ModelStore` should become a `juce::SharedResourcePointer` (one
   download for all instances; an inter-process lock already stops two downloads of the same model).
   Allow one transcription at a time per process: each run holds 0.3–3 GB.
6. **Per-instance vs global settings.** Per project: the view, spectrogram, analyzer and balance
   settings, and the target. Global: text size, model size, compute device, frame-rate cap.
7. **Idle threads.** The DSP thread polls every 10 ms while idle. Fine for one app, wasteful with
   dozens of instances: make it wait on an event until `prepare()`.
8. **Text size.** `ui::textScale` is process-wide; that suits a user preference, but a change in one
   editor should trigger a relayout of the others.
9. **Standalone-only features.** ASIO, WASAPI loopback, device selection and the test signal stay in
   the standalone shell. The plugin analyses its track's audio.

10. **Scope features reserved for the plugin.** The Scope header and menu already show these, disabled:
    - **Sync** to the host transport (window start on bar lines from `AudioPlayHead::getPosition()`: PPQ,
      BPM, time signature, isPlaying) and **Sync to Start** (restart when playback starts). In the standalone
      the window follows the typed BPM and restarts after silence.
    - **Sidechain**: a second input bus (`BusesProperties().withInput("Sidechain", stereo, false)`) drawn as
      the overlay, and **instance sidechain** (another Scope instance's wave via a process-wide registry of
      `ScopeTap`s, keyed by instance ID). The standalone's Side2SC (L−R of the input) stays.
    - **Track name and colour** from `AudioProcessor::updateTrackProperties`.
    - **Listen to the BP band**: route the band-passed signal to the plugin's output while held.
    - **REAPER embedded mode** (`IReaperUIEmbedInterface`, via `VST3ClientExtensions`).
    - `ScopeTap` becomes per instance (one FIFO each; it already has no globals). `ScopeModel` takes the
      host's sample rate in `prepareToPlay`.

## Licences relevant to a plugin

- VST3 SDK: MIT licensed since 2025 (bundled with JUCE).
- JUCE: personal / non-commercial use under JUCE's licence.
- ASIO (standalone only): Steinberg also released ASIO under an open-source licence in Oct 2025; check its terms before distributing a build with ASIO.
- MuScriptor model weights: CC BY-NC 4.0 (non-commercial). They are downloaded by the user, never
  distributed with the app or plugin.
- NeuralNote (Apache-2.0) and muscriptor.cpp (MIT): notices in `licenses/`.
