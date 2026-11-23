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

        // fill manuel : la snare joue plus de coups
        const auto before = totalHits (p, 1);
        setParam (p, "fill", 1.0f); setParam (p, "fill_int", 1.0f);
        run (p, h, 2.0);
        const auto fillHits = totalHits (p, 1) - before;
        setParam (p, "fill", 0.0f);
        CHECK (fillHits > 6, "fill : %u coups de snare sur une mesure (2 sans fill)", fillHits);

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

    // --- 2) état du projet ---------------------------------------------------
    {
        TyphonProcessor a;
        typhon::Step s; s.vel = 127; s.ratchet = 3; s.len = 2; s.choke = 2; a.pattern.set (3, 5, s);
        setParam (a, typhon::pid (2, "pitch"), -7.0f);
        setParam (a, "sp_amt", 0.4f);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        TyphonProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto t = b.pattern.get (3, 5);
        CHECK (t.vel == 127 && t.ratchet == 3 && t.len == 2 && t.choke == 2, "état : pas retrouvé à l'identique");
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
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, juce::String::fromUTF8 ("TRANSIENT \xc2\xb7 DYNAMIQUE")))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("seq_dyn.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, "CAPTURE"))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("capture.png"); }
                if (auto* b = dynamic_cast<juce::Button*> (findBtn (ui, juce::String::fromUTF8 ("PARAM\xc3\x88TRES")))) { b->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); shot ("settings.png"); }
            }
        }
        std::printf ("        captures d'écran dans %s\n", shotDir.toRawUTF8());
    }

    std::printf ("\n%s (%d échec%s)\n", fails == 0 ? "TOUT EST BON" : "PROBLÈMES", fails, fails > 1 ? "s" : "");
    return fails;
}
