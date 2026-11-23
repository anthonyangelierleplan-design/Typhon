// Tests du moteur Typhon (sans JUCE) : g++ -std=c++17 -O2 -I../../Source dsp_tests.cpp -o t && ./t
#include "Dsp.h"
#include "Fallback.h"
#include <cstdio>
#include <random>
using namespace typhon;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf ("ECHEC : " __VA_ARGS__); std::printf ("\n"); } else { std::printf ("ok    : " __VA_ARGS__); std::printf ("\n"); } } while (0)

static bool finiteAll (const std::vector<float>& v) { for (float x : v) if (! std::isfinite (x)) return false; return true; }
static float peakOf (const std::vector<float>& v) { float p = 0; for (float x : v) p = std::max (p, std::fabs (x)); return p; }

int main()
{
    const double sr = 48000.0;
    // 1) pas : rangement
    {
        bool ok = true;
        for (int vel : { 0, 1, 55, 100, 127 }) for (int r = 1; r <= 4; ++r) for (int len = 1; len <= 16; ++len) for (int ch = 0; ch < 3; ++ch) for (int sp = 0; sp < 2; ++sp)
        {
            Step s; s.vel = vel; s.ratchet = r; s.len = len; s.choke = ch; s.split = sp;
            Step u = Step::unpack (s.pack());
            ok &= u.vel == vel && u.ratchet == r && u.len == len && u.choke == ch && u.split == (bool) sp;
        }
        CHECK (ok, "pas : rangement / lecture identiques (toutes combinaisons)");
        Pattern p; Step h; h.vel = 100; h.len = 3; p.set (0, 4, h);
        CHECK (p.headOf (0, 5) == 4 && p.headOf (0, 6) == 4 && p.headOf (0, 7) == -1 && p.headOf (0, 3) == -1, "liaison : pas recouverts reconnus");
    }
    // 2) kit de secours
    for (int l = 0; l < numLanes; ++l)
    {
        auto s = makeFallback (l, sr);
        auto& smp = *s->layers[0][0];
        CHECK (finiteAll (smp.l) && peakOf (smp.l) > 0.05f && peakOf (smp.l) < 1.0f, "secours piste %d : pic %.2f, %d ms", l, peakOf (smp.l), (int) (smp.length() * 1000 / sr));
    }
    // 3) voix + choke
    {
        auto set = makeFallback (5, sr);
        Voice v; v.active = true; v.smp = set->layers[0][0].get(); v.keep = set; v.pos = 0; v.endPos = v.smp->length(); v.inc = 1.0;
        std::vector<float> L (4800, 0.0f), R (4800, 0.0f);
        v.render (L.data(), R.data(), 480, 1);
        v.choke ((int) (0.005 * sr));
        v.render (L.data() + 480, R.data() + 480, 4320, 1);
        float tail = 0; for (int i = 480 + 241; i < 4800; ++i) tail = std::max (tail, std::fabs (L[(size_t) i]));
        CHECK (! v.active && tail == 0.0f, "choke : la voix s'arrête en 5 ms");
        // pas de clic : écart maximal entre deux échantillons pendant le fondu
        float jump = 0; for (int i = 481; i < 480 + 240; ++i) jump = std::max (jump, std::fabs (L[(size_t) i] - L[(size_t) i - 1]));
        CHECK (jump < 0.5f, "choke : fondu sans saut (%.3f)", jump);
        // liaison : coupe au bout du gate
        Voice g; g.active = true; g.smp = set->layers[0][0].get(); g.keep = set; g.endPos = g.smp->length(); g.inc = 1.0; g.gateLeft = 1000; g.gateFadeSamples = 100;
        std::vector<float> A (6000, 0.0f), B (6000, 0.0f);
        g.render (A.data(), B.data(), 6000, 1);
        float after = 0; for (int i = 1110; i < 6000; ++i) after = std::max (after, std::fabs (A[(size_t) i]));
        CHECK (after == 0.0f, "liaison : son coupé à la fin de la note tenue");
    }
    // 4) interpolation : erreur sur un sinus 1 kHz lu à 0.7x
    {
        Sample s; s.rate = sr; for (int i = 0; i < 20000; ++i) s.l.push_back ((float) std::sin (2 * M_PI * 1000.0 * i / sr));
        for (int q = 0; q < 3; ++q)
        {
            double err = 0; int n = 0;
            for (double pos = 100; pos < 19000; pos += 0.7) { const double ref = std::sin (2 * M_PI * 1000.0 * pos / sr); err = std::max (err, std::fabs (interp (s.l, pos, q) - ref)); ++n; }
            std::printf ("        qualité %d : erreur max %.2e\n", q, err);
            CHECK (err < (q == 0 ? 3e-3 : (q == 1 ? 2e-4 : 2e-5)), "interpolation qualité %d", q);
        }
    }
    // 5) transient designer et dynamique : neutres = identité, pas de NaN aux extrêmes
    {
        auto set = makeFallback (1, sr);
        auto src = set->layers[0][0]->l;
        std::vector<float> l = src, r = src;
        TransientDesigner td; td.prepare (sr); td.process (l.data(), r.data(), (int) l.size(), 0, 0);
        CHECK (l == src, "transient neutre = identique");
        Dynamics dy; dy.prepare (sr); dy.process (l.data(), r.data(), (int) l.size(), 0, -18, 1.0f, 5, 80, 1.0f);
        CHECK (l == src, "dynamique ratio 1:1 = identique");
        TransientDesigner t2; t2.prepare (sr);
        std::vector<float> a = src, b = src; t2.process (a.data(), b.data(), (int) a.size(), 12.0f, 0.0f);
        CHECK (peakOf (a) > peakOf (src) * 1.2f, "transient +12 dB d'attaque : pic %.2f -> %.2f", peakOf (src), peakOf (a));
        std::mt19937 rng (7); std::uniform_real_distribution<float> u (0, 1);
        bool ok = true; float mx = 0;
        for (int it = 0; it < 300; ++it)
        {
            std::vector<float> x = src, y = src;
            for (auto& v : x) v *= 4.0f * u (rng); y = x;
            TransientDesigner t; t.prepare (sr); t.process (x.data(), y.data(), (int) x.size(), -12 + 24 * u (rng), -12 + 24 * u (rng));
            Dynamics d; d.prepare (sr); d.process (x.data(), y.data(), (int) x.size(), (int) (u (rng) * 3.99f), -60 * u (rng), 1 + 19 * u (rng), 0.1f + 100 * u (rng), 5 + 995 * u (rng), u (rng));
            SpGrain g; g.prepare (sr); g.process (x.data(), y.data(), (int) x.size(), u (rng), 4 + 12 * u (rng), 4000 + 44000 * u (rng), u (rng), it % 2);
            ok &= finiteAll (x) && finiteAll (y); mx = std::max (mx, peakOf (x));
        }
        CHECK (ok, "300 réglages extrêmes : aucun NaN (pic max %.1f pour une entrée jusqu'à x4)", mx);
        // modes : signe du gain
        CHECK (Dynamics::gainDb (0, -6, -18, 4) < 0, "comp vers le bas : réduit au-dessus du seuil");
        CHECK (Dynamics::gainDb (1, -40, -18, 4) > 0 && Dynamics::gainDb (1, -6, -18, 4) == 0, "comp vers le haut : remonte sous le seuil seulement");
        CHECK (Dynamics::gainDb (2, -40, -18, 4) < 0 && Dynamics::gainDb (2, -6, -18, 4) == 0, "expansion vers le bas : enfonce sous le seuil seulement");
        CHECK (Dynamics::gainDb (3, -6, -18, 4) > 0 && Dynamics::gainDb (3, -40, -18, 4) == 0, "expansion vers le haut : pousse au-dessus du seuil seulement");
        CHECK (Dynamics::gainDb (1, -100, -18, 20) == 0, "comp vers le haut : ne remonte pas le silence");
    }
    // 6) grain SP : amount 0 = identité, 12 bits = paliers
    {
        std::vector<float> l (4800), r (4800);
        for (int i = 0; i < 4800; ++i) l[(size_t) i] = r[(size_t) i] = 0.5f * (float) std::sin (2 * M_PI * 440.0 * i / sr);
        auto src = l;
        SpGrain g; g.prepare (sr); g.process (l.data(), r.data(), 4800, 0.0f, 12, 26040, 0.0f, 0);
        CHECK (l == src, "grain SP à 0 = identique");
        g.process (l.data(), r.data(), 4800, 1.0f, 4, 26040, 0.0f, 0);
        int distinct = 0; std::vector<float> seen; for (float x : l) if (std::find (seen.begin(), seen.end(), x) == seen.end()) { seen.push_back (x); ++distinct; }
        CHECK (distinct <= 16, "4 bits : %d niveaux au plus 16", distinct);
    }
    // 7) fill et événements
    {
        Pattern p; Step s; s.vel = 100; p.set (1, 4, s); p.set (1, 12, s); p.set (4, 14, s);
        CHECK (fillStep (p, 1, 4, 1.0f).ratchet == 1, "fill : la première partie de la mesure reste intacte");
        CHECK (fillStep (p, 1, 15, 1.0f).ratchet == 4 && fillStep (p, 1, 15, 1.0f).vel > 100, "fill : roulement de snare en crescendo à la fin");
        CHECK (! fillStep (p, 4, 14, 1.0f).on(), "fill : les hats se taisent sous le roulement");
        CHECK (fillStep (p, 1, 13, 0.0f).vel == 0, "fill à 0 = pattern d'origine");
        Step sp; sp.vel = 100; sp.split = true; auto h = hitsOf (sp);
        CHECK (h.count == 2 && h.at[1] == 0.5f, "pas divisé : 2 coups à 0 et 1/2");
        Step rt; rt.vel = 100; rt.ratchet = 3; h = hitsOf (rt);
        CHECK (h.count == 3 && std::fabs (h.at[2] - 2.0f / 3.0f) < 1e-6f, "ratchet x3 : 3 coups réguliers");
        CHECK (swingOffset (0, 0.66) == 0.0 && std::fabs (swingOffset (1, 0.66) - 0.32) < 1e-9, "swing : seuls les pas impairs sont décalés");
    }
    std::printf ("\n%s (%d échec%s)\n", fails == 0 ? "TOUT EST BON" : "PROBLÈMES", fails, fails > 1 ? "s" : "");
    return fails;
}
