#include "Bridge.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace bridge
{
Header* shared()
{
    static Header* mapped = []() -> Header*
    {
        const size_t size = sizeof (Header);
       #if JUCE_WINDOWS
        HANDLE m = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, (DWORD) ((uint64_t) size >> 32), (DWORD) size,
                                       L"Local\\UltimateAudioViewer.Bridge.1");
        if (m == nullptr) return nullptr;
        void* p = MapViewOfFile (m, FILE_MAP_ALL_ACCESS, 0, 0, size);      // the handle stays open for the process's life
        if (p == nullptr) { CloseHandle (m); return nullptr; }
       #else
        const int fd = shm_open ("/UltimateAudioViewer.Bridge.1", O_CREAT | O_RDWR, 0600);
        if (fd < 0) return nullptr;
        if (ftruncate (fd, (off_t) size) != 0) { close (fd); return nullptr; }
        void* p = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close (fd);
        if (p == MAP_FAILED) return nullptr;
       #endif
        // new memory is zeroed by the OS: the first user stamps it; a different layout is left alone
        auto* hd = static_cast<Header*> (p);
        uint32_t zero = 0;
        if (hd->magic.compare_exchange_strong (zero, kMagic)) hd->version = kVersion;
        if (hd->magic.load() != kMagic || hd->version.load() != kVersion) return nullptr;
        return hd;
    }();
    return mapped;
}

//==============================================================================
Sender::Sender() : h (shared())
{
    token = (uint32_t) juce::Random::getSystemRandom().nextInt (0x7ffffffe) + 1;
}

Sender::~Sender() { release(); }

void Sender::release()
{
    const int s = slot.exchange (-1);
    if (h != nullptr && s >= 0)
    {
        uint32_t mine = token;
        h->slots[s].owner.compare_exchange_strong (mine, 0u);
    }
}

void Sender::setName (const juce::String& n)
{
    const juce::SpinLock::ScopedLockType sl (nameLock);
    std::memset (pendingName, 0, sizeof (pendingName));
    n.copyToUTF8 (pendingName, kNameLen - 1);
    nameDirty = true;
}

void Sender::publishName (Slot& s)
{
    const juce::SpinLock::ScopedTryLockType sl (nameLock);
    if (! sl.isLocked()) return;                       // next block
    s.nameSeq.fetch_add (1);                           // odd: writing
    std::memcpy (s.name, pendingName, kNameLen);
    s.nameSeq.fetch_add (1);
    nameDirty = false;
}

void Sender::write (const float* L, const float* R, int n, double sampleRate) noexcept
{
    if (h == nullptr || ! enabled.load()) return;
    const int64_t now = nowMs();
    int s = slot.load();
    if (s < 0)
    {
        // claim a free slot, or one whose sender stopped (a crashed or closed DAW)
        for (int i = 0; i < kSlots && s < 0; ++i)
        {
            auto& sl = h->slots[i];
            uint32_t cur = sl.owner.load();
            const bool stale = cur != 0 && now - sl.heartbeatMs.load() > 3000;
            if ((cur == 0 || stale) && sl.owner.compare_exchange_strong (cur, token))
            {
                sl.written = 0;
                sl.startedMs = now;
                sl.heartbeatMs = now;
                nameDirty = true;
                s = i;
            }
        }
        if (s < 0) return;                             // all 16 in use
        slot = s;
    }
    auto& sl = h->slots[s];
    if (sl.owner.load() != token) { slot = -1; return; }   // taken over (we were thought dead)
    if (nameDirty.load()) publishName (sl);
    sl.rateMilliHz = (int64_t) std::llround (sampleRate * 1000.0);
    int64_t w = sl.written.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const int k = (int) ((w + i) & (kFrames - 1));
        sl.l[k] = L[i]; sl.r[k] = R[i];
    }
    sl.written.store (w + n, std::memory_order_release);
    sl.heartbeatMs = now;
}

//==============================================================================
std::vector<SenderInfo> listSenders()
{
    std::vector<SenderInfo> out;
    auto* h = shared();
    if (h == nullptr) return out;
    const int64_t now = nowMs();
    for (int i = 0; i < kSlots; ++i)
    {
        auto& s = h->slots[i];
        if (s.owner.load() == 0) continue;
        char name[kNameLen];
        for (int tries = 0; tries < 8; ++tries)
        {
            const uint32_t a = s.nameSeq.load();
            if (a & 1) continue;
            std::memcpy (name, s.name, kNameLen);
            if (s.nameSeq.load() == a) break;
        }
        name[kNameLen - 1] = 0;
        juce::String n = juce::String::fromUTF8 (name);
        if (n.isEmpty()) n = "Track " + juce::String (i + 1);
        out.push_back ({ i, n, s.rateMilliHz.load() / 1000.0, s.startedMs.load(), now - s.heartbeatMs.load() < 1500 });
    }
    return out;
}

bool looksLikeMaster (const juce::String& t)
{
    const auto s = t.trim().toLowerCase();
    for (auto* k : { "master", "stereo out", "main out", "main", "mix bus", "mixbus", "2 bus", "2-bus", "output" })
        if (s == k || s.startsWith (juce::String (k) + " ") || s.endsWith (" " + juce::String (k))) return true;
    return false;
}

int chooseSlot (const juce::String& want)
{
    const auto senders = listSenders();
    if (want.isNotEmpty())
    {
        for (auto& s : senders) if (s.name == want && s.alive) return s.slot;
        for (auto& s : senders) if (s.name == want) return s.slot;
        return -1;
    }
    int best = -1; int64_t newest = -1; bool bestMaster = false;
    for (auto& s : senders)
    {
        if (! s.alive) continue;
        const bool m = looksLikeMaster (s.name);
        if (best < 0 || (m && ! bestMaster) || (m == bestMaster && s.startedMs > newest))
        { best = s.slot; newest = s.startedMs; bestMaster = m; }
    }
    return best;
}

void Mixer::process (int slot, double dstRate, const float* inL, const float* inR, float* outL, float* outR, int n, bool reset) noexcept
{
    auto* h = shared();
    if (h == nullptr || slot < 0 || slot >= kSlots || dstRate <= 0) { std::copy (inL, inL + n, outL); std::copy (inR, inR + n, outR); return; }
    auto& sl = h->slots[slot];
    const double srcRate = sl.rateMilliHz.load() / 1000.0;
    const int64_t w = sl.written.load (std::memory_order_acquire);
    const double target = srcRate * 0.04;
    if (reset || pos < 0 || pos > (double) w || (double) w - pos > srcRate * 0.25)
        pos = std::max (0.0, (double) w - target);
    const double ratio = srcRate / dstRate * (1.0 + std::clamp (((double) w - pos - target) / srcRate * 0.5, -0.002, 0.002));
    for (int i = 0; i < n; ++i)
    {
        const int64_t k = (int64_t) pos;
        float dl = 0, dr = 0;
        if (k + 1 < w)
        {
            const float f = (float) (pos - (double) k);
            const int a = (int) (k & (kFrames - 1)), b = (int) ((k + 1) & (kFrames - 1));
            dl = sl.l[a] + (sl.l[b] - sl.l[a]) * f;
            dr = sl.r[a] + (sl.r[b] - sl.r[a]) * f;
            pos += ratio;
        }
        outL[i] = inL[i] + dl;
        outR[i] = inR[i] + dr;
    }
}
}
