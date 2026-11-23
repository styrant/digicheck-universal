#include "LayoutCheck.h"
#if defined (__GNUG__)
 #include <cxxabi.h>
#endif

namespace
{
    bool isControl (juce::Component& c)
    {
        return dynamic_cast<juce::Button*> (&c) != nullptr || dynamic_cast<juce::ComboBox*> (&c) != nullptr
            || dynamic_cast<juce::Slider*> (&c) != nullptr || dynamic_cast<juce::TextEditor*> (&c) != nullptr
            || (dynamic_cast<juce::Label*> (&c) != nullptr && dynamic_cast<juce::Label&> (c).getText().isNotEmpty()
                && dynamic_cast<juce::ComboBox*> (c.getParentComponent()) == nullptr && dynamic_cast<juce::Slider*> (c.getParentComponent()) == nullptr);
    }

    juce::String describe (juce::Component& c)
    {
        juce::String t;
        if (auto* b = dynamic_cast<juce::Button*> (&c)) t = b->getButtonText();
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (&c)) t = cb->getText();
        else if (auto* l = dynamic_cast<juce::Label*> (&c)) t = l->getText();
        juce::String raw (typeid (c).name());
       #if defined (__GNUG__)
        int status = 0;
        if (char* d = abi::__cxa_demangle (typeid (c).name(), nullptr, nullptr, &status)) { raw = d; std::free (d); }
       #endif
        auto kind = raw.fromLastOccurrenceOf (" ", false, false).fromLastOccurrenceOf ("::", false, false);
        if (c.getName().isNotEmpty()) kind = c.getName();
        return kind + (t.isNotEmpty() ? " \"" + t.substring (0, 30).replace ("\n", " ") + "\"" : juce::String());
    }

    juce::String pathOf (juce::Component& c, juce::Component& root)
    {
        juce::StringArray parts;
        for (auto* p = &c; p != nullptr && p != &root; p = p->getParentComponent())
            parts.insert (0, describe (*p));
        return parts.joinIntoString (" / ");
    }

    void walk (juce::Component& c, juce::Component& root, const juce::String& page, layoutcheck::Result& out, juce::Array<juce::var>& items)
    {
        std::vector<juce::Component*> controls;
        for (auto* ch : c.getChildren())
        {
            if (! ch->isVisible() || ch->getWidth() <= 0 || ch->getHeight() <= 0) continue;
            if (dynamic_cast<juce::TooltipWindow*> (ch) != nullptr) continue;
            if (isControl (*ch))
            {
                controls.push_back (ch);
                const auto b = root.getLocalArea (&c, ch->getBounds());
                auto* o = new juce::DynamicObject();
                o->setProperty ("page", page);
                o->setProperty ("path", pathOf (*ch, root));
                o->setProperty ("x", b.getX()); o->setProperty ("y", b.getY()); o->setProperty ("w", b.getWidth()); o->setProperty ("h", b.getHeight());
                items.add (juce::var (o));

                // sticking out of the parent
                if (! c.getLocalBounds().expanded (1).contains (ch->getBounds()))
                    out.warnings.add (page + ": " + pathOf (*ch, root) + " extends outside its parent");
                // text that doesn't fit (buttons with their look-and-feel's font)
                if (auto* cb = dynamic_cast<juce::ComboBox*> (ch); cb != nullptr && cb->getText().isNotEmpty())
                {
                    const auto f = cb->getLookAndFeel().getComboBoxFont (*cb);
                    const int need = juce::GlyphArrangement::getStringWidthInt (f, cb->getText()) + cb->getHeight() + 4;   // text and the arrow
                    if (need > cb->getWidth())
                        out.warnings.add (page + ": " + pathOf (*ch, root) + " text needs " + juce::String (need) + " px, has " + juce::String (cb->getWidth()));
                }
                if (auto* tb = dynamic_cast<juce::TextButton*> (ch))
                {
                    const auto f = tb->getLookAndFeel().getTextButtonFont (*tb, tb->getHeight());
                    const int need = juce::GlyphArrangement::getStringWidthInt (f, tb->getButtonText()) + 6;
                    if (tb->getButtonText().isNotEmpty() && need > tb->getWidth())
                        out.warnings.add (page + ": " + pathOf (*ch, root) + " text needs " + juce::String (need) + " px, has " + juce::String (tb->getWidth()));
                }
            }
            walk (*ch, root, page, out, items);
        }
        // overlapping sibling controls (more than a 2 px sliver)
        for (size_t i = 0; i < controls.size(); ++i)
            for (size_t j = i + 1; j < controls.size(); ++j)
            {
                const auto x = controls[i]->getBounds().getIntersection (controls[j]->getBounds());
                if (x.getWidth() > 2 && x.getHeight() > 2)
                    out.errors.add (page + ": " + pathOf (*controls[i], root) + "  overlaps  " + describe (*controls[j]) + " (" + juce::String (x.getWidth()) + "x" + juce::String (x.getHeight()) + " px)");
            }
    }
}

void layoutcheck::inspect (juce::Component& root, const juce::String& page, Result& into)
{
    juce::Array<juce::var> items;
    walk (root, root, page, into, items);
    if (! into.fingerprint.isArray()) into.fingerprint = juce::Array<juce::var>();
    for (auto& v : items) into.fingerprint.append (v);
}
