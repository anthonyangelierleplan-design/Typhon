#pragma once
#include <JuceHeader.h>
#include "Dsp.h"
#include "Shared.h"
#include "Diag.h"
#include "Library.h"
#include "ApcLink.h"

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
    std::atomic<int> theme { 0 };          // 0 Atelier, 1 Nuit, 2 Phosphore, 3 Bel Air, 4 TR-808, 5 Linn
    std::atomic<int> panelOpen { 1 };      // panneau de piste ouvert (1) ou rangé (0)
    std::atomic<int> uiScale { 1 };        // 0 75 %, 1 100 %, 2 125 %, 3 150 %
    std::atomic<int> snapZero { 1 };
    std::atomic<int> captureRate { 0 };    // 0 48 kHz / 24 bits, 1 96 kHz / 32 bits flottant
    std::atomic<int> apcOn { 0 };          // Akai APC64 piloté en direct
    std::atomic<int> textSize { 1 };       // 0 normal, 1 grand, 2 très grand
    std::atomic<int> toggleMod { 0 };      // allumer / éteindre un pas : 0 Ctrl+clic, 1 Alt+clic, 2 double-clic seulement
    static constexpr int numShortcuts = 14;
    std::atomic<int> shortcutKeys[numShortcuts];   // touche (code) par action, 0 = aucune
    TyphonSettings();
    juce::String libraryPath() const { const juce::ScopedLock sl (lock); return libPath; }
    void setLibraryPath (const juce::String& p) { { const juce::ScopedLock sl (lock); libPath = p; } save(); }
    juce::CriticalSection lock;
    juce::String libPath;

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
    std::shared_ptr<typhon::SampleSet> baseSet (int lane) const { return std::atomic_load (&baseSets[(size_t) lane]); }   // avant stretch
    bool stretchRendering (int lane) const { return stretchBusy[lane].load(); }
    float stretchFactor (int lane) const { return stretchFactorNow[lane].load(); }
    juce::String laneSource (int lane) const { const juce::ScopedLock sl (srcLock); return laneSourceText[lane]; }

    bool loadSampleFile (int lane, const juce::File& f);      // glisser-déposer
    bool loadKitFolder (const juce::File& folder);             // kit capturé ou enregistré
    juce::File saveUserKit (const juce::String& name);         // « Mes kits » : copie les 8 pistes telles quelles
    void renameLaneSource (int lane, const juce::String& n) { const juce::ScopedLock sl (srcLock); laneSourceText[lane] = n; }
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
    int seqLength() const { return pSeqLen->load() > 0.5f ? 32 : 16; }
    float masterLevel() const { return masterMeter.load(); }
    double hostBpm() const { return lastBpm.load(); }
    // Découpe le sample de la piste en coups, les répartit sur les pistes ; renvoie le nombre de tranches
    int sliceLane (int lane, float sensitivity, int maxSlices, bool writePattern);

    // --- patterns A..H + FILL écrit ------------------------------------------
    static constexpr int numPatterns = 9, fillPatternIndex = 8;
    int currentPattern() const { return curPattern.load(); }
    int queuedPattern() const { return pendingPattern.load(); }
    void selectPattern (int index);            // au début de la boucle suivante si ça joue, sinon tout de suite
    void copyPattern (int from, int to);
    void clearPattern (int index) { if (index == curPattern.load()) pattern.clear(); else if (index >= 0 && index < numPatterns) bank[index].clear(); patternChanges.fetch_add (1); }
    bool patternHasNotes (int index) const;
    uint32_t patternChangeCount() const { return patternChanges.load(); }
    std::atomic<int> stepLoopLen { 0 };        // STEP LOOP maintenu : 0 = off, sinon nombre de pas
    std::atomic<int> uiRollDiv { 0 };          // ROLL depuis l'interface : 0 off, 1 = 1/8, 2 = 1/16, 3 = 1/32
    std::atomic<int> uiRollLane { 1 };
    // vitesse propre à la piste (multiplicateur de la double-croche)
    double laneSpeed (int lane) const { static const double m[6] = { 2.0, 1.5, 1.0, 0.75, 0.5, 0.25 }; return m[juce::jlimit (0, 5, (int) laneSpdP[lane]->load())]; }
    int lanePlayStep (int lane) const { return laneCur[lane].load(); }   // pas joué sur cette piste (longueur et vitesse propres), -1 à l'arrêt
    int laneLength (int lane) const { const int l = (int) laneLenP[lane]->load(); return l > 0 ? std::min (l, typhon::numSteps) : seqLength(); }

    // --- bibliothèque de machines ------------------------------------------
    std::vector<typhon::LibMachine> library;     // dernier scan (fil principal)
    void tuneSound (typhon::LibSound& s);              // range les variantes du grave à l'aigu, choisit le pitch zéro
    void setSoundAccord (typhon::LibSound& s, int zeroVar, float tune);
    int autoTuneMachine (typhon::LibMachine& m);
    void rescanLibrary();
    bool loadLibrarySound (int lane, const typhon::LibSound& s);
    void loadMachineKit (const typhon::LibMachine& m);
    void previewSound (const typhon::LibSound& s, int variant, float tune = 0.0f);
    int laneVariants (int lane) const { auto s = laneSet (lane); return s ? std::max (1, s->numVariants()) : 1; }

    CaptureEngine& capture() { return *captureEngine; }
    int captureNote (int lane) const { return captureNotes[lane].load(); }
    void setCaptureNote (int lane, int note) { captureNotes[lane].store (juce::jlimit (0, 127, note)); }

    static juce::File libraryFolder();

    // --- Annuler / rétablir (pattern, verrous et réglages ; pas les samples chargés) ---
    void undoCommit();          // enregistre l'état courant s'il a changé depuis la dernière étape
    bool undo();
    bool redo();
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    static constexpr int cutAllNote = 24;   // C0 : coupe tous les sons

    // --- Akai APC64 (mode Custom, canal 16) ---
    juce::String apcStatus() const { return apcLink ? apcLink->status() : juce::String(); }

private:
    void timerCallback() override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static BusesProperties makeBuses();

    void trigger (int lane, int vel, int chokeMode, int64_t gateSamples, int step, int pitchOffset, bool stepRev = false);
    void renderLanes (int from, int to);

    std::array<std::shared_ptr<typhon::SampleSet>, typhon::numLanes> sets, baseSets;
    void setPlayableSet (int lane, std::shared_ptr<typhon::SampleSet> s);
    void updateStretch();
    struct StretchKey
    {
        const typhon::SampleSet* base = nullptr; int mode = 0; double factor = 1.0; float win = 0.5f;
        bool operator== (const StretchKey& o) const { return base == o.base && mode == o.mode && factor == o.factor && win == o.win; }
    };
    StretchKey stretchWant[typhon::numLanes];
    double stretchChanged[typhon::numLanes] {};
    bool stretchPending[typhon::numLanes] {};
    std::atomic<int> stretchGen[typhon::numLanes] {};
    std::atomic<bool> stretchBusy[typhon::numLanes] {};
    std::atomic<float> stretchFactorNow[typhon::numLanes] {};
    juce::ThreadPool stretchPool { 1 };
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
        typhon::Svf filt;
        typhon::GrainCloud clouds[2];
        float ovCut = std::numeric_limits<float>::quiet_NaN(), ovSendA = std::numeric_limits<float>::quiet_NaN(),
              ovSendB = std::numeric_limits<float>::quiet_NaN();
        uint32_t rr = 0;
        float vol = 1.0f, panL = 1.0f, panR = 1.0f;
    };
    LaneDsp lanes[typhon::numLanes];
    juce::AudioBuffer<float> laneBuf;  // 2 canaux par piste
    juce::AudioBuffer<float> fxA, fxB;  // envois vers les bus d'effets
    std::vector<float> duckGain, dlyL, dlyR;
    int dlyPos = 0; float dlyToneL = 0, dlyToneR = 0;
    juce::Reverb reverb;
    typhon::TransientDesigner masterTr;
    typhon::Dynamics masterDyn;
    struct Ev { int offset, lane, vel, choke; int64_t gate; int step, pitch; bool rev; };
    std::array<Ev, 768> evBuf {};
    std::array<int, 128> duckBuf {};
    std::shared_ptr<typhon::SampleSet> previewSet;   // écoute dans la bibliothèque
    std::atomic<bool> previewTrigger { false };
    typhon::Voice previewVoice;
    float duckEnv = 10.0f;   // temps depuis le dernier coup de la source (s)
    int reverbTail = 0, delayTail = 0;
    bool wasPlaying = false;
    std::atomic<float> masterMeter { 0.0f };
    std::atomic<double> lastBpm { 120.0 };
    typhon::GrainParams grainParams (int lane) const;
    double sr = 48000.0;
    int curQuality = 1;

    // paramètres lus en direct
    struct LaneParamPtrs
    {
        std::atomic<float> *vol, *pan, *pitch, *start, *end, *decay, *choke, *group, *fade, *vsens,
                           *out, *rev, *tatt, *tsus, *dmode, *dthr, *dratio, *datt, *drel, *dmix, *mute, *gate,
                           *fcut, *fres, *ftype, *sendA, *sendB, *duck,
                           *gran, *gpos, *gsize, *gdens, *gspray, *gpitch, *gprand, *gstereo, *gspeed, *gdur, *grev, *gfreeze, *steplen, *var;
    } lp[typhon::numLanes];
    std::atomic<float> *pSwing, *pSpAmt, *pSpBits, *pSpRate, *pSpFilt, *pMaster, *pFill, *pFillAuto, *pFillInt, *pChokeAll,
                       *pSeqLen, *pFillLen, *pFillGrid, *pFillOthers, *pFillCrash, *pFillLane[typhon::numLanes],
                       *pFxaSize, *pFxaDamp, *pFxaWidth, *pFxaRet, *pFxbTime, *pFxbFb, *pFxbTone, *pFxbPp, *pFxbRet,
                       *pDuckSrc, *pDuckAmt, *pDuckRel, *pDuckFx,
                       *pMtAtt, *pMtSus, *pMdMode, *pMdThr, *pMdRatio, *pMdAtt, *pMdRel, *pMdMix;

    std::atomic<int> auditionMask { 0 };
    std::atomic<int> curStep { -1 };
    std::atomic<bool> isPlayingNow { false }, fillNow { false };
    std::atomic<float> meters[typhon::numLanes];
    std::atomic<uint32_t> hitCounter[typhon::numLanes];
    std::atomic<int> captureNotes[typhon::numLanes];
    double lastPpqEnd = -1.0;
    bool prevBarWasFill = false;
    int64_t lastBarIndex = -1;

    // --- APC64 : la surface demande, le fil principal exécute ---
    struct ApcHost : typhon::apc::Host
    {
        TyphonProcessor& p; explicit ApcHost (TyphonProcessor& x) : p (x) {}
        typhon::apc::ActionQueue queue;
        juce::RangedAudioParameter* stripParam (int strip, int lane) const;
        int seqLen() const override { return p.seqLength(); }
        bool muted (int l) const override { return p.lp[l].mute->load() > 0.5f; }
        float stripValue (int s, int l) const override { auto* q = stripParam (s, l); return q ? q->getValue() : 0.0f; }
        void toggleMute (int l) override { queue.push ({ typhon::apc::Act::toggleMute, 0, (int8_t) l }); }
        void setStrip (int s, int l, float v) override { queue.push ({ typhon::apc::Act::setStrip, (int8_t) s, (int8_t) l, v }); }
        void toggleSeqLen() override { queue.push ({ typhon::apc::Act::toggleSeqLen }); }
        void audition (int l) override { p.audition (l); }
        void cutAll() override { p.cutRequest.store (true); }
        void undo() override { queue.push ({ typhon::apc::Act::undo }); }
        void redo() override { queue.push ({ typhon::apc::Act::redo }); }
        void commit() override { queue.push ({ typhon::apc::Act::commit }); }
        std::string laneLabel (int l) const override;
        void selectPattern (int k) override { p.selectPattern (k); }
        int stripLock (int s) const override { return s == 4 ? typhon::lkCut : (s == 5 ? typhon::lkDecay : (s == 6 ? -2 : -1)); }
        float stripLockValue (int s, int l, float v) const override { auto* q = stripParam (s, l); return q ? q->convertFrom0to1 (v) : v; }
    } apcHost { *this };
    std::unique_ptr<typhon::apc::Link> apcLink;
    std::atomic<bool> cutRequest { false };
    typhon::apc::Surface::Hit rollHits[64];
    void applyApcActions();

    struct Accord { juce::String zeroFile; float tune = 0.0f; };
    std::map<juce::String, Accord> accords;
    std::map<juce::String, std::pair<float, bool>> pitchCache;
    bool accordsLoaded = false;
    void loadAccords();
    void saveAccords();

    typhon::Pattern bank[numPatterns];
    std::atomic<int> curPattern { 0 }, pendingPattern { -1 };
    std::atomic<uint32_t> patternChanges { 0 };
    void switchPatternNow (int index);
    void syncBank();
    bool wasPlayingSeq = false;
    int stepLoopSize = 0; int64_t stepLoopStart = 0;
    std::atomic<float>* laneLenP[typhon::numLanes] {};
    std::atomic<float>* laneSpdP[typhon::numLanes] {};
    std::atomic<int> laneCur[typhon::numLanes] {};
public:
    std::atomic<bool> motionRec { false };   // MOTION REC (non sauvegardé)
private:
    std::atomic<float>* pFillSrc = nullptr;
    std::atomic<float>* soloP[typhon::numLanes] {};
public:
    bool anySolo() const { for (auto* p : soloP) if (p != nullptr && p->load() > 0.5f) return true; return false; }
    bool laneSilent (int i) const { return lp[i].mute->load() > 0.5f || (anySolo() && soloP[i]->load() < 0.5f); }
private:
    double uiRollFree = 0.0; bool uiRollRunning = false;

    std::unique_ptr<CaptureEngine> captureEngine;
    std::unique_ptr<diag::Watchdog> watchdog;

    struct Snapshot
    {
        std::vector<uint64_t> steps;
        std::vector<float> locks, params;
        bool operator== (const Snapshot& o) const;
    };
    Snapshot takeSnapshot() const;
    void applySnapshot (const Snapshot&);
    std::vector<Snapshot> undoStack, redoStack;
    Snapshot stable;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TyphonProcessor)
};
