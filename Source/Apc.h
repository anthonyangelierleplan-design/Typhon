#pragma once
// ============================================================================
//  Typhon × Akai APC64 — l'APC64 devient le séquenceur de Typhon (sans JUCE, testable)
//
//  Typhon ouvre lui-même le port « APC64 » (port DAW) et passe la machine en mode DAW,
//  comme le fait Ableton : grille 8×8 dessinée par Typhon (couleurs, tête de lecture),
//  boutons de piste, scènes, flèches, Shift, Clear, Duplicate, Undo, touch strips, écran.
//  Protocole : messages observés sur l'APC64 (firmware 1.2.x), d'après le travail ouvert
//  github.com/esazhin/apc64-control et l'extension APC64 de Bitwig (licence MIT).
//
//  Vue PISTES (scène 1) : 8 pistes × 8 pas, ◀ ▶ = page de 8 pas.
//  Vue PISTE  (scène 2) : les 32 pas de la piste choisie en haut ; en bas :
//      rangée 4 = vélocité d'écriture (et du dernier pas touché)
//      rangée 3 = ratchet 1-4 · division · reverse · pitch −1 · pitch +1 (dernier pas touché)
//      rangée 2 = jouer les pistes · rangée 1 = mutes
//  Scènes : 1 PISTES · 2 PISTE · 3 16/32 PAS · 4 SUIVRE · 5 FILL · 6 ROLL 1/8 · 7 ROLL 1/16 · 8 ROLL 1/32 (5-8 : maintenir)
//  Boutons de piste = choisir la piste (Mute allumé : mute) · ▲ ▼ = piste · Shift + pad = accent / note fantôme
//  Clear maintenu + pad ou bouton de piste = effacer la piste · Duplicate = recopier la page / la 1re moitié
//  Undo = annuler (Shift : rétablir) · Stop = couper tous les sons
//  Touch strips : Grain SP · Bits · Fréquence · Filtre SP | Filtre · Decay · Pitch de la piste | Swing
// ============================================================================
#include "Dsp.h"
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

namespace typhon
{
    namespace apc
    {
        // --- identifiants des commandes de l'APC64 en mode DAW ---
        enum : int { nTempo = 72, nClear = 73, nDuplicate = 74, nQuantize = 75, nFixed = 76, nUndo = 77,
                     nStripTouch0 = 82, nPlay = 91, nRec = 92, nStop = 93, nDown = 94, nUp = 95, nLeft = 96, nRight = 97,
                     nTrack0 = 100, nRecArm = 108, nMute = 109, nSolo = 110, nStopMode = 111, nScene0 = 112, nShift = 120 };

        // palette Ableton (index de couleur = vélocité)
        enum : uint8_t { cOff = 0, cGrey = 1, cWhite = 3, cRed = 5, cOrange = 9, cYellow = 13, cLime = 17, cGreen = 21,
                         cCyan = 37, cSky = 41, cBlue = 45, cPurple = 49, cMagenta = 53 };
        inline uint8_t laneColour (int l) { static const uint8_t c[8] = { cRed, cOrange, cYellow, cLime, cCyan, cSky, cPurple, cMagenta }; return c[l & 7]; }
        // comportement = canal MIDI : 0 10 %, 1 25 %, 2 50 %, 4 75 %, 6 plein, 8 pulsation 1/8, 13 clignote 1/8
        enum : uint8_t { b10 = 0, b25 = 1, b50 = 2, b75 = 4, bFull = 6, bPulse = 8, bBlink = 13 };

        // --- messages système exclusif ---
        inline std::vector<uint8_t> sx (uint8_t cmd, std::initializer_list<uint8_t> data)
        {
            std::vector<uint8_t> m { 0xF0, 0x47, 0x00, 0x53, cmd, 0x00, (uint8_t) data.size() };
            m.insert (m.end(), data.begin(), data.end()); m.push_back (0xF7); return m;
        }
        inline std::vector<std::vector<uint8_t>> handshake()
        {
            return { sx (0x1B, { 0 }), sx (0x19, { 0 }), { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 } };
        }
        inline std::vector<uint8_t> dawMode (bool on) { return sx (0x1C, { (uint8_t) (on ? 1 : 0) }); }
        inline std::vector<uint8_t> oled (int row, const std::string& text)
        {
            std::string t = text.substr (0, 14);
            std::vector<uint8_t> m { 0xF0, 0x47, 0x00, 0x53, 0x10, 0x00, (uint8_t) (t.size() + 2), (uint8_t) row };
            for (char ch : t) m.push_back ((uint8_t) (ch & 0x7F));
            m.push_back (0); m.push_back (0xF7); return m;
        }
        // réponse d'identité de l'APC64 (produit 0x53) ; rapport de vue : F0 47 00 53 19 00 01 <m> F7
        inline bool isIdentityReply (const uint8_t* d, int n) { return n >= 10 && d[0] == 0xF0 && d[1] == 0x7E && d[3] == 0x06 && d[4] == 0x02 && d[5] == 0x47 && d[6] == 0x53; }
        inline int viewReport (const uint8_t* d, int n) { return (n >= 9 && d[0] == 0xF0 && d[1] == 0x47 && d[3] == 0x53 && d[4] == 0x19) ? d[7] : -1; }

        // --- ce que Typhon fournit à la surface ---
        struct Host
        {
            virtual ~Host() = default;
            virtual int seqLen() const = 0;
            virtual bool muted (int lane) const = 0;
            virtual float stripValue (int strip, int lane) const = 0;   // 0..1
            virtual void toggleMute (int lane) = 0;
            virtual void setStrip (int strip, int lane, float v01) = 0;
            virtual void toggleSeqLen() = 0;
            virtual void audition (int lane) = 0;
            virtual void cutAll() = 0;
            virtual void undo() = 0;
            virtual void redo() = 0;
            virtual void commit() = 0;
            virtual std::string laneLabel (int lane) const = 0;
        };

        // Image voulue de la surface (comparée à la précédente : seuls les changements partent)
        struct Frame
        {
            uint8_t padB[64] {}, padC[64] {};    // comportement, couleur
            uint8_t trackC[8] {};                // boutons de piste (RVB)
            uint8_t btn[128] {};                 // boutons à une LED : 0 éteint, sinon comportement + 1
            uint8_t stripC[8] {}; uint16_t stripPos[8] {};
            std::string oled0, oled1;
        };

        struct Surface
        {
            static constexpr uint8_t writeVels[8] = { 15, 31, 47, 63, 79, 95, 111, 127 };

            std::atomic<int> view { 0 }, page { 0 }, selLane { 0 }, writeVel { 5 }, lastStep { -1 }, rollDiv { 0 };
            std::atomic<bool> shift { false }, clearHeld { false }, muteMode { false }, follow { false }, fillHeld { false };
            std::atomic<bool> sessionView { true };   // la grille appartient à Typhon (vue Session de l'APC64)
            std::atomic<int> touchedStrips { 0 };       // un strip ne règle rien tant qu'on ne le touche pas (évite les échos)
            std::atomic<uint64_t> dirtyPads { 0 };      // pads à redessiner tout de suite (l'APC64 change la LED d'un pad appuyé)

            int pages (int seqLen) const { return std::max (1, seqLen / 8); }

            // ---------------------------------------------------------------------------------
            //  Message reçu de l'APC64 (fil MIDI). Les pas s'écrivent directement (atomiques),
            //  le reste passe par l'hôte.
            // ---------------------------------------------------------------------------------
            void handle (const uint8_t* d, int n, Pattern& pat, Host& h)
            {
                if (n < 1) return;
                if (d[0] == 0xF0) { const int v = viewReport (d, n); if (v >= 0) sessionView.store (v == 0); return; }
                if (n < 3) return;
                const int type = d[0] & 0xF0, ch = d[0] & 0x0F;
                if (type == 0xE0 && ch < 8)
                {
                    if (touchedStrips.load() & (1 << ch)) h.setStrip (ch, selLane.load(), (float) ((d[2] << 7) | d[1]) / 16383.0f);
                    return;
                }
                const bool on = type == 0x90 && d[2] > 0, off = type == 0x80 || (type == 0x90 && d[2] == 0);
                if (! on && ! off) return;
                const int note = d[1];
                if (note < 64) { if (on) pad (note % 8, note / 8, pat, h); dirtyPads.fetch_or (1ull << note); return; }
                if (note >= nStripTouch0 && note < nStripTouch0 + 8)
                {
                    const int bit = 1 << (note - nStripTouch0);
                    if (on) touchedStrips.fetch_or (bit); else touchedStrips.fetch_and (~bit);
                    return;
                }
                switch (note)
                {
                    case nShift: shift.store (on); return;
                    case nClear: clearHeld.store (on); return;
                    default: break;
                }
                if (note >= nScene0 && note < nScene0 + 8)
                {
                    const int sc = note - nScene0;
                    if (sc == 4) fillHeld.store (on);
                    else if (sc >= 5) { if (on) rollDiv.store (sc - 4); else if (rollDiv.load() == sc - 4) rollDiv.store (0); }
                    else if (on)
                    {
                        if (sc == 0) view.store (0);
                        else if (sc == 1) view.store (1);
                        else if (sc == 2) { h.toggleSeqLen(); h.commit(); }
                        else follow.store (! follow.load());
                    }
                    return;
                }
                if (! on) return;
                if (note >= nTrack0 && note < nTrack0 + 8)
                {
                    const int l = note - nTrack0;
                    if (clearHeld.load()) clearLane (pat, l, h);
                    else if (muteMode.load()) { h.toggleMute (l); h.commit(); }
                    else { selLane.store (l); lastStep.store (-1); }
                    return;
                }
                const int pg = pages (h.seqLen());
                switch (note)
                {
                    case nMute:  muteMode.store (! muteMode.load()); break;
                    case nLeft:  page.store (std::max (0, page.load() - 1)); break;
                    case nRight: page.store (std::min (pg - 1, page.load() + 1)); break;
                    case nUp:    selLane.store (std::max (0, selLane.load() - 1)); lastStep.store (-1); break;
                    case nDown:  selLane.store (std::min (7, selLane.load() + 1)); lastStep.store (-1); break;
                    case nUndo:  if (shift.load()) h.redo(); else h.undo(); break;
                    case nStop:  h.cutAll(); break;
                    case nDuplicate: duplicate (pat, h); break;
                    default: break;
                }
            }

            void pad (int col, int row, Pattern& pat, Host& h)
            {
                const int len = h.seqLen();
                if (view.load() == 0)
                {
                    const int lane = 7 - row, st = page.load() * 8 + col;
                    if (clearHeld.load()) { clearLane (pat, lane, h); return; }
                    if (st < len) editStep (pat, lane, st, h);
                    return;
                }
                const int lane = selLane.load();
                if (row >= 4) { const int st = (7 - row) * 8 + col; if (st < len) editStep (pat, lane, st, h); return; }
                if (row == 1) { selLane.store (col); h.audition (col); return; }
                if (row == 0) { h.toggleMute (col); h.commit(); return; }
                if (row == 3)
                {
                    writeVel.store (col);
                    const int ls = lastStep.load();
                    if (ls >= 0) { auto s = pat.get (lane, ls); if (s.on()) { s.vel = writeVels[col]; pat.set (lane, ls, s); h.commit(); } }
                    return;
                }
                // rangée 2 : réglages du dernier pas touché
                const int ls = lastStep.load();
                if (ls < 0) return;
                auto s = pat.get (lane, ls);
                if (! s.on()) return;
                if (col < 4) { s.ratchet = col + 1; s.split = false; }
                else if (col == 4) s.split = ! s.split;
                else if (col == 5) s.rev = ! s.rev;
                else if (col == 6) s.pitch = std::max (-12, s.pitch - 1);
                else s.pitch = std::min (12, s.pitch + 1);
                pat.set (lane, ls, s);
                h.commit();
            }

            void editStep (Pattern& pat, int lane, int st, Host& h)
            {
                auto s = pat.get (lane, st);
                const int head = s.on() ? st : pat.headOf (lane, st);
                if (shift.load())
                {
                    // Shift : accent / note fantôme
                    if (head == st) s.vel = s.vel >= 110 ? 70 : 120;
                    else { s = Step(); s.vel = 45; }
                    if (head >= 0 && head != st) { auto hs = pat.get (lane, head); hs.len = st - head; pat.set (lane, head, hs); }
                    pat.set (lane, st, s);
                    lastStep.store (st);
                }
                else if (head == st) { pat.set (lane, st, Step()); pat.clearLocks (lane, st); lastStep.store (-1); }
                else
                {
                    if (head >= 0) { auto hs = pat.get (lane, head); hs.len = st - head; pat.set (lane, head, hs); }   // coupe la liaison
                    Step n; n.vel = writeVels[std::max (0, std::min (7, writeVel.load()))];
                    pat.set (lane, st, n);
                    lastStep.store (st);
                }
                if (view.load() == 0) selLane.store (lane);
                h.commit();
            }

            void clearLane (Pattern& pat, int lane, Host& h)
            {
                for (int s = 0; s < numSteps; ++s) { pat.set (lane, s, Step()); pat.clearLocks (lane, s); }
                lastStep.store (-1);
                h.commit();
            }

            void duplicate (Pattern& pat, Host& h)
            {
                auto copy = [&] (int lane, int from, int to, int n)
                {
                    for (int k = 0; k < n; ++k)
                    {
                        pat.set (lane, to + k, pat.get (lane, from + k));
                        for (int x = 0; x < numLocks; ++x) pat.setLock (lane, to + k, x, pat.lock (lane, from + k, x));
                    }
                };
                const int len = h.seqLen();
                if (view.load() == 1 || page.load() * 8 + 8 >= len)
                {
                    // 1re moitié → 2e moitié (passe en 32 pas si besoin)
                    if (len == 16) h.toggleSeqLen();
                    if (view.load() == 1) copy (selLane.load(), 0, 16, 16);
                    else for (int l = 0; l < 8; ++l) copy (l, 0, 16, 16);
                }
                else
                {
                    const int p = page.load();
                    for (int l = 0; l < 8; ++l) copy (l, p * 8, p * 8 + 8, 8);
                    page.store (p + 1);
                }
                h.commit();
            }

            // ---------------------------------------------------------------------------------
            //  Image de la surface (fil principal, ~25 fois par seconde)
            // ---------------------------------------------------------------------------------
            void render (const Pattern& pat, const Host& h, int playStep, Frame& f)
            {
                const int len = h.seqLen(), pg = pages (len);
                if (page.load() >= pg) page.store (pg - 1);
                if (follow.load() && playStep >= 0 && view.load() == 0) page.store (std::min (pg - 1, playStep / 8));
                const int v = view.load(), sel = selLane.load(), ls = lastStep.load();
                auto put = [&] (int col, int row, uint8_t b, uint8_t c) { f.padB[row * 8 + col] = b; f.padC[row * 8 + col] = c; };
                auto stepLed = [&] (int col, int row, int lane, int st)
                {
                    if (st >= len) { put (col, row, b10, cOff); return; }
                    const auto s = pat.get (lane, st);
                    const bool tie = ! s.on() && pat.headOf (lane, st) >= 0;
                    const bool mu = h.muted (lane);
                    if (st == playStep) { put (col, row, s.on() || tie ? bFull : b25, cWhite); return; }
                    if (s.on())
                    {
                        const uint8_t b = mu ? b25 : (s.vel < 60 ? b50 : (s.vel < 100 ? b75 : bFull));
                        put (col, row, (v == 1 && st == ls) ? bPulse : b, laneColour (lane));
                    }
                    else if (tie) put (col, row, b25, laneColour (lane));
                    else put (col, row, b10, st % 4 == 0 ? cWhite : cOff);
                };
                if (v == 0)
                {
                    for (int row = 0; row < 8; ++row)
                        for (int col = 0; col < 8; ++col) stepLed (col, row, 7 - row, page.load() * 8 + col);
                }
                else
                {
                    for (int row = 4; row < 8; ++row)
                        for (int col = 0; col < 8; ++col) stepLed (col, row, sel, (7 - row) * 8 + col);
                    const auto s = ls >= 0 ? pat.get (sel, ls) : Step();
                    const bool have = s.on();
                    for (int col = 0; col < 8; ++col)
                    {
                        put (col, 3, col == writeVel.load() ? bFull : b25, col <= writeVel.load() ? laneColour (sel) : cGrey);
                        uint8_t b = have ? b25 : b10, c = cGrey;
                        if (col < 4) { c = cYellow; if (have && ! s.split && s.ratchet == col + 1) b = bFull; }
                        else if (col == 4) { c = cCyan; if (have && s.split) b = bFull; }
                        else if (col == 5) { c = cSky; if (have && s.rev) b = bFull; }
                        else if (col == 6) { c = cPurple; if (have && s.pitch < 0) b = bFull; }
                        else { c = cPurple; if (have && s.pitch > 0) b = bFull; }
                        put (col, 2, b, c);
                        put (col, 1, col == sel ? bFull : b50, col == sel ? cWhite : laneColour (col));
                        put (col, 0, h.muted (col) ? bFull : b25, h.muted (col) ? cRed : cGreen);
                    }
                }
                for (int l = 0; l < 8; ++l)
                    f.trackC[l] = muteMode.load() ? (h.muted (l) ? cOff : laneColour (l)) : (l == sel ? cWhite : laneColour (l));
                std::memset (f.btn, 0, sizeof (f.btn));
                auto lit = [&] (int note, bool on, uint8_t b = bFull) { f.btn[note] = on ? (uint8_t) (b + 1) : 0; };
                lit (nScene0 + 0, v == 0); lit (nScene0 + 1, v == 1); lit (nScene0 + 2, len == 32); lit (nScene0 + 3, follow.load());
                lit (nScene0 + 4, fillHeld.load()); for (int k = 1; k <= 3; ++k) lit (nScene0 + 4 + k, rollDiv.load() == k);
                lit (nMute, muteMode.load()); lit (nShift, shift.load()); lit (nClear, clearHeld.load(), bFull); lit (nUndo, true, b25);
                lit (nDuplicate, true, b25); lit (nStop, true, b25);
                lit (nLeft, v == 0 && page.load() > 0, b50); lit (nRight, v == 0 && page.load() < pg - 1, b50);
                lit (nUp, sel > 0, b50); lit (nDown, sel < 7, b50);
                for (int i = 0; i < 8; ++i)
                {
                    f.stripC[i] = i < 4 ? cOrange : (i < 7 ? laneColour (sel) : cWhite);
                    f.stripPos[i] = (uint16_t) std::lround (std::max (0.0f, std::min (1.0f, h.stripValue (i, sel))) * 16383.0f);
                }
                f.oled0 = "TYPHON TY-12";
                std::string lab = h.laneLabel (sel);
                if (v == 0) lab = "PISTES " + std::to_string (page.load() * 8 + 1) + "-" + std::to_string (page.load() * 8 + 8);
                else lab += " " + std::to_string (len);
                f.oled1 = lab;
            }

            // ---------------------------------------------------------------------------------
            //  ROLL (fil audio) : coups calés sur la grille quand l'hôte joue, horloge libre sinon
            // ---------------------------------------------------------------------------------
            struct Hit { int offset, lane, vel; };
            double freePpq = 0.0; bool freeRunning = false;
            int renderRoll (bool playing, double p0, double ppqPerSample, int n, Hit* out, int maxOut)
            {
                const int r = rollDiv.load();
                if (r == 0) { freeRunning = false; return 0; }
                static const double steps[4] = { 0.0, 2.0, 1.0, 0.5 };
                const double iv = steps[r] * 0.25;
                double start = p0;
                if (! playing) { if (! freeRunning) { freeRunning = true; freePpq = -1.0e-9; } start = freePpq; }
                const double end = start + n * ppqPerSample;
                int count = 0;
                for (double k = std::ceil (start / iv - 1.0e-9); k * iv < end && count < maxOut; k += 1.0)
                {
                    const double t = std::max (0.0, k * iv);
                    if (t < start) continue;
                    out[count++] = { std::max (0, std::min (n - 1, (int) ((t - start) / ppqPerSample))), selLane.load(), 110 };
                }
                if (! playing) freePpq = end;
                return count;
            }
        };
    }
}
