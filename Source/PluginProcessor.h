#pragma once
#include <JuceHeader.h>
#include "Dsp.h"
#include "Shared.h"

class CaptureEngine;

// ============================================================================
//  Réglages globaux (gardés pour tous les projets : Foudre Audio/Typhon.settings)
// ============================================================================
struct TyphonSettings
{
    std::atomic<int> quality { 1 };        // 0 Éco, 1 Haute, 2 Ultra
    std::atomic<int> renderQuality { 1 };  // 0 comme la lecture, 1 Ultra
    std::atomic<int> spMode { 0 };         // 0 authentique, 1 propre
    std::atomic<int> multiOut { 1 };       // 0 stéréo, 1 multi-out
    std::atomic<int> midiMap { 0 };        // 0 Typhon, 1 General MIDI, 2 EZdrummer
    std::atomic<int> midiChannel { 0 };    // 0 omni, sinon 1..16
    std::atomic<int> theme { 0 };          // 0 Atelier, 1 Nuit, 2 Phosphore, 3 Bel Air
    std::atomic<int> uiScale { 1 };        // 0 75 %, 1 100 %, 2 125 %, 3 150 %
    std::atomic<int> snapZero { 1 };
    std::atomic<int> captureRate { 0 };    // 0 48 kHz / 24 bits, 1 96 kHz / 32 bits flottant

    void load();
    void save() const;
    static juce::File file();
};

class TyphonProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    TyphonProcessor();
    ~TyphonProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Typhon"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- accès pour l'interface --------------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    typhon::Pattern pattern;
    TyphonSettings settings;

    std::shared_ptr<typhon::SampleSet> laneSet (int lane) const { return std::atomic_load (&sets[(size_t) lane]); }
    void setLaneSet (int lane, std::shared_ptr<typhon::SampleSet> s);
    juce::String laneSource (int lane) const { const juce::ScopedLock sl (srcLock); return laneSourceText[lane]; }

    bool loadSampleFile (int lane, const juce::File& f);      // glisser-déposer
    bool loadKitFolder (const juce::File& folder);             // kit capturé
    void resetLaneToFallback (int lane);
    juce::String currentKitName() const { const juce::ScopedLock sl (srcLock); return kitName; }

    void audition (int lane) { auditionMask.fetch_or (1 << lane); }
    int playingStep() const { return curStep.load(); }
    bool hostPlaying() const { return isPlayingNow.load(); }
    bool fillActive() const { return fillNow.load(); }
    float laneMeter (int lane) const { return meters[lane].load(); }
    uint32_t laneHitCounter (int lane) const { return hitCounter[lane].load(); }

    juce::File writePatternMidi() const;   // pour glisser le pattern dans Ableton
    int noteForLane (int lane) const;

    CaptureEngine& capture() { return *captureEngine; }
    int captureNote (int lane) const { return captureNotes[lane].load(); }
    void setCaptureNote (int lane, int note) { captureNotes[lane].store (juce::jlimit (0, 127, note)); }

    static juce::File libraryFolder();
    static constexpr int cutAllNote = 24;   // C0 : coupe tous les sons

private:
    void timerCallback() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();

    void trigger (int lane, int vel, int chokeMode, int64_t gateSamples);
    void renderLanes (int from, int to);

    std::array<std::shared_ptr<typhon::SampleSet>, typhon::numLanes> sets;
    std::vector<std::shared_ptr<typhon::SampleSet>> graveyard;   // libéré sur le fil principal
    juce::CriticalSection graveLock, srcLock;
    juce::String laneSourceText[typhon::numLanes];
    juce::String laneSourcePath[typhon::numLanes];
    juce::String kitName { "Kit de secours" };

    struct LaneDsp
    {
        typhon::Voice voices[typhon::maxVoicesPerLane];
        typhon::TransientDesigner tr;
        typhon::Dynamics dyn;
        typhon::SpGrain sp;
        uint32_t rr = 0;
        float vol = 1.0f, panL = 1.0f, panR = 1.0f;
    };
    LaneDsp lanes[typhon::numLanes];
    juce::AudioBuffer<float> laneBuf;  // 2 canaux par piste
    double sr = 48000.0;
    int curQuality = 1;

    // paramètres lus en direct
    struct LaneParamPtrs
    {
        std::atomic<float> *vol, *pan, *pitch, *start, *end, *decay, *choke, *group, *fade, *vsens,
                           *out, *rev, *tatt, *tsus, *dmode, *dthr, *dratio, *datt, *drel, *dmix, *mute, *gate;
    } lp[typhon::numLanes];
    std::atomic<float> *pSwing, *pSpAmt, *pSpBits, *pSpRate, *pSpFilt, *pMaster, *pFill, *pFillAuto, *pFillInt, *pChokeAll;

    std::atomic<int> auditionMask { 0 };
    std::atomic<int> curStep { -1 };
    std::atomic<bool> isPlayingNow { false }, fillNow { false };
    std::atomic<float> meters[typhon::numLanes];
    std::atomic<uint32_t> hitCounter[typhon::numLanes];
    std::atomic<int> captureNotes[typhon::numLanes];
    double lastPpqEnd = -1.0;
    bool prevBarWasFill = false;
    int64_t lastBarIndex = -1;

    std::unique_ptr<CaptureEngine> captureEngine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TyphonProcessor)
};
