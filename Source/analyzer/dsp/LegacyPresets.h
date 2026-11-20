// LegacyPresets.h - factory presets, colour schemes and import of presets
// saved by the original VST2 plug-in (v1.06 - v1.09).
//
// The original stored state as raw native-endian int arrays:
//   preset chunk: int value[32]   = {version, w, h, unused, unused, settings[27]}
//   bank chunk:   int count, int index, char name[count][28], int value[count][32]
// and hosts wrapped those in .fxp ('FPCh') / .fxb ('FBCh') files with a
// big-endian 'CcnK' header. All parsing here is bounds-checked (the original
// setChunk() was not).
//
// No JUCE dependency.

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace sa::legacy
{

// Original settings order (sa::settings::Index)
enum Index
{
    inputChannel, peakEnable, peakDecay,
    avrgEnable, avrgTime, avrgBarType, avrgBarSize,
    holdEnable, holdInfinite, holdTime, holdDecay, holdBarType, holdBarSize,
    levelCeil, levelRange, levelGrid,
    freqGridType, bandsPerOctave, avrgSlope,
    peakBarColor, holdBarColor, avrgBarColor, gridBorderColor,
    gridLineColor, gridLabelColor, bkgTopColor, bkgBottomColor,
    Count
};

constexpr int ColorsIndex    = peakBarColor;
constexpr int ColorsCount    = Count - ColorsIndex;   // 8
constexpr int HeaderCount    = 5;                     // version, w, h, unused x2
constexpr int ParameterCount = HeaderCount + Count;   // 32
constexpr int PresetVersion  = 2;

// Values in the original units:
//  times in ms, decays in dB/s, bandsPerOctave as index {3,4,6},
//  avrgSlope in half-dB/oct steps, colours as ARGB.
using Values = std::array<int, Count>;

constexpr Values defaults
{
    2, 1, 15,  1, 3000, 1, 3,  1, 0, 2000, 3, 1, 2,  3, 51, 5,  0, 2, 0,
    (int) 0xCC2080F0u, (int) 0x99FFFFFFu, (int) 0xEBC0C0C0u, (int) 0x5EFFFFFFu,
    (int) 0x21FFFFFFu, (int) 0x8FFFFFFFu, (int) 0xFF505050u, (int) 0xFF101010u
};

struct FactoryPreset
{
    const char* name;
    std::array<int, ColorsIndex> v;   // settings without colours
};

inline const std::array<FactoryPreset, 8>& factoryPresets()
{
    static const std::array<FactoryPreset, 8> p
    {{
        { "Ancient",                 { 0, 1, 12, 0, 6000, 0, 4, 1, 0, 3000,  3, 0, 4,  0, 42, 10, 1, 0, 0 } },
        { "Medieval",                { 0, 1, 18, 1, 2000, 0, 4, 1, 0, 6000,  2, 0, 3, -3, 48,  6, 1, 1, 0 } },
        { "Modern 1",                { 2, 1, 15, 1, 3000, 1, 3, 1, 0, 2000,  3, 1, 2, -2, 52,  5, 0, 2, 0 } },
        { "Modern 2",                { 2, 1, 20, 1,  500, 1, 3, 1, 0,    0,  2, 1, 2, -2, 52,  5, 0, 2, 0 } },
        { ".2 Short RMS, Slow Peak", { 2, 0, 18, 1,  500, 1, 2, 1, 0,    0,  2, 1, 2, -3, 48,  6, 1, 2, 0 } },
        { ".2 Long RMS, Fast Peak",  { 2, 0, 15, 1, 6000, 1, 2, 1, 0,    0, 15, 1, 2, -3, 48,  6, 1, 2, 0 } },
        { "Broken Pixels",           { 3, 0, 12, 1,  500, 0, 4, 1, 0, 4000, 48, 0, 4, -3, 54, 12, 1, 2, 0 } },
        { "Modern Fill Me",          { 2, 0, 24, 1, 4000, 2, 2, 1, 0,    0, 12, 2, 2, -2, 52,  5, 0, 2, 3 } },
    }};
    return p;
}

struct ColourScheme
{
    const char* name;
    std::array<uint32_t, ColorsCount> argb;  // peak, hold, avrg, border, line, label, bkgTop, bkgBottom
};

inline const std::vector<ColourScheme>& colourSchemes()
{
    static const std::vector<ColourScheme> s
    {
        { "Default",           { 0xCC2080F0, 0x99FFFFFF, 0xEBC0C0C0, 0x5EFFFFFF, 0x21FFFFFF, 0x8FFFFFFF, 0xFF505050, 0xFF101010 } },
        { "7th Fashion",       { 0xabffffff, 0xe1f7b333, 0xabffffff, 0x54ffffff, 0x1cffffff, 0xabffffff, 0xff5a8296, 0xff1e4664 } },
        { "Blues",             { 0x666699ff, 0xeb6699ff, 0x7f66cc99, 0x99f3f3f3, 0x0ff3f3f3, 0x99f3f3f3, 0xff0c0c0c, 0xff33547d } },
        { "Brassica Oleracea", { 0x5416a085, 0xa816a085, 0xffffffff, 0x5434495e, 0x1c34495e, 0xa834495e, 0xffd3ede9, 0xffc4e7e0 } },
        { "Contrast",          { 0xb300ff00, 0xb3ffff00, 0xb3ff3c3c, 0xff909090, 0x4c909090, 0xff909090, 0xff303030, 0xff202020 } },
        { "Contrast, Not So",  { 0xe6669933, 0xe6c03349, 0x99ffff66, 0xff909090, 0x4c909090, 0xff909090, 0xff303030, 0xff202020 } },
        { "Crimson Cloudy",    { 0x66ff6699, 0x99ff6699, 0x54ffcc33, 0x99f3f3f3, 0x0ff3f3f3, 0x99f3f3f3, 0xff0c0c0c, 0xff7d3354 } },
        { "Dirty Milk",        { 0xffbdc3c7, 0xff27ae60, 0xff9b59b6, 0xff7f8c8d, 0x007f8c8d, 0xff7f8c8d, 0xffecf0f1, 0xffecf0f1 } },
        { "Dull Day",          { 0xa8144664, 0xbfffac33, 0xbfffffff, 0xa8ffffff, 0x54ffffff, 0x54144664, 0xffccd8e4, 0xffffffff } },
        { "Fire Drill",        { 0xccff8033, 0x8fffffff, 0x8fffcc33, 0x5effffff, 0x21ffffff, 0x8fffffff, 0xff606060, 0xff202020 } },
        { "Fre(a)ko",          { 0xe1666666, 0xa8ffffff, 0xffe0a060, 0xffc08040, 0xffe0a060, 0xff806040, 0xffffcc99, 0xffdd9966 } },
        { "Haze",              { 0xbf000810, 0x99000000, 0xbff4faff, 0x7f000810, 0x14000810, 0x99000810, 0xfff4faff, 0xffd0e8ff } },
        { "Hominoids",         { 0xccebd2ce, 0xccc02020, 0xff206990, 0xff808080, 0x54808080, 0xff808080, 0xff2f2f2f, 0xff202020 } },
        { "Ichtiandr Estate",  { 0x5433ccc0, 0xff33ccc0, 0xff34495e, 0x54f3f3f3, 0x1cf3f3f3, 0xa8f3f3f3, 0xff34495e, 0xff3d5a6d } },
        { "Immersion",         { 0x6633ccc0, 0xeb33ccc0, 0x9933ccff, 0x99f3f3f3, 0x0ff3f3f3, 0x99f3f3f3, 0xff0c0c0c, 0xff336666 } },
        { "Molecule",          { 0xbf0688c4, 0xff7bb042, 0xffcf9a26, 0xff677b74, 0xff3a403f, 0xff677b74, 0xff1e2523, 0xff101010 } },
        { "Tropics",           { 0x5466cc99, 0xa833cc66, 0xa8ccff66, 0x54ffffff, 0x0fffffff, 0xa8ffffff, 0xff111312, 0xff345846 } },
        { "Vegetable",         { 0xffb2b8bc, 0xffffc53f, 0xffa5e346, 0xff828a8e, 0xff828a8e, 0xff828a8e, 0xff434c51, 0xff434c51 } },
        { "Water In Charcoal", { 0xcc2080f0, 0x99ffffff, 0xebc0c0c0, 0x5effffff, 0x21ffffff, 0x8fffffff, 0xff505050, 0xff101010 } },
    };
    return s;
}

//==============================================================================
struct ImportedPreset
{
    std::string name;
    Values      values = defaults;
    int         width  = 0;   // editor size stored with the preset (0 = none)
    int         height = 0;
};

namespace detail
{
    inline int32_t readLE (const uint8_t* p) noexcept
    {
        return (int32_t) ((uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24));
    }
    inline int32_t readBE (const uint8_t* p) noexcept
    {
        return (int32_t) ((uint32_t) p[3] | ((uint32_t) p[2] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[0] << 24));
    }
    inline bool fourcc (const uint8_t* p, const char* id) noexcept { return std::memcmp (p, id, 4) == 0; }

    inline std::string cstring (const uint8_t* p, size_t max)
    {
        size_t n = 0;
        while (n < max && p[n] != 0) ++n;
        return std::string ((const char*) p, n);
    }

    inline int clampSetting (int i, int v) noexcept
    {
        struct R { int lo, hi; };
        static constexpr R r[ColorsIndex] =
        {
            {0,3}, {0,1}, {1,60}, {0,1}, {100,20000}, {0,2}, {1,4}, {0,1}, {0,1}, {0,20000},
            {1,60}, {0,2}, {1,4}, {-80,20}, {10,120}, {2,12}, {0,1}, {0,2}, {-6,12}
        };
        if (i >= ColorsIndex) return v;
        return v < r[i].lo ? r[i].lo : (v > r[i].hi ? r[i].hi : v);
    }

    // Port of sa::legacy::convertPreset - raw int[32] (version 1 or 2) -> values.
    inline std::optional<ImportedPreset> convert (const int32_t* raw, int n)
    {
        if (n < HeaderCount + 1)
            return std::nullopt;

        int32_t v[ParameterCount] {};
        for (int i = 0; i < ParameterCount; ++i)
            v[i] = i < n ? raw[i] : 0;

        ImportedPreset out;
        const int version = v[0];

        if (version == PresetVersion)
        {
            out.width  = v[1];
            out.height = v[2];
            for (int i = 0; i < Count; ++i)
                out.values[(size_t) i] = (HeaderCount + i < n) ? v[HeaderCount + i] : defaults[(size_t) i];
        }
        else if (version == 1)
        {
            // v1 layout: [ver, ?, ?, ?, w, h, settings without avrgSlope...]
            static constexpr int map[ParameterCount] =
            {
                -1, -1, -1, -1, -2, -3,
                inputChannel, peakEnable, peakDecay, avrgEnable, avrgTime, avrgBarType, avrgBarSize,
                holdEnable, holdInfinite, holdTime, holdDecay, holdBarType, holdBarSize,
                levelCeil, levelRange, levelGrid, freqGridType, bandsPerOctave,
                peakBarColor, holdBarColor, avrgBarColor, gridBorderColor, gridLineColor,
                gridLabelColor, bkgTopColor, bkgBottomColor
            };
            for (int i = 0; i < std::min (n, ParameterCount); ++i)
            {
                if (map[i] >= 0)       out.values[(size_t) map[i]] = v[i];
                else if (map[i] == -2) out.width  = v[i];
                else if (map[i] == -3) out.height = v[i];
            }
        }
        else
        {
            return std::nullopt;
        }

        for (int i = 0; i < ColorsIndex; ++i)
            out.values[(size_t) i] = clampSetting (i, out.values[(size_t) i]);

        if (out.width < 100 || out.width > 8000 || out.height < 50 || out.height > 8000)
            out.width = out.height = 0;

        return out;
    }
}

// Raw VST2 chunk as produced by the original getChunk(). `isBank` is a hint;
// if unknown, both forms are tried.
inline std::optional<ImportedPreset> parseChunk (const void* data, size_t size, std::optional<bool> isBank = std::nullopt)
{
    using namespace detail;
    if (data == nullptr || size < 4 * (size_t) (HeaderCount + 1))
        return std::nullopt;

    const auto* p = static_cast<const uint8_t*> (data);

    auto tryPreset = [&] () -> std::optional<ImportedPreset>
    {
        if (size % 4 != 0 || size > 4 * (size_t) ParameterCount)
            return std::nullopt;
        const int n = (int) (size / 4);
        int32_t raw[ParameterCount] {};
        for (int i = 0; i < n; ++i) raw[i] = readLE (p + 4 * i);
        return convert (raw, n);
    };

    auto tryBank = [&] () -> std::optional<ImportedPreset>
    {
        const int32_t count = readLE (p);
        const int32_t index = readLE (p + 4);
        if (count < 1 || count > 128)
            return std::nullopt;

        const size_t namesBytes = (size_t) count * 28;
        if (size < 8 + namesBytes + (size_t) count * 4 * (HeaderCount + 1))
            return std::nullopt;

        const size_t valueBytes = size - 8 - namesBytes;
        const size_t perPreset  = valueBytes / (size_t) count;
        if (perPreset % 4 != 0)
            return std::nullopt;
        const int n = (int) std::min<size_t> (perPreset / 4, ParameterCount);

        const int32_t sel = (index >= 0 && index < count) ? index : 0;
        const uint8_t* vals = p + 8 + namesBytes + (size_t) sel * perPreset;
        int32_t raw[ParameterCount] {};
        for (int i = 0; i < n; ++i) raw[i] = readLE (vals + 4 * i);

        auto r = convert (raw, n);
        if (r)
            r->name = cstring (p + 8 + (size_t) sel * 28, 28);
        return r;
    };

    if (isBank.has_value())
        return *isBank ? tryBank() : tryPreset();

    if (auto r = tryPreset()) return r;
    return tryBank();
}

// .fxp / .fxb file contents.
inline std::optional<ImportedPreset> parseFxFile (const void* data, size_t size)
{
    using namespace detail;
    const auto* p = static_cast<const uint8_t*> (data);
    if (data == nullptr || size < 60 || ! fourcc (p, "CcnK"))
        return std::nullopt;

    const int32_t fxId = readBE (p + 16);
    if (fxId != (int32_t) 0x53506841 /* 'SPhA' */)
        return std::nullopt;

    if (fourcc (p + 8, "FPCh"))
    {
        // magic, size, fxMagic, version, fxID, fxVersion, numParams, name[28], chunkSize, chunk
        const size_t header = 56;
        const int32_t chunkSize = readBE (p + header);
        if (chunkSize <= 0 || header + 4 + (size_t) chunkSize > size)
            return std::nullopt;
        auto r = parseChunk (p + header + 4, (size_t) chunkSize, false);
        if (r)
            r->name = cstring (p + 28, 28);
        return r;
    }

    if (fourcc (p + 8, "FBCh"))
    {
        // magic, size, fxMagic, version, fxID, fxVersion, numPrograms, future[128], chunkSize, chunk
        const size_t header = 156;
        if (size < header + 4)
            return std::nullopt;
        const int32_t chunkSize = readBE (p + header);
        if (chunkSize <= 0 || header + 4 + (size_t) chunkSize > size)
            return std::nullopt;
        return parseChunk (p + header + 4, (size_t) chunkSize, true);
    }

    return std::nullopt;
}

} // namespace sa::legacy
