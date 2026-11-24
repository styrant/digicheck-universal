#include "AppInfo.h"

namespace
{
    const char* const kOldName = "DigiCheck Universal";

    juce::File under (juce::File::SpecialLocationType where, const char* name)
    {
        return juce::File::getSpecialLocation (where).getChildFile (name);
    }

    // moves old to new if only the old one exists (a rename: instant on the same drive, even for models)
    void moveOver (const juce::File& oldOne, const juce::File& newOne)
    {
        if (oldOne.exists() && ! newOne.exists()) oldOne.moveFileTo (newOne);
    }
}

juce::File appinfo::documentsFolder() { return under (juce::File::userDocumentsDirectory, kFullName); }

juce::File appinfo::appDataFolder()
{
    // the folder JUCE's PropertiesFile uses for folderName on this platform
   #if JUCE_LINUX || JUCE_BSD
    return under (juce::File::userHomeDirectory, kFullName);
   #else
    return under (juce::File::userApplicationDataDirectory, kFullName);
   #endif
}

void appinfo::migrate()
{
    static bool done = false;
    if (std::exchange (done, true)) return;
    moveOver (under (juce::File::userDocumentsDirectory, kOldName), documentsFolder());
   #if JUCE_LINUX || JUCE_BSD
    const auto oldData = under (juce::File::userHomeDirectory, kOldName);
   #else
    const auto oldData = under (juce::File::userApplicationDataDirectory, kOldName);
   #endif
    moveOver (oldData, appDataFolder());
    // the settings files' new names
    const auto d = appDataFolder();
    moveOver (d.getChildFile ("Spectroscope.settings"), d.getChildFile (juce::String (kFullName) + ".settings"));
    moveOver (d.getChildFile ("DigiCheck Universal Plugin.settings"), d.getChildFile (juce::String (kFullName) + " Plugin.settings"));
}
