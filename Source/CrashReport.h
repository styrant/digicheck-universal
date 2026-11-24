// Crash reports (Windows): when the app, or the plugin's own code, crashes, a small minidump and a text
// note (version, exception, address, module) are written to Documents\Ultimate Audio Viewer\Crashes, so the
// crash can be found later from the release's symbols (CI keeps the PDBs as the "symbols" artifact).
//
// The app catches every unhandled crash (an unhandled-exception filter). The plugin shares the DAW's
// process, so it uses a vectored handler that only records fatal exceptions (access violation, illegal
// instruction, stack overflow...) raised inside its own module and then lets them go on to the DAW's own
// handling; it's removed when the last instance goes, so nothing points into an unloaded plugin.
#pragma once

namespace crashreport
{
    void install (const char* product, const char* version, bool pluginInHost);
    void uninstall();                        // the plugin: when its last instance is deleted
}
