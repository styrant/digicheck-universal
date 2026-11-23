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
    juce::Array<ScopeTheme> list { dark(), neon(), digicheck() };
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
