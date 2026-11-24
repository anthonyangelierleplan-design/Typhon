#pragma once
#include <JuceHeader.h>
#include "Dsp.h"

// ============================================================================
//  Typhon — données partagées (noms, cartes MIDI, thèmes)
//  Ne jamais réordonner ces listes : elles sont enregistrées dans les projets.
// ============================================================================
namespace typhon
{
    static const char* const laneNames[numLanes] = { "Kick", "Snare", "Ghost", "Rim", "Hat ferm\xc3\xa9", "Hat ouvert", "Crash", "Perc" };
    static const char* const laneShort[numLanes] = { "Kick", "Snare", "Ghost", "Rim", "Hat F", "Hat O", "Crash", "Perc" };
    inline juce::String laneName (int i)  { return juce::String::fromUTF8 (laneNames[juce::jlimit (0, numLanes - 1, i)]); }
    inline juce::String laneShortName (int i) { return juce::String::fromUTF8 (laneShort[juce::jlimit (0, numLanes - 1, i)]); }
    static const bool laneChokeDefault[numLanes] = { true, true, true, false, true, true, false, false };
    static const int laneGroupDefault[numLanes]  = { 0, 1, 1, 0, 2, 2, 0, 0 };   // 0 aucun, 1 A, 2 B, 3 C, 4 D

    // Notes MIDI d'entrée : Typhon, General MIDI, EZdrummer
    static const int midiMaps[3][numLanes] = {
        { 36, 38, 39, 37, 42, 46, 49, 40 },
        { 36, 38, 40, 37, 42, 46, 49, 45 },
        { 36, 38, 40, 37, 42, 46, 49, 48 }
    };
    // Notes EZdrummer capturées par défaut pour chaque piste
    static const int captureNoteDefault[numLanes] = { 36, 38, 38, 37, 42, 46, 49, 48 };

    // Réglages verrouillables par pas (ordre = indice dans Pattern::locks)
    enum LockId { lkVol = 0, lkPan, lkStart, lkDecay, lkCut, lkGpos, lkSendA, lkSendB };
    static const char* const lockParam[numLocks] = { "vol", "pan", "start", "decay", "fcut", "gpos", "sendA", "sendB" };
    inline int lockIndexFor (const juce::String& what) { for (int k = 0; k < numLocks; ++k) if (what == lockParam[k]) return k; return -1; }
    static const bool laneDuckDefault[numLanes] = { false, true, true, true, true, true, true, true };

    inline juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); }

    inline juce::String pid (int lane, const char* what) { return "l" + juce::String (lane) + "_" + what; }

    struct Theme
    {
        const char* name;
        juce::Colour bg, panel, panel2, head, headTx, headSub, lcdBg, lcdTx, acc, acc2, off, offBeat, tx, sub, knob, knobEdge, bottom, bottomTx;
        juce::Colour groups[4];   // couleurs des pas par groupe de 4 (façon TR-808), transparent = couleur d'action
        bool wood = false;        // flancs en bois (façon LinnDrum)
        float grain = 0.06f;      // grain de peinture de la façade
        bool hasGroups() const { return groups[0].getAlpha() != 0; }
    };

    constexpr int numThemes = 6;
    inline const Theme& theme (int i)
    {
        static const Theme t[numThemes] = {
            { "Atelier", juce::Colour (0xff1d1d1f), juce::Colour (0xff252528), juce::Colour (0xff2d2d31), juce::Colour (0xffd3ccbe), juce::Colour (0xff1d1d1f), juce::Colour (0xff4a4740), juce::Colour (0xff0d1810), juce::Colour (0xff8dffb0), juce::Colour (0xffff5a1f), juce::Colour (0xffb84a22), juce::Colour (0xff2b2b2f), juce::Colour (0xff3d3d43), juce::Colour (0xffecebe6), juce::Colour (0xffa6a6ab), juce::Colour (0xff2a2a2e), juce::Colour (0xff55555b), juce::Colour (0xffcfc8bb), juce::Colour (0xff1d1d1f),
              { juce::Colour(), juce::Colour(), juce::Colour(), juce::Colour() }, false, 0.06f },
            { "Nuit", juce::Colour (0xff0d0f14), juce::Colour (0xff151a25), juce::Colour (0xff1c2230), juce::Colour (0xff1b2030), juce::Colour (0xffe4e6ef), juce::Colour (0xff9aa3b8), juce::Colour (0xff0c1020), juce::Colour (0xffffc36b), juce::Colour (0xff7aa2ff), juce::Colour (0xff3f5c99), juce::Colour (0xff1a1f2b), juce::Colour (0xff2a3244), juce::Colour (0xffe4e6ef), juce::Colour (0xff8d97ad), juce::Colour (0xff232a3a), juce::Colour (0xff45516e), juce::Colour (0xff1b2030), juce::Colour (0xffe4e6ef),
              { juce::Colour(), juce::Colour(), juce::Colour(), juce::Colour() }, false, 0.06f },
            { "Phosphore", juce::Colour (0xff050805), juce::Colour (0xff0b120c), juce::Colour (0xff101b13), juce::Colour (0xff0b120c), juce::Colour (0xffb9ffcb), juce::Colour (0xff6fae80), juce::Colour (0xff020502), juce::Colour (0xffffb000), juce::Colour (0xff43ff7a), juce::Colour (0xff2c8a48), juce::Colour (0xff0f1a12), juce::Colour (0xff1c3121), juce::Colour (0xffc4f7d1), juce::Colour (0xff6fae80), juce::Colour (0xff101b13), juce::Colour (0xff2c5a38), juce::Colour (0xff0b120c), juce::Colour (0xffb9ffcb),
              { juce::Colour(), juce::Colour(), juce::Colour(), juce::Colour() }, false, 0.06f },
            { "Bel Air", juce::Colour (0xfff3ede2), juce::Colour (0xffe8dfcf), juce::Colour (0xffddd2bb), juce::Colour (0xff2a2522), juce::Colour (0xfff3ede2), juce::Colour (0xffc9bfae), juce::Colour (0xff2a2522), juce::Colour (0xffffd9e6), juce::Colour (0xffe0457b), juce::Colour (0xffb8365f), juce::Colour (0xffddd2bb), juce::Colour (0xffc9bb9f), juce::Colour (0xff1f1b18), juce::Colour (0xff5a5148), juce::Colour (0xff2a2522), juce::Colour (0xff6b6258), juce::Colour (0xffe2d9c6), juce::Colour (0xff1f1b18),
              { juce::Colour(), juce::Colour(), juce::Colour(), juce::Colour() }, false, 0.06f },
            { "TR-808", juce::Colour (0xff1a1a1a), juce::Colour (0xff242424), juce::Colour (0xff2f2f2f), juce::Colour (0xff2b2b2b), juce::Colour (0xfff1ede4), juce::Colour (0xffbdb6a8), juce::Colour (0xff1c0705), juce::Colour (0xffff4d2e), juce::Colour (0xfff07d1a), juce::Colour (0xffa85a17), juce::Colour (0xff2c2c2c), juce::Colour (0xff3e3e3e), juce::Colour (0xfff1ede4), juce::Colour (0xffaaa59b), juce::Colour (0xffe9e2cf), juce::Colour (0xffbdb5a0), juce::Colour (0xff2b2b2b), juce::Colour (0xfff1ede4),
              { juce::Colour (0xffd62d20), juce::Colour (0xfff07d1a), juce::Colour (0xfff2c12e), juce::Colour (0xffece4cf) }, false, 0.06f },
            { "Linn", juce::Colour (0xff16130f), juce::Colour (0xff1e1a17), juce::Colour (0xff27221d), juce::Colour (0xff1e1a17), juce::Colour (0xffefe6d6), juce::Colour (0xffb8a888), juce::Colour (0xff140a04), juce::Colour (0xffff8a3d), juce::Colour (0xffe8b04a), juce::Colour (0xff9c7530), juce::Colour (0xff27221d), juce::Colour (0xff3a322a), juce::Colour (0xffefe6d6), juce::Colour (0xffa8997f), juce::Colour (0xff8a6a45), juce::Colour (0xffb08d5e), juce::Colour (0xff1e1a17), juce::Colour (0xffefe6d6),
              { juce::Colour(), juce::Colour(), juce::Colour(), juce::Colour() }, true, 0.06f },
        };
        return t[juce::jlimit (0, numThemes - 1, i)];
    }
}
