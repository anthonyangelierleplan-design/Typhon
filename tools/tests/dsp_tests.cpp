// Tests du moteur Typhon (sans JUCE) : g++ -std=c++17 -O2 -I../../Source dsp_tests.cpp -o t && ./t
#include "Dsp.h"
#include "Fallback.h"
#include "Apc.h"
namespace typhon { enum { lkCut = 4 }; }
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
            ok &= u.vel == vel && u.ratchet == r && u.len == len && u.choke == ch && u.split == (bool) sp && u.pitch == 0 && ! u.rev;
        }
        for (int pt = -12; pt <= 12; ++pt) for (int rv = 0; rv < 2; ++rv) for (int len = 1; len <= 32; len += 31)
        { Step s; s.vel = 90; s.pitch = pt; s.rev = rv; s.len = len; s.ratchet = 3; s.choke = 2; Step u = Step::unpack (s.pack()); ok &= u.pitch == pt && u.rev == (bool) rv && u.len == len && u.ratchet == 3 && u.choke == 2; }
        CHECK (ok, "pas : rangement / lecture identiques (toutes combinaisons, pitch, reverse, 32 pas)");
        { Pattern q; CHECK (! q.hasLocks (0, 0), "verrous : vides au départ"); q.setLock (2, 31, lkCut, 800.0f); CHECK (q.hasLocks (2, 31) && q.lock (2, 31, lkCut) == 800.0f, "verrous : écriture / lecture"); }
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
        FillOptions fo; fo.intensity = 1.0f; fo.lenSteps = 4; fo.grid = 1; fo.laneMask = (1u << 1) | (1u << 7); fo.othersMute = true;
        HitList h0;
        CHECK (! fillHits (fo, 1, 4, h0), "fill : la première partie de la mesure reste intacte");
        int snare = 0, perc = 0, lastVel = 0; bool cresc = true;
        for (int st = 12; st < 16; ++st)
        {
            HitList a; fillHits (fo, 1, st, a); snare += a.count; for (int k = 0; k < a.count; ++k) { cresc &= a.vel[k] >= lastVel; lastVel = a.vel[k]; }
            HitList b; fillHits (fo, 7, st, b); perc += b.count; for (int k = 0; k < b.count; ++k) { cresc &= b.vel[k] >= lastVel; lastVel = b.vel[k]; }
        }
        CHECK (snare == 2 && perc == 2 && cresc, "fill 1 temps en 1/16 : snare puis perc à tour de rôle (%d + %d coups), crescendo", snare, perc);
        HitList hh; const bool handled = fillHits (fo, 4, 13, hh);
        CHECK (handled && hh.count == 0, "fill : les autres pistes se taisent si demandé");
        fo.othersMute = false; HitList hk;
        CHECK (! fillHits (fo, 4, 13, hk), "fill : les autres pistes gardent leur pattern si demandé");
        fo.grid = 2; fo.lenSteps = 16; fo.laneMask = 1u << 1; int n32 = 0;
        for (int st = 0; st < 16; ++st) { HitList a; fillHits (fo, 1, st, a); n32 += a.count; }
        CHECK (n32 == 32, "fill 1 mesure en 1/32 : %d coups (32)", n32);
        fo.grid = 3; int nt = 0; for (int st = 0; st < 16; ++st) { HitList a; fillHits (fo, 1, st, a); nt += a.count; }
        CHECK (nt == 24, "fill 1 mesure en triolets : %d coups (24)", nt);
        fo.intensity = 0.2f; fo.grid = 2; int ns = 0; for (int st = 0; st < 16; ++st) { HitList a; fillHits (fo, 1, st, a); ns += a.count; }
        CHECK (ns == 24, "fill doux : la 1re moitié va 2x moins vite (%d coups)", ns);
        Step sp; sp.vel = 100; sp.split = true; auto h = hitsOf (sp);
        CHECK (h.count == 2 && h.at[1] == 0.5f, "pas divisé : 2 coups à 0 et 1/2");
        Step rt; rt.vel = 100; rt.ratchet = 3; h = hitsOf (rt);
        CHECK (h.count == 3 && std::fabs (h.at[2] - 2.0f / 3.0f) < 1e-6f, "ratchet x3 : 3 coups réguliers");
        CHECK (swingOffset (0, 0.66) == 0.0 && std::fabs (swingOffset (1, 0.66) - 0.32) < 1e-9, "swing : seuls les pas impairs sont décalés");
    }
    // 8) filtre de piste
    {
        std::vector<float> l (48000), r (48000);
        for (int i = 0; i < 48000; ++i) l[(size_t) i] = r[(size_t) i] = (float) std::sin (2 * M_PI * 8000.0 * i / sr);
        Svf f; f.prepare (sr); f.set (500.0f, 0.707f); f.process (l.data(), r.data(), 48000, 0);
        float pk = 0; for (int i = 24000; i < 48000; ++i) pk = std::max (pk, std::fabs (l[(size_t) i]));
        CHECK (pk < 0.01f, "filtre passe-bas 500 Hz : un 8 kHz ressort à %.4f", pk);
        bool ok = true;
        for (int mode = 0; mode < 3; ++mode) for (float q : { 0.5f, 10.0f }) for (float fc : { 20.0f, 20000.0f })
        {
            std::vector<float> a (4800, 0.0f), b (4800, 0.0f); a[0] = b[0] = 1.0f;
            Svf g; g.prepare (sr); g.set (fc, q); g.process (a.data(), b.data(), 4800, mode);
            ok &= finiteAll (a) && peakOf (a) < 50.0f;
        }
        CHECK (ok, "filtre : stable aux extrêmes (3 modes, résonance max, 20 Hz / 20 kHz)");
    }
    // 9) granulaire
    {
        auto set = makeFallback (6, sr);
        GrainParams gp; gp.durMs = 400; gp.density = 60; gp.sizeMs = 40; gp.spray = 0.5f; gp.stereo = 1.0f; gp.pitchRand = 5.0f; gp.reverse = 0.5f;
        GrainCloud c; c.start (set->layers[0][0].get(), set, sr, gp, 1.0f, std::numeric_limits<float>::quiet_NaN());
        std::vector<float> l (48000, 0.0f), r (48000, 0.0f);
        c.render (l.data(), r.data(), 48000, gp, 1);
        CHECK (finiteAll (l) && finiteAll (r) && peakOf (l) > 0.01f && peakOf (l) < 4.0f, "granulaire : nuage de 400 ms, pic %.2f", peakOf (l));
        float tail = 0; for (int i = 24000; i < 48000; ++i) tail = std::max (tail, std::fabs (l[(size_t) i]));
        CHECK (tail == 0.0f && ! c.active, "granulaire : le nuage s'arrête après sa durée");
        GrainCloud d; d.start (set->layers[0][0].get(), set, sr, gp, 1.0f, std::numeric_limits<float>::quiet_NaN());
        std::vector<float> a (9600, 0.0f), b (9600, 0.0f);
        d.render (a.data(), b.data(), 2400, gp, 1); d.choke (240); d.render (a.data() + 2400, b.data() + 2400, 7200, gp, 1);
        float after = 0; for (int i = 2400 + 260; i < 9600; ++i) after = std::max (after, std::fabs (a[(size_t) i]));
        CHECK (after == 0.0f, "granulaire : le choke coupe le nuage");
    }
    // 10) slice : un faux break de 8 coups
    {
        Sample br; br.rate = sr;
        const int hitsAt[8] = { 0, 12000, 24000, 30000, 42000, 54000, 66000, 78000 };
        br.l.assign (90000, 0.0f);
        for (int h : hitsAt) { auto k = makeFallback (h % 3 == 0 ? 0 : 1, sr); auto& x = k->layers[0][0]->l; for (size_t i = 0; i < x.size() && h + i < br.l.size(); ++i) br.l[h + i] += x[i] * 0.8f; }
        br.r = br.l;
        auto sl = detectSlices (br, 0.6f, 8);
        int good = 0; for (int h : hitsAt) for (int p : sl) if (std::abs (p - h) < 400) { ++good; break; }
        CHECK ((int) sl.size() == 8 && good == 8, "slice : %d coups trouvés, %d bien placés (8)", (int) sl.size(), good);
        auto sl4 = detectSlices (br, 0.6f, 4);
        CHECK ((int) sl4.size() == 4, "slice : limité à 4 tranches (%d)", (int) sl4.size());
    }

    // APC64 en mode DAW : la grille 8x8 = séquenceur
    {
        using namespace typhon::apc;
        struct H : Host
        {
            int len = 16; bool mu[8] {}; int aud = -1, commits = 0, undos = 0; float strip[8] {}; int stripLane = -1;
            int seqLen() const override { return len; } bool muted (int l) const override { return mu[l]; }
            float stripValue (int s, int) const override { return strip[s]; }
            void toggleMute (int l) override { mu[l] = ! mu[l]; } void setStrip (int s, int l, float v) override { strip[s] = v; stripLane = l; }
            void toggleSeqLen() override { len = len == 16 ? 32 : 16; } void audition (int l) override { aud = l; } void cutAll() override {}
            void undo() override { ++undos; } void redo() override {} void commit() override { ++commits; }
            std::string laneLabel (int) const override { return "SNARE"; }
        } h;
        Pattern pat; Surface sf;
        auto press = [&] (int note, int ch = 6) { uint8_t m[3] = { (uint8_t) (0x90 | ch), (uint8_t) note, 127 }; sf.handle (m, 3, pat, h); };
        auto release = [&] (int note) { uint8_t m[3] = { 0x80, (uint8_t) note, 0 }; sf.handle (m, 3, pat, h); };
        // vue PISTES : rangée du haut = kick, colonne 0 = pas 1
        press (56); CHECK (pat.get (0, 0).on(), "apc : pad en haut à gauche = kick pas 1");
        press (56); CHECK (! pat.get (0, 0).on(), "apc : second appui efface");
        press (1); CHECK (pat.get (7, 1).on(), "apc : rangée du bas = perc");
        press (nRight, 0); press (56 + 3); CHECK (pat.get (0, 11).on() && sf.page.load() == 1, "apc : page 2 = pas 9-16");
        press (nShift, 0); press (56 + 3); release (nShift);
        CHECK (pat.get (0, 11).vel == 120, "apc : Shift + pad = accent (%d)", pat.get (0, 11).vel);
        press (nRight, 0); CHECK (sf.page.load() == 1, "apc : pas de page 3 en 16 pas");
        // vue PISTE : 32 pas de la snare, réglages du dernier pas
        press (nTrack0 + 1, 0); press (nScene0 + 1, 0);
        CHECK (sf.view.load() == 1 && sf.selLane.load() == 1, "apc : bouton de piste 2 + scène 2 = vue PISTE snare");
        press (63); press (4 * 8 + 0);
        CHECK (pat.get (1, 7).on() && ! pat.get (1, 24).on(), "apc : vue PISTE, rangée du haut = pas 1-8 ; pas 25 ignoré en 16 pas");
        Step t; t.vel = 100; pat.set (1, 4, t); sf.lastStep.store (4);
        press (2 * 8 + 2); CHECK (pat.get (1, 4).ratchet == 3, "apc : rangée 3, pad 3 = ratchet x3 du dernier pas");
        press (2 * 8 + 7); press (2 * 8 + 7); CHECK (pat.get (1, 4).pitch == 2, "apc : pitch +2");
        press (3 * 8 + 1); CHECK (pat.get (1, 4).vel == 31, "apc : rangée 4 = vélocité du dernier pas (%d)", pat.get (1, 4).vel);
        press (1 * 8 + 5); CHECK (h.aud == 5 && sf.selLane.load() == 5, "apc : rangée 2 = jouer la piste");
        press (0 * 8 + 2); CHECK (h.mu[2], "apc : rangée du bas = mute");
        // liaison coupée par un pad au milieu
        pat.clear(); Step tie; tie.vel = 100; tie.len = 4; pat.set (5, 0, tie);
        press (7 * 8 + 2); CHECK (pat.get (5, 0).len == 2 && pat.get (5, 2).on(), "apc : pad dans une liaison = coupe la liaison et pose un pas");
        // duplicate : 1re moitié -> 2e, passe en 32 pas
        press (nDuplicate, 0); CHECK (h.len == 32 && pat.get (5, 18).on(), "apc : Duplicate recopie 1-16 sur 17-32");
        // Clear maintenu + bouton de piste
        press (nClear, 0); press (nTrack0 + 5, 0); release (nClear);
        CHECK (! pat.get (5, 2).on() && ! pat.get (5, 18).on(), "apc : Clear + piste = efface la piste");
        // strips (pitch bend 14 bits)
        uint8_t pb[3] = { 0xE1, 0x7F, 0x7F }; sf.handle (pb, 3, pat, h);
        CHECK (h.strip[1] > 0.999f, "apc : strip 2 = Bits");
        // vue Session perdue / retrouvée
        uint8_t rep[9] = { 0xF0, 0x47, 0x00, 0x53, 0x19, 0x00, 0x01, 0x02, 0xF7 }; sf.handle (rep, 9, pat, h);
        CHECK (! sf.sessionView.load(), "apc : vue Note de l'APC64 détectée");
        rep[7] = 0; sf.handle (rep, 9, pat, h); CHECK (sf.sessionView.load(), "apc : retour en vue Session");
        // image : tête de lecture blanche, pas actifs couleur de piste
        sf.view.store (0); sf.page.store (0); pat.clear(); Step k; k.vel = 127; pat.set (0, 0, k);
        Frame f; sf.render (pat, h, 3, f);
        CHECK (f.padC[56] == laneColour (0) && f.padB[56] == bFull && f.padC[59] == cWhite && f.padC[57] == cOff, "apc : LED pas actif / tête de lecture / vide");
        CHECK (f.oled0 == "TYPHON TY-12" && f.btn[nScene0] == bFull + 1, "apc : écran et scène PISTES allumée");
        // roll 1/16 à 120 BPM transport arrêté : 8 coups par seconde
        uint8_t sc[3] = { 0x90, (uint8_t) (nScene0 + 6), 127 }; sf.handle (sc, 3, pat, h);
        Surface::Hit hits[64]; int nh = 0;
        for (int b = 0; b < 100; ++b) nh += sf.renderRoll (false, 0.0, 2.0 / 48000.0, 480, hits, 64);
        CHECK (nh == 8, "apc : roll 1/16 = 8 coups en 1 s (%d)", nh);
        release (nScene0 + 6); CHECK (sf.renderRoll (false, 0.0, 2.0 / 48000.0, 480, hits, 64) == 0, "apc : roll relâché");
        CHECK (dawMode (true) == std::vector<uint8_t> ({ 0xF0, 0x47, 0x00, 0x53, 0x1C, 0x00, 0x01, 0x01, 0xF7 }), "apc : message mode DAW");
    }

    // STRETCH : longueur, niveau, hauteur gardée (simple), pas de NaN (Paul)
    {
        Sample tone; tone.rate = 48000.0;
        for (int i = 0; i < 24000; ++i) tone.l.push_back (0.5f * (float) std::sin (2.0 * 3.14159265 * 440.0 * i / 48000.0) * (i < 22000 ? 1.0f : (24000 - i) / 2000.0f));
        tone.r = tone.l;
        auto zc = [] (const std::vector<float>& v, size_t a, size_t b) { int n = 0; for (size_t i = a + 1; i < b; ++i) n += (v[i - 1] < 0) != (v[i] < 0); return n; };
        auto ws = stretchSample (tone, 1, 4.0, 0.5f);
        CHECK (std::abs ((int) ws.l.size() - 96000) < 20 && finiteAll (ws.l), "stretch simple x4 : 0,5 s -> %.2f s", ws.l.size() / 48000.0);
        const int z0 = zc (tone.l, 4800, 9600), z1 = zc (ws.l, 24000, 28800);
        CHECK (std::abs (z0 - z1) <= 3, "stretch simple : même hauteur (passages à zéro %d / %d)", z0, z1);
        CHECK (peakOf (ws.l) > 0.45f && peakOf (ws.l) <= 0.51f, "stretch simple : niveau gardé (%.2f)", peakOf (ws.l));
        auto ps = stretchSample (tone, 2, 16.0, 0.5f);
        CHECK (std::abs ((int) ps.l.size() - 384000) < 20 && finiteAll (ps.l) && finiteAll (ps.r), "paulstretch x16 : 8 s, sans NaN");
        double e = 0; for (size_t i = 96000; i < 288000; ++i) e += ps.l[i] * ps.l[i];
        CHECK (e > 100.0, "paulstretch : nappe continue au milieu (énergie %.0f)", e);
        bool stop = true; auto cs = stretchSample (tone, 2, 16.0, 0.5f, [&] { return stop; });
        CHECK (cs.l.size() == tone.l.size(), "stretch : annulable");
        SampleSet set; set.variants.push_back ({ std::make_shared<Sample> (tone), std::make_shared<Sample> (tone) });
        auto st = stretchSet (set, 1, 2.0, 0.3f);
        CHECK (st->numVariants() == 1 && st->variants[0].size() == 2 && st->variants[0][1]->l.size() > 47000, "stretch : toutes les variantes et round robin");
    }

    std::printf ("\n%s (%d échec%s)\n", fails == 0 ? "TOUT EST BON" : "PROBLÈMES", fails, fails > 1 ? "s" : "");
    return fails;
}
