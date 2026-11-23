#include "ClipboardImage.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

bool clipboard::copyImage (const juce::Image& source)
{
    if (source.isNull()) return false;
    const auto im = source.convertedToFormat (juce::Image::ARGB);
    const int w = im.getWidth(), h = im.getHeight();

    // CF_DIB: a bottom-up 32-bit BI_RGB bitmap (alpha set opaque)
    const size_t pixelBytes = (size_t) w * (size_t) h * 4;
    HGLOBAL dib = GlobalAlloc (GMEM_MOVEABLE, sizeof (BITMAPINFOHEADER) + pixelBytes);
    if (dib == nullptr) return false;
    {
        auto* hdr = static_cast<BITMAPINFOHEADER*> (GlobalLock (dib));
        *hdr = {};
        hdr->biSize = sizeof (BITMAPINFOHEADER);
        hdr->biWidth = w; hdr->biHeight = h; hdr->biPlanes = 1; hdr->biBitCount = 32; hdr->biCompression = BI_RGB;
        hdr->biSizeImage = (DWORD) pixelBytes;
        auto* px = reinterpret_cast<juce::uint8*> (hdr + 1);
        const juce::Image::BitmapData bd (im, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
            {
                const auto c = bd.getPixelColour (x, h - 1 - y);
                auto* p = px + ((size_t) y * (size_t) w + (size_t) x) * 4;
                p[0] = c.getBlue(); p[1] = c.getGreen(); p[2] = c.getRed(); p[3] = 255;
            }
        GlobalUnlock (dib);
    }

    // "PNG": the same picture, lossless, for the apps that prefer it
    juce::MemoryOutputStream png;
    juce::PNGImageFormat().writeImageToStream (im, png);
    HGLOBAL pngMem = GlobalAlloc (GMEM_MOVEABLE, png.getDataSize());
    if (pngMem != nullptr) { std::memcpy (GlobalLock (pngMem), png.getData(), png.getDataSize()); GlobalUnlock (pngMem); }

    if (! OpenClipboard (nullptr)) { GlobalFree (dib); if (pngMem) GlobalFree (pngMem); return false; }
    EmptyClipboard();
    const bool ok = SetClipboardData (CF_DIB, dib) != nullptr;
    if (! ok) GlobalFree (dib);
    if (pngMem != nullptr && SetClipboardData (RegisterClipboardFormatW (L"PNG"), pngMem) == nullptr) GlobalFree (pngMem);
    CloseClipboard();
    return ok;
}
#else
bool clipboard::copyImage (const juce::Image&) { return false; }
#endif
