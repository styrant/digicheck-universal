// Musical key classifier: C++ inference for the OpenKeyScan / KeyNet model
// (Korzeniowski & Widmer 2018, "Genre-agnostic key classification with CNNs"; weights from
// styrant/openkeyscan-analyzer, MIT). Pure C++17, no JUCE, so it can be unit-tested on its own.
//
// Input : log1p-magnitude CQT, 105 bins (65 Hz, 24 bins/octave), frames every 0.2 s -> [105][T]
// Network: 9 conv blocks (conv + folded batch norm + ELU), 2x2 max-pool after blocks 2, 4, 6,
//          global average -> 24 logits. Class order is Camelot: 0..11 = 1A..12A (minor),
//          12..23 = 1B..12B (major).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class KeyNet
{
public:
    // blob: see tools/export_keynet.py ("KNET", folded weights in float16)
    bool load (const void* data, size_t size, std::string& error);
    bool isLoaded() const noexcept { return ! layers.empty(); }

    // spec: row-major [bins][frames] (bins = 105). Returns 24 logits.
    std::vector<float> run (const float* spec, int bins, int frames) const;

    static std::vector<float> softmax (const std::vector<float>& logits);

    // ---- labels
    static int tonicPitchClass (int cls) noexcept;      // 0 = C ... 11 = B
    static bool isMajor (int cls) noexcept              { return cls >= 12; }
    static std::string keyName (int cls);                // e.g. "A minor", "C# major"
    static std::string camelot (int cls);                // e.g. "8A"
    static std::string openKey (int cls);                // e.g. "1m"

private:
    struct Layer { int cout = 0, cin = 0, k = 0; std::vector<float> w, b; };
    std::vector<Layer> layers;

    static void convElu (const Layer& L, const std::vector<float>& in, int H, int W, std::vector<float>& out,
                         std::vector<float>& col);
    static void maxPool (const std::vector<float>& in, int C, int H, int W, std::vector<float>& out);
};
