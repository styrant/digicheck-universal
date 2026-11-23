// Is Windows' HDR ("advanced color") on for the monitor at a point? Used by full screen: an OpenGL window
// that covers a whole HDR monitor gets promoted by some drivers to exclusive / independent-flip
// presentation, and Windows then switches the monitor out of HDR (see Main.cpp, setAppFullscreen).
#pragma once
#include <JuceHeader.h>

namespace hdr
{
    // physicalPoint: a point in physical (desktop) pixels on the monitor. false off Windows or if unknown.
    bool isOnAt (juce::Point<int> physicalPoint);
}
