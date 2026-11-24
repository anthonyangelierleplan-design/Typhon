#pragma once
// ============================================================================
//  Bibliothèque de boîtes à rythmes : un dossier racine, un sous-dossier par
//  machine (LinnDrum, TR-808...), puis des catégories (Bass Drum, HiHat...)
//  ou directement les fichiers.
//  - « DR-110 BD RR1 ... RR20 » : variantes de jeu (round robin) d'un même son
//  - « Cabasa 1, Cabasa 2 »     : variantes (accords / versions) d'un même son
//  - les fichiers « ._xxx » (restes de macOS) sont ignorés
// ============================================================================
#include <JuceHeader.h>
#include "Dsp.h"

namespace typhon
{
    // rôle = piste de Typhon visée (0 kick ... 7 perc), -1 inconnu
    struct LibSound
    {
        juce::String machine, category, name;
        int role = -1;
        std::vector<std::vector<juce::File>> variants;   // variante -> fichiers round robin
        int totalFiles() const { int n = 0; for (auto& v : variants) n += (int) v.size(); return n; }
    };

    struct LibMachine
    {
        juce::String name;
        std::vector<LibSound> sounds;
    };

    inline juce::String lowerWords (const juce::String& s)
    {
        return " " + s.toLowerCase().replaceCharacters ("_-.()[]", "       ") + " ";
    }

    // Devine la piste à partir de la catégorie et du nom
    inline int guessRole (const juce::String& category, const juce::String& name)
    {
        const auto c = lowerWords (category), n = lowerWords (name), both = c + n;
        auto has = [] (const juce::String& hay, std::initializer_list<const char*> keys) { for (auto k : keys) if (hay.contains (k)) return true; return false; };
        if (has (both, { " open", " oh ", "openhat", "open hat", "hh open", "hihat open", " ohh" })) return 5;
        if (has (both, { "bass drum", " bd ", " bd", "kick", " bassdrum", " kik", "bass dr" })) return 0;
        if (has (both, { "rim", "side stick", "sidestick", " rs ", "stick" })) return 3;
        if (has (both, { "snare", " sd ", " sn ", " snr" })) return 1;
        if (has (both, { "hihat", "hi hat", "hi-hat", " hh ", " ch ", "closed", " hat" })) return 4;
        if (has (both, { "cymbal", "crash", "ride", " cym", "china", "splash" })) return 6;
        if (has (both, { "clap", " cp ", "tom", "conga", "cowbell", " cb ", "cabasa", "clave", "shaker", "tamb", "bongo", "perc", "maraca", "agogo", "guiro", "timbale", "block" })) return 7;
        return -1;
    }

    // « DR-110 BD RR12 » -> base « DR-110 BD », rr 12 ; « Cabasa 2 » -> base « Cabasa », variante 2
    inline void splitName (juce::String stem, juce::String& base, int& rr, int& var)
    {
        rr = 0; var = 0;
        stem = stem.trim();
        auto lastWord = stem.fromLastOccurrenceOf (" ", false, false);
        if (lastWord.length() > 2 && lastWord.startsWithIgnoreCase ("rr") && lastWord.substring (2).containsOnly ("0123456789"))
        {
            rr = lastWord.substring (2).getIntValue();
            stem = stem.upToLastOccurrenceOf (" ", false, false).trim();
            lastWord = stem.fromLastOccurrenceOf (" ", false, false);
        }
        if (lastWord.isNotEmpty() && lastWord.containsOnly ("0123456789") && stem.contains (" "))
        {
            var = lastWord.getIntValue();
            stem = stem.upToLastOccurrenceOf (" ", false, false).trim();
        }
        base = stem;
    }

    // Enlève le nom de la machine en tête du nom de son (« DR-110 BD » -> « BD »)
    inline juce::String prettyName (const juce::String& base, const juce::String& machine)
    {
        juce::StringArray mw; mw.addTokens (machine, " ", "");
        juce::String b = base;
        for (auto& w : mw)
            if (w.length() >= 3 && b.startsWithIgnoreCase (w + " ")) b = b.substring (w.length() + 1).trim();
        return b.isEmpty() ? base : b;
    }

    inline bool isAudioFile (const juce::File& f)
    {
        if (f.getFileName().startsWith (".")) return false;      // ._fichier : restes de macOS
        if (f.hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
        if (f.hasFileExtension ("asd;txt;pdf;png;jpg;jpeg;gif;json;xml;ini;db;nfo;rtf;doc;docx;zip;rar;als;adg;adv;mid;midi;exe;dll;url;lnk")) return false;
        // fichier sans extension (exporté depuis un Mac) : on regarde l'en-tête
        juce::FileInputStream in (f);
        if (! in.openedOk()) return false;
        char h[4] {};
        in.read (h, 4);
        return std::memcmp (h, "RIFF", 4) == 0 || std::memcmp (h, "FORM", 4) == 0 || std::memcmp (h, "fLaC", 4) == 0;
    }

    inline LibMachine scanMachine (const juce::File& dir)
    {
        LibMachine m; m.name = dir.getFileName();
        struct Entry { juce::String category, base; int rr, var; juce::File file; };
        std::vector<Entry> entries;
        for (const auto& it : juce::RangedDirectoryIterator (dir, true, "*", juce::File::findFiles))
        {
            const auto f = it.getFile();
            if (! isAudioFile (f)) continue;
            juce::String category = f.getParentDirectory() == dir ? juce::String() : f.getParentDirectory().getRelativePathFrom (dir).replaceCharacter ('\\', '/');
            Entry e; e.category = category; e.file = f;
            // « Kick.wav.wav » : on enlève toutes les extensions audio
            auto stem = f.getFileNameWithoutExtension();
            while (juce::File (stem).hasFileExtension ("wav;aif;aiff;flac")) stem = juce::File (stem).getFileNameWithoutExtension();
            splitName (stem, e.base, e.rr, e.var);
            entries.push_back (e);
        }
        // regroupement : (catégorie, base) -> variantes -> round robin
        std::map<juce::String, LibSound> byKey;
        std::map<juce::String, std::map<int, std::vector<std::pair<int, juce::File>>>> parts;
        for (auto& e : entries)
        {
            const auto key = e.category + "|" + e.base.toLowerCase();
            auto& s = byKey[key];
            if (s.name.isEmpty())
            {
                s.machine = m.name; s.category = e.category; s.name = prettyName (e.base, m.name);
                s.role = guessRole (e.category, e.base);
            }
            parts[key][e.var].push_back ({ e.rr, e.file });
        }
        for (auto& [key, s] : byKey)
        {
            for (auto& [var, files] : parts[key])
            {
                std::sort (files.begin(), files.end(), [] (auto& a, auto& b) { return a.first != b.first ? a.first < b.first : a.second.getFileName() < b.second.getFileName(); });
                // même fichier en double (« RR3 » et « RR3.wav ») : on n'en garde qu'un
                std::vector<juce::File> list;
                int lastRr = -1;
                for (auto& [rr, f] : files) { if (rr > 0 && rr == lastRr) continue; list.push_back (f); lastRr = rr; }
                s.variants.push_back (list);
            }
            m.sounds.push_back (s);
        }
        std::sort (m.sounds.begin(), m.sounds.end(), [] (const LibSound& a, const LibSound& b)
        {
            const int ra = a.role < 0 ? 99 : a.role, rb = b.role < 0 ? 99 : b.role;
            return ra != rb ? ra < rb : (a.category != b.category ? a.category < b.category : a.name < b.name);
        });
        return m;
    }

    inline std::vector<LibMachine> scanLibrary (const juce::File& root)
    {
        std::vector<LibMachine> out;
        if (! root.isDirectory()) return out;
        juce::Array<juce::File> dirs;
        for (const auto& it : juce::RangedDirectoryIterator (root, false, "*", juce::File::findDirectories)) dirs.add (it.getFile());
        dirs.sort();
        for (auto& d : dirs)
        {
            auto m = scanMachine (d);
            if (! m.sounds.empty()) out.push_back (std::move (m));
        }
        return out;
    }

    // Choisit un son par piste pour charger le kit complet d'une machine
    inline std::array<int, numLanes> pickKit (const LibMachine& m)
    {
        std::array<int, numLanes> pick; pick.fill (-1);
        for (int i = 0; i < (int) m.sounds.size(); ++i)
        {
            const int r = m.sounds[(size_t) i].role;
            if (r >= 0 && r < numLanes && pick[(size_t) r] < 0) pick[(size_t) r] = i;
        }
        if (pick[2] < 0) pick[2] = pick[1];                  // ghost = la snare jouée doucement
        if (pick[5] < 0) pick[5] = pick[4];                  // pas de hat ouvert : le hat fermé
        if (pick[7] < 0)                                     // perc : le premier son inconnu restant
            for (int i = 0; i < (int) m.sounds.size(); ++i) if (m.sounds[(size_t) i].role < 0) { pick[7] = i; break; }
        return pick;
    }

    // Sérialisation d'un son (pour retrouver la piste à l'ouverture du projet)
    inline juce::String encodeSound (const LibSound& s)
    {
        juce::StringArray vars;
        for (auto& v : s.variants) { juce::StringArray f; for (auto& x : v) f.add (x.getFullPathName()); vars.add (f.joinIntoString ("|")); }
        return "lib:" + s.machine + " - " + s.name + "\n" + vars.joinIntoString ("\n");
    }
    inline bool decodeSound (const juce::String& txt, LibSound& s)
    {
        if (! txt.startsWith ("lib:")) return false;
        juce::StringArray lines; lines.addLines (txt.substring (4));
        if (lines.size() < 2) return false;
        s.name = lines[0];
        for (int i = 1; i < lines.size(); ++i)
        {
            juce::StringArray f; f.addTokens (lines[i], "|", "");
            std::vector<juce::File> v;
            for (auto& p : f) if (p.isNotEmpty()) v.push_back (juce::File (p));
            if (! v.empty()) s.variants.push_back (v);
        }
        return ! s.variants.empty();
    }
}
