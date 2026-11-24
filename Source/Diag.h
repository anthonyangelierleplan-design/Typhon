#pragma once
// ============================================================================
//  Diagnostic de plantage / blocage (Windows)
//  - repères : la dernière étape connue du fil audio et de l'interface
//  - si une erreur grave se produit DANS Typhon : rapport avec pile d'appels
//  - si le fil audio ou l'interface restent bloqués plus de 2 s : rapport
//  Fichier : Documents/Foudre Audio/Typhon/crash_log.txt
//  N'intercepte rien : Ableton garde son propre gestionnaire de plantage.
// ============================================================================
#include <JuceHeader.h>
#include <atomic>

namespace diag
{
    inline std::atomic<const char*>& audioStep() { static std::atomic<const char*> s { "-" }; return s; }
    inline std::atomic<const char*>& uiStep()    { static std::atomic<const char*> s { "-" }; return s; }
    inline std::atomic<juce::uint32>& audioEnter() { static std::atomic<juce::uint32> v { 0 }; return v; }   // 0 = hors processBlock
    inline std::atomic<juce::uint32>& uiBeat()     { static std::atomic<juce::uint32> v { 0 }; return v; }

    inline void audio (const char* s) { audioStep().store (s, std::memory_order_relaxed); }
    inline void ui (const char* s)    { uiStep().store (s, std::memory_order_relaxed); }

    inline juce::File logFile()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
            .getChildFile ("Foudre Audio").getChildFile ("Typhon").getChildFile ("crash_log.txt");
    }
    inline void write (const juce::String& what)
    {
        auto f = logFile();
        f.getParentDirectory().createDirectory();
        f.appendText ("==== " + juce::Time::getCurrentTime().toString (true, true, true, true) + "  Typhon "
                    #ifdef JucePlugin_VersionString
                      + juce::String (JucePlugin_VersionString)
                    #endif
                      + "\n" + what
                      + "\nDerniere etape audio : " + juce::String (audioStep().load())
                      + "\nDerniere etape interface : " + juce::String (uiStep().load()) + "\n\n");
    }

    // Surveille les blocages (fil audio coincé dans processBlock, interface figée)
    class Watchdog : private juce::Thread
    {
    public:
        Watchdog() : juce::Thread ("Typhon watchdog") { startThread (juce::Thread::Priority::low); }
        ~Watchdog() override { stopThread (2000); }
    private:
        void run() override
        {
            bool audioReported = false, uiReported = false;
            while (! threadShouldExit())
            {
                wait (250);
                const auto now = juce::Time::getMillisecondCounter();
                const auto a = audioEnter().load();
                if (a != 0 && now - a > 2000) { if (! audioReported) { write ("BLOCAGE : le fil audio est dans Typhon depuis plus de 2 s"); audioReported = true; } }
                else audioReported = false;
                const auto u = uiBeat().load();
                if (u != 0 && now - u > 3000) { if (! uiReported) { write ("BLOCAGE : l'interface de Typhon ne repond plus depuis 3 s"); uiReported = true; } }
                else uiReported = false;
            }
        }
    };

   #if JUCE_WINDOWS
    void installCrashHandler();
    void removeCrashHandler();
   #else
    inline void installCrashHandler() {}
    inline void removeCrashHandler() {}
   #endif

    // Repère RAII pour processBlock
    struct AudioScope
    {
        AudioScope()  { audioEnter().store (std::max<juce::uint32> (1, juce::Time::getMillisecondCounter()), std::memory_order_relaxed); }
        ~AudioScope() { audioEnter().store (0, std::memory_order_relaxed); audio ("hors processBlock"); }
    };
}
