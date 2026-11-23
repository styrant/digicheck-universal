#include "InstanceLink.h"

namespace instancelink
{
void Ring::write (const float* L, const float* R, int n) noexcept
{
    long long w = written.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const int k = (int) ((w + i) & (kFrames - 1));
        l[(size_t) k] = L[i]; r[(size_t) k] = R[i];
    }
    written.store (w + n, std::memory_order_release);
    lastMs = juce::Time::getMillisecondCounterHiRes();
}

bool Ring::readLatest (float* L, float* R, int n) const noexcept
{
    const long long end = written.load (std::memory_order_acquire);
    if (end == 0 || juce::Time::getMillisecondCounterHiRes() - lastMs.load() > 500.0) return false;
    for (int i = 0; i < n; ++i)
    {
        const long long p = end - n + i;
        if (p < 0) { L[i] = R[i] = 0; continue; }
        const int k = (int) (p & (kFrames - 1));
        L[i] = l[(size_t) k]; R[i] = r[(size_t) k];
    }
    return true;
}

Hub& Hub::get() { static Hub h; return h; }

int Hub::add (std::shared_ptr<Ring> ring)
{
    const juce::ScopedLock sl (lock);
    const int id = next++;
    items.push_back ({ id, "Instance " + juce::String (id), std::move (ring) });
    return id;
}

void Hub::remove (int id)
{
    const juce::ScopedLock sl (lock);
    items.erase (std::remove_if (items.begin(), items.end(), [id] (const Instance& i) { return i.id == id; }), items.end());
}

void Hub::rename (int id, const juce::String& n)
{
    const juce::ScopedLock sl (lock);
    for (auto& i : items) if (i.id == id && n.isNotEmpty()) i.name = n;
}

std::vector<Instance> Hub::list() const        { const juce::ScopedLock sl (lock); return items; }

std::shared_ptr<Ring> Hub::find (int id) const
{
    const juce::ScopedLock sl (lock);
    for (auto& i : items) if (i.id == id) return i.ring;
    return {};
}

juce::String Hub::nameOf (int id) const
{
    const juce::ScopedLock sl (lock);
    for (auto& i : items) if (i.id == id) return i.name;
    return {};
}
}
