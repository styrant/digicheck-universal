#include "HdrInfo.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <vector>

bool hdr::isOnAt (juce::Point<int> p)
{
    // the monitor's GDI device name ("\\.\DISPLAY1") ...
    HMONITOR mon = MonitorFromPoint (POINT { p.x, p.y }, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW mi {}; mi.cbSize = sizeof (mi);
    if (mon == nullptr || ! GetMonitorInfoW (mon, &mi)) return false;

    // ... matched to its display path, whose target reports advanced color (HDR) on or off
    UINT32 nPaths = 0, nModes = 0;
    if (GetDisplayConfigBufferSizes (QDC_ONLY_ACTIVE_PATHS, &nPaths, &nModes) != ERROR_SUCCESS) return false;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths (nPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes (nModes);
    if (QueryDisplayConfig (QDC_ONLY_ACTIVE_PATHS, &nPaths, paths.data(), &nModes, modes.data(), nullptr) != ERROR_SUCCESS) return false;
    for (UINT32 i = 0; i < nPaths; ++i)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME src {};
        src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        src.header.size = sizeof (src);
        src.header.adapterId = paths[i].sourceInfo.adapterId;
        src.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo (&src.header) != ERROR_SUCCESS) continue;
        if (wcscmp (src.viewGdiDeviceName, mi.szDevice) != 0) continue;

        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color {};
        color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        color.header.size = sizeof (color);
        color.header.adapterId = paths[i].targetInfo.adapterId;
        color.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo (&color.header) != ERROR_SUCCESS) return false;
        return color.advancedColorEnabled != 0;
    }
    return false;
}
#else
bool hdr::isOnAt (juce::Point<int>) { return false; }
#endif
