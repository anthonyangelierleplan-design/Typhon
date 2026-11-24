# Typhon — Foudre Audio

Boîte à rythme / sampler de batterie (VST3, Windows) pour Ableton Live.
Capture les sons d'EZdrummer 3 une fois pour toutes, puis les rejoue comme des samples :
écriture rapide au séquenceur, choke, grain SP-1200, transient et dynamique par piste, multi-out.

## Nouveautés v2
- 16 ou 32 pas (2 pages, la page peut suivre la lecture).
- Clic droit sur un pas : vélocité en haut, puis Rythme (diviser, ratchets, délier), Pitch (-12..+12), Choke, Jouer à l'envers, Verrous, Effacer.
- Verrous par pas (« Régler ce pas ») : volume, pan, début, décroissance, filtre, envois A / B, position du grain. Point vert sur le pas.
- Filtre par piste (LPF / HPF / BPF + résonance), envois vers 2 bus d'effets.
- Onglets de piste : SAMPLE, GRAIN (moteur granulaire), SLICE (découpe d'un break sur les 8 pistes, avec ou sans pattern), DYN.
- Waveform zoomable (molette), marqueurs de début / fin à la souris, éditeur en grand (double-clic ou « Agrandir »).
- Pitch : Ctrl maintenu = demi-tons entiers.
- Fill : pistes choisies (elles alternent), longueur (1 temps, 2 temps, 1 mesure), grille (1/8, 1/16, 1/32, triolets), autres pistes muettes ou non, crash au retour.
- Onglet MIX : réverbe (bus A), delay calé sur le tempo (bus B), envois et ducking par piste, master (transient + compression / expansion vers le haut ou le bas).
- À l'arrêt de la lecture, tous les sons s'arrêtent (fondu court).

## Bibliothèque de boîtes à rythmes (onglet SONS)
- Dossier racine (réglable) : un sous-dossier par machine, puis des catégories (Bass Drum, HiHat...) ou directement les fichiers.
- « DR-110 BD RR1..RR20 » = variantes de jeu (round robin, alternées automatiquement) ; « Cabasa 1, 2, 3 » = variantes du son.
- Fichiers sans extension lus quand même ; « ._xxx » ignorés ; doublons « RR3 » / « RR3.wav » fusionnés.
- Clic = écouter, double-clic = charger sur la piste conseillée, « Charger le kit complet » = 8 pistes d'un coup.
- Variante : flèches dans la waveform (piste) ou clic droit > Variante (par pas).

## Sortir Typhon sur plusieurs pistes dans Ableton
1. PARAMÈTRES > Sorties > Multi-out (réglé par défaut).
2. Crée une piste audio par élément. Dans « Audio From », choisis la piste de Typhon, puis la sortie (ex. « Out 2 Snare »), et mets Monitor sur « In ».
3. Chaque piste de Typhon choisit sa sortie dans SAMPLE (menu Sortie). Une sortie non utilisée revient sur la sortie principale (piste de Typhon).
Les retours de réverbe / delay et le master sortent sur la sortie principale.

## Séquenceur (8 pistes x 16 ou 32 pas)
- Clic : allumer / éteindre un pas. Clic + glisser vers la droite : lier des pas (note tenue, coupée à la fin).
- Clic droit sur un pas : Normal / Accent / Doux, Diviser en 2, Ratchet x2 x3 x4, Délier, Choke du pas (suit la piste, toujours, laisse sonner), Effacer.
- Clic sur le nom d'une piste : écouter. Clic droit : charger un sample, son de secours, Mute, Gate MIDI, effacer, copier / coller.
- Glisser un WAV / AIFF / FLAC sur une piste : il remplace le son de la piste.
- Swing, FILL (maintenir, clic droit = verrouiller, ou auto toutes les 4 / 8 mesures, intensité), glisser le pattern dans Ableton en clip MIDI.

## Choke
- Par piste (bouton CHOKE), par groupe A à D (ex. hat fermé coupe hat ouvert), ou global (CHOKE GLOBAL : tout coup coupe tout le kit).
- Par pas dans le séquenceur interne.
- Depuis un clip MIDI : Gate MIDI (la durée de la note = durée du son), note C0 = tout couper.

## Clips MIDI
Kick C1, Rim C#1, Snare D1, Ghost D#1, Perc E1, Hat fermé F#1, Hat ouvert A#1, Crash C#2 (carte Typhon ; General MIDI et EZdrummer au choix).
La vélocité règle le volume (potard Vélocité) et choisit la couche de son capturée.

## Piste
- SAMPLE : pitch (vitesse variable), début / fin, décroissance, fondu du choke, vélocité, volume, pan, reverse, groupe, sortie.
- TRANSIENT · DYNAMIQUE : attaque / sustain, compression ou expansion vers le haut ou vers le bas (seuil, ratio, temps, mix). Ratio 1:1 = neutre.
- Grain SP global (12 bits, ~26 kHz sans anti-repliement, filtre de sortie), réglable en bits et fréquence.

## Capture EZdrummer
Onglet CAPTURE : Charger EZdrummer (cherché dans le dossier VST3), choisir le kit et le mix dans sa fenêtre, puis Capturer le kit.
Typhon joue chaque élément à 1 / 2 / 4 / 8 vélocités, avec 1 à 4 variantes, et enregistre un kit dans `Documents/Foudre Audio/Typhon/Kits`.
Par lot : charger un kit dans EZdrummer, « Ajouter l'état actuel à la file », recommencer pour chaque kit, puis « Tout capturer ».
Les kits capturés restent pour un usage personnel (licence Toontrack).

## Paramètres (gardés pour tous les projets : `Foudre Audio/Typhon.settings`)
Qualité Éco / Haute / Ultra (interpolation linéaire, Hermite, sinc 16 points) et qualité au rendu, mode SP authentique / propre,
format de capture, stéréo / multi-out, carte MIDI et canal, thème (Atelier, Nuit, Phosphore, Bel Air), taille de fenêtre, cran au zéro.

## Build
GitHub Actions compile le VST3 Windows à chaque push (release `latest`, fichier `Typhon-VST3-Windows.zip`).
En local : `cmake -B build && cmake --build build --config Release`.
Tests : voir `tools/tests/LISEZMOI.txt`.

## Akai APC64 (mode Custom)

Typhon se pilote depuis l'APC64 en **mode Custom**, sur le **canal 16**.

**Réglage dans l'APC64 Project Editor (une seule fois)**
1. Glisser 4 groupes **Drum Pads** (4×4) sur la grille, canal 16 :
   bas-gauche = note **36** (C1 dans Ableton), bas-droite = **52** (E2), haut-gauche = **68** (G#3), haut-droite = **84** (C5).
2. Les 8 touch faders : *Single (1 CC)*, canal 16, **CC 20 à 27**.
3. Couleurs conseillées (Pad Off) : rangée 1 orange, rangée 2 gris, rangée 3 bleu, rangée 4 blanc, 4 rangées du haut gris foncé.
4. *Send Project* vers un slot de l'APC64.

**Dans Ableton** : une piste MIDI « APC → Typhon », entrée = APC64 canal 16, Monitor = In, sortie = la piste de Typhon.
Dans Typhon : Paramètres → Akai APC64 → On.

**La grille (rangée 1 = en bas)**

| Rangée | Pads 1-4 | Pads 5-8 |
|---|---|---|
| 8 → 5 | les 32 pas de la piste sélectionnée (rangée 8 = pas 1-8 … rangée 5 = pas 25-32) | |
| 4 | vélocité d'écriture : Doux · Normal · Accent · Au toucher | Effacer la piste · Annuler · Rétablir · 16/32 pas |
| 3 | ROLL (maintenir) 1/8 · 1/16 · 1/32 · 1/16T | FILL (maintenir) · Choke global · Couper tout · SP Authentique/Propre |
| 2 | Mute pistes 1-4 | Mute pistes 5-8 |
| 1 | jouer Kick · Snare · Ghost · Rim | jouer Hat fermé · Hat ouvert · Crash · Perc |

Jouer une piste la sélectionne. Roll : maintenir une cadence + un ou plusieurs pads de piste ; calé sur la grille quand Ableton joue, au tempo sinon ; l'aftertouch du pad règle la force du roll.

**Faders** : Grain SP · Bits · Fréquence · Filtre SP | Filtre · Decay · Pitch de la piste sélectionnée | Swing.
