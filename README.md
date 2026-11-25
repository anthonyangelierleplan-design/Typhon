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

## Akai APC64 : la grille 8×8 devient le séquenceur

Typhon ouvre lui-même l'APC64 et le passe en mode DAW, comme Ableton le fait : les pas s'allument, la tête de lecture défile.

**Réglage** : dans Ableton, Préférences > Link/Tempo/MIDI, mets la surface de contrôle APC64 sur « Aucun » (et les ports APC64 sans Piste ni Télécommande). Dans Typhon : Paramètres > Akai APC64 > On. Repasse sur Off pour rendre l'APC64 à Ableton.

| Commande | Effet |
|---|---|
| Scène 1 · PISTES | 8 pistes × 8 pas (rangée du haut = Kick) · ◀ ▶ = page de 8 pas |
| Scène 2 · PISTE | les 32 pas de la piste choisie en haut ; rangée 4 = vélocité ; rangée 3 = ratchet 1-4, division, reverse, pitch −/+ du dernier pas touché ; rangée 2 = jouer les pistes ; rangée 1 = mutes |
| Scène 3 / 4 | 16/32 pas · suivre la lecture |
| Scène 5 (maintenir) | FILL |
| Scènes 6-8 (maintenir) | ROLL 1/8, 1/16, 1/32 sur la piste choisie |
| Boutons de piste | choisir la piste (bouton Mute allumé : muter) · ▲ ▼ = piste précédente / suivante |
| Shift + pad | accent / note fantôme |
| Maintenir un pas + strip Filtre, Decay ou Pitch | verrou sur ce pas (le pas reste allumé) ; un pas allumé tapé seul s'éteint au relâchement |
| Clear maintenu + pad ou piste | effacer la piste |
| Duplicate | recopier la page sur la suivante (ou 1-16 → 17-32) |
| Undo / Shift + Undo | annuler / rétablir |
| Stop | couper tous les sons |
| Touch strips | Grain SP · Bits · Fréquence · Filtre SP · Filtre, Decay, Pitch de la piste · Swing |

## Stretch

Onglet STRETCH de chaque piste : **Simple** (allonge sans changer la hauteur) ou **Paulstretch** (nappes extrêmes), facteur ×0,5 à ×32, taille de fenêtre, ou calage sur 1 à 32 pas au tempo du projet. Le calcul se fait hors du fil audio ; l'original reste gardé.

## v3 : façon TR-1000

- **Patterns A à H + FILL** en haut de la grille : le changement se fait au début de la boucle suivante (clignote en attendant). Clic droit : copier vers un autre pattern, effacer. Sur l'APC64 : Shift + scène 1 à 8.
- **Fill écrit** : choisis FILL, écris ton fill, reviens sur A..H ; Réglages fill > Source = Pattern FILL.
- **Onglet PAS** (clic sur un pas) : vélocité, sous-pas 1 / 1/2 / 1/3 / 1/4, division, **flam**, **probabilité**, **décalage hors grille** (en ms au tempo), pitch, variante, durée, choke, reverse, verrous.
- **Longueur par piste** (clic droit sur le nom de la piste) : polyrythmies, 1 à 32 pas.
- **ROLL** et **STEP LOOP** à maintenir sur la façade (clic droit : cadence / longueur).
- Polices embarquées sous licence OFL (Resources/fonts).

## Finitions (audit d'ergonomie)

- **Une seule fiche par pas** : l'onglet PAS. Le clic droit sur un pas ne garde que vélocité, condition, verrous, délier, coller, effacer.
- **Verrous à la volée** : Alt + tourner un potard de la piste (volume, pan, début, decay, filtre, envois, position du grain) = verrou sur le pas choisi, ou sur tous les pas allumés de la sélection. À l'APC64 : maintenir le pas et toucher un strip.
- **Conditions par pas** (onglet PAS, à côté de la probabilité) : FILL (ne joue que pendant un fill, même si le fill remplace la piste), PAS FILL, et 1:2, 2:2, 1:3… 4:4 (joue au tour A sur B de la piste). L'export MIDI déroule assez de mesures pour les placer.
- **Vitesse par piste** (clic droit sur le nom) : x2, x1,5 (triolets), x1, x3/4, x1/2, x1/4. Avec la longueur par piste, polyrythmies façon TR-8S / Elektron. La tête de lecture suit chaque piste.
- **MOTION REC** (● REC en haut du panneau de piste) : en lecture, les mouvements des potards verrouillables s'enregistrent sur les pas joués. Clic droit sur la piste > Effacer les verrous de la piste.
- **SONS** regroupe la bibliothèque et la capture EZdrummer ; **MIX** devient une vraie table (volume, pan, envois, duck) et accueille CHOKE GLOBAL.
