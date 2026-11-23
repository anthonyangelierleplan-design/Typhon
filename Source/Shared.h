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

    inline juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (n, true, true, 3); }

    inline juce::String pid (int lane, const char* what) { return "l" + juce::String (lane) + "_" + what; }

    struct Theme
    {
        const char* name;
        juce::Colour bg, panel, panel2, head, headTx, headSub, lcdBg, lcdTx, acc, acc2, off, offBeat, tx, sub, knob, knobEdge, bottom, bottomTx;
    };

    inline const Theme& theme (int i)
    {
        static const Theme t[4] = {
            { "Atelier",   juce::Colour (0xff1d1d1f), juce::Colour (0xff252528), juce::Colour (0xff2a2a2d), juce::Colour (0xffcfc8bb), juce::Colour (0xff1d1d1f), juce::Colour (0xff3b3a37),
                           juce::Colour (0xff0f1a12), juce::Colour (0xff7dffa0), juce::Colour (0xffff5a1f), juce::Colour (0xffb84a22), juce::Colour (0xff2a2a2d), juce::Colour (0xff38383d),
                           juce::Colour (0xffe8e6e1), juce::Colour (0xffa8a8a8), juce::Colour (0xff333338), juce::Colour (0xff48484e), juce::Colour (0xffcfc8bb), juce::Colour (0xff1d1d1f) },
            { "Nuit",      juce::Colour (0xff0d0f14), juce::Colour (0xff141924), juce::Colour (0xff1a1f2b), juce::Colour (0xff161b26), juce::Colour (0xffd6e4ff), juce::Colour (0xff8d9bb5),
                           juce::Colour (0xff06121c), juce::Colour (0xff4fc3ff), juce::Colour (0xff4fc3ff), juce::Colour (0xff2a6f99), juce::Colour (0xff1a1f2b), juce::Colour (0xff242b3b),
                           juce::Colour (0xffd6e4ff), juce::Colour (0xff8d9bb5), juce::Colour (0xff1f2533), juce::Colour (0xff36405a), juce::Colour (0xff161b26), juce::Colour (0xffd6e4ff) },
            { "Phosphore", juce::Colour (0xff050805), juce::Colour (0xff0b120c), juce::Colour (0xff0f1a12), juce::Colour (0xff0b120c), juce::Colour (0xff7dffa0), juce::Colour (0xff6fae80),
                           juce::Colour (0xff020502), juce::Colour (0xff7dffa0), juce::Colour (0xff7dffa0), juce::Colour (0xff3c8a50), juce::Colour (0xff0f1a12), juce::Colour (0xff17281b),
                           juce::Colour (0xffb9f5c8), juce::Colour (0xff6fae80), juce::Colour (0xff0f1a12), juce::Colour (0xff2c5a38), juce::Colour (0xff0b120c), juce::Colour (0xff7dffa0) },
            { "Bel Air",   juce::Colour (0xffefe6d2), juce::Colour (0xffe4d9c1), juce::Colour (0xffddd2bb), juce::Colour (0xff2a8c8c), juce::Colour (0xfffbf7ee), juce::Colour (0xffd8f0ee),
                           juce::Colour (0xff1f3b3b), juce::Colour (0xffbff0e6), juce::Colour (0xffc0392b), juce::Colour (0xffe07b5f), juce::Colour (0xffddd2bb), juce::Colour (0xffcfc3a9),
                           juce::Colour (0xff2b2620), juce::Colour (0xff5c544a), juce::Colour (0xffc9c1ae), juce::Colour (0xffb8b8b8), juce::Colour (0xff2a8c8c), juce::Colour (0xfffbf7ee) }
        };
        return t[juce::jlimit (0, 3, i)];
    }
}
