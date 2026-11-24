#include "Diag.h"

#if JUCE_WINDOWS
#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>

namespace diag
{
    static PVOID handle = nullptr;
    static int users = 0;
    static std::atomic<bool> busy { false };

    static bool insideTyphon (void* addr)
    {
        HMODULE self = nullptr;
        if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                  (LPCWSTR) (void*) &insideTyphon, &self))
            return false;
        MODULEINFO mi {};
        if (! GetModuleInformation (GetCurrentProcess(), self, &mi, sizeof (mi))) return false;
        auto* base = (char*) mi.lpBaseOfDll;
        return (char*) addr >= base && (char*) addr < base + mi.SizeOfImage;
    }

    static size_t moduleOffset (void* addr)
    {
        HMODULE m = nullptr;
        if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) addr, &m))
            return (size_t) ((char*) addr - (char*) m);
        return 0;
    }

    static LONG CALLBACK onException (PEXCEPTION_POINTERS ep)
    {
        const DWORD code = ep->ExceptionRecord->ExceptionCode;
        const bool serious = code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_STACK_OVERFLOW || code == EXCEPTION_ILLEGAL_INSTRUCTION
                          || code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == EXCEPTION_ARRAY_BOUNDS_EXCEEDED || code == EXCEPTION_PRIV_INSTRUCTION;
        void* addr = ep->ExceptionRecord->ExceptionAddress;
        if (serious && insideTyphon (addr) && ! busy.exchange (true))
        {
            juce::String s;
            s << "ERREUR DANS TYPHON : code 0x" << juce::String::toHexString ((int) code)
              << " a Typhon.vst3+0x" << juce::String::toHexString ((juce::int64) moduleOffset (addr));
            if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2)
                s << (ep->ExceptionRecord->ExceptionInformation[0] ? " (ecriture" : " (lecture")
                  << " a 0x" << juce::String::toHexString ((juce::int64) ep->ExceptionRecord->ExceptionInformation[1]) << ")";
            if (code != EXCEPTION_STACK_OVERFLOW)
                s << "\nPile :\n" << juce::SystemStats::getStackBacktrace();
            write (s);
            busy.store (false);
        }
        return EXCEPTION_CONTINUE_SEARCH;   // Ableton gère la suite normalement
    }

    void installCrashHandler()
    {
        if (users++ == 0) handle = AddVectoredExceptionHandler (0, onException);
    }
    void removeCrashHandler()
    {
        if (--users == 0 && handle != nullptr) { RemoveVectoredExceptionHandler (handle); handle = nullptr; }
    }
}
#endif
