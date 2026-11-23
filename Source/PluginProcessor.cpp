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
                  P ("out"), P ("rev"), P ("tatt"), P ("tsus"), P ("dmode"), P ("dthr"), P ("dratio"), P ("datt"), P ("drel"), P ("dmix"), P ("mute"), P ("gate") };
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

    // Pattern de départ : un beat simple, pour entendre tout de suite
    auto put = [&] (int lane, std::initializer_list<int> st, int vel) { for (int s : st) { Step x; x.vel = vel; pattern.set (lane, s, x); } };
    put (0, { 0, 10 }, 110);
    put (1, { 4, 12 }, 110);
    put (4, { 0, 2, 4, 6, 8, 10, 12, 14 }, 90);

    captureEngine = std::make_unique<CaptureEngine> (*this);
    startTimerHz (4);
}

TyphonProcessor::~TyphonProcessor()
{
    stopTimer();
    captureEngine.reset();
}

void TyphonProcessor::timerCallback()
{
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
    laneBuf.setSize (numLanes * 2, std::max (32, samplesPerBlock), false, true, true);
    for (auto& L : lanes)
    {
        for (auto& v : L.voices) { v.active = false; v.keep.reset(); }
        L.tr.prepare (sr); L.dyn.prepare (sr); L.sp.prepare (sr);
    }
    lastPpqEnd = -1.0;
    lastBarIndex = -1;
    if (captureEngine != nullptr) captureEngine->setHostConfig (sampleRate, samplesPerBlock);
}

void TyphonProcessor::trigger (int lane, int vel, int chokeMode, int64_t gateSamples)
{
    auto& P = lp[lane];
    auto set = std::atomic_load (&sets[(size_t) lane]);
    if (set == nullptr || set->empty() || vel <= 0) return;

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
        }
    }

    auto& L = lanes[lane];
    Voice* slot = nullptr;
    for (auto& v : L.voices) if (! v.active) { slot = &v; break; }
    if (slot == nullptr)
    {
        slot = &L.voices[0];
        for (auto& v : L.voices) if (v.age > slot->age) slot = &v;
    }

    const Sample* smp = set->pick (vel, L.rr);
    if (smp == nullptr || smp->length() < 2) return;
    const double len = smp->length();
    double s0 = juce::jlimit (0.0, len - 2.0, (double) P.start->load() * len);
    double s1 = juce::jlimit (s0 + 1.0, len, (double) P.end->load() * len);

    Voice& v = *slot;
    v.keep = set;
    v.smp = smp;
    v.reverse = P.rev->load() > 0.5f;
    v.startPos = s0; v.endPos = s1;
    v.pos = v.reverse ? s1 - 1.0 : s0;
    v.inc = smp->rate / sr * std::pow (2.0, P.pitch->load() / 12.0);
    const float vs = P.vsens->load();
    const float vv = (float) vel / 127.0f;
    v.gain = (1.0f - vs) + vs * vv * vv;
    v.fade = 1.0f; v.fadeStep = 0.0f;
    v.env = 1.0f;
    const float dec = P.decay->load();
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
        for (auto& v : lanes[i].voices)
            if (v.active) v.render (laneBuf.getWritePointer (i * 2) + from, laneBuf.getWritePointer (i * 2 + 1) + from, n, curQuality);
}

void TyphonProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (laneBuf.getNumSamples() < n) laneBuf.setSize (numLanes * 2, n, false, true, true);
    laneBuf.clear();

    curQuality = isNonRealtime() && settings.renderQuality.load() == 1 ? 2 : settings.quality.load();

    struct Ev { int offset, lane, vel, choke; int64_t gate; };
    Ev evs[512]; int nev = 0;
    auto addEv = [&] (int off, int lane, int vel, int ch, int64_t gate) { if (nev < 512) evs[nev++] = { juce::jlimit (0, n - 1, off), lane, vel, ch, gate }; };

    // 1) MIDI (lu avant les réglages du bloc)
    const int chan = settings.midiChannel.load();
    const int map = juce::jlimit (0, 2, settings.midiMap.load());
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
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

    // 3) Séquenceur, calé sur la tête de lecture de l'hôte
    bool playing = false; double ppq = 0.0, bpm = 120.0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) ppq = *p; else playing = false;
            if (auto b = pos->getBpm()) bpm = *b;
        }
    isPlayingNow.store (playing);
    bool fillOnNow = false;
    if (playing && bpm > 1.0)
    {
        const double ppqPerSample = bpm / 60.0 / sr;
        const double p0 = ppq, p1 = ppq + n * ppqPerSample;
        const double swing = pSwing->load();
        const float intensity = pFillInt->load();
        const bool manualFill = pFill->load() > 0.5f;
        const int autoEvery = (int) pFillAuto->load() == 1 ? 4 : ((int) pFillAuto->load() == 2 ? 8 : 0);
        auto barIsFill = [&] (int64_t bar) { return manualFill || (autoEvery > 0 && bar >= 0 && (bar % autoEvery) == autoEvery - 1); };

        const int64_t barStart = (int64_t) std::floor (p0 / 4.0);
        const int64_t barEnd = (int64_t) std::floor (p1 / 4.0);
        for (int64_t bar = barStart - 1; bar <= barEnd; ++bar)
        {
            const bool fill = barIsFill (bar);
            for (int lane = 0; lane < numLanes; ++lane)
                for (int st = 0; st < numSteps; ++st)
                {
                    const Step s = fill ? fillStep (pattern, lane, st, intensity) : pattern.get (lane, st);
                    if (! s.on()) continue;
                    const HitList h = hitsOf (s);
                    for (int k = 0; k < h.count; ++k)
                    {
                        const double t = (double) bar * 4.0 + (st + swingOffset (st, swing) + h.at[k]) * 0.25;
                        if (t < p0 || t >= p1) continue;
                        const int64_t gate = s.len > 1 ? (int64_t) ((s.len * 0.25 - h.at[k] * 0.25) / ppqPerSample) : -1;
                        const int vel = (h.count > 1 && k > 0) ? std::max (1, s.vel - 10) : s.vel;
                        addEv ((int) ((t - p0) / ppqPerSample), lane, vel, s.choke, gate);
                    }
                }
            // crash au retour sur le 1 après un fill
            if (bar >= 0 && barIsFill (bar - 1) && ! barIsFill (bar) && intensity > 0.4f && ! pattern.get (6, 0).on())
            {
                const double t = (double) bar * 4.0;
                if (t >= p0 && t < p1) addEv ((int) ((t - p0) / ppqPerSample), 6, 110, 0, -1);
            }
        }
        fillOnNow = barIsFill ((int64_t) std::floor (p0 / 4.0));
        curStep.store ((int) std::floor (std::fmod (std::max (0.0, p0), 4.0) / 0.25));
    }
    else curStep.store (-1);
    fillNow.store (fillOnNow);

    // 4) Rendu des voix entre les événements
    for (int a = 1; a < nev; ++a)   // tri par insertion : stable, sans allocation
    {
        const Ev x = evs[a]; int k = a - 1;
        while (k >= 0 && evs[k].offset > x.offset) { evs[k + 1] = evs[k]; --k; }
        evs[k + 1] = x;
    }
    int cursor = 0;
    for (int e = 0; e < nev; ++e)
    {
        renderLanes (cursor, evs[e].offset);
        cursor = std::max (cursor, evs[e].offset);
        const auto& ev = evs[e];
        if (ev.lane < 0)            // note de coupure : coupe tout
        {
            for (int o = 0; o < numLanes; ++o) for (auto& v : lanes[o].voices) v.choke ((int) (lp[o].fade->load() * 0.001 * sr));
        }
        else if (ev.vel < 0)        // fin de note MIDI, piste en Gate MIDI
        {
            for (auto& v : lanes[ev.lane].voices) v.choke ((int) (lp[ev.lane].fade->load() * 0.001 * sr));
        }
        else if (lp[ev.lane].mute->load() < 0.5f) trigger (ev.lane, ev.vel, ev.choke, ev.gate);
    }
    renderLanes (cursor, n);

    // 5) Chaîne par piste : transient -> dynamique -> grain SP -> volume / pan -> sortie
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
        L.tr.process (l, r, n, P.tatt->load(), P.tsus->load());
        L.dyn.process (l, r, n, (int) P.dmode->load(), P.dthr->load(), P.dratio->load(), P.datt->load(), P.drel->load(), P.dmix->load());
        L.sp.process (l, r, n, spAmt, spBits, spRate, spFilt, spMode);

        const float vol = P.mute->load() > 0.5f ? 0.0f : dbToGain (P.vol->load()) * master;
        const float pan = P.pan->load();
        const float tl = vol * std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f;
        const float tr = vol * std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
        const float dl = (tl - L.panL) / (float) n, dr = (tr - L.panR) / (float) n;
        float peak = 0.0f;
        for (int s = 0; s < n; ++s)
        {
            L.panL += dl; L.panR += dr;
            l[s] *= L.panL; r[s] *= L.panR;
            // garde-fou : au-delà de +6 dBFS, saturation douce (protège les oreilles si un réglage s'emballe)
            auto guard = [] (float x) { const float a = std::fabs (x); return a <= 2.0f ? x : std::copysign (2.0f + std::tanh (a - 2.0f), x); };
            l[s] = guard (l[s]); r[s] = guard (r[s]);
            if (! std::isfinite (l[s]) || ! std::isfinite (r[s])) { l[s] = r[s] = 0.0f; L.tr.reset(); L.dyn.reset(); L.sp.reset(); }
            peak = std::max (peak, std::max (std::fabs (l[s]), std::fabs (r[s])));
        }
        L.panL = tl; L.panR = tr;
        meters[i].store (std::max (peak, meters[i].load() * 0.85f));

        int busIdx = multi ? (int) P.out->load() : 0;
        if (busIdx < 0 || busIdx >= getBusCount (false)) busIdx = 0;
        auto* bus = getBus (false, busIdx);
        if (busIdx != 0 && (bus == nullptr || ! bus->isEnabled())) busIdx = 0;
        auto out = busIdx == 0 ? mainBus : getBusBuffer (buffer, false, busIdx);
        if (out.getNumChannels() < 2) out = mainBus;
        out.addFrom (0, 0, l, n);
        out.addFrom (1, 0, r, n);
    }
}

// ============================================================================
//  Pattern -> fichier MIDI (glisser dans Ableton)
// ============================================================================
juce::File TyphonProcessor::writePatternMidi() const
{
    juce::MidiMessageSequence seq;
    const int tpq = 960;
    const double swing = pSwing->load();
    for (int lane = 0; lane < numLanes; ++lane)
        for (int st = 0; st < numSteps; ++st)
        {
            const Step s = pattern.get (lane, st);
            if (! s.on()) continue;
            const HitList h = hitsOf (s);
            for (int k = 0; k < h.count; ++k)
            {
                const double beat = (st + swingOffset (st, swing) + h.at[k]) * 0.25;
                const double lenBeats = s.len > 1 ? s.len * 0.25 - h.at[k] * 0.25 : 0.25 / h.count * 0.9;
                const int vel = (h.count > 1 && k > 0) ? std::max (1, s.vel - 10) : s.vel;
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
                pattern.steps[l][s].store (s < a.size() ? (uint32_t) a[s].getLargeIntValue() : 0u);
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
}

juce::AudioProcessorEditor* TyphonProcessor::createEditor() { return new TyphonEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TyphonProcessor(); }
