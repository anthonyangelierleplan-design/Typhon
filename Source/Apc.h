#pragma once
// ============================================================================
//  Typhon × Akai APC64 — couche de contrôle (sans JUCE, testable)
//
//  L'APC64 est utilisé en mode CUSTOM. Dans l'APC64 Project Editor :
//    · 4 groupes « Drum Pads » 4×4, canal 16 :
//        bas-gauche  = note 36 (C1 dans Ableton)   bas-droite  = note 52 (E2)
//        haut-gauche = note 68 (G#3)               haut-droite = note 84 (C5)
//    · les 8 touch faders en « Single (1 CC) », canal 16, CC 20 à 27.
//  Un groupe Drum Pads numérote ses pads de bas en haut, 4 par rangée :
//  la grille 8×8 entière envoie donc des notes 36..99 connues d'avance.
//
//  Plan de la grille (rangée 0 = en bas) :
//    7..4  les 32 pas de la piste sélectionnée (rangée 7 = pas 1-8 ... rangée 4 = pas 25-32)
//    3     vélocité d'écriture : DOUX · NORMAL · ACCENT · AU TOUCHER | EFFACER LA PISTE · ANNULER · RÉTABLIR · 16/32 PAS
//    2     ROLL 1/8 · 1/16 · 1/32 · 1/16T (maintenir) | FILL (maintenir) · CHOKE GLOBAL · COUPER TOUT · SP AUTHENTIQUE/PROPRE
//    1     MUTE des pistes 1-8
//    0     jouer les pistes 1-8 (sensible au toucher) ; jouer une piste la sélectionne
//  Faders : Grain SP · Bits · Fréquence · Filtre SP | Filtre · Decay · Pitch de la piste sélectionnée | Swing
//  Aftertouch polyphonique sur un pad de piste pendant un ROLL = vélocité du roll.
// ============================================================================
#include <atomic>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace typhon
{
    namespace apc
    {
        constexpr int channel   = 16;
        constexpr int firstNote = 36;
        constexpr int firstCC   = 20;

        // note → position sur la grille (col 0..7 de gauche à droite, row 0..7 de bas en haut)
        inline bool padPos (int note, int& col, int& row)
        {
            if (note < firstNote || note >= firstNote + 64) return false;
            const int g = (note - firstNote) / 16, i = (note - firstNote) % 16;
            col = (i % 4) + ((g & 1) ? 4 : 0);
            row = (i / 4) + (g >= 2 ? 4 : 0);
            return true;
        }
        inline int noteAt (int col, int row)
        {
            const int g = (row >= 4 ? 2 : 0) + (col >= 4 ? 1 : 0);
            return firstNote + g * 16 + (row % 4) * 4 + (col % 4);
        }
        inline int stepOf (int col, int row) { return (7 - row) * 8 + col; }   // rangées 7..4 → pas 0..31

        // Actions à exécuter sur le fil principal (paramètres de l'hôte, annuler)
        enum class Act : uint8_t { none, setParam, toggleParam, undo, redo, commit };
        enum Target : uint8_t { tSpAmt, tSpBits, tSpRate, tSpFilt, tLaneCut, tLaneDecay, tLanePitch, tSwing,
                                tLaneMute, tChokeAll, tSeqLen };
        struct Action { Act act = Act::none; uint8_t target = 0; int8_t lane = 0; float value = 0.0f; };

        // File sans verrou, un producteur (fil audio) / un consommateur (fil principal)
        struct ActionQueue
        {
            static constexpr int size = 256;
            Action buf[size];
            std::atomic<int> w { 0 }, r { 0 };
            bool push (const Action& a)
            {
                const int wi = w.load (std::memory_order_relaxed), nx = (wi + 1) % size;
                if (nx == r.load (std::memory_order_acquire)) return false;
                buf[wi] = a; w.store (nx, std::memory_order_release); return true;
            }
            bool pop (Action& a)
            {
                const int ri = r.load (std::memory_order_relaxed);
                if (ri == w.load (std::memory_order_acquire)) return false;
                a = buf[ri]; r.store ((ri + 1) % size, std::memory_order_release); return true;
            }
        };

        // Coups demandés au moteur par la couche APC pendant un bloc
        struct Out
        {
            struct Hit { int offset, lane, vel; };
            Hit hits[64]; int numHits = 0;
            bool cutAll = false;
            void hit (int off, int lane, int vel) { if (numHits < 64) hits[numHits++] = { off, lane, vel }; }
        };

        // Accès minimal au pattern (pour écrire les pas depuis la grille)
        struct PatternAccess
        {
            virtual ~PatternAccess() = default;
            virtual bool stepOn (int lane, int step) const = 0;
            virtual void setStep (int lane, int step, int vel) = 0;   // vel 0 = efface
            virtual void clearLane (int lane) = 0;
        };

        struct Control
        {
            // état lu par l'interface / le moteur
            std::atomic<int> selectedLane { 0 };
            std::atomic<bool> fillHeld { false };
            std::atomic<int> spModeToggle { 0 };   // incrémenté à chaque appui (le moteur bascule le réglage)
            std::atomic<int> lastNote { -1 }, lastCC { -1 };
            ActionQueue queue;

            int writeVelMode = 1;            // 0 doux, 1 normal, 2 accent, 3 au toucher
            int rollMask = 0;                // pads ROLL maintenus (bits 0..3)
            uint32_t heldLanes = 0;          // pads de piste maintenus
            int laneVel[8] { 100, 100, 100, 100, 100, 100, 100, 100 };
            double freePpq = 0.0;            // horloge libre du roll, transport à l'arrêt
            bool freeRunning = false;

            static int writeVel (int mode, int padVel)
            {
                static const int v[3] = { 40, 90, 120 };
                return mode >= 0 && mode < 3 ? v[mode] : std::max (1, std::min (127, padVel));
            }
            // durée d'un coup de roll en pas de 1/16 (le dernier rollpad appuyé l'emporte : bit le plus haut)
            double rollSteps() const
            {
                if (rollMask == 0) return 0.0;
                int b = 3; while (b > 0 && ! (rollMask & (1 << b))) --b;
                static const double d[4] = { 2.0, 1.0, 0.5, 2.0 / 3.0 };
                return d[b];
            }

            // ---- un message MIDI venu de l'APC (déjà filtré sur le canal 16) ----
            void noteOn (int note, int vel, int offset, PatternAccess& pat, int seqLen, Out& out)
            {
                lastNote.store (note);
                int col, row;
                if (! padPos (note, col, row)) return;
                if (row >= 4)
                {
                    const int st = stepOf (col, row);
                    if (st >= seqLen) return;
                    const int lane = selectedLane.load();
                    pat.setStep (lane, st, pat.stepOn (lane, st) ? 0 : writeVel (writeVelMode, vel));
                    queue.push ({ Act::commit });
                    return;
                }
                switch (row)
                {
                    case 0:
                        selectedLane.store (col);
                        heldLanes |= (1u << col);
                        laneVel[col] = vel;
                        if (rollMask == 0) out.hit (offset, col, vel);
                        else if (! freeRunning) { freeRunning = true; freePpq = -1.0e-9; }
                        break;
                    case 1: queue.push ({ Act::toggleParam, tLaneMute, (int8_t) col }); queue.push ({ Act::commit }); break;
                    case 2:
                        if (col < 4) { rollMask |= (1 << col); if (heldLanes) { freeRunning = true; freePpq = -1.0e-9; } }
                        else if (col == 4) fillHeld.store (true);
                        else if (col == 5) { queue.push ({ Act::toggleParam, tChokeAll }); queue.push ({ Act::commit }); }
                        else if (col == 6) out.cutAll = true;
                        else spModeToggle.fetch_add (1);
                        break;
                    case 3:
                        if (col < 4) writeVelMode = col;
                        else if (col == 4) { pat.clearLane (selectedLane.load()); queue.push ({ Act::commit }); }
                        else if (col == 5) queue.push ({ Act::undo });
                        else if (col == 6) queue.push ({ Act::redo });
                        else { queue.push ({ Act::toggleParam, tSeqLen }); queue.push ({ Act::commit }); }
                        break;
                    default: break;
                }
            }

            void noteOff (int note)
            {
                int col, row;
                if (! padPos (note, col, row)) return;
                if (row == 0) heldLanes &= ~(1u << col);
                else if (row == 2 && col < 4) rollMask &= ~(1 << col);
                else if (row == 2 && col == 4) fillHeld.store (false);
                if (heldLanes == 0 || rollMask == 0) freeRunning = false;
            }

            void aftertouch (int note, int pressure)
            {
                int col, row;
                if (padPos (note, col, row) && row == 0 && (heldLanes & (1u << col)))
                    laneVel[col] = std::max (1, std::min (127, pressure));
            }

            void controller (int cc, int value)
            {
                lastCC.store (cc);
                const int f = cc - firstCC;
                if (f < 0 || f >= 8) return;
                static const Target t[8] = { tSpAmt, tSpBits, tSpRate, tSpFilt, tLaneCut, tLaneDecay, tLanePitch, tSwing };
                queue.push ({ Act::setParam, (uint8_t) t[f], (int8_t) selectedLane.load(), value / 127.0f });
            }

            // ---- ROLL : coups calés sur la grille de l'hôte, ou sur une horloge libre à l'arrêt ----
            //  p0 = position (en noires) au début du bloc, ppqPerSample, n échantillons
            void renderRoll (bool playing, double p0, double ppqPerSample, int n, Out& out)
            {
                const double steps = rollSteps();
                if (steps <= 0.0 || heldLanes == 0) { freeRunning = false; return; }
                const double iv = steps * 0.25;
                double start = p0;
                if (! playing)
                {
                    if (! freeRunning) { freeRunning = true; freePpq = -1.0e-9; }
                    start = freePpq;
                }
                const double end = start + n * ppqPerSample;
                for (double k = std::ceil (start / iv - 1.0e-9); k * iv < end; k += 1.0)
                {
                    const double t = std::max (0.0, k * iv);
                    if (t < start) continue;
                    const int off = std::max (0, std::min (n - 1, (int) ((t - start) / ppqPerSample)));
                    for (int l = 0; l < 8; ++l) if (heldLanes & (1u << l)) out.hit (off, l, laneVel[l]);
                }
                if (! playing) freePpq = end;
            }
        };
    }
}
