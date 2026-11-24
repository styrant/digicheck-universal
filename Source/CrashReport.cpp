#include "CrashReport.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <dbghelp.h>
 #include <shlobj.h>
 #include <cstdio>
 #include <cwchar>

namespace
{
    // everything the handler needs is prepared at install time: a crashing process can't be trusted to allocate
    wchar_t folder[MAX_PATH] {};
    char productName[64] {}, productVersion[32] {};
    bool onlyOurs = false;
    HMODULE ourModule = nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER previous = nullptr;
    PVOID vectored = nullptr;
    volatile LONG busy = 0;

    bool inOurModule (void* address)
    {
        HMODULE m = nullptr;
        return GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCWSTR) address, &m) && m == ourModule;
    }

    void write (EXCEPTION_POINTERS* ep, void* addr)
    {
        if (InterlockedExchange (&busy, 1) == 0)
        {
            SYSTEMTIME t; GetLocalTime (&t);
            wchar_t base[MAX_PATH + 64];
            swprintf (base, MAX_PATH + 64, L"%ls\\%hs %hs %04d-%02d-%02d %02d%02d%02d", folder, productName, productVersion,
                      t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
            wchar_t dmp[MAX_PATH + 80], txt[MAX_PATH + 80];
            swprintf (dmp, MAX_PATH + 80, L"%ls.dmp", base);
            swprintf (txt, MAX_PATH + 80, L"%ls.txt", base);
            CreateDirectoryW (folder, nullptr);

            HANDLE f = CreateFileW (dmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (f != INVALID_HANDLE_VALUE)
            {
                MINIDUMP_EXCEPTION_INFORMATION mei { GetCurrentThreadId(), ep, FALSE };
                MiniDumpWriteDump (GetCurrentProcess(), GetCurrentProcessId(), f,
                                   (MINIDUMP_TYPE) (MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                                   ep != nullptr ? &mei : nullptr, nullptr, nullptr);
                CloseHandle (f);
            }
            // the note: which module and offset (enough to find the line with the release's PDB)
            HMODULE m = nullptr; wchar_t modName[MAX_PATH] = L"?";
            if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) addr, &m))
                GetModuleFileNameW (m, modName, MAX_PATH);
            char note[2048];
            const int n = std::snprintf (note, sizeof (note),
                "%s %s crashed.\r\nException 0x%08lX at %p (module offset 0x%llX)\r\nModule: %ls\r\n\r\n"
                "Please send this file and the .dmp next to it.\r\n",
                productName, productVersion,
                ep != nullptr && ep->ExceptionRecord != nullptr ? (unsigned long) ep->ExceptionRecord->ExceptionCode : 0ul,
                addr, (unsigned long long) ((char*) addr - (char*) m), modName);
            HANDLE tf = CreateFileW (txt, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (tf != INVALID_HANDLE_VALUE) { DWORD w; WriteFile (tf, note, (DWORD) (n > 0 ? n : 0), &w, nullptr); CloseHandle (tf); }
        }
    }

    void* addressOf (EXCEPTION_POINTERS* ep) { return ep != nullptr && ep->ExceptionRecord != nullptr ? ep->ExceptionRecord->ExceptionAddress : nullptr; }

    // the app: every unhandled crash
    LONG WINAPI filter (EXCEPTION_POINTERS* ep)
    {
        write (ep, addressOf (ep));
        return previous != nullptr ? previous (ep) : EXCEPTION_CONTINUE_SEARCH;
    }

    // the plugin: fatal exceptions raised in its own code only, then on to the DAW's handling
    LONG WINAPI vectoredHandler (EXCEPTION_POINTERS* ep)
    {
        if (ep == nullptr || ep->ExceptionRecord == nullptr) return EXCEPTION_CONTINUE_SEARCH;
        switch (ep->ExceptionRecord->ExceptionCode)
        {
            case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_STACK_OVERFLOW:
            case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_PRIV_INSTRUCTION: case EXCEPTION_IN_PAGE_ERROR:
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: case 0xC0000409 /* STATUS_STACK_BUFFER_OVERRUN */:
                if (inOurModule (addressOf (ep))) write (ep, addressOf (ep));
                break;
            default: break;
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
}

void crashreport::install (const char* product, const char* version, bool pluginInHost)
{
    PWSTR docs = nullptr;
    if (FAILED (SHGetKnownFolderPath (FOLDERID_Documents, 0, nullptr, &docs))) return;
    swprintf (folder, MAX_PATH, L"%ls\\Ultimate Audio Viewer", docs);
    CoTaskMemFree (docs);
    CreateDirectoryW (folder, nullptr);
    wcscat_s (folder, MAX_PATH, L"\\Crashes");
    strncpy_s (productName, product, _TRUNCATE);
    strncpy_s (productVersion, version, _TRUNCATE);
    onlyOurs = pluginInHost;
    GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                        (LPCWSTR) &crashreport::install, &ourModule);
    if (onlyOurs) { if (vectored == nullptr) vectored = AddVectoredExceptionHandler (0, vectoredHandler); }
    else previous = SetUnhandledExceptionFilter (filter);
}

void crashreport::uninstall()
{
    if (vectored != nullptr) { RemoveVectoredExceptionHandler (vectored); vectored = nullptr; }
}
#else
void crashreport::install (const char*, const char*, bool) {}
void crashreport::uninstall() {}
#endif
