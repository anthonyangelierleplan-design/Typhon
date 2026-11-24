#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Capture.h"
#include "Fallback.h"

using namespace typhon;

// ============================================================================
//  Réglages globaux
// ============================================================================
juce::File TyphonSettings::file()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Foudre Audio").getChildFile ("Typhon.settings");
}

void TyphonSettings::load()
{
    auto xml = juce::XmlDocument::parse (file());
    if (xml == nullptr) return;
    auto get = [&] (const char* k, std::atomic<int>& v, int lo, int hi) { v.store (juce::jlimit (lo, hi, xml->getIntAttribute (k, v.load()))); };
    get ("quality", quality, 0, 2);
    get ("renderQuality", renderQuality, 0, 1);
    get ("spMode", spMode, 0, 1);
    get ("multiOut", multiOut, 0, 1);
    get ("midiMap", midiMap, 0, 2);
    get ("midiChannel", midiChannel, 0, 16);
    get ("theme", theme, 0, 3);
    get ("uiScale", uiScale, 0, 3);
    get ("snapZero", snapZero, 0, 1);
    get ("captureRate", captureRate, 0, 1);
    get ("apcOn", apcOn, 0, 1);
    { const juce::ScopedLock sl (lock); libPath = xml->getStringAttribute ("libraryPath", libPath); }
}

void TyphonSettings::save() const
{
    juce::XmlElement x ("TYPHON_SETTINGS");
    x.setAttribute ("quality", quality.load());
    x.setAttribute ("renderQuality", renderQuality.load());
    x.setAttribute ("spMode", spMode.load());
    x.setAttribute ("multiOut", multiOut.load());
    x.setAttribute ("midiMap", midiMap.load());
    x.setAttribute ("midiChannel", midiChannel.load());
    x.setAttribute ("theme", theme.load());
    x.setAttribute ("uiScale", uiScale.load());
    x.setAttribute ("snapZero", snapZero.load());
    x.setAttribute ("captureRate", captureRate.load());
    x.setAttribute ("apcOn", apcOn.load());
    { const juce::ScopedLock sl (lock); x.setAttribute ("libraryPath", libPath); }
    file().getParentDirectory().createDirectory();
    x.writeTo (file());
}

juce::File TyphonProcessor::libraryFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Foudre Audio").getChildFile ("Typhon").getChildFile ("Kits");
}

// ============================================================================
//  Bus de sortie : 1 principal + 7 auxiliaires (une sortie par piste en multi-out)
// ============================================================================
juce::AudioProcessor::BusesProperties TyphonProcessor::makeBuses()
{
    BusesProperties b;
    b = b.withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int i = 1; i < numLanes; ++i)
        b = b.withOutput (juce::String ("Out ") + juce::String (i + 1) + " " + laneName (i), juce::AudioChannelSet::stereo(), false);
    return b;
}

bool TyphonProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
    {
        const auto& s = layouts.outputBuses.getReference (i);
        if (! s.isDisabled() && s != juce::AudioChannelSet::stereo()) return false;
    }
    return layouts.inputBuses.isEmpty();
}

// ============================================================================
//  Paramètres (identifiés par leur nom : ne jamais renommer)
// ============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TyphonProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    auto f = [] (const String& id, const String& name, float lo, float hi, float def, float skewMid = 0.0f) {
        NormalisableRange<float> r (lo, hi);
        if (skewMid > 0.0f) r.setSkewForCentre (skewMid);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def);
    };
    StringArray groups { "Aucun", "A", "B", "C", "D" };
    StringArray outs; for (int i = 0; i < numLanes; ++i) outs.add (String (i * 2 + 1) + "/" + String (i * 2 + 2));
    StringArray modes { juce::String::fromUTF8 ("Comp \xe2\x86\x93"), juce::String::fromUTF8 ("Comp \xe2\x86\x91"), juce::String::fromUTF8 ("Expand \xe2\x86\x93"), juce::String::fromUTF8 ("Expand \xe2\x86\x91") };

    for (int i = 0; i < numLanes; ++i)
    {
        const String n = laneName (i) + " ";
        auto grp = std::make_unique<AudioProcessorParameterGroup> ("lane" + String (i), laneName (i), " | ");
        grp->addChild (f (pid (i, "vol"), n + "Volume", -60.0f, 6.0f, 0.0f, -12.0f));
        grp->addChild (f (pid (i, "pan"), n + "Pan", -1.0f, 1.0f, 0.0f));
        grp->addChild (f (pid (i, "pitch"), n + "Pitch", -24.0f, 24.0f, 0.0f));
        grp->addChild (f (pid (i, "start"), n + juce::String::fromUTF8 ("D\xc3\xa9" "but"), 0.0f, 1.0f, 0.0f));
        grp->addChild (f (pid (i, "end"), n + "Fin", 0.0f, 1.0f, 1.0f));
        grp->addChild (f (pid (i, "decay"), n + juce::String::fromUTF8 ("D\xc3\xa9" "croissance"), 0.0f, 1.0f, 1.0f));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "choke"), 1 }, n + "Choke", laneChokeDefault[i]));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "group"), 1 }, n + "Groupe de choke", groups, laneGroupDefault[i]));
        grp->addChild (f (pid (i, "fade"), n + "Fondu choke", 0.5f, 50.0f, 5.0f, 6.0f));
        grp->addChild (f (pid (i, "vsens"), n + juce::String::fromUTF8 ("V\xc3\xa9locit\xc3\xa9"), 0.0f, 1.0f, 1.0f));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "out"), 1 }, n + "Sortie", outs, i));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "rev"), 1 }, n + "Reverse", false));
        grp->addChild (f (pid (i, "tatt"), n + "Transient attaque", -12.0f, 12.0f, 0.0f));
        grp->addChild (f (pid (i, "tsus"), n + "Transient sustain", -12.0f, 12.0f, 0.0f));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "dmode"), 1 }, n + "Dynamique mode", modes, 0));
        grp->addChild (f (pid (i, "dthr"), n + "Dynamique seuil", -60.0f, 0.0f, -18.0f));
        grp->addChild (f (pid (i, "dratio"), n + "Dynamique ratio", 1.0f, 20.0f, 1.0f, 4.0f));
        grp->addChild (f (pid (i, "datt"), n + "Dynamique attaque", 0.1f, 100.0f, 5.0f, 10.0f));
        grp->addChild (f (pid (i, "drel"), n + juce::String::fromUTF8 ("Dynamique rel\xc3\xa2" "che"), 5.0f, 1000.0f, 80.0f, 120.0f));
        grp->addChild (f (pid (i, "dmix"), n + "Dynamique mix", 0.0f, 1.0f, 1.0f));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "mute"), 1 }, n + "Mute", false));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "gate"), 1 }, n + "Gate MIDI", false));
        grp->addChild (f (pid (i, "fcut"), n + "Filtre", 20.0f, 20000.0f, 20000.0f, 1000.0f));
        grp->addChild (f (pid (i, "fres"), n + juce::String::fromUTF8 ("R\xc3\xa9sonance"), 0.5f, 10.0f, 0.707f, 2.0f));
        grp->addChild (std::make_unique<AudioParameterChoice> (ParameterID { pid (i, "ftype"), 1 }, n + "Type de filtre", StringArray { "Passe-bas", "Passe-haut", "Passe-bande" }, 0));
        grp->addChild (f (pid (i, "sendA"), n + juce::String::fromUTF8 ("Envoi A (r\xc3\xa9verbe)"), 0.0f, 1.0f, 0.0f));
        grp->addChild (f (pid (i, "sendB"), n + "Envoi B (delay)", 0.0f, 1.0f, 0.0f));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "duck"), 1 }, n + "Ducking", laneDuckDefault[i]));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "gran"), 1 }, n + "Granulaire", false));
        grp->addChild (f (pid (i, "gpos"), n + "Grain position", 0.0f, 1.0f, 0.3f));
        grp->addChild (f (pid (i, "gsize"), n + "Grain taille", 5.0f, 500.0f, 80.0f, 80.0f));
        grp->addChild (f (pid (i, "gdens"), n + juce::String::fromUTF8 ("Grain densit\xc3\xa9"), 1.0f, 120.0f, 24.0f, 24.0f));
        grp->addChild (f (pid (i, "gspray"), n + "Grain spray", 0.0f, 1.0f, 0.1f));
        grp->addChild (f (pid (i, "gpitch"), n + "Grain pitch", -24.0f, 24.0f, 0.0f));
        grp->addChild (f (pid (i, "gprand"), n + juce::String::fromUTF8 ("Grain al\xc3\xa9" "a pitch"), 0.0f, 12.0f, 0.0f, 2.0f));
        grp->addChild (f (pid (i, "gstereo"), n + juce::String::fromUTF8 ("Grain st\xc3\xa9r\xc3\xa9o"), 0.0f, 1.0f, 0.3f));
        grp->addChild (f (pid (i, "gspeed"), n + "Grain vitesse", -2.0f, 2.0f, 0.0f));
        grp->addChild (f (pid (i, "gdur"), n + juce::String::fromUTF8 ("Grain dur\xc3\xa9" "e"), 30.0f, 4000.0f, 600.0f, 600.0f));
        grp->addChild (f (pid (i, "grev"), n + juce::String::fromUTF8 ("Grains invers\xc3\xa9s"), 0.0f, 1.0f, 0.0f));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "gfreeze"), 1 }, n + "Grain freeze", false));
        grp->addChild (std::make_unique<AudioParameterInt> (ParameterID { pid (i, "var"), 1 }, n + "Variante", 1, 16, 1));
        grp->addChild (std::make_unique<AudioParameterBool> (ParameterID { pid (i, "steplen"), 1 }, n + juce::String::fromUTF8 ("Dur\xc3\xa9" "e limit\xc3\xa9" "e au pas"), false));
        L.add (std::move (grp));
    }
    L.add (f ("swing", "Swing", 0.5f, 0.75f, 0.5f));
    L.add (f ("sp_amt", "Grain SP", 0.0f, 1.0f, 0.0f));
    L.add (f ("sp_bits", "SP bits", 4.0f, 16.0f, 12.0f));
    L.add (f ("sp_rate", juce::String::fromUTF8 ("SP fr\xc3\xa9quence"), 4000.0f, 48000.0f, 26040.0f, 20000.0f));
    L.add (f ("sp_filt", "Filtre SP", 0.0f, 1.0f, 0.0f));
    L.add (f ("master", "Master", -60.0f, 6.0f, 0.0f, -12.0f));
    L.add (std::make_unique<AudioParameterBool> (ParameterID { "fill", 1 }, "Fill", false));
    L.add (std::make_unique<AudioParameterChoice> (ParameterID { "fill_auto", 1 }, "Fill auto", StringArray { "Jamais", "4 mesures", "8 mesures" }, 0));
    L.add (std::make_unique<AudioParameterBool> (ParameterID { "choke_all", 1 }, "Choke global", false));
    L.add (f ("fill_int", juce::String::fromUTF8 ("Fill intensit\xc3\xa9"), 0.0f, 1.0f, 0.5f));
    L.add (std::make_unique<AudioParameterChoice> (ParameterID { "seq_len", 1 }, juce::String::fromUTF8 ("Longueur du s\xc3\xa9quenceur"), StringArray { "16 pas", "32 pas" }, 0));
    L.add (std::make_unique<AudioParameterChoice> (ParameterID { "fill_len", 1 }, "Fill longueur", StringArray { "1 temps", "2 temps", "1 mesure" }, 0));
    L.add (std::make_unique<AudioParameterChoice> (ParameterID { "fill_grid", 1 }, "Fill grille", StringArray { "1/8", "1/16", "1/32", "1/16 triolet" }, 1));
    L.add (std::make_unique<AudioParameterChoice> (ParameterID { "fill_others", 1 }, "Fill autres pistes", StringArray { "Gardent leur pattern", "Se taisent" }, 1));
    L.add (std::make_unique<AudioParameterBool> (ParameterID { "fill_crash", 1 }, "Fill crash au retour", true));
    for (int i = 0; i < numLanes; ++i)
        L.add (std::make_unique<AudioParameterBool> (ParameterID { "fill_l" + String (i), 1 }, "Fill " + laneName (i), i == 1 || i == 7));
    L.add (f ("fxa_size", juce::String::fromUTF8 ("R\xc3\xa9verbe taille"), 0.0f, 1.0f, 0.6f));
    L.add (f ("fxa_damp", juce::String::fromUTF8 ("R\xc3\xa9verbe amorti"), 0.0f, 1.0f, 0.5f));
    L.add (f ("fxa_width", juce::String::fromUTF8 ("R\xc3\xa9verbe largeur"), 0.0f, 1.0f, 1.0f));
    L.add (f ("fxa_ret", juce::String::fromUTF8 ("R\xc3\xa9verbe retour"), -60.0f, 6.0f, 0.0f, -12.0f));
    L.add (std::make_unique<AudioParameterChoice> (ParameterID { "fxb_time", 1 }, "Delay temps", StringArray { "1/16", "1/8", juce::String::fromUTF8 ("1/8 point\xc3\xa9"), "1/4", juce::String::fromUTF8 ("1/4 point\xc3\xa9"), "1/2" }, 2));
    L.add (f ("fxb_fb", "Delay feedback", 0.0f, 0.95f, 0.35f));
    L.add (f ("fxb_tone", juce::String::fromUTF8 ("Delay tonalit\xc3\xa9"), 0.0f, 1.0f, 0.6f));
    L.add (std::make_unique<AudioParameterBool> (ParameterID { "fxb_pp", 1 }, "Delay ping-pong", true));
    L.add (f ("fxb_ret", "Delay retour", -60.0f, 6.0f, 0.0f, -12.0f));
    {
        StringArray ln; for (int i = 0; i < numLanes; ++i) ln.add (laneName (i));
        L.add (std::make_unique<AudioParameterChoice> (ParameterID { "duck_src", 1 }, "Ducking source", ln, 0));
    }
    L.add (f ("duck_amt", "Ducking profondeur", 0.0f, 1.0f, 0.0f));
    L.add (f ("duck_rel", juce::String::fromUTF8 ("Ducking rel\xc3\xa2" "che"), 20.0f, 800.0f, 180.0f, 180.0f));
    L.add (std::make_unique<AudioParameterBool> (ParameterID { "duck_fx", 1 }, "Ducking des effets", true));
    L.add (f ("m_tatt", "Master transient attaque", -12.0f, 12.0f, 0.0f));
    L.add (f ("m_tsus", "Master transient sustain", -12.0f, 12.0f, 0.0f));
    L.add (std::make_unique<AudioParameterChoice> (ParameterID { "m_dmode", 1 }, "Master dynamique mode", modes, 0));
    L.add (f ("m_dthr", "Master dynamique seuil", -60.0f, 0.0f, -12.0f));
    L.add (f ("m_dratio", "Master dynamique ratio", 1.0f, 20.0f, 1.0f, 4.0f));
    L.add (f ("m_datt", "Master dynamique attaque", 0.1f, 100.0f, 10.0f, 10.0f));
    L.add (f ("m_drel", juce::String::fromUTF8 ("Master dynamique rel\xc3\xa2" "che"), 5.0f, 1000.0f, 120.0f, 120.0f));
    L.add (f ("m_dmix", "Master dynamique mix", 0.0f, 1.0f, 1.0f));
    return L;
}

// ============================================================================
TyphonProcessor::TyphonProcessor()
    : AudioProcessor (makeBuses()), apvts (*this, nullptr, "TYPHON", createLayout())
{
    settings.load();
    for (int i = 0; i < numLanes; ++i)
    {
        auto P = [&] (const char* w) { return apvts.getRawParameterValue (pid (i, w)); };
        lp[i] = { P ("vol"), P ("pan"), P ("pitch"), P ("start"), P ("end"), P ("decay"), P ("choke"), P ("group"), P ("fade"), P ("vsens"),
                  P ("out"), P ("rev"), P ("tatt"), P ("tsus"), P ("dmode"), P ("dthr"), P ("dratio"), P ("datt"), P ("drel"), P ("dmix"), P ("mute"), P ("gate"),
                  P ("fcut"), P ("fres"), P ("ftype"), P ("sendA"), P ("sendB"), P ("duck"),
                  P ("gran"), P ("gpos"), P ("gsize"), P ("gdens"), P ("gspray"), P ("gpitch"), P ("gprand"), P ("gstereo"), P ("gspeed"), P ("gdur"), P ("grev"), P ("gfreeze"), P ("steplen"), P ("var") };
        meters[i].store (0.0f);
        hitCounter[i].store (0);
        captureNotes[i].store (captureNoteDefault[i]);
        sets[(size_t) i] = makeFallback (i);
        laneSourceText[i] = "Secours";
    }
    pSwing = apvts.getRawParameterValue ("swing");
    pSpAmt = apvts.getRawParameterValue ("sp_amt");
    pSpBits = apvts.getRawParameterValue ("sp_bits");
    pSpRate = apvts.getRawParameterValue ("sp_rate");
    pSpFilt = apvts.getRawParameterValue ("sp_filt");
    pMaster = apvts.getRawParameterValue ("master");
    pFill = apvts.getRawParameterValue ("fill");
    pFillAuto = apvts.getRawParameterValue ("fill_auto");
    pFillInt = apvts.getRawParameterValue ("fill_int");
    pChokeAll = apvts.getRawParameterValue ("choke_all");
    auto G = [&] (const char* id) { return apvts.getRawParameterValue (id); };
    pSeqLen = G ("seq_len"); pFillLen = G ("fill_len"); pFillGrid = G ("fill_grid"); pFillOthers = G ("fill_others"); pFillCrash = G ("fill_crash");
    for (int i = 0; i < numLanes; ++i) pFillLane[i] = apvts.getRawParameterValue ("fill_l" + juce::String (i));
    pFxaSize = G ("fxa_size"); pFxaDamp = G ("fxa_damp"); pFxaWidth = G ("fxa_width"); pFxaRet = G ("fxa_ret");
    pFxbTime = G ("fxb_time"); pFxbFb = G ("fxb_fb"); pFxbTone = G ("fxb_tone"); pFxbPp = G ("fxb_pp"); pFxbRet = G ("fxb_ret");
    pDuckSrc = G ("duck_src"); pDuckAmt = G ("duck_amt"); pDuckRel = G ("duck_rel"); pDuckFx = G ("duck_fx");
    pMtAtt = G ("m_tatt"); pMtSus = G ("m_tsus"); pMdMode = G ("m_dmode"); pMdThr = G ("m_dthr"); pMdRatio = G ("m_dratio");
    pMdAtt = G ("m_datt"); pMdRel = G ("m_drel"); pMdMix = G ("m_dmix");

    // Pattern de départ : un beat simple, pour entendre tout de suite
    auto put = [&] (int lane, std::initializer_list<int> st, int vel) { for (int s : st) { Step x; x.vel = vel; pattern.set (lane, s, x); } };
    put (0, { 0, 10 }, 110);
    put (1, { 4, 12 }, 110);
    put (4, { 0, 2, 4, 6, 8, 10, 12, 14 }, 90);

    diag::installCrashHandler();
    watchdog = std::make_unique<diag::Watchdog>();
    captureEngine = std::make_unique<CaptureEngine> (*this);
    stable = takeSnapshot();
    startTimerHz (25);
}

TyphonProcessor::~TyphonProcessor()
{
    stopTimer();
    captureEngine.reset();
    watchdog.reset();
    diag::removeCrashHandler();
}

void TyphonProcessor::applyApcActions()
{
    using namespace typhon::apc;
    Action a; bool commit = false;
    auto param = [&] (const Action& x) -> juce::RangedAudioParameter*
    {
        const int l = juce::jlimit (0, numLanes - 1, (int) x.lane);
        switch ((Target) x.target)
        {
            case tSpAmt: return apvts.getParameter ("sp_amt");
            case tSpBits: return apvts.getParameter ("sp_bits");
            case tSpRate: return apvts.getParameter ("sp_rate");
            case tSpFilt: return apvts.getParameter ("sp_filt");
            case tLaneCut: return apvts.getParameter (pid (l, "fcut"));
            case tLaneDecay: return apvts.getParameter (pid (l, "decay"));
            case tLanePitch: return apvts.getParameter (pid (l, "pitch"));
            case tSwing: return apvts.getParameter ("swing");
            case tLaneMute: return apvts.getParameter (pid (l, "mute"));
            case tChokeAll: return apvts.getParameter ("choke_all");
            case tSeqLen: return apvts.getParameter ("seq_len");
        }
        return nullptr;
    };
    while (apcCtl.queue.pop (a))
    {
        switch (a.act)
        {
            case Act::setParam:
                if (auto* p = param (a)) { p->beginChangeGesture(); p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, a.value)); p->endChangeGesture(); commit = true; }
                break;
            case Act::toggleParam:
                if (auto* p = param (a)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture(); }
                break;
            case Act::commit: commit = true; break;
            case Act::undo: undo(); commit = false; break;
            case Act::redo: redo(); commit = false; break;
            default: break;
        }
    }
    const int sp = apcCtl.spModeToggle.load();
    if (sp != apcSpSeen) { if ((sp - apcSpSeen) & 1) { settings.spMode.store (1 - settings.spMode.load()); settings.save(); } apcSpSeen = sp; }
    if (commit) undoCommit();
}

void TyphonProcessor::timerCallback()
{
    applyApcActions();
    // libère les anciens jeux de samples quand plus aucune voix ne les tient
    const juce::ScopedLock sl (graveLock);
    graveyard.erase (std::remove_if (graveyard.begin(), graveyard.end(),
                                     [] (const std::shared_ptr<SampleSet>& s) { return s.use_count() <= 1; }),
                     graveyard.end());
}

void TyphonProcessor::setLaneSet (int lane, std::shared_ptr<SampleSet> s)
{
    if (s == nullptr || lane < 0 || lane >= numLanes) return;
    auto old = std::atomic_exchange (&sets[(size_t) lane], s);
    const juce::ScopedLock sl (graveLock);
    if (old != nullptr) graveyard.push_back (old);
}

void TyphonProcessor::resetLaneToFallback (int lane)
{
    setLaneSet (lane, makeFallback (lane));
    const juce::ScopedLock sl (srcLock);
    laneSourceText[lane] = "Secours";
    laneSourcePath[lane] = {};
}

static std::shared_ptr<Sample> readSample (juce::AudioFormatManager& fm, const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr)   // fichier sans extension (ou mal nommé) : on essaie tous les formats sur son contenu
        if (auto in = f.createInputStream()) r.reset (fm.createReaderFor (std::unique_ptr<juce::InputStream> (in.release())));
    if (r == nullptr || r->lengthInSamples <= 0) return nullptr;
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
    juce::AudioBuffer<float> b ((int) std::max (1u, r->numChannels), n);
    r->read (&b, 0, n, 0, true, true);
    auto s = std::make_shared<Sample>();
    s->rate = r->sampleRate;
    s->l.assign (b.getReadPointer (0), b.getReadPointer (0) + n);
    if (b.getNumChannels() > 1) s->r.assign (b.getReadPointer (1), b.getReadPointer (1) + n);
    else s->r = s->l;
    return s;
}

bool TyphonProcessor::loadSampleFile (int lane, const juce::File& f)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto s = readSample (fm, f);
    if (s == nullptr) return false;
    auto set = std::make_shared<SampleSet>();
    set->layers.push_back ({ s });
    set->layerTopVel.push_back (127);
    set->name = f.getFileNameWithoutExtension().toStdString();
    setLaneSet (lane, set);
    const juce::ScopedLock sl (srcLock);
    laneSourceText[lane] = f.getFileNameWithoutExtension();
    laneSourcePath[lane] = f.getFullPathName();
    return true;
}

bool TyphonProcessor::loadKitFolder (const juce::File& folder)
{
    auto xml = juce::XmlDocument::parse (folder.getChildFile ("kit.xml"));
    if (xml == nullptr || ! xml->hasTagName ("TYPHON_KIT")) return false;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    bool any = false;
    for (auto* laneX : xml->getChildWithTagNameIterator ("LANE"))
    {
        const int lane = laneX->getIntAttribute ("index", -1);
        if (lane < 0 || lane >= numLanes) continue;
        auto set = std::make_shared<SampleSet>();
        for (auto* layX : laneX->getChildWithTagNameIterator ("LAYER"))
        {
            std::vector<std::shared_ptr<Sample>> layer;
            for (auto* fX : layX->getChildWithTagNameIterator ("FILE"))
                if (auto s = readSample (fm, folder.getChildFile (fX->getStringAttribute ("name")))) layer.push_back (s);
            if (! layer.empty())
            {
                set->layers.push_back (layer);
                set->layerTopVel.push_back (juce::jlimit (1, 127, layX->getIntAttribute ("top", 127)));
            }
        }
        if (set->empty()) continue;
        set->name = xml->getStringAttribute ("name").toStdString();
        setLaneSet (lane, set);
        any = true;
        const juce::ScopedLock sl (srcLock);
        laneSourceText[lane] = xml->getStringAttribute ("name");
        laneSourcePath[lane] = "kit:" + folder.getFullPathName();
    }
    if (any) { const juce::ScopedLock sl (srcLock); kitName = xml->getStringAttribute ("name", folder.getFileName()); }
    return any;
}

int TyphonProcessor::noteForLane (int lane) const
{
    return midiMaps[juce::jlimit (0, 2, settings.midiMap.load())][lane];
}

// ============================================================================
void TyphonProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    const int bs = std::max (32, samplesPerBlock);
    laneBuf.setSize (numLanes * 2, bs, false, true, true);
    fxA.setSize (2, bs, false, true, true);
    fxB.setSize (2, bs, false, true, true);
    duckGain.assign ((size_t) bs, 1.0f);
    dlyL.assign ((size_t) (sr * 4.0) + 8, 0.0f);
    dlyR.assign ((size_t) (sr * 4.0) + 8, 0.0f);
    dlyPos = 0; dlyToneL = dlyToneR = 0.0f;
    reverb.setSampleRate (sr);
    reverb.reset();
    masterTr.prepare (sr); masterDyn.prepare (sr);
    duckEnv = 10.0f;
    for (auto& L : lanes)
    {
        for (auto& v : L.voices) { v.active = false; v.keep.reset(); }
        for (auto& c : L.clouds) { c.active = false; c.keep.reset(); }
        L.tr.prepare (sr); L.dyn.prepare (sr); L.sp.prepare (sr); L.filt.prepare (sr);
    }
    // volume / pan lissés : on part de la valeur réelle (sinon le 1er bloc sortirait à plein volume)
    for (int i = 0; i < numLanes; ++i)
    {
        const float vol = lp[i].mute->load() > 0.5f ? 0.0f : dbToGain (lp[i].vol->load());
        const float pan = lp[i].pan->load();
        lanes[i].panL = vol * std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f;
        lanes[i].panR = vol * std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
    }
    lastPpqEnd = -1.0;
    lastBarIndex = -1;
    if (captureEngine != nullptr) captureEngine->setHostConfig (sampleRate, samplesPerBlock);
}

typhon::GrainParams TyphonProcessor::grainParams (int lane) const
{
    const auto& P = lp[lane];
    GrainParams g;
    g.pos = P.gpos->load(); g.sizeMs = P.gsize->load(); g.density = P.gdens->load(); g.spray = P.gspray->load();
    g.pitch = P.gpitch->load(); g.pitchRand = P.gprand->load(); g.stereo = P.gstereo->load(); g.speed = P.gspeed->load();
    g.durMs = P.gdur->load(); g.reverse = P.grev->load(); g.freeze = P.gfreeze->load() > 0.5f;
    return g;
}

void TyphonProcessor::trigger (int lane, int vel, int chokeMode, int64_t gateSamples, int step, int pitchOffset, bool stepRev)
{
    auto& P = lp[lane];
    auto set = std::atomic_load (&sets[(size_t) lane]);
    if (set == nullptr || set->empty() || vel <= 0) return;

    // verrous du pas (NaN = valeur du réglage)
    auto lk = [&] (int k) { return step >= 0 ? pattern.lock (lane, step, k) : std::numeric_limits<float>::quiet_NaN(); };
    auto& L = lanes[lane];
    L.ovCut = lk (lkCut); L.ovSendA = lk (lkSendA); L.ovSendB = lk (lkSendB);
    auto val = [&] (int k, std::atomic<float>* p) { const float v = lk (k); return std::isnan (v) ? p->load() : v; };

    const bool laneChoke = P.choke->load() > 0.5f;
    const bool doChoke = chokeMode == 1 ? true : (chokeMode == 2 ? false : laneChoke);
    const int fadeSamples = (int) (P.fade->load() * 0.001 * sr);
    if (doChoke)
    {
        const int grp = (int) P.group->load();
        for (int o = 0; o < numLanes; ++o)
        {
            const bool same = (o == lane) || pChokeAll->load() > 0.5f || (grp != 0 && (int) lp[o].group->load() == grp);
            if (! same) continue;
            const int f = o == lane ? fadeSamples : (int) (lp[o].fade->load() * 0.001 * sr);
            for (auto& v : lanes[o].voices) v.choke (f);
            for (auto& c : lanes[o].clouds) c.choke (f);
        }
    }

    int variant = (int) P.var->load() - 1;
    if (step >= 0) { const Step st = pattern.get (lane, step); if (st.var > 0) variant = st.var - 1; }
    const Sample* smp = set->pick (vel, L.rr, variant);
    if (smp == nullptr || smp->length() < 2) return;

    const float vs = P.vsens->load();
    const float vv = (float) vel / 127.0f;
    const float gain = ((1.0f - vs) + vs * vv * vv) * dbToGain (std::isnan (lk (lkVol)) ? 0.0f : lk (lkVol) - P.vol->load());
    const float pan = std::isnan (lk (lkPan)) ? 0.0f : lk (lkPan);   // verrou de pan : relatif au pan de la piste
    const float gL = std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f, gR = std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;

    if (P.gran->load() > 0.5f)
    {
        GrainCloud* c = &L.clouds[0];
        if (L.clouds[0].active && ! L.clouds[1].active) c = &L.clouds[1];
        else if (L.clouds[0].active && L.clouds[1].active) c = L.clouds[0].age > L.clouds[1].age ? &L.clouds[0] : &L.clouds[1];
        c->pitchOffset = P.pitch->load() + (float) pitchOffset;
        c->rng = (uint32_t) (hitCounter[lane].load() * 2654435761u + 17u);
        c->invertRev = stepRev != (P.rev->load() > 0.5f);
        c->start (smp, set, sr, grainParams (lane), gain, lk (lkGpos));
        c->gL = gL; c->gR = gR;
        c->gateLeft = gateSamples; c->gateFadeSamples = std::max (16, fadeSamples);
        for (auto& o : L.clouds) if (&o != c && o.active) ++o.age;
        hitCounter[lane].fetch_add (1);
        return;
    }

    Voice* slot = nullptr;
    for (auto& v : L.voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        slot = &L.voices[0];
        for (auto& v : L.voices) if (v.age > slot->age) slot = &v;
    }
    const double len = smp->length();
    double s0 = juce::jlimit (0.0, len - 2.0, (double) val (lkStart, P.start) * len);
    double s1 = juce::jlimit (s0 + 1.0, len, (double) P.end->load() * len);

    Voice& v = *slot;
    v.keep = set;
    v.smp = smp;
    v.reverse = (P.rev->load() > 0.5f) != stepRev;
    v.startPos = s0; v.endPos = s1;
    v.pos = v.reverse ? s1 - 1.0 : s0;
    v.inc = smp->rate / sr * std::pow (2.0, (P.pitch->load() + (float) pitchOffset) / 12.0);
    v.gain = gain; v.gL = gL; v.gR = gR;
    v.fade = 1.0f; v.fadeStep = 0.0f;
    v.env = 1.0f;
    const float dec = val (lkDecay, P.decay);
    v.envMul = dec >= 0.999f ? 1.0f : (float) std::exp (-1.0 / (0.015 * std::pow (150.0, (double) dec) * sr));
    v.gateLeft = gateSamples;
    v.gateFadeSamples = std::max (16, fadeSamples);
    v.age = 0;
    v.active = true;
    for (auto& o : L.voices) if (&o != &v && o.active) ++o.age;
    hitCounter[lane].fetch_add (1);
}

void TyphonProcessor::renderLanes (int from, int to)
{
    const int n = to - from;
    if (n <= 0) return;
    for (int i = 0; i < numLanes; ++i)
    {
        float* l = laneBuf.getWritePointer (i * 2) + from;
        float* r = laneBuf.getWritePointer (i * 2 + 1) + from;
        for (auto& v : lanes[i].voices) if (v.active) v.render (l, r, n, curQuality);
        bool anyCloud = false;
        for (auto& c : lanes[i].clouds) anyCloud |= c.active;
        if (anyCloud)
        {
            const auto gp = grainParams (i);
            for (auto& c : lanes[i].clouds) if (c.active) c.render (l, r, n, gp, curQuality);
        }
    }
}

void TyphonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    diag::AudioScope diagScope;
    diag::audio ("debut du bloc");
    const int n = buffer.getNumSamples();
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (laneBuf.getNumSamples() < n) laneBuf.setSize (numLanes * 2, n, false, true, true);
    if (fxA.getNumSamples() < n) { fxA.setSize (2, n, false, true, true); fxB.setSize (2, n, false, true, true); }
    if ((int) duckGain.size() < n) duckGain.resize ((size_t) n, 1.0f);
    laneBuf.clear(); fxA.clear(); fxB.clear();

    curQuality = isNonRealtime() && settings.renderQuality.load() == 1 ? 2 : settings.quality.load();

    Ev* evs = evBuf.data(); int nev = 0;   // tampon préalloué (pas de gros tableau sur la pile du fil audio)
    auto addEv = [&] (int off, int lane, int vel, int ch, int64_t gate, int step = -1, int pitch = 0, bool rev = false)
    {
        if (nev < 768) evs[nev++] = { juce::jlimit (0, n - 1, off), lane, vel, ch, gate, step, pitch, rev };
    };

    // 1) MIDI
    const int chan = settings.midiChannel.load();
    const int map = juce::jlimit (0, 2, settings.midiMap.load());
    const bool apcOn = settings.apcOn.load() == 1;
    apcOut.numHits = 0; apcOut.cutAll = false;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        if (apcOn && msg.getChannel() == typhon::apc::channel)
        {
            if (msg.isNoteOn()) apcCtl.noteOn (msg.getNoteNumber(), msg.getVelocity(), m.samplePosition, apcPattern, seqLength(), apcOut);
            else if (msg.isNoteOff()) apcCtl.noteOff (msg.getNoteNumber());
            else if (msg.isAftertouch()) apcCtl.aftertouch (msg.getNoteNumber(), msg.getAfterTouchValue());
            else if (msg.isController()) apcCtl.controller (msg.getControllerNumber(), msg.getControllerValue());
            continue;
        }
        if (chan != 0 && msg.getChannel() != chan) continue;
        if (msg.isNoteOn() && msg.getNoteNumber() == cutAllNote) { addEv (m.samplePosition, -1, 0, 0, -1); continue; }
        if (msg.isNoteOff())
        {
            for (int l = 0; l < numLanes; ++l)
                if (midiMaps[map][l] == msg.getNoteNumber() && lp[l].gate->load() > 0.5f) addEv (m.samplePosition, l, -1, 0, -1);
            continue;
        }
        if (! msg.isNoteOn()) continue;
        for (int l = 0; l < numLanes; ++l)
            if (midiMaps[map][l] == msg.getNoteNumber()) addEv (m.samplePosition, l, msg.getVelocity(), 0, -1);
    }
    if (apcOut.cutAll) addEv (0, -1, 0, 0, -1);
    for (int k = 0; k < apcOut.numHits; ++k) addEv (apcOut.hits[k].offset, apcOut.hits[k].lane, apcOut.hits[k].vel, 0, -1);

    // Page CAPTURE ouverte : EZdrummer tourne en continu (son moteur reste actif), s'entend et reçoit les notes
    if (captureEngine != nullptr && captureEngine->monitor.load())
    {
        auto main = getBusBuffer (buffer, false, 0);
        captureEngine->processMonitor (main, midi, n);
        midi.clear();
        return;
    }
    midi.clear();

    // 2) Écoute (clic sur le nom d'une piste)
    const int aud = auditionMask.exchange (0);
    for (int l = 0; l < numLanes; ++l) if (aud & (1 << l)) addEv (0, l, 100, 0, -1);

    diag::audio ("sequenceur");
    // 3) Séquenceur, calé sur la tête de lecture de l'hôte
    bool playing = false; double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p; else playing = false;
            if (auto b = pos->getBpm()) bpm = *b;
        }
    // garde-fous : une tête de lecture incohérente ne doit jamais bloquer le fil audio
    if (! std::isfinite (ppq) || ! std::isfinite (bpm) || bpm < 1.0 || bpm > 2000.0 || std::fabs (ppq) > 1.0e9) { playing = false; bpm = 120.0; ppq = 0.0; }
    isPlayingNow.store (playing);
    lastBpm.store (bpm);
    // Arrêt de la lecture (barre d'espace) : tous les sons s'arrêtent avec un fondu court (les queues d'effets continuent)
    if (wasPlaying && ! playing)
        for (int o = 0; o < numLanes; ++o)
        {
            const int f = std::max ((int) (0.008 * sr), (int) (lp[o].fade->load() * 0.001 * sr));
            for (auto& v : lanes[o].voices) v.choke (f);
            for (auto& c : lanes[o].clouds) c.choke (f);
        }
    wasPlaying = playing;
    bool fillOnNow = false;
    if (playing && bpm > 1.0)
    {
        const double ppqPerSample = bpm / 60.0 / sr;
        const double p0 = ppq, p1 = ppq + n * ppqPerSample;
        const double swing = pSwing->load();
        const int len = seqLength();
        const double loopPpq = len * 0.25;
        const bool manualFill = pFill->load() > 0.5f || (apcOn && apcCtl.fillHeld.load());
        const int autoEvery = (int) pFillAuto->load() == 1 ? 4 : ((int) pFillAuto->load() == 2 ? 8 : 0);
        auto barIsFill = [&] (int64_t bar) { return manualFill || (autoEvery > 0 && bar >= 0 && (bar % autoEvery) == autoEvery - 1); };
        FillOptions fo;
        fo.intensity = pFillInt->load();
        const int fl = (int) pFillLen->load();
        fo.lenSteps = fl == 0 ? 4 : (fl == 1 ? 8 : 16);
        fo.grid = (int) pFillGrid->load();
        fo.othersMute = (int) pFillOthers->load() == 1;
        fo.laneMask = 0;
        for (int l = 0; l < numLanes; ++l) if (pFillLane[l]->load() > 0.5f) fo.laneMask |= (1u << l);

        const int64_t loopStart = (int64_t) std::floor (p0 / loopPpq);
        const int64_t loopEnd = std::min (loopStart + 2, (int64_t) std::floor (p1 / loopPpq));
        for (int64_t loop = loopStart - 1; loop <= loopEnd; ++loop)
            for (int st = 0; st < len; ++st)
            {
                const double stepPpq = (double) loop * loopPpq + st * 0.25;
                const int64_t bar = (int64_t) std::floor (stepPpq / 4.0 + 1.0e-9);
                const int posInBar = st % 16;
                const bool fill = barIsFill (bar);
                for (int lane = 0; lane < numLanes; ++lane)
                {
                    HitList h; Step s; bool fromFill = false;
                    if (fill && fillHits (fo, lane, posInBar, h)) fromFill = true;
                    else { s = pattern.get (lane, st); if (! s.on()) continue; h = hitsOf (s); }
                    for (int k = 0; k < h.count; ++k)
                    {
                        const double t = stepPpq + (swingOffset (st, swing) + h.at[k]) * 0.25;
                        if (t < p0 || t >= p1) continue;
                        // durée : liaison = jusqu'au bout des pas fusionnés ; option « durée limitée au pas » = le son s'arrête à la fin de son pas
                        // (ou de sa part de pas pour un ratchet / un pas divisé, ou de son coup de fill)
                        int64_t gate = -1;
                        const bool limit = fromFill ? lp[lane].steplen->load() > 0.5f
                                                    : (s.dur == 1 || (s.dur == 0 && lp[lane].steplen->load() > 0.5f));
                        if (! fromFill && s.len > 1) gate = (int64_t) ((s.len * 0.25 - h.at[k] * 0.25) / ppqPerSample);
                        else if (limit)
                        {
                            const double next = k + 1 < h.count ? h.at[k + 1] : 1.0;
                            const double dur = fromFill ? (fo.grid == 0 ? 2.0 : (fo.grid == 1 ? 1.0 : (fo.grid == 2 ? 0.5 : 2.0 / 3.0))) : (next - h.at[k]);
                            gate = std::max<int64_t> (16, (int64_t) (dur * 0.25 / ppqPerSample));
                        }
                        addEv ((int) ((t - p0) / ppqPerSample), lane, h.vel[k], fromFill ? 0 : s.choke, gate,
                               fromFill ? -1 : st, fromFill ? 0 : s.pitch, ! fromFill && s.rev);
                    }
                }
                // crash au retour sur le 1 après un fill
                if (posInBar == 0 && pFillCrash->load() > 0.5f && barIsFill (bar - 1) && ! barIsFill (bar) && bar >= 1 && ! pattern.get (6, st).on())
                    if (stepPpq >= p0 && stepPpq < p1) addEv ((int) ((stepPpq - p0) / ppqPerSample), 6, 110, 0, -1);
            }
        fillOnNow = barIsFill ((int64_t) std::floor (p0 / 4.0));
        curStep.store ((int) std::floor (std::fmod (std::max (0.0, p0), loopPpq) / 0.25));
    }
    else curStep.store (-1);
    fillNow.store (fillOnNow);

    // ROLL de l'APC64 : calé sur la grille quand Ableton joue, horloge libre au tempo sinon
    if (apcOn)
    {
        apcOut.numHits = 0;
        apcCtl.renderRoll (playing, ppq, bpm / 60.0 / sr, n, apcOut);
        for (int k = 0; k < apcOut.numHits; ++k) addEv (apcOut.hits[k].offset, apcOut.hits[k].lane, apcOut.hits[k].vel, 0, -1);
    }

    diag::audio ("rendu des voix");
    // 4) Rendu des voix entre les événements (tri par insertion : stable, sans allocation)
    for (int a = 1; a < nev; ++a)
    {
        const Ev x = evs[a]; int k = a - 1;
        while (k >= 0 && evs[k].offset > x.offset) { evs[k + 1] = evs[k]; --k; }
        evs[k + 1] = x;
    }
    const int duckSrc = (int) pDuckSrc->load();
    const float duckAmt = pDuckAmt->load();
    const float duckRelS = std::max (5.0f, pDuckRel->load()) * 0.001f, duckAttS = 0.003f;
    int* duckOffsets = duckBuf.data(); int nDuck = 0;
    int cursor = 0;
    for (int e = 0; e < nev; ++e)
    {
        renderLanes (cursor, evs[e].offset);
        cursor = std::max (cursor, evs[e].offset);
        const auto& ev = evs[e];
        if (ev.lane < 0)
        {
            for (int o = 0; o < numLanes; ++o)
            {
                const int f = (int) (lp[o].fade->load() * 0.001 * sr);
                for (auto& v : lanes[o].voices) v.choke (f);
                for (auto& c : lanes[o].clouds) c.choke (f);
            }
        }
        else if (ev.vel < 0)
        {
            const int f = (int) (lp[ev.lane].fade->load() * 0.001 * sr);
            for (auto& v : lanes[ev.lane].voices) v.choke (f);
            for (auto& c : lanes[ev.lane].clouds) c.choke (f);
        }
        else if (lp[ev.lane].mute->load() < 0.5f)
        {
            trigger (ev.lane, ev.vel, ev.choke, ev.gate, ev.step, ev.pitch, ev.rev);
            if (ev.lane == duckSrc && nDuck < 128) duckOffsets[nDuck++] = ev.offset;
        }
    }
    renderLanes (cursor, n);

    // 5) Ducking : courbe de gain déclenchée par les coups de la piste source
    {
        int di = 0;
        for (int s = 0; s < n; ++s)
        {
            while (di < nDuck && duckOffsets[di] <= s) { duckEnv = 0.0f; ++di; }   // duckEnv = temps depuis le coup (s)
            float shape = 0.0f;
            if (duckEnv < duckAttS) shape = duckEnv / duckAttS;                                   // 3 ms de descente (pas de clic)
            else if (duckEnv < duckAttS + duckRelS) { const float x = 1.0f - (duckEnv - duckAttS) / duckRelS; shape = x * x; }  // remontée
            duckGain[(size_t) s] = 1.0f - duckAmt * shape;
            duckEnv += (float) (1.0 / sr);
        }
    }

    diag::audio ("chaine des pistes");
    // 6) Chaîne par piste : filtre -> transient -> dynamique -> grain SP -> ducking -> volume / pan -> envois -> sortie
    const float spAmt = pSpAmt->load(), spBits = pSpBits->load(), spRate = pSpRate->load(), spFilt = pSpFilt->load();
    const int spMode = settings.spMode.load();
    const float master = dbToGain (pMaster->load());
    const bool multi = settings.multiOut.load() == 1;
    auto mainBus = getBusBuffer (buffer, false, 0);

    for (int i = 0; i < numLanes; ++i)
    {
        auto& P = lp[i]; auto& L = lanes[i];
        float* l = laneBuf.getWritePointer (i * 2);
        float* r = laneBuf.getWritePointer (i * 2 + 1);

        const int ftype = (int) P.ftype->load();
        const float cut = std::isnan (L.ovCut) ? P.fcut->load() : L.ovCut;
        const bool filterOn = (ftype == 0 && cut < 19900.0f) || (ftype == 1 && cut > 21.0f) || ftype == 2 || P.fres->load() > 0.75f;
        if (filterOn) { L.filt.set (cut, P.fres->load()); L.filt.process (l, r, n, ftype); }
        L.tr.process (l, r, n, P.tatt->load(), P.tsus->load());
        L.dyn.process (l, r, n, (int) P.dmode->load(), P.dthr->load(), P.dratio->load(), P.datt->load(), P.drel->load(), P.dmix->load());
        L.sp.process (l, r, n, spAmt, spBits, spRate, spFilt, spMode);

        const bool ducked = duckAmt > 0.0001f && i != duckSrc && P.duck->load() > 0.5f;
        const float vol = P.mute->load() > 0.5f ? 0.0f : dbToGain (P.vol->load());
        const float pan = P.pan->load();
        const float tl = vol * std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f;
        const float tr = vol * std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
        const float dl = (tl - L.panL) / (float) n, dr = (tr - L.panR) / (float) n;
        float peak = 0.0f;
        for (int s = 0; s < n; ++s)
        {
            L.panL += dl; L.panR += dr;
            const float dg = ducked ? duckGain[(size_t) s] : 1.0f;
            l[s] *= L.panL * dg; r[s] *= L.panR * dg;
            if (! std::isfinite (l[s]) || ! std::isfinite (r[s])) { l[s] = r[s] = 0.0f; L.tr.reset(); L.dyn.reset(); L.sp.reset(); L.filt.reset(); }
            peak = std::max (peak, std::max (std::fabs (l[s]), std::fabs (r[s])));
        }
        L.panL = tl; L.panR = tr;
        meters[i].store (std::max (peak, meters[i].load() * 0.85f));

        // envois vers les bus d'effets (après volume)
        const float sa = std::isnan (L.ovSendA) ? P.sendA->load() : L.ovSendA;
        const float sb = std::isnan (L.ovSendB) ? P.sendB->load() : L.ovSendB;
        if (sa > 0.0001f) { fxA.addFrom (0, 0, l, n, sa); fxA.addFrom (1, 0, r, n, sa); }
        if (sb > 0.0001f) { fxB.addFrom (0, 0, l, n, sb); fxB.addFrom (1, 0, r, n, sb); }

        int busIdx = multi ? (int) P.out->load() : 0;
        if (busIdx < 0 || busIdx >= getBusCount (false)) busIdx = 0;
        auto* bus = getBus (false, busIdx);
        if (busIdx != 0 && (bus == nullptr || ! bus->isEnabled())) busIdx = 0;
        auto out = busIdx == 0 ? mainBus : getBusBuffer (buffer, false, busIdx);
        if (out.getNumChannels() < 2) out = mainBus;
        out.addFrom (0, 0, l, n);
        out.addFrom (1, 0, r, n);
    }

    diag::audio ("reverbe");
    // 7) Bus A : réverbe
    const float retA = dbToGain (pFxaRet->load()), retB = dbToGain (pFxbRet->load());
    const bool duckFx = pDuckFx->load() > 0.5f && duckAmt > 0.0001f;
    if (fxA.getMagnitude (0, n) > 1.0e-7f || fxA.getMagnitude (1, n) > 1.0e-7f || reverbTail > 0)
    {
        if (fxA.getMagnitude (0, n) > 1.0e-7f || fxA.getMagnitude (1, n) > 1.0e-7f) reverbTail = (int) (sr * 12.0);
        else reverbTail -= n;
        juce::Reverb::Parameters rp;
        rp.roomSize = pFxaSize->load(); rp.damping = pFxaDamp->load(); rp.width = pFxaWidth->load();
        rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.freezeMode = 0.0f;
        reverb.setParameters (rp);
        reverb.processStereo (fxA.getWritePointer (0), fxA.getWritePointer (1), n);
        for (int s = 0; s < n; ++s)
        {
            const float g = retA * (duckFx ? duckGain[(size_t) s] : 1.0f);
            mainBus.getWritePointer (0)[s] += fxA.getSample (0, s) * g;
            mainBus.getWritePointer (1)[s] += fxA.getSample (1, s) * g;
        }
    }

    // écoute d'un son de la bibliothèque
    if (previewTrigger.exchange (false))
    {
        auto ps = std::atomic_load (&previewSet);
        if (ps != nullptr && ! ps->empty())
        {
            uint32_t rr = 0;
            previewVoice.keep = ps; previewVoice.smp = ps->pick (100, rr, 0);
            if (previewVoice.smp != nullptr)
            {
                previewVoice.pos = 0; previewVoice.startPos = 0; previewVoice.endPos = previewVoice.smp->length();
                previewVoice.inc = previewVoice.smp->rate / sr; previewVoice.reverse = false;
                previewVoice.gain = 0.8f; previewVoice.gL = previewVoice.gR = 1.0f; previewVoice.fade = 1.0f; previewVoice.fadeStep = 0.0f;
                previewVoice.env = 1.0f; previewVoice.envMul = 1.0f; previewVoice.gateLeft = -1; previewVoice.active = true;
            }
        }
    }
    if (previewVoice.active) previewVoice.render (mainBus.getWritePointer (0), mainBus.getWritePointer (1), n, curQuality);

    diag::audio ("delay");
    // 8) Bus B : delay calé sur le tempo (ping-pong possible, tonalité dans la boucle)
    {
        static const double beats[6] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0 };
        const double dSec = beats[juce::jlimit (0, 5, (int) pFxbTime->load())] * 60.0 / std::max (20.0, bpm);
        const int dSamp = juce::jlimit (1, (int) dlyL.size() - 2, (int) (dSec * sr));
        const float fb = juce::jlimit (0.0f, 0.95f, pFxbFb->load());
        const float toneC = 0.05f + 0.9f * pFxbTone->load();
        const bool pp = pFxbPp->load() > 0.5f;
        const int N = (int) dlyL.size();
        const bool hasInput = fxB.getMagnitude (0, n) > 1.0e-7f || fxB.getMagnitude (1, n) > 1.0e-7f;
        if (hasInput) delayTail = (int) (sr * 20.0); else delayTail -= n;
        if (delayTail > 0)
            for (int s = 0; s < n; ++s)
            {
                int rp = dlyPos - dSamp; if (rp < 0) rp += N;
                const float yl = dlyL[(size_t) rp], yr = dlyR[(size_t) rp];
                dlyToneL += toneC * (yl - dlyToneL); dlyToneR += toneC * (yr - dlyToneR);
                const float inL = fxB.getSample (0, s), inR = fxB.getSample (1, s);
                if (pp) { dlyL[(size_t) dlyPos] = 0.5f * (inL + inR) + fb * dlyToneR; dlyR[(size_t) dlyPos] = fb * dlyToneL; }
                else    { dlyL[(size_t) dlyPos] = inL + fb * dlyToneL; dlyR[(size_t) dlyPos] = inR + fb * dlyToneR; }
                if (++dlyPos >= N) dlyPos = 0;
                const float g = retB * (duckFx ? duckGain[(size_t) s] : 1.0f);
                mainBus.getWritePointer (0)[s] += yl * g;
                mainBus.getWritePointer (1)[s] += yr * g;
            }
    }

    diag::audio ("master");
    // 9) Master : transient -> dynamique (sortie principale), puis volume master sur toutes les sorties
    masterTr.process (mainBus.getWritePointer (0), mainBus.getWritePointer (1), n, pMtAtt->load(), pMtSus->load());
    masterDyn.process (mainBus.getWritePointer (0), mainBus.getWritePointer (1), n, (int) pMdMode->load(), pMdThr->load(),
                       pMdRatio->load(), pMdAtt->load(), pMdRel->load(), pMdMix->load());
    float mpeak = 0.0f;
    for (int c = 0; c < buffer.getNumChannels(); ++c)
    {
        float* d = buffer.getWritePointer (c);
        for (int s = 0; s < n; ++s)
        {
            float x = d[s] * master;
            // garde-fou : au-delà de +6 dBFS, saturation douce (protège les oreilles si un réglage s'emballe)
            const float a = std::fabs (x);
            if (a > 2.0f) x = std::copysign (2.0f + std::tanh (a - 2.0f), x);
            if (! std::isfinite (x)) x = 0.0f;
            d[s] = x;
            if (c < 2) mpeak = std::max (mpeak, std::fabs (x));
        }
    }
    masterMeter.store (std::max (mpeak, masterMeter.load() * 0.85f));
}

// ============================================================================
//  Slice : découpe le sample d'une piste et répartit les coups sur les pistes
// ============================================================================
int TyphonProcessor::sliceLane (int lane, float sensitivity, int maxSlices, bool writePattern)
{
    auto set = laneSet (lane);
    if (set == nullptr || set->empty()) return 0;
    const Sample* src = set->display ((int) lp[lane].var->load() - 1);
    if (src == nullptr) return 0;
    auto starts = detectSlices (*src, sensitivity, juce::jlimit (2, numLanes, maxSlices));
    const int count = std::min ((int) starts.size(), numLanes);
    if (count < 1) return 0;

    auto folder = libraryFolder().getParentDirectory().getChildFile ("Slices");
    folder.createDirectory();
    const auto base = juce::File::createLegalFileName (laneSource (lane)) + "_" + juce::String (juce::Time::currentTimeMillis() % 100000);
    juce::WavAudioFormat wav;
    const double secsPerStep = 60.0 / std::max (20.0, lastBpm.load()) / 4.0;
    if (writePattern) for (int l = 0; l < numLanes; ++l) for (int s = 0; s < numSteps; ++s) { pattern.set (l, s, Step()); pattern.clearLocks (l, s); }

    for (int k = 0; k < count; ++k)
    {
        const int a = starts[(size_t) k];
        const int b = k + 1 < (int) starts.size() ? starts[(size_t) k + 1] : src->length();
        if (b - a < 32) continue;
        juce::AudioBuffer<float> buf (2, b - a);
        buf.copyFrom (0, 0, src->l.data() + a, b - a);
        buf.copyFrom (1, 0, (src->r.empty() ? src->l : src->r).data() + a, b - a);
        const int fade = std::min (b - a, (int) (0.003 * src->rate));
        buf.applyGainRamp (b - a - fade, fade, 1.0f, 0.0f);
        auto f = folder.getChildFile (base + "_" + juce::String (k + 1) + ".wav");
        {
            std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
            if (os == nullptr) continue;
            std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), src->rate, 2, 24, {}, 0));
            if (w == nullptr) continue;
            os.release();
            w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }
        loadSampleFile (k, f);
        { const juce::ScopedLock sl (srcLock); laneSourceText[k] = "Slice " + juce::String (k + 1); }
        if (writePattern)
        {
            const int step = (int) std::round ((a / src->rate) / secsPerStep);
            if (step >= 0 && step < seqLength()) { Step s; s.vel = 100; pattern.set (k, step, s); }
        }
    }
    { const juce::ScopedLock sl (srcLock); kitName = "Slices"; }
    return count;
}

// ============================================================================
//  Pattern -> fichier MIDI (glisser dans Ableton)
// ============================================================================
juce::File TyphonProcessor::writePatternMidi() const
{
    juce::MidiMessageSequence seq;
    const int tpq = 960;
    const double swing = pSwing->load();
    const int len = seqLength();
    for (int lane = 0; lane < numLanes; ++lane)
        for (int st = 0; st < len; ++st)
        {
            const Step s = pattern.get (lane, st);
            if (! s.on()) continue;
            const HitList h = hitsOf (s);
            for (int k = 0; k < h.count; ++k)
            {
                const double beat = (st + swingOffset (st, swing) + h.at[k]) * 0.25;
                const double lenBeats = s.len > 1 ? s.len * 0.25 - h.at[k] * 0.25 : 0.25 / h.count * 0.9;
                const int vel = h.vel[k];
                seq.addEvent (juce::MidiMessage::noteOn (10, noteForLane (lane), (juce::uint8) vel), beat * tpq);
                seq.addEvent (juce::MidiMessage::noteOff (10, noteForLane (lane)), (beat + lenBeats) * tpq);
            }
        }
    seq.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Typhon pattern.mid");
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (os.openedOk()) mf.writeTo (os);
    return f;
}

// ============================================================================
//  État du projet
// ============================================================================
void TyphonProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    juce::XmlElement root ("TYPHON_STATE");
    root.setAttribute ("version", 1);
    if (auto x = state.createXml()) root.addChildElement (x.release());

    auto* pat = root.createNewChildElement ("PATTERN");
    for (int l = 0; l < numLanes; ++l)
    {
        juce::StringArray a;
        for (int s = 0; s < numSteps; ++s) a.add (juce::String ((juce::int64) pattern.steps[l][s].load()));
        pat->setAttribute ("lane" + juce::String (l), a.joinIntoString (","));
        juce::StringArray lk;
        for (int s = 0; s < numSteps; ++s)
            for (int k = 0; k < numLocks; ++k)
                if (! std::isnan (pattern.lock (l, s, k))) lk.add (juce::String (s) + ":" + juce::String (k) + ":" + juce::String (pattern.lock (l, s, k), 6));
        pat->setAttribute ("locks" + juce::String (l), lk.joinIntoString (";"));
    }
    auto* src = root.createNewChildElement ("SOURCES");
    {
        const juce::ScopedLock sl (srcLock);
        src->setAttribute ("kitName", kitName);
        for (int l = 0; l < numLanes; ++l) src->setAttribute ("lane" + juce::String (l), laneSourcePath[l]);
    }
    auto* cap = root.createNewChildElement ("CAPTURE_NOTES");
    for (int l = 0; l < numLanes; ++l) cap->setAttribute ("lane" + juce::String (l), captureNotes[l].load());
    copyXmlToBinary (root, destData);
}

void TyphonProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName ("TYPHON_STATE")) return;
    if (auto* p = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*p));

    if (auto* pat = xml->getChildByName ("PATTERN"))
        for (int l = 0; l < numLanes; ++l)
        {
            juce::StringArray a; a.addTokens (pat->getStringAttribute ("lane" + juce::String (l)), ",", "");
            for (int s = 0; s < numSteps; ++s)
            {
                pattern.steps[l][s].store (s < a.size() ? (uint32_t) a[s].getLargeIntValue() : 0u);
                pattern.clearLocks (l, s);
            }
            juce::StringArray lk; lk.addTokens (pat->getStringAttribute ("locks" + juce::String (l)), ";", "");
            for (auto& e : lk)
            {
                juce::StringArray t; t.addTokens (e, ":", "");
                if (t.size() == 3) { const int s = t[0].getIntValue(), k = t[1].getIntValue(); if (s >= 0 && s < numSteps && k >= 0 && k < numLocks) pattern.setLock (l, s, k, t[2].getFloatValue()); }
            }
        }
    if (auto* cap = xml->getChildByName ("CAPTURE_NOTES"))
        for (int l = 0; l < numLanes; ++l) captureNotes[l].store (cap->getIntAttribute ("lane" + juce::String (l), captureNoteDefault[l]));

    if (auto* src = xml->getChildByName ("SOURCES"))
    {
        juce::StringArray kitsLoaded;
        for (int l = 0; l < numLanes; ++l)
        {
            const auto path = src->getStringAttribute ("lane" + juce::String (l));
            if (path.isEmpty()) { resetLaneToFallback (l); continue; }
            if (path.startsWith ("lib:"))
            {
                LibSound s;
                if (! decodeSound (path, s) || ! loadLibrarySound (l, s)) resetLaneToFallback (l);
                continue;
            }
            if (path.startsWith ("kit:"))
            {
                const auto folder = path.substring (4);
                if (! kitsLoaded.contains (folder)) { kitsLoaded.add (folder); loadKitFolder (juce::File (folder)); }
            }
            else if (! loadSampleFile (l, juce::File (path))) resetLaneToFallback (l);
        }
        const juce::ScopedLock sl (srcLock);
        kitName = src->getStringAttribute ("kitName", kitName);
    }
    undoStack.clear(); redoStack.clear();
    stable = takeSnapshot();
}

// ============================================================================
//  Annuler / rétablir
// ============================================================================
bool TyphonProcessor::Snapshot::operator== (const Snapshot& o) const
{
    if (steps != o.steps || params != o.params || locks.size() != o.locks.size()) return false;
    for (size_t i = 0; i < locks.size(); ++i)
    {
        const bool an = std::isnan (locks[i]), bn = std::isnan (o.locks[i]);
        if (an != bn || (! an && locks[i] != o.locks[i])) return false;
    }
    return true;
}

TyphonProcessor::Snapshot TyphonProcessor::takeSnapshot() const
{
    Snapshot s;
    s.steps.reserve ((size_t) numLanes * numSteps);
    s.locks.reserve ((size_t) numLanes * numSteps * numLocks);
    for (int l = 0; l < numLanes; ++l)
        for (int st = 0; st < numSteps; ++st)
        {
            s.steps.push_back (pattern.steps[l][st].load());
            for (int k = 0; k < numLocks; ++k) s.locks.push_back (pattern.lock (l, st, k));
        }
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            s.params.push_back (rp->getParameterID() == "fill" ? 0.0f : rp->getValue());
    return s;
}

void TyphonProcessor::applySnapshot (const Snapshot& s)
{
    size_t i = 0, j = 0;
    for (int l = 0; l < numLanes; ++l)
        for (int st = 0; st < numSteps; ++st)
        {
            if (i < s.steps.size()) pattern.steps[l][st].store (s.steps[i++]);
            for (int k = 0; k < numLocks; ++k) if (j < s.locks.size()) pattern.setLock (l, st, k, s.locks[j++]);
        }
    size_t p = 0;
    for (auto* prm : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            if (p < s.params.size() && rp->getParameterID() != "fill" && rp->getValue() != s.params[p])
            {
                rp->beginChangeGesture(); rp->setValueNotifyingHost (s.params[p]); rp->endChangeGesture();
            }
            ++p;
        }
}

void TyphonProcessor::undoCommit()
{
    auto now = takeSnapshot();
    if (stable.steps.empty()) { stable = std::move (now); return; }
    if (now == stable) return;
    undoStack.push_back (std::move (stable));
    if (undoStack.size() > 200) undoStack.erase (undoStack.begin());
    redoStack.clear();
    stable = std::move (now);
}

bool TyphonProcessor::undo()
{
    undoCommit();
    if (undoStack.empty()) return false;
    redoStack.push_back (stable);
    stable = undoStack.back(); undoStack.pop_back();
    applySnapshot (stable);
    return true;
}

bool TyphonProcessor::redo()
{
    undoCommit();
    if (redoStack.empty()) return false;
    undoStack.push_back (stable);
    stable = redoStack.back(); redoStack.pop_back();
    applySnapshot (stable);
    return true;
}

// ============================================================================
//  Bibliothèque de machines
// ============================================================================
void TyphonProcessor::rescanLibrary()
{
    auto path = settings.libraryPath();
    if (path.isEmpty())
    {
        auto guess = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ableton").getChildFile ("Alex Ball Free Sample Packs");
        if (guess.isDirectory()) { path = guess.getFullPathName(); settings.setLibraryPath (path); }
    }
    library = scanLibrary (juce::File (path));
}

static std::shared_ptr<SampleSet> loadSoundSet (const LibSound& s, juce::AudioFormatManager& fm)
{
    auto set = std::make_shared<SampleSet>();
    for (auto& v : s.variants)
    {
        std::vector<std::shared_ptr<Sample>> list;
        for (auto& f : v) if (auto smp = readSample (fm, f)) list.push_back (smp);
        if (! list.empty()) set->variants.push_back (list);
    }
    set->name = (s.machine.isNotEmpty() ? s.machine + " - " + s.name : s.name).toStdString();
    return set->variants.empty() ? nullptr : set;
}

bool TyphonProcessor::loadLibrarySound (int lane, const LibSound& s)
{
    if (lane < 0 || lane >= numLanes) return false;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto set = loadSoundSet (s, fm);
    if (set == nullptr) return false;
    setLaneSet (lane, set);
    if (auto* p = apvts.getParameter (pid (lane, "var"))) p->setValueNotifyingHost (p->convertTo0to1 (1.0f));
    const juce::ScopedLock sl (srcLock);
    laneSourceText[lane] = juce::String (set->name);
    laneSourcePath[lane] = encodeSound (s);
    return true;
}

void TyphonProcessor::loadMachineKit (const LibMachine& m)
{
    const auto pick = pickKit (m);
    for (int l = 0; l < numLanes; ++l)
        if (pick[(size_t) l] >= 0) loadLibrarySound (l, m.sounds[(size_t) pick[(size_t) l]]);
    const juce::ScopedLock sl (srcLock);
    kitName = m.name;
}

void TyphonProcessor::previewSound (const LibSound& s, int variant)
{
    if (variant < 0 || variant >= (int) s.variants.size() || s.variants[(size_t) variant].empty()) return;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    auto smp = readSample (fm, s.variants[(size_t) variant][0]);
    if (smp == nullptr) return;
    auto set = std::make_shared<SampleSet>();
    set->variants.push_back ({ smp });
    auto old = std::atomic_exchange (&previewSet, set);
    { const juce::ScopedLock sl (graveLock); if (old != nullptr) graveyard.push_back (old); }
    previewTrigger.store (true);
}

juce::AudioProcessorEditor* TyphonProcessor::createEditor() { return new TyphonEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TyphonProcessor(); }
