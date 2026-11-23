// Puts an image on the system clipboard (Windows: as a bitmap and as PNG, so image editors, Office, chat
// apps and browsers can all paste it). Returns false where that isn't available (other platforms).
#pragma once
#include <JuceHeader.h>

namespace clipboard
{
    bool copyImage (const juce::Image&);
}
