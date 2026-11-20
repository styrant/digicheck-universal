#include "LoopbackCapture.h"
#include <chrono>

#ifdef _WIN32
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <initguid.h>
 #include <windows.h>
 #include <mmdeviceapi.h>
 #include <audioclient.h>
 #include <functiondiscoverykeys_devpkey.h>
 #include <ksmedia.h>
 #include <cstring>
 #include <cstdio>

namespace
{
    template <typename T> struct ComPtr
    {
        T* p = nullptr;
        ~ComPtr() { if (p) p->Release(); }
        T** operator&() { return &p; }
        T* operator->() const { return p; }
        explicit operator bool() const { return p != nullptr; }
    };

    std::string hr2s (const char* what, HRESULT hr)
    {
        char b[96]; std::snprintf (b, sizeof b, "%s failed (0x%08lX)", what, (unsigned long) hr);
        return b;
    }
}

std::vector<LoopbackCapture::Endpoint> LoopbackCapture::listOutputDevices()
{
    std::vector<Endpoint> out;
    const HRESULT ci = CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    {
        ComPtr<IMMDeviceEnumerator> en;
        if (SUCCEEDED (CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en)))
        {
            std::wstring defId;
            {
                ComPtr<IMMDevice> def;
                if (SUCCEEDED (en->GetDefaultAudioEndpoint (eRender, eConsole, &def)))
                {
                    LPWSTR id = nullptr;
                    if (SUCCEEDED (def->GetId (&id))) { defId = id; CoTaskMemFree (id); }
                }
            }
            ComPtr<IMMDeviceCollection> col;
            if (SUCCEEDED (en->EnumAudioEndpoints (eRender, DEVICE_STATE_ACTIVE, &col)))
            {
                UINT n = 0; col->GetCount (&n);
                for (UINT i = 0; i < n; ++i)
                {
                    ComPtr<IMMDevice> dev;
                    if (FAILED (col->Item (i, &dev))) continue;
                    Endpoint e;
                    LPWSTR id = nullptr;
                    if (SUCCEEDED (dev->GetId (&id))) { e.id = id; CoTaskMemFree (id); }
                    ComPtr<IPropertyStore> props;
                    if (SUCCEEDED (dev->OpenPropertyStore (STGM_READ, &props)))
                    {
                        PROPVARIANT v; PropVariantInit (&v);
                        if (SUCCEEDED (props->GetValue (PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) e.name = v.pwszVal;
                        PropVariantClear (&v);
                    }
                    e.isDefault = (e.id == defId);
                    if (! e.id.empty()) out.push_back (e);
                }
            }
        }
    }
    if (SUCCEEDED (ci)) CoUninitialize();
    return out;
}

bool LoopbackCapture::start (const std::wstring& id, Callback cb, std::string& error)
{
    stop();
    callback = std::move (cb);
    quit = false; state = 0; startError.clear();
    thread = std::thread ([this, id] { run (id); });
    while (state == 0) std::this_thread::sleep_for (std::chrono::milliseconds (2));
    if (state < 0) { error = startError; stop(); return false; }
    return true;
}

void LoopbackCapture::stop()
{
    quit = true;
    if (thread.joinable()) thread.join();
}

void LoopbackCapture::run (std::wstring id)
{
    CoInitializeEx (nullptr, COINIT_MULTITHREADED);
    auto fail = [this] (std::string e) { startError = std::move (e); state = -1; };
    {
        ComPtr<IMMDeviceEnumerator> en;
        ComPtr<IMMDevice> dev;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> cap;
        WAVEFORMATEX* fmt = nullptr;
        HRESULT hr;

        if (FAILED (hr = CoCreateInstance (__uuidof (MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof (IMMDeviceEnumerator), (void**) &en))) { fail (hr2s ("Device enumerator", hr)); goto done; }
        if (FAILED (hr = en->GetDevice (id.c_str(), &dev)))                       { fail (hr2s ("Open output device", hr)); goto done; }
        if (FAILED (hr = dev->Activate (__uuidof (IAudioClient), CLSCTX_ALL, nullptr, (void**) &client))) { fail (hr2s ("Activate audio client", hr)); goto done; }
        if (FAILED (hr = client->GetMixFormat (&fmt)))                            { fail (hr2s ("GetMixFormat", hr)); goto done; }
        if (FAILED (hr = client->Initialize (AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK, 2000000 /*200 ms*/, 0, fmt, nullptr)))
                                                                                  { fail (hr2s ("Start loopback capture", hr)); goto done; }
        if (FAILED (hr = client->GetService (__uuidof (IAudioCaptureClient), (void**) &cap))) { fail (hr2s ("GetService", hr)); goto done; }

        {
            // Work out the sample format
            bool isFloat = fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
            if (fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
                isFloat = IsEqualGUID (reinterpret_cast<WAVEFORMATEXTENSIBLE*> (fmt)->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != 0;
            const int bits = fmt->wBitsPerSample, nch = fmt->nChannels, frameBytes = fmt->nBlockAlign;
            sampleRate = fmt->nSamplesPerSec; numChannels = nch;

            std::vector<std::vector<float>> chans ((size_t) nch, std::vector<float> (48000));
            std::vector<const float*> ptrs ((size_t) nch);
            client->Start();
            state = 1;

            // Loopback delivers nothing while Windows plays silence, so pad with zeros by wall clock
            // to keep the display scrolling.
            using clk = std::chrono::steady_clock;
            const auto t0 = clk::now();
            long long delivered = 0;

            while (! quit)
            {
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
                UINT32 packet = 0;
                bool gotData = false;
                while (SUCCEEDED (cap->GetNextPacketSize (&packet)) && packet > 0)
                {
                    BYTE* data = nullptr; UINT32 frames = 0; DWORD flags = 0;
                    if (FAILED (cap->GetBuffer (&data, &frames, &flags, nullptr, nullptr))) break;
                    if (frames > chans[0].size()) for (auto& c : chans) c.resize (frames);
                    const bool silent = (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
                    for (int c = 0; c < nch; ++c)
                    {
                        float* d = chans[(size_t) c].data();
                        for (UINT32 i = 0; i < frames; ++i)
                        {
                            if (silent) { d[i] = 0; continue; }
                            const BYTE* s = data + (size_t) i * frameBytes + (size_t) c * (bits / 8);
                            if (isFloat && bits == 32)  { float v; std::memcpy (&v, s, 4); d[i] = v; }
                            else if (bits == 16)        { int16_t v; std::memcpy (&v, s, 2); d[i] = v / 32768.0f; }
                            else if (bits == 24)        { const uint32_t u = ((uint32_t) s[0] << 8) | ((uint32_t) s[1] << 16) | ((uint32_t) s[2] << 24); d[i] = (float) ((int32_t) u / 2147483648.0); }
                            else if (bits == 32)        { int32_t v; std::memcpy (&v, s, 4); d[i] = (float) (v / 2147483648.0); }
                            else d[i] = 0;
                        }
                        ptrs[(size_t) c] = d;
                    }
                    cap->ReleaseBuffer (frames);
                    if (callback) callback (ptrs.data(), nch, (int) frames);
                    delivered += frames; gotData = true;
                }

                const double elapsed = std::chrono::duration<double> (clk::now() - t0).count();
                const long long expected = (long long) (elapsed * sampleRate);
                const long long deficit = expected - delivered;
                if (! gotData && deficit > (long long) (sampleRate * 0.03))
                {
                    const int n = (int) std::min<long long> (deficit, (long long) chans[0].size());
                    for (int c = 0; c < nch; ++c) { std::fill (chans[(size_t) c].begin(), chans[(size_t) c].begin() + n, 0.0f); ptrs[(size_t) c] = chans[(size_t) c].data(); }
                    if (callback) callback (ptrs.data(), nch, n);
                    delivered += n;
                }
                else if (deficit < -(long long) sampleRate) delivered = expected;   // clock drift guard
            }
            client->Stop();
        }
    done:
        if (fmt) CoTaskMemFree (fmt);
    }
    CoUninitialize();
}

#else   // not Windows: loopback capture is unavailable

std::vector<LoopbackCapture::Endpoint> LoopbackCapture::listOutputDevices() { return {}; }
bool LoopbackCapture::start (const std::wstring&, Callback, std::string& error) { error = "Loopback capture needs Windows"; return false; }
void LoopbackCapture::stop() {}
void LoopbackCapture::run (std::wstring) {}

#endif
