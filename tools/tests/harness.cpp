// Banc d'essai Linux du plugin complet (moteur + état + captures d'écran de l'interface).
// Voir tools/tests/LISEZMOI.txt pour la compilation.
#include <JuceHeader.h>
#include "../../Source/PluginProcessor.h"
#include "../../Source/PluginEditor.h"
#include <chrono>
#include <cstdio>

static int fails = 0;
#define CHECK(c, ...) do { std::printf ((c) ? "ok    : " : "ECHEC : "); std::printf (__VA_ARGS__); std::printf ("\n"); if (!(c)) ++fails; } while (0)

struct FakeHead : juce::AudioPlayHead
{
    double ppq = 0.0, bpm = 120.0; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setIsPlaying (playing); p.setPpqPosition (ppq); p.setBpm (bpm);
        return p;
    }
};

static uint32_t totalHits (TyphonProcessor& p, int lane) { return p.laneHitCounter (lane); }

struct RunResult { float peak = 0; bool finite = true; };
static RunResult run (TyphonProcessor& p, FakeHead& h, double seconds, juce::MidiBuffer* firstMidi = nullptr, int block = 512)
{
    RunResult r;
    const double sr = 48000.0;
    juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), block);
    const int blocks = (int) (seconds * sr / block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer mb;
        if (b == 0 && firstMidi != nullptr) mb = *firstMidi;
        p.processBlock (buf, mb);
        for (int c = 0; c < buf.getNumChannels(); ++c)
            for (int i = 0; i < block; ++i)
            {
                const float x = buf.getSample (c, i);
                if (! std::isfinite (x)) r.finite = false;
                r.peak = std::max (r.peak, std::fabs (x));
            }
        if (h.playing) h.ppq += block / sr * h.bpm / 60.0;
    }
    return r;
}

static void setParam (TyphonProcessor& p, const juce::String& id, float realValue)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (realValue));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String shotDir = argc > 1 ? argv[1] : "/tmp/typhon_shots";

    // --- 1) lecture du pattern de départ ---------------------------------
    {
        TyphonProcessor p; FakeHead h; p.setPlayHead (&h);
        p.prepareToPlay (48000.0, 512);
        auto r = run (p, h, 8.0);   // 4 mesures à 120 BPM
        CHECK (r.finite, "4 mesures : aucune valeur non finie");
        CHECK (r.peak > 0.1f && r.peak < 1.5f, "4 mesures : pic de sortie %.2f", r.peak);
        const auto k = totalHits (p, 0), s = totalHits (p, 1), hh = totalHits (p, 4);
        CHECK (k == 8 && s == 8 && hh == 32, "coups joués sur 4 mesures : kick %u (8), snare %u (8), hat %u (32)", k, s, hh);

        // swing : le moteur reste stable
        setParam (p, "swing", 0.66f);
        r = run (p, h, 4.0);
        CHECK (r.finite, "swing 66 %% : stable");

        // fill manuel, 1 mesure en 1/32 : snare et perc se partagent 32 coups par mesure
        const auto before = totalHits (p, 1) + totalHits (p, 7);
        setParam (p, "fill", 1.0f); setParam (p, "fill_int", 1.0f); setParam (p, "fill_len", 2.0f); setParam (p, "fill_grid", 2.0f);
        run (p, h, 4.0);
        const auto fillHits = totalHits (p, 1) + totalHits (p, 7) - before;
        setParam (p, "fill", 0.0f);
        CHECK (fillHits >= 60 && fillHits <= 68, "fill 1 mesure en 1/32 : %u coups snare + perc sur 2 mesures (64)", fillHits);

        // MIDI : C1 joue le kick, C0 coupe tout
        h.playing = false;
        const auto k0 = totalHits (p, 0);
        juce::MidiBuffer mb; mb.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 10);
        run (p, h, 0.05, &mb);
        CHECK (totalHits (p, 0) == k0 + 1, "MIDI : note C1 -> kick");
        juce::MidiBuffer crash; crash.addEvent (juce::MidiMessage::noteOn (1, 49, (juce::uint8) 120), 0);
        run (p, h, 0.2, &crash);
        juce::MidiBuffer cut; cut.addEvent (juce::MidiMessage::noteOn (1, 24, (juce::uint8) 100), 0);
        run (p, h, 0.06, &cut);
        auto tail = run (p, h, 0.2);
        CHECK (tail.peak < 1.0e-4f, "MIDI : C0 coupe le crash (reste %.6f)", tail.peak);

        // pas fusionnés + ratchet : un seul grand pas, coups répartis sur toute sa longueur
        {
            auto saved = p.pattern.get (2, 0);
            for (int st = 0; st < 32; ++st) p.pattern.set (2, st, typhon::Step());
            typhon::Step tr; tr.vel = 100; tr.len = 4; tr.ratchet = 2; p.pattern.set (2, 0, tr);
            h.playing = true; h.ppq = 0.0;
            const auto g0 = totalHits (p, 2);
            run (p, h, 0.2);                       // 120 BPM : 2e coup attendu à 0,25 s (milieu des 4 pas), pas à 0,0625 s
            const auto early = totalHits (p, 2) - g0;
            run (p, h, 0.3);
            const auto both = totalHits (p, 2) - g0;
            CHECK (early == 1 && both == 2, "pas fusionnés x4 + ratchet x2 : coups au pas 1 et au pas 3 (%u puis %u)", early, both);
            p.pattern.set (2, 0, saved); h.playing = false;
            run (p, h, 0.06, &cut); run (p, h, 0.2);
        }

        // gate MIDI : note courte = son court
        setParam (p, typhon::pid (6, "gate"), 1.0f);
        juce::MidiBuffer g; g.addEvent (juce::MidiMessage::noteOn (1, 49, (juce::uint8) 120), 0); g.addEvent (juce::MidiMessage::noteOff (1, 49), 2400);
        run (p, h, 0.05, &g);
        tail = run (p, h, 0.2);
        CHECK (tail.peak < 1.0e-4f, "Gate MIDI : le crash s'arrête avec la note (reste %.6f)", tail.peak);
        setParam (p, typhon::pid (6, "gate"), 0.0f);

        // choke global : un kick coupe le crash
        setParam (p, "choke_all", 1.0f);
        juce::MidiBuffer cc; cc.addEvent (juce::MidiMessage::noteOn (1, 49, (juce::uint8) 120), 0); cc.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 4800);
        run (p, h, 0.11, &cc);
        const float meterCrash = p.laneMeter (6);
        run (p, h, 0.8);
        CHECK (p.laneMeter (6) < 1.0e-3f, "choke global : le kick coupe le crash (crash %.4f -> %.6f)", meterCrash, p.laneMeter (6));
        setParam (p, "choke_all", 0.0f);
    }

    // --- 1b) v2 : 32 pas, pitch par pas, verrous, arrêt, ducking, effets, master, granulaire ---
    {
        TyphonProcessor p; FakeHead h; p.setPlayHead (&h);
        p.prepareToPlay (48000.0, 512);
        p.pattern.clear();
        setParam (p, "seq_len", 1.0f);
        typhon::Step s; s.vel = 100; p.pattern.set (3, 20, s);        // un seul coup, au pas 21
        run (p, h, 8.0);                                               // 4 mesures = 2 tours de 32 pas
        CHECK (p.laneHitCounter (3) == 2, "32 pas : le pas 21 joue une fois par tour (%u / 2)", p.laneHitCounter (3));
        setParam (p, "seq_len", 0.0f);
        run (p, h, 8.0);
        CHECK (p.laneHitCounter (3) == 2, "16 pas : les pas 17-32 ne jouent plus");

        // arrêt de la lecture : le crash s'arrête vite
        p.pattern.clear();
        typhon::Step c; c.vel = 120; p.pattern.set (6, 0, c);
        h.ppq = 3.99; run (p, h, 0.2);
        h.playing = false; run (p, h, 0.05);
        auto tail = run (p, h, 0.3);
        CHECK (tail.peak < 1.0e-4f, "arrêt de la lecture : le crash est coupé (reste %.6f)", tail.peak);
        h.playing = true;

        // verrou de volume : -30 dB sur un pas
        p.pattern.clear();
        typhon::Step k; k.vel = 127; p.pattern.set (0, 0, k); p.pattern.set (0, 8, k);
        p.pattern.setLock (0, 8, typhon::lkVol, -30.0f);
        float a = 0, b = 0;
        h.ppq = 0.0;
        for (int i = 0; i < 180; ++i)
        {
            juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), 512); juce::MidiBuffer mb;
            p.processBlock (buf, mb);
            const float m = buf.getMagnitude (0, 0, 512);
            if (h.ppq < 1.5) a = std::max (a, m); else if (h.ppq >= 2.0 && h.ppq < 3.5) b = std::max (b, m);
            h.ppq += 512.0 / 48000.0 * 2.0;
        }
        CHECK (b > 0.0f && juce::Decibels::gainToDecibels (b / a) < -24.0f, "verrou de volume : le pas 9 sonne %.1f dB plus bas", juce::Decibels::gainToDecibels (b / a));

        // pitch par pas : +12 = coup plus court (lu 2x plus vite)
        TyphonProcessor q; FakeHead hq; q.setPlayHead (&hq); q.prepareToPlay (48000.0, 512); q.pattern.clear();
        typhon::Step pp; pp.vel = 100; pp.pitch = 12; q.pattern.set (6, 0, pp);
        setParam (q, typhon::pid (6, "choke"), 0.0f);
        hq.ppq = 3.999;
        int64_t lenUp = 0; { juce::AudioBuffer<float> buf (2, 512); for (int i = 0; i < 175; ++i) { juce::MidiBuffer mb; q.processBlock (buf, mb); hq.ppq += 512.0 / 48000.0 * 2.0; if (buf.getMagnitude (0, 512) > 1.0e-4f) lenUp += 512; } }
        CHECK (lenUp > 30000 && lenUp < 55000, "pitch +12 : crash de 1,8 s joué en ~0,9 s (%.2f s)", lenUp / 48000.0);

        // ducking : le kick (muet, source) fait baisser le hat juste après chaque coup
        auto duckLevel = [] (float amt)
        {
            TyphonProcessor d; FakeHead hd; d.setPlayHead (&hd); d.pattern.clear();
            setParam (d, typhon::pid (0, "vol"), -60.0f);
            d.prepareToPlay (48000.0, 512);
            typhon::Step x; x.vel = 100; d.pattern.set (4, 1, x);   // hat 1/16 après le kick
            setParam (d, "duck_rel", 400.0f);
            typhon::Step kk; kk.vel = 127; d.pattern.set (0, 0, kk);
            setParam (d, "duck_amt", amt); setParam (d, typhon::pid (0, "vol"), -60.0f);
            float m = 0.0f; hd.ppq = 0.0;
            for (int i = 0; i < 30; ++i) { juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb; d.processBlock (buf, mb); hd.ppq += 512.0 / 48000.0 * 2.0; m = std::max (m, buf.getMagnitude (0, 512)); }
            return m;
        };
        const float dry = duckLevel (0.0f), ducked = duckLevel (1.0f);
        CHECK (ducked < dry * 0.7f, "ducking : hat 1/16 après le kick %.3f -> %.3f", dry, ducked);

        // effets : un envoi vers la réverbe laisse une queue
        TyphonProcessor e; FakeHead he; he.playing = false; e.setPlayHead (&he); e.prepareToPlay (48000.0, 512);
        setParam (e, typhon::pid (1, "sendA"), 1.0f);
        e.audition (1);
        run (e, he, 0.5);
        auto rt = run (e, he, 0.5);
        CHECK (rt.peak > 1.0e-3f && rt.finite, "réverbe : queue encore présente après 0,5 s (%.4f)", rt.peak);
        setParam (e, typhon::pid (1, "sendA"), 0.0f); setParam (e, typhon::pid (1, "sendB"), 1.0f);
        e.audition (1); run (e, he, 0.6);
        auto dt = run (e, he, 0.4);
        CHECK (dt.peak > 1.0e-3f && dt.finite, "delay : échos présents (%.4f)", dt.peak);

        // granulaire sur une piste
        TyphonProcessor g; FakeHead hg; hg.playing = false; g.setPlayHead (&hg); g.prepareToPlay (48000.0, 512);
        setParam (g, typhon::pid (6, "gran"), 1.0f);
        g.audition (6);
        auto gr = run (g, hg, 0.4);
        CHECK (gr.finite && gr.peak > 0.01f, "granulaire : la piste crash joue un nuage (pic %.2f)", gr.peak);

        // master : compression vers le bas à fond = niveau plus bas, stable
        TyphonProcessor mm; FakeHead hm; mm.setPlayHead (&hm); mm.prepareToPlay (48000.0, 512);
        auto r1 = run (mm, hm, 4.0);
        setParam (mm, "m_dratio", 20.0f); setParam (mm, "m_dthr", -40.0f);
        auto r2 = run (mm, hm, 4.0);
        CHECK (r2.finite && r2.peak < r1.peak, "master : compression à fond (pic %.2f -> %.2f)", r1.peak, r2.peak);
    }

    // --- 1c) durée limitée au pas, annuler / rétablir ------------------------
    {
        auto crashLen = [] (bool limit, int len)
        {
            TyphonProcessor q; FakeHead hq; q.setPlayHead (&hq); q.pattern.clear();
            if (limit) setParam (q, typhon::pid (6, "steplen"), 1.0f);
            q.prepareToPlay (48000.0, 512);
            typhon::Step c; c.vel = 110; c.len = len; q.pattern.set (6, 0, c);
            hq.ppq = 3.999; int64_t n = 0;
            juce::AudioBuffer<float> buf (2, 512);
            for (int i = 0; i < 175; ++i) { juce::MidiBuffer mb; q.processBlock (buf, mb); hq.ppq += 512.0 / 48000.0 * 2.0; if (buf.getMagnitude (0, 512) > 1.0e-4f) n += 512; }
            return n / 48000.0;
        };
        const double free1 = crashLen (false, 1), lim1 = crashLen (true, 1), lim3 = crashLen (true, 3);
        CHECK (free1 > 1.5 && lim1 < 0.16 && lim3 > 0.33 && lim3 < 0.42, "durée = pas : crash libre %.2f s, limité à 1 pas %.3f s, à 3 pas fusionnés %.3f s", free1, lim1, lim3);

        TyphonProcessor u;
        typhon::Step s1; s1.vel = 100;
        u.pattern.set (2, 3, s1); u.undoCommit();
        setParam (u, typhon::pid (2, "pitch"), 5.0f); u.undoCommit();
        u.pattern.set (2, 4, s1); u.undoCommit();
        u.undo();
        CHECK (! u.pattern.get (2, 4).on() && std::fabs (u.apvts.getRawParameterValue (typhon::pid (2, "pitch"))->load() - 5.0f) < 0.01f, "annuler : le dernier pas disparaît, le pitch reste");
        u.undo();
        CHECK (std::fabs (u.apvts.getRawParameterValue (typhon::pid (2, "pitch"))->load()) < 0.01f && u.pattern.get (2, 3).on(), "annuler x2 : le pitch revient à 0");
        u.redo(); u.redo();
        CHECK (u.pattern.get (2, 4).on() && ! u.canRedo(), "rétablir x2 : tout revient");
    }

    // --- 1d) bibliothèque de machines (dossier de test : getenv TY_LIB) ---------
    if (auto* libDir = std::getenv ("TY_LIB"))
    {
        TyphonProcessor p; FakeHead h; p.setPlayHead (&h);
        p.settings.setLibraryPath (libDir);
        p.rescanLibrary();
        CHECK (p.library.size() == 2, "bibliothèque : %d machines (2, dossier vide ignoré)", (int) p.library.size());
        const typhon::LibMachine* dr = nullptr; const typhon::LibMachine* lm = nullptr;
        for (auto& m : p.library) { if (m.name == "Boss DR-110") dr = &m; if (m.name == "Linn LM-1") lm = &m; }
        if (dr != nullptr && lm != nullptr)
        {
            juce::String roles;
            for (auto& sd : dr->sounds) roles << sd.name << "=" << sd.role << "(" << sd.totalFiles() << ") ";
            std::printf ("        DR-110 : %s\n", roles.toRawUTF8());
            const typhon::LibSound* bd = nullptr; for (auto& sd : dr->sounds) if (sd.name == "BD") bd = &sd;
            CHECK (bd != nullptr && bd->role == 0 && bd->variants.size() == 1 && bd->variants[0].size() == 3, "DR-110 BD : kick, 3 round robin (fichiers sans extension lus, doublon et ._ ignorés)");
            bool ohOk = false, chOk = false; for (auto& sd : dr->sounds) { if (sd.name == "OH") ohOk = sd.role == 5; if (sd.name == "CH") chOk = sd.role == 4; }
            CHECK (ohOk && chOk, "DR-110 : CH = hat fermé, OH = hat ouvert");
            juce::String r2; for (auto& sd : lm->sounds) r2 << sd.name << "=" << sd.role << "[" << (int) sd.variants.size() << "] ";
            std::printf ("        LM-1 : %s\n", r2.toRawUTF8());
            const typhon::LibSound* kick = nullptr; for (auto& sd : lm->sounds) if (sd.name == "Kick") kick = &sd;
            CHECK (kick != nullptr && kick->variants.size() == 5 && kick->role == 0, "LM-1 Kick 1..5 : un son, 5 variantes");
            p.loadMachineKit (*dr);
            p.prepareToPlay (48000.0, 512);
            auto rr = run (p, h, 4.0);
            CHECK (rr.finite && rr.peak > 0.05f, "kit DR-110 chargé et joué (pic %.2f), pistes : %s / %s", rr.peak, p.laneSource (0).toRawUTF8(), p.laneSource (4).toRawUTF8());
            // variante par pas : Kick LM-1 sur la piste 0, variante 5 au pas 1
            CHECK (p.loadLibrarySound (0, *kick), "LM-1 Kick chargé sur la piste Kick");
            CHECK (p.laneVariants (0) == 5, "piste Kick : 5 variantes");
            typhon::Step vs; vs.vel = 100; vs.var = 5; p.pattern.set (0, 0, vs);
            auto r3 = run (p, h, 2.0);
            CHECK (r3.finite, "variante par pas : lecture stable");
            juce::MemoryBlock mb; p.getStateInformation (mb);
            TyphonProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
            CHECK (q.laneVariants (0) == 5 && q.laneSource (0).contains ("Kick") && q.pattern.get (0, 0).var == 5, "état : son de machine et variante retrouvés (%s)", q.laneSource (0).toRawUTF8());
            p.previewSound (*kick, 2);
            auto r4 = run (p, h, 0.3);
            CHECK (r4.finite, "écoute d'un son de la bibliothèque");
            // Mes kits : enregistrer les 8 pistes, les recharger ailleurs
            const auto lbl4 = p.laneSource (4);
            auto kf = p.saveUserKit ("Test Harness Kit");
            CHECK (kf.getChildFile ("kit.xml").existsAsFile(), "Mes kits : kit enregistré (%s)", kf.getFullPathName().toRawUTF8());
            TyphonProcessor k2;
            CHECK (k2.loadKitFolder (kf) && k2.laneVariants (0) == 5 && k2.laneSource (0) == p.laneSource (0) && k2.laneSource (4) == lbl4,
                   "Mes kits : rechargé avec variantes et noms des pistes (%s / %s)", k2.laneSource (0).toRawUTF8(), k2.laneSource (4).toRawUTF8());
            kf.deleteRecursively();
            // stretch : rendu hors ligne, l'original est gardé
            setParam (p, typhon::pid (4, "strmode"), 1.0f); setParam (p, typhon::pid (4, "stramt"), 2.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (900);
            for (int w = 0; w < 40 && p.stretchRendering (4); ++w) juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
            auto len = [] (std::shared_ptr<typhon::SampleSet> x) { auto* d = x->display (0); return d ? d->length() : 0; };
            CHECK (std::abs (len (p.laneSet (4)) - 2 * len (p.baseSet (4))) < 2000, "stretch simple x2 sur la piste : %d -> %d échantillons", len (p.baseSet (4)), len (p.laneSet (4)));
            setParam (p, typhon::pid (4, "strmode"), 0.0f);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
            CHECK (p.laneSet (4) == p.baseSet (4), "stretch Off : retour à l'original");
        }
    }

    // --- 2) état du projet ---------------------------------------------------
    {
        TyphonProcessor a;
        typhon::Step s; s.vel = 127; s.ratchet = 3; s.len = 2; s.choke = 2; s.pitch = -5; s.rev = true; a.pattern.set (3, 29, s);
        a.pattern.setLock (3, 29, typhon::lkCut, 1234.0f);
        setParam (a, typhon::pid (2, "pitch"), -7.0f);
        setParam (a, "sp_amt", 0.4f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        TyphonProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto t = b.pattern.get (3, 29);
        CHECK (t.vel == 127 && t.ratchet == 3 && t.len == 2 && t.choke == 2 && t.pitch == -5 && t.rev, "état : pas 30 retrouvé à l'identique (pitch, reverse)");
        CHECK (std::fabs (b.pattern.lock (3, 29, typhon::lkCut) - 1234.0f) < 0.5f && ! b.pattern.hasLocks (3, 28), "état : verrou retrouvé");
        CHECK (std::fabs (b.apvts.getRawParameterValue (typhon::pid (2, "pitch"))->load() + 7.0f) < 0.01f, "état : réglage de piste retrouvé");
        CHECK (std::fabs (b.apvts.getRawParameterValue ("sp_amt")->load() - 0.4f) < 0.01f, "état : réglage global retrouvé");
    }

    // --- 3) multi-out : chaque piste sur sa sortie ---------------------------
    {
        TyphonProcessor p; FakeHead h; p.setPlayHead (&h);
        auto layout = p.getBusesLayout();
        for (auto& b : layout.outputBuses) b = juce::AudioChannelSet::stereo();
        CHECK (p.setBusesLayout (layout), "multi-out : 8 sorties stéréo acceptées");
        p.prepareToPlay (48000.0, 512);
        p.settings.multiOut.store (1);
        juce::AudioBuffer<float> buf (p.getTotalNumOutputChannels(), 512);
        float bus[8] = {};
        for (int bl = 0; bl < 200; ++bl)
        {
            juce::MidiBuffer mb; p.processBlock (buf, mb);
            for (int c = 0; c < buf.getNumChannels(); ++c) bus[c / 2] = std::max (bus[c / 2], buf.getMagnitude (c, 0, 512));
            h.ppq += 512.0 / 48000.0 * 2.0;
        }
        CHECK (bus[0] > 0.01f && bus[1] > 0.01f && bus[4] > 0.01f && bus[2] == 0.0f, "multi-out : kick %.2f sur 1/2, snare %.2f sur 3/4, hat %.2f sur 9/10, ghost muet %.2f", bus[0], bus[1], bus[4], bus[2]);
    }

    // --- 4) charge : pattern plein, ratchets x4, qualité Ultra ------------------
    {
        TyphonProcessor p; FakeHead h; p.setPlayHead (&h);
        p.prepareToPlay (48000.0, 512);
        p.settings.quality.store (2);
        for (int l = 0; l < typhon::numLanes; ++l)
            for (int j = 0; j < typhon::numSteps; ++j) { typhon::Step s; s.vel = 100; s.ratchet = 4; p.pattern.set (l, j, s); }
        for (int l = 0; l < typhon::numLanes; ++l)
        {
            setParam (p, typhon::pid (l, "choke"), 0.0f);
            setParam (p, typhon::pid (l, "tatt"), 6.0f);
            setParam (p, typhon::pid (l, "dratio"), 4.0f);
        }
        setParam (p, "sp_amt", 1.0f); setParam (p, "sp_filt", 0.5f);
        const auto t0 = std::chrono::steady_clock::now();
        auto r = run (p, h, 20.0);
        const double el = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (r.finite, "charge maximale : stable (pic %.2f)", r.peak);
        std::printf ("        charge maximale : %.1f %% d'un coeur (20 s rendues en %.2f s, build -O1)\n", el / 20.0 * 100.0, el);
    }

    // --- 5) captures d'écran de l'interface -------------------------------------
    {
        juce::File (shotDir).createDirectory();
        TyphonProcessor p;
        p.settings.uiScale.store (1);
        const char* names[4] = { "atelier", "nuit", "phosphore", "belair" };
        for (int th = 0; th < 4; ++th)
        {
            p.settings.theme.store (th);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            ed->setSize (1280, 800);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::PNGImageFormat png;
            juce::FileOutputStream os (juce::File (shotDir).getChildFile (juce::String ("seq_") + names[th] + ".png"));
            os.setPosition (0); os.truncate();
            png.writeImageToStream (img, os);
            if (th == 0)
            {
                // pages Capture et Paramètres, onglet dynamique
                auto* ui = ed->getChildComponent (0);
                std::function<juce::Component* (juce::Component*, const juce::String&)> findBtn = [&] (juce::Component* c, const juce::String& text) -> juce::Component*
                {
                    if (auto* b = dynamic_cast<juce::Button*> (c)) if (b->getButtonText() == text) return c;
                    for (auto* ch : c->getChildren()) if (auto* f = findBtn (ch, text)) return f;
                    return nullptr;
                };
                auto shot = [&] (const char* fn)
                {
                    auto im = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                    juce::FileOutputStream o (juce::File (shotDir).getChildFile (fn));
                    o.setPosition (0); o.truncate();
                    png.writeImageToStream (im, o);
                };
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "DYN"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("seq_dyn.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "GRAIN"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("seq_grain.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "SLICE"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("seq_slice.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "STRETCH"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("seq_stretch.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "SAMPLE"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "32 PAS"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("seq_32.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, juce::String::fromUTF8 ("\xe2\xa4\xa2 AGRANDIR")))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("editor.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "FERMER"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "MIX"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("mix.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "SONS"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (200); shot ("sons.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "CAPTURE"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("capture.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, juce::String::fromUTF8 ("PARAM\xc3\x88TRES")))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("settings.png"); }
            }
        }
        std::printf ("        captures d'écran dans %s\n", shotDir.toRawUTF8());
    }

    std::printf ("\n%s (%d échec%s)\n", fails == 0 ? "TOUT EST BON" : "PROBLÈMES", fails, fails > 1 ? "s" : "");
    return fails;
}
