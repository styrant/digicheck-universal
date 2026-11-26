# CLAUDE.md: handoff for Ultimate Audio Viewer (UAV)

Read this first. It covers what the project is, how the user likes to work, how to build and test, how it fits together, and what's open.

## What it is
**Ultimate Audio Viewer (UAV)**, formerly *DigiCheck Universal* / *Spectroscope*: an all-in-one tool for seeing and measuring music, inspired by RME DigiCheck NG but working with any interface. It's a native Windows C++20 / JUCE 9.0.6 app, a VST3 plugin, and a tiny **UAV Bridge** VST3.
- Pages: Spectrogram (reassigned), Analyzer (Bars constant-Q + FFT), Balance (tonal balance vs targets), Key (key / chords / melody via a transcription model), Scope (tempo-synced), Monitor (Insight-style loudness / levels / sound field / phase / history), Custom (several views in panes).
- Extras: Ref (reference track), Link (plugin instances share tracks), Listen (band solo, plugin), Target Builder (separate exe), and the DAW bridge (below).
- Personal, non-commercial use. The user has a Focusrite Scarlett 16i16 on Windows.
- Repo: `styrant/digicheck-universal` (the name stays). It's temporarily public.
- `docs/MANUAL.md` is the user manual. Keep it in step with features, and with the help and tooltip text in the UI.

## How the user wants to work
- **Be efficient:** the user is watching their usage limits. Make targeted reads and greps instead of reading big files whole (`MainComponent.cpp` is about 2k lines). Keep messages short.
- **Proposals:** when asked for reviews or suggestions, give a numbered list. The user approves or denies each item; implement only approved ones.
- **Commits:** commit and push to `main` when work is done (CI builds the release). End commit messages with:
  ```
  Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01SfLb2kwvzXQvXFtMgtQmM7
  ```
  (or this session's own attribution lines, if Claude Code supplies them).
- **Things never to do:**
  - Never put signing credentials in code; they belong only in GitHub secrets.
  - Never bundle the transcription model. It's downloaded inside the app and its license is non-commercial.
- **Naming:** files, exe, installer and folders use the full name *Ultimate Audio Viewer*; labels inside the UI say **UAV**. Exception: the Bridge is **UAV Bridge** everywhere (`UAV Bridge.vst3`). The publisher / company (plugin vendor, exe metadata, installer) is **Styrant Audio**. Internal names are kept on purpose so existing DAW projects still load:
  - CMake targets `Spectroscope` (app), `DigiCheckUniversal` (plugin), `TargetBuilder`, `UAVBridge`
  - plugin codes `Dcun` / `Dcu1` (Bridge `Dcub`), bundle IDs `com.digicheckuniversal.*`
  - the plugin state tag

## Build (Windows, local)
CI's recipe (`.github/workflows/build.yml`, steps "Configure" and "Build") runs in a VS developer prompt:
```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl ^
      -DMUSCRIPTOR_VULKAN=ON -DMUSCRIPTOR_CPU_VARIANTS=ON
cmake --build build --parallel
```
- Use Visual Studio's own clang-cl (`VCINSTALLDIR\Tools\Llvm\x64\bin`).
- Vulkan requires the Vulkan SDK. For faster iteration, leave out `-DMUSCRIPTOR_VULKAN=ON` and `-DMUSCRIPTOR_CPU_VARIANTS=ON`.
- JUCE and muscriptor (the transcription engine, ggml) are fetched by FetchContent. `-DJUCE_DIR=` uses a local JUCE.
- ASIO is on in the app; the SDK headers come with JUCE.
- Outputs:
  - `build/Spectroscope_artefacts/Release/Ultimate Audio Viewer.exe`
  - `build/DigiCheckUniversal_artefacts/Release/VST3/Ultimate Audio Viewer.vst3`
  - `build/UAVBridge_artefacts/Release/VST3/UAV Bridge.vst3`
  - `Ultimate Audio Viewer Target Builder.exe`
- PDBs go to `build/symbols`.
- Installer: Inno Setup, `installer/UltimateAudioViewer.iss`.
- Version is `VERSION` (1.3) plus the CI run number; local builds are `.0`.

## Tests (all in CMakeLists.txt; CI runs them)
- **Console tests:** `dsp_test`, `loudness_test`, `analyzer_dsp_test`, `key_test` (and with `KEYNET_NO_AVX2=1`), `balance_test`, `harmony_test`, `transcribe_test`, `player_test`, `fft_test`, `monitor_test`, `scope_test`, `isolator_test`, `target_loader_test`.
- **Bridge tests:** `bridge_test` covers shared memory, slots, names, and the DAW **Mixer** (pitch, gaps, level, drift). `bridge_test --send <s>` acts as a fake DAW sender.
- **Plugin host tests:**
  - `plugin_host_test "<UAV.vst3>" outdir`
  - `bridge_plugin_test "<Bridge.vst3>"`
  - pluginval at strictness 5 for both. pluginval is a GUI exe, so read its exit code via `Start-Process -Wait -PassThru`.
- **Layout check:** `"Ultimate Audio Viewer.exe" --layout-check <folder>`, with `SPECTROSCOPE_NO_GPU=1` on machines without a GPU.
  - Renders 9 pages at 1920×1050, 1300×760, 1024×640 and 900×520 (900×520 is the plugin's minimum size).
  - Writes PNGs and `layout_report.txt`, listing overlaps and text that needs more room. A clean report has no such lines.
  - It runs the test signal, so the "No input" banner never appears in it.
  - It never saves settings.
  - `tools/layout_diff.py` compares two runs.

## Architecture: where things are
- **`Source/Main.cpp`:** app start-up, crash report, `appinfo::migrate()` (moves old DigiCheck folders), layout check, test hooks (`SPECTROSCOPE_MEASURE_FILE`, `SPECTROSCOPE_REPORT_OUT`).
- **`MainComponent`:** shared by the app and the plugin. It holds the views, Options panel, Custom page, help mode, copy image, update check, backup / restore, Ref panel, the "No input" banner (`updateNoInput()`, app only), and status line. `host` (HostLink*) is non-null in the plugin; `device` (AudioEngine*) is non-null in the app.
- **`BottomBar`:**
  - Page tabs, LUFS boxes, per-view controls, corner buttons. It has a compact mode under the full-width total: short tab names, then no Tempo button.
  - The Bars analyzer's controls are re-parented into `AnalyzerPanel`'s header strip (`attachAnalyzerHeader` / `layoutAnalyzer`).
- **Engines:**
  - `AnalysisEngine` (DSP thread; `push` / `pushLR`).
  - `AudioEngine` (app inputs: Windows Audio shared / exclusive, ASIO, WASAPI loopback, Test, From DAW).
- **`Settings.h`:** every setting, with load / save.
- **`AppInfo`:** names, folders and migration.
- **`CrashReport`:** minidumps to `Documents\Ultimate Audio Viewer\Crashes`.
- **Per page:** `Source/{analyzer,fft,balance,key,scope,monitor,reference,listen,transcribe}`.
- **The plugin:** `Source/plugin/` (PluginProcessor / Editor; HostLink is the plugin-to-MainComponent interface).
- **The DAW bridge** (`Source/bridge/Bridge.{h,cpp}`): lets the app hear a DAW that uses ASIO.
  - Shared memory `Local\UltimateAudioViewer.Bridge.1`: 16 slots, each a 65536-frame stereo ring. Header magic `0x44435542`, version 1.
  - Slots are claimed by compare-and-swap and treated as stale after 3 s.
  - `bridge::Sender::write` is real-time safe. `listSenders()` / `chooseSlot()` pick a track: automatic means the master-named track, else the newest sender.
  - The senders are **UAV Bridge** (`Source/bridgeplugin/BridgePlugin.cpp`: pass-through, status window, links only `juce_audio_processors`, about 5.6 MB against 77 MB for the full plugin) and every full UAV instance ("Send to the UAV app", on by default).
  - Ways the app receives:
    1. The input **From DAW** (`AudioEngine` bridge thread).
    2. **"DAW (UAV Bridge)"** (Options checkbox, `settings.mixBridge`). `bridge::Mixer` adds the master to any input on the input's own thread: linear resampling, about 40 ms behind the writer, 0.2 % drift correction. The status line shows "+ DAW ...".
- **Installer:**
  - Admin is the default; `/ALLUSERS` relaunch offer; removes an install found in another folder.
  - `[InstallDelete]` removes the old DigiCheck exe, shortcuts and vst3 (this fixed error 183).
  - Task boxes: `vst3` and `bridge`.
- **CI** (`windows-latest`, clang-cl, Ninja, ccache): builds, runs every test, checks the layout, runs pluginval, signs (gated on the `AZURE_CLIENT_SECRET` secret), builds the installer, and publishes the **"latest"** release.
  - Release assets: `Ultimate-Audio-Viewer-Setup.exe`, `Ultimate-Audio-Viewer.exe`, `Ultimate-Audio-Viewer-Target-Builder.exe`, `Ultimate-Audio-Viewer-VST3.zip`, `Ultimate-Audio-Viewer-Bridge-VST3.zip`. The names are hyphenated because GitHub turns spaces into dots.
  - PDBs are kept as the `symbols` artifact.

## Gotchas learned the hard way
- **Inno Setup:** in `[Code]`, a line starting with `[` is read as a section header. Keep array literals on the call's line.
- **lld-link:** it needs `build/symbols` to exist (CMake creates it).
- **CI runner:** it has no GPU. Set `SPECTROSCOPE_NO_GPU=1` for anything that opens the app's window.
- **Settings in tests:** `blockSavingForTest()` keeps test runs from overwriting the user's settings.
- **Snapshots:** snapshotting a hosted VST3 editor gives a blank image (native child window). Render the editor directly instead.
- **Full screen** is the main window made borderless in place (`setAppFullscreen` in `Main.cpp`). The earlier separate full-screen window lost mouse and keyboard on the user's second monitor.
- **GPU frame pacing:** with the GPU renderer, JUCE paints all components on the GL thread *while holding the message lock*. `MainComponent::onFrame` therefore never starts GL frames back to back (skips while one is pending, waits as long as the last paint took). Don't call `triggerRepaint()` from elsewhere every frame.
- **The keynet weights are already fp16.** The ggml CPU variants gain about 20 % on prefill only, and there's no AVX-512 variant.

## Recent work, newest first (all pushed; CI green)
- **Session 2 (Nov 2026), newest first:** review fixes (Custom pane header only from the pane's top edge; full screen on top only while active; no idle redraws in Monitor / Scope / Bars when held or frozen; cached `paneRects`; isolator fades on a BP/LP/HP switch; SafePointer in the start-up resume); Custom page pane header + right-click menu (`BottomBar::paneMenu`) + Ctrl+1..9 saved layouts; Scope palettes (right-click / wheel on Spectral, C / Shift+C; `MenuButton` in `ScopePage.h`); Scope BP / LP / HP (`scopeBPType`, also the plugin's Listen); no per-mouse-move repaints in views that redraw every frame; GPU frame pacing; version label in Options; Analyzer Alt+click pin / Alt+right-click unpin + Marker line width (Bars: processor `markerWidth`; FFT: `fftMarkerWidth`); FFT Freeze no longer shrinks the plot; full screen in place, CPU-override fix, Hide bar layout fix; installer's short task labels.
0. **Fixes from the user's testing:** the spectrogram's Ref scroll recolours once per frame (`recolourPending`, was a full-history recolour per wheel step: hangs); the checkbox is now "DAW (UAV Bridge)"; the Bridge is `UAV Bridge.vst3` (the installer removes the old name; same plugin ID); publisher Styrant Audio; self-signing.
1. **"Also hear your DAW" mix** (`bridge::Mixer`, an Options checkbox, the status line, the manual).
2. **UI tidy-up** (all 9 approved):
   - short loudness captions (S / I / TP)
   - Balance target box keeps a minimum width
   - the corner buttons share the bar's size and font
   - no "Scope" / "Monitor" titles
   - one "No input" banner with **Open Options**
   - Bars controls in a top strip
   - labelled Scope values (Gain, Hz)
   - Key page control groups with dividers
   - slimmer Monitor module headers
   - fits the plugin's 900×520 minimum: compact bottom bar, the FFT header drops buttons in order, the Loudness module fits short panes
3. **UAV Bridge VST3**, plus its installer box, CI test, pluginval and release zip.
4. **The rename** to Ultimate Audio Viewer (folder and settings migration). Before that: installer fixes, the full plugin's DAW bridge, a code review (crash reports, plugin LTO, KeyNet AVX2, shared reference data, an idle plugin with its editor closed), and a project review (Ref, Link, saved layouts, backup, update check, colour-safe meters and more).

## Open / next
- **Code signing:** CI self-signs everything as **Styrant Audio** (`tools/selfsign.ps1`, a fresh certificate per run, no secrets) whenever the Azure secrets are absent. It shows the publisher in Properties > Digital Signatures only; Windows doesn't trust it. Real signing is still undecided; the options were:
  1. Azure Artifact Signing (about $10 a month; CI is already wired and needs six secrets: `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET`, `SIGNING_ENDPOINT`, `SIGNING_ACCOUNT`, `SIGNING_PROFILE`)
  2. a CA certificate (Certum or Sectigo; a token or cloud HSM; sign locally with a script)
  3. SignPath (likely ineligible: proprietary ASIO SDK, a non-commercial model license)
  4. a self-signed certificate for the user's own PCs only
- **Checked on real hardware by the user** (Nov 2026):
  - UAV Bridge in the user's DAW
  - the DAW mix with loopback or the Scarlett plus an ASIO DAW (also with different sample rates)
  - the "No input" banner
  - the UI at the plugin's 900×520 minimum (the layout check covers the app's window only; the plugin's bar has Listen and Link too)
- **Possible follow-ups (offered, not requested):**
  - a DAW-mix gain or track picker (it follows the master automatically today)
  - sign the uninstaller once signing exists
  - point the update check and manual links at a new address if the repo is renamed
