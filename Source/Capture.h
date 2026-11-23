#pragma once
#include <JuceHeader.h>
#include "Dsp.h"

class TyphonProcessor;

// ============================================================================
//  Capture : charge EZdrummer dans Typhon, joue chaque élément à plusieurs
//  vélocités (hors temps réel) et enregistre le résultat comme un kit Typhon.
//  La capture par lot rejoue une file d'états EZdrummer (un par kit).
// ============================================================================
class CaptureEngine : private juce::Thread
{
public:
    explicit CaptureEngine (TyphonProcessor&);
    ~CaptureEngine() override;

    // --- EZdrummer -----------------------------------------------------------
    bool isLoaded() const { return ezd != nullptr; }
    bool load (juce::String& error);           // fil principal
    void showEditor();                         // fil principal
    juce::String pluginName() const;

    // --- réglages ------------------------------------------------------------
    std::atomic<int> velLayers { 4 };          // 1, 2, 4 ou 8
    std::atomic<int> roundRobin { 2 };         // 1..4
    std::atomic<float> maxSeconds { 2.0f };    // durée max d'un coup
    std::atomic<float> loadWaitSeconds { 10.0f }; // attente du chargement d'un kit en lot
    std::atomic<bool> laneEnabled[typhon::numLanes];

    // --- écoute (page CAPTURE) : EZdrummer tourne en continu et s'entend dans Typhon
    std::atomic<bool> monitor { false };
    void setHostConfig (double sampleRate, int blockSize);
    void processMonitor (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, int numSamples);

    // --- capture -------------------------------------------------------------
    void captureNow (const juce::String& kitName);   // le kit actuellement chargé dans EZdrummer
    void addCurrentToQueue (const juce::String& kitName);
    void removeFromQueue (int index);
    void clearDone();
    void startBatch();
    void stop();
    bool busy() const { return isThreadRunning(); }

    struct QueueView { juce::String name; int status; };  // 0 en attente, 1 en cours, 2 fait, 3 erreur
    juce::Array<QueueView> queue() const;

    float progress() const { const int t = total.load(); return t > 0 ? (float) done.load() / (float) t : 0.0f; }
    juce::String statusText() const { const juce::ScopedLock sl (msgLock); return message; }
    double estimateSecondsPerKit() const;

private:
    void run() override;
    bool captureKit (const juce::String& name, juce::File& outFolder);
    bool renderHit (int note, int vel, double sr, int maxLen, juce::AudioBuffer<float>& out);
    bool renderHitOnce (int note, int vel, double sr, int maxLen, juce::AudioBuffer<float>& out, bool paced, float& peak);
    void prepareLocked (double sr, int block, bool offline);
    void log (const juce::String& line);
    void setMessage (const juce::String& m) { const juce::ScopedLock sl (msgLock); message = m; }
    static juce::File findPluginFile();

    TyphonProcessor& proc;
    juce::AudioPluginFormatManager formats;
    std::unique_ptr<juce::AudioPluginInstance> ezd;
    std::unique_ptr<juce::DocumentWindow> window;
    juce::CriticalSection pluginLock;   // le rendu et le changement d'état ne se croisent pas

    struct Item { juce::String name; juce::MemoryBlock state; int status = 0; };
    std::vector<Item> items;
    mutable juce::CriticalSection queueLock, msgLock;
    juce::String message { juce::String::fromUTF8 ("EZdrummer non charg\xc3\xa9") };
    juce::String singleName;
    bool batchMode = false;
    std::atomic<int> done { 0 }, total { 0 };
    std::atomic<double> hostRate { 48000.0 };
    std::atomic<int> hostBlock { 512 };
    std::atomic<bool> pacedMode { false };
    std::atomic<float> lastPeak { 0.0f };
    double preparedRate = 0.0;
    int preparedBlock = 0;
    bool offlinePrepared = false;
    bool warmedUp = false;
    juce::AudioBuffer<float> work;
    juce::MidiBuffer monitorMidi;
    std::shared_ptr<std::atomic<bool>> alive { std::make_shared<std::atomic<bool>> (true) };

    JUCE_DECLARE_NON_COPYABLE (CaptureEngine)
};
