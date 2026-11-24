// The product's name, and where its files live.
//   Ultimate Audio Viewer: files, folders, the exe, the VST3 and the installer
//   UAV: everything shown in the app's and plugin's UI
// Earlier builds were called DigiCheck Universal (the app: Spectroscope); migrate() moves their folders
// and settings over once, so nothing is lost (settings, presets, targets, themes, downloaded models).
#pragma once
#include <JuceHeader.h>

namespace appinfo
{
    inline const char* const kFullName  = "Ultimate Audio Viewer";
    inline const char* const kShortName = "UAV";

    // Documents\Ultimate Audio Viewer (targets, themes, recordings, reports, screenshots, crash reports)
    juce::File documentsFolder();
    // %APPDATA%\Ultimate Audio Viewer (settings, transcription models)
    juce::File appDataFolder();

    // once, before anything reads settings: the old folders and settings files move to the new names
    // (only where the new ones don't exist yet). Safe to call more than once.
    void migrate();
}
