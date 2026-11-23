#include "ScopeTheme.h"

namespace
{
    juce::Colour hex (const juce::String& s)
    {
        auto h = s.trim().trimCharactersAtStart ("#");
        if (h.length() == 6) h = "ff" + h;
        return juce::Colour ((juce::uint32) h.getHexValue64());
    }
}

ScopeTheme ScopeTheme::dark()
{
    ScopeTheme t;
    t.name = "Dark";
    t.bg = hex ("#16162a"); t.scopeBg = hex ("#0f0f1a"); t.controlBg = hex ("#1a1a2e"); t.buttonOutline = hex ("#3a3a5a");
    t.text = hex ("#888899"); t.textBtnOff = hex ("#888899"); t.textBtnOn = hex ("#cc8833");
    t.accent = hex ("#00ff88"); t.accentText = hex ("#0a0a14"); t.secondaryAccent = hex ("#cc8833"); t.secondaryAccentText = hex ("#ffffff");
    t.recording = hex ("#cc3333"); t.wave = hex ("#8899aa"); t.hold = hex ("#4a5566"); t.sideChain = hex ("#ff8844"); t.sideChainAlpha = 0.8f;
    t.grid = hex ("#2a2a4a"); t.playhead = hex ("#00ff88"); t.clipping = hex ("#cc3333"); t.hoverBg = hex ("#1a1a2e"); t.hoverText = hex ("#00ff88");
    t.selection = hex ("#00ff88"); t.sliderTrack = hex ("#3a3a5a");
    t.spectral = { hex ("#cc3344"), hex ("#ff7733"), hex ("#88cc44"), hex ("#44aadd"), hex ("#8866ff") };
    return t;
}

ScopeTheme ScopeTheme::neon()
{
    auto t = dark();
    t.name = "Neon";
    t.bg = hex ("#0d0b1e"); t.scopeBg = hex ("#080616"); t.controlBg = hex ("#14112b"); t.buttonOutline = hex ("#3b3366");
    t.text = hex ("#8d87b5"); t.textBtnOff = hex ("#8d87b5"); t.textBtnOn = hex ("#ff2e88");
    t.accent = hex ("#ff2e88"); t.accentText = hex ("#ffffff"); t.secondaryAccent = hex ("#18e0ff"); t.secondaryAccentText = hex ("#06121a");
    t.wave = hex ("#ff2e88"); t.hold = hex ("#4b3e7a"); t.sideChain = hex ("#18e0ff"); t.grid = hex ("#241f47"); t.playhead = hex ("#18e0ff");
    t.clipping = hex ("#ffd02e"); t.hoverText = hex ("#18e0ff"); t.selection = hex ("#8a5cff"); t.sliderTrack = hex ("#3b3366");
    t.spectral = { hex ("#ff2e63"), hex ("#ff8a00"), hex ("#ffe14d"), hex ("#18e0ff"), hex ("#a06bff") };
    return t;
}

ScopeTheme ScopeTheme::digicheck()
{
    auto t = dark();
    t.name = "DigiCheck";
    t.bg = hex ("#111214"); t.scopeBg = hex ("#0b0c0e"); t.controlBg = hex ("#17191d"); t.buttonOutline = hex ("#2e3138");
    t.text = hex ("#8c93a3"); t.textBtnOff = hex ("#8c93a3"); t.textBtnOn = hex ("#4c8dff");
    t.accent = hex ("#4c8dff"); t.accentText = hex ("#ffffff"); t.secondaryAccent = hex ("#4c8dff"); t.secondaryAccentText = hex ("#ffffff");
    t.wave = hex ("#9aa5ce"); t.hold = hex ("#3a3f4b"); t.sideChain = hex ("#f0b429"); t.grid = hex ("#23262c"); t.playhead = hex ("#3ddc84");
    t.clipping = hex ("#ff5252"); t.hoverBg = hex ("#202227"); t.hoverText = hex ("#e8e8e8"); t.selection = hex ("#4c8dff"); t.sliderTrack = hex ("#2e3138");
    t.spectral = { hex ("#ff5252"), hex ("#f78c6c"), hex ("#3ddc84"), hex ("#5fd7d7"), hex ("#c792ea") };
    return t;
}

ScopeTheme ScopeTheme::amber()
{
    auto t = dark();
    t.name = "Amber";
    t.bg = hex ("#140e06"); t.scopeBg = hex ("#0c0803"); t.controlBg = hex ("#1d1409"); t.buttonOutline = hex ("#4a3517");
    t.text = hex ("#a88352"); t.textBtnOff = hex ("#a88352"); t.textBtnOn = hex ("#ffb000");
    t.accent = hex ("#ffb000"); t.accentText = hex ("#140e06"); t.secondaryAccent = hex ("#ff7a00"); t.secondaryAccentText = hex ("#140e06");
    t.wave = hex ("#ffb000"); t.hold = hex ("#5a4320"); t.sideChain = hex ("#ff5a1f"); t.grid = hex ("#2a1d0c"); t.playhead = hex ("#ffd27a");
    t.clipping = hex ("#ff3b1f"); t.hoverBg = hex ("#241a0c"); t.hoverText = hex ("#ffd27a"); t.selection = hex ("#ffb000"); t.sliderTrack = hex ("#4a3517");
    t.spectral = { hex ("#ff4a1c"), hex ("#ff7a00"), hex ("#ffb000"), hex ("#ffd966"), hex ("#fff1c2") };
    return t;
}

ScopeTheme ScopeTheme::phosphor()
{
    auto t = dark();
    t.name = "Phosphor";
    t.bg = hex ("#06120a"); t.scopeBg = hex ("#020a05"); t.controlBg = hex ("#0b1d11"); t.buttonOutline = hex ("#1f4a2c");
    t.text = hex ("#5fa877"); t.textBtnOff = hex ("#5fa877"); t.textBtnOn = hex ("#39ff7a");
    t.accent = hex ("#39ff7a"); t.accentText = hex ("#02100a"); t.secondaryAccent = hex ("#9dff3a"); t.secondaryAccentText = hex ("#02100a");
    t.wave = hex ("#39ff7a"); t.hold = hex ("#1f5c33"); t.sideChain = hex ("#d4ff3a"); t.grid = hex ("#0f2a18"); t.playhead = hex ("#b8ffcf");
    t.clipping = hex ("#ff4d4d"); t.hoverBg = hex ("#0e2416"); t.hoverText = hex ("#b8ffcf"); t.selection = hex ("#39ff7a"); t.sliderTrack = hex ("#1f4a2c");
    t.spectral = { hex ("#1f9e4a"), hex ("#39ff7a"), hex ("#8dff9e"), hex ("#c8ffd6"), hex ("#e9fff0") };
    return t;
}

ScopeTheme ScopeTheme::ice()
{
    auto t = dark();
    t.name = "Ice";
    t.bg = hex ("#0b1220"); t.scopeBg = hex ("#070c16"); t.controlBg = hex ("#111b2e"); t.buttonOutline = hex ("#2b3d5e");
    t.text = hex ("#8ba3c7"); t.textBtnOff = hex ("#8ba3c7"); t.textBtnOn = hex ("#7fd4ff");
    t.accent = hex ("#7fd4ff"); t.accentText = hex ("#06101c"); t.secondaryAccent = hex ("#b49cff"); t.secondaryAccentText = hex ("#06101c");
    t.wave = hex ("#bfe6ff"); t.hold = hex ("#34496b"); t.sideChain = hex ("#ff9ec7"); t.grid = hex ("#18233a"); t.playhead = hex ("#7fd4ff");
    t.clipping = hex ("#ff5c7a"); t.hoverBg = hex ("#16223a"); t.hoverText = hex ("#e6f6ff"); t.selection = hex ("#7fd4ff"); t.sliderTrack = hex ("#2b3d5e");
    t.spectral = { hex ("#3d6bff"), hex ("#4da3ff"), hex ("#7fd4ff"), hex ("#b8ecff"), hex ("#e8f8ff") };
    return t;
}

ScopeTheme ScopeTheme::sunset()
{
    auto t = dark();
    t.name = "Sunset";
    t.bg = hex ("#1a0f1f"); t.scopeBg = hex ("#120a16"); t.controlBg = hex ("#24142b"); t.buttonOutline = hex ("#4d2c57");
    t.text = hex ("#b08aa8"); t.textBtnOff = hex ("#b08aa8"); t.textBtnOn = hex ("#ff8a5c");
    t.accent = hex ("#ff8a5c"); t.accentText = hex ("#1a0f1f"); t.secondaryAccent = hex ("#ff4f9a"); t.secondaryAccentText = hex ("#ffffff");
    t.wave = hex ("#ffb07a"); t.hold = hex ("#5a3659"); t.sideChain = hex ("#5cc8ff"); t.grid = hex ("#2e1a35"); t.playhead = hex ("#ffd36e");
    t.clipping = hex ("#ff2e4d"); t.hoverBg = hex ("#2c1934"); t.hoverText = hex ("#ffd36e"); t.selection = hex ("#ff8a5c"); t.sliderTrack = hex ("#4d2c57");
    t.spectral = { hex ("#ff2e63"), hex ("#ff6a3d"), hex ("#ffb03d"), hex ("#ff7ac6"), hex ("#b76bff") };
    return t;
}

ScopeTheme ScopeTheme::mono()
{
    auto t = dark();
    t.name = "Mono";
    t.bg = hex ("#0e0e0e"); t.scopeBg = hex ("#050505"); t.controlBg = hex ("#161616"); t.buttonOutline = hex ("#3a3a3a");
    t.text = hex ("#9a9a9a"); t.textBtnOff = hex ("#9a9a9a"); t.textBtnOn = hex ("#ffffff");
    t.accent = hex ("#e6e6e6"); t.accentText = hex ("#000000"); t.secondaryAccent = hex ("#bdbdbd"); t.secondaryAccentText = hex ("#000000");
    t.wave = hex ("#f2f2f2"); t.hold = hex ("#555555"); t.sideChain = hex ("#9e9e9e"); t.grid = hex ("#232323"); t.playhead = hex ("#ffffff");
    t.clipping = hex ("#ff4040"); t.hoverBg = hex ("#1e1e1e"); t.hoverText = hex ("#ffffff"); t.selection = hex ("#e6e6e6"); t.sliderTrack = hex ("#3a3a3a");
    t.spectral = { hex ("#ffffff"), hex ("#d9d9d9"), hex ("#b3b3b3"), hex ("#8c8c8c"), hex ("#6e6e6e") };
    return t;
}

ScopeTheme ScopeTheme::paper()
{
    auto t = dark();
    t.name = "Paper";
    t.bg = hex ("#e9e7e2"); t.scopeBg = hex ("#f7f6f2"); t.controlBg = hex ("#dedbd4"); t.buttonOutline = hex ("#b9b4a9");
    t.text = hex ("#5d5a54"); t.textBtnOff = hex ("#5d5a54"); t.textBtnOn = hex ("#1f5fbf");
    t.accent = hex ("#1f5fbf"); t.accentText = hex ("#ffffff"); t.secondaryAccent = hex ("#c26a12"); t.secondaryAccentText = hex ("#ffffff");
    t.wave = hex ("#2b3440"); t.hold = hex ("#c9c4b8"); t.sideChain = hex ("#d2541f"); t.grid = hex ("#dcd8cf"); t.playhead = hex ("#1f5fbf");
    t.clipping = hex ("#d0202a"); t.hoverBg = hex ("#d3cfc6"); t.hoverText = hex ("#1f1f1f"); t.selection = hex ("#1f5fbf"); t.sliderTrack = hex ("#b9b4a9");
    t.spectral = { hex ("#b3122e"), hex ("#d2541f"), hex ("#2f8f3a"), hex ("#1f6fbf"), hex ("#6a3dbf") };
    return t;
}

juce::StringArray ScopeTheme::paletteNames()
{
    return { "Theme's own", "Scyllascope", "Heat", "Ocean", "Rainbow", "Pastel", "Forest", "Mono" };
}

std::array<juce::Colour, 5> ScopeTheme::palette (int i, const ScopeTheme& t)
{
    switch (i)
    {
        case 1: return dark().spectral;
        case 2: return { hex ("#7a0010"), hex ("#d7261e"), hex ("#ff8c00"), hex ("#ffd23f"), hex ("#fff6c8") };
        case 3: return { hex ("#123a8c"), hex ("#1f6fd1"), hex ("#22a7c9"), hex ("#4fe0c3"), hex ("#b8fff0") };
        case 4: return { hex ("#ff3b3b"), hex ("#ffa23b"), hex ("#f5f53b"), hex ("#3bdf6e"), hex ("#4b7bff") };
        case 5: return { hex ("#ff9aa2"), hex ("#ffb38a"), hex ("#e2f0a0"), hex ("#9ad8e8"), hex ("#c3a8f0") };
        case 6: return { hex ("#6b3e1f"), hex ("#a1662f"), hex ("#6ea83a"), hex ("#3f9e6b"), hex ("#b6e3a8") };
        case 7: { const auto w = t.wave; return { w, w.withMultipliedBrightness (0.9f), w.withMultipliedBrightness (0.8f), w.withMultipliedBrightness (0.7f), w.withMultipliedBrightness (0.6f) }; }
        default: return t.spectral;
    }
}

void ScopeTheme::applyOverrides (const juce::String& ov)
{
    auto* o = new juce::DynamicObject();
    for (auto& kv : juce::StringArray::fromTokens (ov, ";", {}))
        if (kv.contains ("=")) o->setProperty (kv.upToFirstOccurrenceOf ("=", false, false).trim(), kv.fromFirstOccurrenceOf ("=", false, false).trim());
    if (o->getProperties().size() == 0) { delete o; return; }
    o->setProperty ("waveColor", o->hasProperty ("waveColor") ? o->getProperty ("waveColor") : juce::var ("#" + wave.toDisplayString (true)));
    const auto keep = name;
    fromJson (juce::var (o), *this);
    name = keep;
}

juce::var ScopeTheme::toJson() const
{
    auto* o = new juce::DynamicObject();
    auto put = [&] (const char* k, juce::Colour c) { o->setProperty (k, "#" + c.toDisplayString (c.getAlpha() != 255)); };
    o->setProperty ("name", name);
    put ("bgColor", bg); put ("scopeBgColor", scopeBg); put ("controlBgColor", controlBg); put ("buttonOutlineColor", buttonOutline);
    put ("textColor", text); put ("textBtnOffColor", textBtnOff); put ("textBtnOnColor", textBtnOn);
    put ("accentColor", accent); put ("accentTextColor", accentText); put ("secondaryAccentColor", secondaryAccent);
    put ("secondaryAccentTextColor", secondaryAccentText); put ("recordingColor", recording); put ("waveColor", wave);
    put ("holdColor", hold); put ("sideChainColor", sideChain); put ("gridColor", grid); put ("playheadColor", playhead);
    put ("clippingColor", clipping); put ("hoverBgColor", hoverBg); put ("hoverTextColor", hoverText);
    put ("selectionColor", selection); put ("sliderTrackColor", sliderTrack);
    o->setProperty ("sideChainAlpha", sideChainAlpha);
    juce::Array<juce::var> sp; for (auto c : spectral) sp.add ("#" + c.toDisplayString (false));
    o->setProperty ("spectralColors", sp);
    return juce::var (o);
}

bool ScopeTheme::fromJson (const juce::var& v, ScopeTheme& t)
{
    auto* o = v.getDynamicObject();
    if (o == nullptr) return false;
    // keys at the top level, or inside a single named object
    if (! o->hasProperty ("waveColor") && ! o->hasProperty ("scopeBgColor"))
    {
        for (auto& p : o->getProperties())
            if (auto* inner = p.value.getDynamicObject(); inner != nullptr && (inner->hasProperty ("waveColor") || inner->hasProperty ("scopeBgColor")))
            {
                if (t.name.isEmpty()) t.name = p.name.toString();
                return fromJson (p.value, t);
            }
        return false;
    }
    auto col = [&] (const char* key, juce::Colour& c) { if (o->hasProperty (key)) c = hex (o->getProperty (key).toString()); };
    col ("bgColor", t.bg); col ("scopeBgColor", t.scopeBg); col ("controlBgColor", t.controlBg); col ("buttonOutlineColor", t.buttonOutline);
    col ("textColor", t.text); col ("textBtnOffColor", t.textBtnOff); col ("textBtnOnColor", t.textBtnOn);
    col ("accentColor", t.accent); col ("accentTextColor", t.accentText); col ("secondaryAccentColor", t.secondaryAccent);
    col ("secondaryAccentTextColor", t.secondaryAccentText); col ("recordingColor", t.recording); col ("waveColor", t.wave);
    col ("holdColor", t.hold); col ("sideChainColor", t.sideChain); col ("gridColor", t.grid); col ("playheadColor", t.playhead);
    col ("clippingColor", t.clipping); col ("hoverBgColor", t.hoverBg); col ("hoverTextColor", t.hoverText);
    col ("selectionColor", t.selection); col ("sliderTrackColor", t.sliderTrack);
    if (o->hasProperty ("sideChainAlpha")) t.sideChainAlpha = (float) (double) o->getProperty ("sideChainAlpha");
    if (auto* a = o->getProperty ("spectralColors").getArray())
        for (int i = 0; i < juce::jmin (5, a->size()); ++i) t.spectral[(size_t) i] = hex ((*a)[i].toString());
    if (o->hasProperty ("name")) t.name = o->getProperty ("name").toString();
    return true;
}

juce::File ScopeTheme::folder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("DigiCheck Universal").getChildFile ("Scope Themes");
}

juce::Array<ScopeTheme> ScopeTheme::all()
{
    juce::Array<ScopeTheme> list { dark(), neon(), digicheck(), amber(), phosphor(), ice(), sunset(), mono(), paper() };
    for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.json"))
    {
        auto t = dark();
        t.name = {};
        if (fromJson (juce::JSON::parse (f), t))
        {
            if (t.name.isEmpty()) t.name = f.getFileNameWithoutExtension();
            list.add (t);
        }
    }
    return list;
}

ScopeTheme ScopeTheme::named (const juce::String& n)
{
    for (auto& t : all()) if (t.name == n) return t;
    return dark();
}
