#pragma once
// ============================================================================
//  Typhon — moteur audio (aucune dépendance JUCE : se teste seul)
//  Voix de lecture de samples, choke, transient designer, dynamique
//  (compression / expansion vers le haut et vers le bas), grain SP-1200,
//  séquenceur (pas, ratchets, division, liaisons, swing, fill).
// ============================================================================
#include <atomic>
#include <cmath>
#ifndef M_PI
 #define M_PI 3.14159265358979323846
#endif
#include <cstdint>
#include <memory>
#include <vector>
#include <algorithm>
#include <string>
#include <limits>
#include <complex>
#include <random>
#include <functional>

namespace typhon
{
    static constexpr int numLanes = 8;
    static constexpr int numSteps = 32;      // capacité du pattern (la longueur jouée est 16 ou 32)
    static constexpr int numLocks = 8;       // réglages verrouillables par pas
    static constexpr int maxVoicesPerLane = 6;

    inline float dbToGain (float db) { return db <= -99.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g)  { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
    inline float clampf (float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

    // ------------------------------------------------------------------------
    //  Un pas du séquenceur, rangé dans un entier 32 bits (lecture sans verrou)
    //  bits 0..6   vélocité (0 = pas éteint)
    //  bits 7..8   ratchet - 1 (1..4 coups)
    //  bit  9      divisé en deux
    //  bits 10..14 longueur de liaison - 1 (1..32 pas)
    //  bits 15..16 choke du pas : 0 = suit la piste, 1 = toujours, 2 = laisse sonner
    //  bits 17..21 pitch du pas + 12 (-12..+12 demi-tons)
    //  bit  22     coup joué à l'envers
    //  bits 23..24 durée du pas : 0 = suit la piste, 1 = limitée au pas, 2 = libre
    //  bits 25..28 variante du son : 0 = suit la piste, 1..15 = variante 1..15
    // ------------------------------------------------------------------------
    struct Step
    {
        int vel = 0, ratchet = 1, len = 1, choke = 0, pitch = 0, dur = 0, var = 0;
        bool split = false, rev = false;

        bool on() const { return vel > 0; }

        uint32_t pack() const
        {
            return (uint32_t) (vel & 127)
                 | ((uint32_t) ((std::max (1, std::min (4, ratchet)) - 1) & 3) << 7)
                 | ((uint32_t) (split ? 1 : 0) << 9)
                 | ((uint32_t) ((std::max (1, std::min (32, len)) - 1) & 31) << 10)
                 | ((uint32_t) (choke & 3) << 15)
                 | ((uint32_t) ((std::max (-12, std::min (12, pitch)) + 12) & 31) << 17)
                 | ((uint32_t) (rev ? 1 : 0) << 22)
                 | ((uint32_t) (dur & 3) << 23)
                 | ((uint32_t) (std::max (0, std::min (15, var)) & 15) << 25);
        }
        static Step unpack (uint32_t v)
        {
            Step s;
            s.vel = (int) (v & 127);
            s.ratchet = (int) ((v >> 7) & 3) + 1;
            s.split = ((v >> 9) & 1) != 0;
            s.len = (int) ((v >> 10) & 31) + 1;
            s.choke = (int) ((v >> 15) & 3);
            s.pitch = (int) ((v >> 17) & 31) - 12;
            if (s.pitch < -12 || s.pitch > 12) s.pitch = 0;
            s.rev = ((v >> 22) & 1) != 0;
            s.dur = (int) ((v >> 23) & 3); if (s.dur > 2) s.dur = 0;
            s.var = (int) ((v >> 25) & 15);
            return s;
        }
    };

    struct Pattern
    {
        std::atomic<uint32_t> steps[numLanes][numSteps];
        // Verrous par pas : NaN = pas de verrou (valeur réelle du réglage sinon)
        std::atomic<float> locks[numLanes][numSteps][numLocks];
        Pattern() { clear(); }
        void clear()
        {
            for (auto& l : steps) for (auto& s : l) s.store (0);
            for (auto& l : locks) for (auto& st : l) for (auto& v : st) v.store (std::numeric_limits<float>::quiet_NaN());
        }
        float lock (int lane, int step, int k) const { return locks[lane][step][k].load (std::memory_order_relaxed); }
        void setLock (int lane, int step, int k, float v) { locks[lane][step][k].store (v, std::memory_order_relaxed); }
        bool hasLocks (int lane, int step) const
        {
            for (int k = 0; k < numLocks; ++k) if (! std::isnan (lock (lane, step, k))) return true;
            return false;
        }
        void clearLocks (int lane, int step) { for (int k = 0; k < numLocks; ++k) setLock (lane, step, k, std::numeric_limits<float>::quiet_NaN()); }
        Step get (int lane, int step) const { return Step::unpack (steps[lane][step].load (std::memory_order_relaxed)); }
        void set (int lane, int step, const Step& s) { steps[lane][step].store (s.pack(), std::memory_order_relaxed); }

        // Le pas j est-il recouvert par la liaison d'un pas précédent ? renvoie l'indice du pas de tête, ou -1
        int headOf (int lane, int j) const
        {
            for (int k = j - 1; k >= 0; --k)
            {
                auto s = get (lane, k);
                if (s.on()) return (k + s.len - 1 >= j) ? k : -1;
            }
            return -1;
        }
    };

    // ------------------------------------------------------------------------
    //  Un sample : stéréo, à sa propre fréquence d'échantillonnage
    // ------------------------------------------------------------------------
    struct Sample
    {
        std::vector<float> l, r;
        double rate = 44100.0;
        int length() const { return (int) l.size(); }
    };

    // Couches de vélocité, chacune avec ses variantes (round robin)
    struct SampleSet
    {
        std::vector<std::vector<std::shared_ptr<Sample>>> layers; // du plus doux au plus fort
        std::vector<std::vector<std::shared_ptr<Sample>>> variants; // sons de machine : variante -> round robin (si non vide, prime sur layers)
        std::vector<int> layerTopVel;                             // vélocité max de chaque couche
        std::string name;

        bool empty() const { return (layers.empty() || layers[0].empty()) && variants.empty(); }
        int numVariants() const { return (int) variants.size(); }
        // échantillon affiché (forme d'onde)
        const Sample* display (int variant = 0) const
        {
            if (! variants.empty()) { const auto& v = variants[(size_t) std::max (0, std::min ((int) variants.size() - 1, variant))]; return v.empty() ? nullptr : v[0].get(); }
            return (layers.empty() || layers.back().empty()) ? nullptr : layers.back()[0].get();
        }

        const Sample* pick (int vel, uint32_t& rr, int variant = 0) const
        {
            if (! variants.empty())
            {
                const auto& v = variants[(size_t) std::max (0, std::min ((int) variants.size() - 1, variant))];
                return v.empty() ? nullptr : v[(rr++) % v.size()].get();
            }
            if (empty()) return nullptr;
            size_t li = layers.size() - 1;
            for (size_t i = 0; i < layerTopVel.size() && i < layers.size(); ++i)
                if (vel <= layerTopVel[i]) { li = i; break; }
            auto& layer = layers[li];
            if (layer.empty()) return layers[0][0].get();
            return layer[(rr++) % layer.size()].get();
        }
    };

    // ------------------------------------------------------------------------
    //  Interpolation : 0 = linéaire (Éco), 1 = Hermite 4 points (Haute),
    //  2 = sinc fenêtrée 16 points (Ultra)
    // ------------------------------------------------------------------------
    inline float sampleAt (const std::vector<float>& d, int i)
    {
        return (i >= 0 && i < (int) d.size()) ? d[(size_t) i] : 0.0f;
    }

    // sinc fenêtrée 16 points (fenêtre de Blackman-Harris), table précalculée
    struct SincTable
    {
        static constexpr int taps = 16, phases = 1024;
        std::vector<float> t;
        SincTable() : t ((size_t) (phases + 1) * taps)
        {
            for (int ph = 0; ph <= phases; ++ph)
            {
                const double f = (double) ph / phases;
                double sum = 0.0;
                for (int k = 0; k < taps; ++k)
                {
                    const double x = (double) (k - taps / 2 + 1) - f;          // -7..8 - f
                    const double sinc = std::fabs (x) < 1e-9 ? 1.0 : std::sin (M_PI * x) / (M_PI * x);
                    const double n = (x + taps / 2.0) / taps;                   // 0..1
                    const double w = 0.35875 - 0.48829 * std::cos (2 * M_PI * n) + 0.14128 * std::cos (4 * M_PI * n) - 0.01168 * std::cos (6 * M_PI * n);
                    t[(size_t) ph * taps + (size_t) k] = (float) (sinc * w);
                    sum += sinc * w;
                }
                for (int k = 0; k < taps; ++k) t[(size_t) ph * taps + (size_t) k] = (float) (t[(size_t) ph * taps + (size_t) k] / sum);
            }
        }
    };

    inline float interp (const std::vector<float>& d, double pos, int quality)
    {
        const int i = (int) std::floor (pos);
        const float f = (float) (pos - i);
        if (quality <= 0)
            return sampleAt (d, i) + f * (sampleAt (d, i + 1) - sampleAt (d, i));
        if (quality == 1)
        {
            const float xm1 = sampleAt (d, i - 1), x0 = sampleAt (d, i), x1 = sampleAt (d, i + 1), x2 = sampleAt (d, i + 2);
            const float c = (x1 - xm1) * 0.5f;
            const float v = x0 - x1;
            const float w = c + v;
            const float a = w + v + (x2 - x0) * 0.5f;
            const float b = w + a;
            return (((a * f) - b) * f + c) * f + x0;
        }
        static const SincTable table;
        const float fp = f * SincTable::phases;
        const int p0 = (int) fp;
        const float pf = fp - (float) p0;
        const float* c0 = &table.t[(size_t) p0 * SincTable::taps];
        const float* c1 = &table.t[(size_t) std::min (p0 + 1, SincTable::phases) * SincTable::taps];
        float acc = 0.0f;
        const int base = i - SincTable::taps / 2 + 1;
        if (base >= 0 && base + SincTable::taps <= (int) d.size())
        {
            const float* x = d.data() + base;
            for (int k = 0; k < SincTable::taps; ++k) acc += x[k] * (c0[k] + pf * (c1[k] - c0[k]));
        }
        else
            for (int k = 0; k < SincTable::taps; ++k) acc += sampleAt (d, base + k) * (c0[k] + pf * (c1[k] - c0[k]));
        return acc;
    }

    // ------------------------------------------------------------------------
    //  Réglages d'une piste (lus une fois par bloc, lissés dans la voix)
    // ------------------------------------------------------------------------
    struct LaneParams
    {
        float pitch = 0.0f;      // demi-tons
        float start = 0.0f;      // 0..1 de la longueur
        float end = 1.0f;        // 0..1
        float decay = 1.0f;      // 0..1 (1 = pas de décroissance ajoutée)
        float chokeFadeMs = 5.0f;
        float velSens = 1.0f;    // 0..1
        bool reverse = false;
    };

    // ------------------------------------------------------------------------
    //  Une voix : lit un sample, peut être coupée (choke) avec un fondu
    // ------------------------------------------------------------------------
    struct Voice
    {
        bool active = false;
        const Sample* smp = nullptr;
        std::shared_ptr<SampleSet> keep;   // garde le jeu de samples en vie tant que la voix joue
        double pos = 0.0, inc = 1.0, endPos = 0.0, startPos = 0.0;
        bool reverse = false;
        float gain = 1.0f, gL = 1.0f, gR = 1.0f;
        float fade = 1.0f, fadeStep = 0.0f;   // fondu de coupure
        float env = 1.0f, envMul = 1.0f;       // décroissance ajoutée
        int64_t gateLeft = -1;                 // liaison : coupe au bout de N échantillons (-1 = libre)
        int gateFadeSamples = 64;
        int age = 0;

        void choke (int fadeSamples)
        {
            if (! active) return;
            const float n = (float) std::max (1, fadeSamples);
            const float step = fade / n;
            if (fadeStep == 0.0f || step > fadeStep) fadeStep = step;
        }

        // Ajoute la voix dans out (stéréo), renvoie false quand elle a fini
        void render (float* outL, float* outR, int n, int quality)
        {
            if (! active || smp == nullptr) { active = false; return; }
            for (int s = 0; s < n; ++s)
            {
                if (gateLeft == 0) choke (gateFadeSamples);
                if (gateLeft > 0) --gateLeft;

                const bool done = reverse ? (pos < startPos) : (pos >= endPos);
                if (done) { active = false; keep.reset(); return; }

                float l = interp (smp->l, pos, quality);
                float r = smp->r.empty() ? l : interp (smp->r, pos, quality);
                const float g = gain * fade * env;
                outL[s] += l * g * gL;
                outR[s] += r * g * gR;

                pos += reverse ? -inc : inc;
                env *= envMul;
                if (fadeStep > 0.0f)
                {
                    fade -= fadeStep;
                    if (fade <= 0.0f) { active = false; keep.reset(); return; }
                }
            }
            ++age;
        }
    };

    // ------------------------------------------------------------------------
    //  Suiveur d'enveloppe (attaque / relâche en ms)
    // ------------------------------------------------------------------------
    struct EnvFollower
    {
        float a = 0.0f, r = 0.0f, y = 0.0f;
        void set (double sr, float attMs, float relMs)
        {
            a = (float) std::exp (-1.0 / (0.001 * std::max (0.01f, attMs) * sr));
            r = (float) std::exp (-1.0 / (0.001 * std::max (0.01f, relMs) * sr));
        }
        float process (float x)
        {
            const float c = x > y ? a : r;
            y = c * y + (1.0f - c) * x;
            return y;
        }
        void reset() { y = 0.0f; }
    };

    // ------------------------------------------------------------------------
    //  Transient designer : attaque et sustain en dB (0 = neutre)
    // ------------------------------------------------------------------------
    struct TransientDesigner
    {
        EnvFollower fastA, slowA, longS, shortS;
        float gSmooth = 1.0f;
        void prepare (double sr)
        {
            fastA.set (sr, 0.3f, 60.0f);
            slowA.set (sr, 25.0f, 60.0f);
            longS.set (sr, 0.3f, 300.0f);
            shortS.set (sr, 0.3f, 25.0f);
            reset();
        }
        void reset() { fastA.reset(); slowA.reset(); longS.reset(); shortS.reset(); gSmooth = 1.0f; }

        void process (float* l, float* r, int n, float attackDb, float sustainDb)
        {
            if (std::fabs (attackDb) < 0.01f && std::fabs (sustainDb) < 0.01f) { gSmooth = 1.0f; return; }
            for (int i = 0; i < n; ++i)
            {
                const float x = std::max (std::fabs (l[i]), std::fabs (r[i])) + 1.0e-9f;
                const float fa = fastA.process (x), sa = slowA.process (x);
                const float ls = longS.process (x), ss = shortS.process (x);
                const float t = clampf ((gainToDb (fa) - gainToDb (sa)) / 9.0f, 0.0f, 1.0f);
                const float u = clampf ((gainToDb (ls) - gainToDb (ss)) / 9.0f, 0.0f, 1.0f);
                const float g = dbToGain (attackDb * t + sustainDb * u);
                gSmooth += 0.02f * (g - gSmooth);
                l[i] *= gSmooth; r[i] *= gSmooth;
            }
        }
    };

    // ------------------------------------------------------------------------
    //  Dynamique : 0 = compression vers le bas, 1 = compression vers le haut,
    //  2 = expansion vers le bas, 3 = expansion vers le haut
    //  Ratio 1:1 = neutre.
    // ------------------------------------------------------------------------
    struct Dynamics
    {
        EnvFollower det;
        float gSmooth = 1.0f;
        double sr = 48000.0;
        float lastA = -1.0f, lastR = -1.0f;
        void prepare (double s) { sr = s; lastA = lastR = -1.0f; reset(); }
        void reset() { det.reset(); gSmooth = 1.0f; }

        static float gainDb (int mode, float levelDb, float thr, float ratio)
        {
            if (levelDb < -90.0f) return 0.0f;
            const float knee = 3.0f;
            switch (mode)
            {
                case 0: { // compression vers le bas
                    const float over = levelDb - thr;
                    if (over <= -knee) return 0.0f;
                    const float o = over < knee ? (over + knee) * (over + knee) / (4.0f * knee) : over;
                    return -o * (1.0f - 1.0f / ratio);
                }
                case 1: { // compression vers le haut : remonte ce qui est sous le seuil
                    const float under = thr - levelDb;
                    if (under <= 0.0f) return 0.0f;
                    const float floorFade = clampf ((levelDb + 75.0f) / 15.0f, 0.0f, 1.0f); // ne remonte pas le bruit
                    return std::min (24.0f, under * (1.0f - 1.0f / ratio)) * floorFade;
                }
                case 2: { // expansion vers le bas : enfonce ce qui est sous le seuil
                    const float under = thr - levelDb;
                    if (under <= 0.0f) return 0.0f;
                    return std::max (-60.0f, -under * (ratio - 1.0f));
                }
                default: { // expansion vers le haut : pousse ce qui dépasse
                    const float over = levelDb - thr;
                    if (over <= 0.0f) return 0.0f;
                    return std::min (24.0f, over * (ratio - 1.0f));
                }
            }
        }

        void process (float* l, float* r, int n, int mode, float thr, float ratio,
                      float attMs, float relMs, float mix)
        {
            if (ratio <= 1.001f || mix <= 0.0001f) { gSmooth = 1.0f; return; }
            if (attMs != lastA || relMs != lastR) { det.set (sr, attMs, relMs); lastA = attMs; lastR = relMs; }
            for (int i = 0; i < n; ++i)
            {
                const float x = std::max (std::fabs (l[i]), std::fabs (r[i]));
                const float lev = det.process (x);
                const float g = dbToGain (gainDb (mode, gainToDb (lev), thr, ratio));
                gSmooth += 0.05f * (g - gSmooth);
                const float k = 1.0f + mix * (gSmooth - 1.0f);
                l[i] *= k; r[i] *= k;
            }
        }
    };

    // ------------------------------------------------------------------------
    //  Grain SP-1200 : réduction de fréquence (maintien) + bits, filtre de sortie
    //  mode 0 = authentique (sans anti-repliement), 1 = propre (filtré avant)
    // ------------------------------------------------------------------------
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void lowpass (double sr, double f, double q)
        {
            f = std::min (f, sr * 0.45);
            const double w = 2.0 * 3.14159265358979 * f / sr, c = std::cos (w), al = std::sin (w) / (2.0 * q);
            const double a0 = 1.0 + al;
            b0 = (float) ((1.0 - c) * 0.5 / a0); b1 = (float) ((1.0 - c) / a0); b2 = b0;
            a1 = (float) (-2.0 * c / a0); a2 = (float) ((1.0 - al) / a0);
        }
        float process (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() { z1 = z2 = 0; }
    };

    struct SpGrain
    {
        double sr = 48000.0, phase = 0.0;
        float holdL = 0, holdR = 0;
        Biquad preL, preR, preL2, preR2, outL, outR;
        float lastRate = -1, lastFilt = -1, lastSr = -1;
        int lastMode = -1;

        void prepare (double s) { sr = s; lastRate = lastFilt = -1; reset(); }
        void reset() { phase = 0; holdL = holdR = 0; preL.reset(); preR.reset(); preL2.reset(); preR2.reset(); outL.reset(); outR.reset(); }

        void process (float* l, float* r, int n, float amount, float bits, float rate, float filter, int mode)
        {
            const bool doGrain = amount > 0.0001f;
            const bool doFilter = filter > 0.0001f;
            if (! doGrain && ! doFilter) return;
            if (rate != lastRate || mode != lastMode || lastSr != (float) sr)
            {
                preL.lowpass (sr, rate * 0.45, 0.707); preR.lowpass (sr, rate * 0.45, 0.707);
                preL2.lowpass (sr, rate * 0.45, 0.707); preR2.lowpass (sr, rate * 0.45, 0.707);
                lastRate = rate; lastMode = mode; lastSr = (float) sr;
            }
            if (filter != lastFilt)
            {
                const double fc = 20000.0 * std::pow (3500.0 / 20000.0, (double) filter);
                outL.lowpass (sr, fc, 0.8); outR.lowpass (sr, fc, 0.8);
                lastFilt = filter;
            }
            const float levels = std::pow (2.0f, std::max (2.0f, bits) - 1.0f);
            const double step = std::min (1.0, (double) rate / sr);
            for (int i = 0; i < n; ++i)
            {
                float xl = l[i], xr = r[i];
                if (doGrain)
                {
                    float il = xl, ir = xr;
                    if (mode == 1) { il = preL2.process (preL.process (il)); ir = preR2.process (preR.process (ir)); }
                    phase += step;
                    if (phase >= 1.0)
                    {
                        phase -= 1.0;
                        holdL = std::round (clampf (il, -1.0f, 1.0f) * levels) / levels;
                        holdR = std::round (clampf (ir, -1.0f, 1.0f) * levels) / levels;
                    }
                    xl = xl + amount * (holdL - xl);
                    xr = xr + amount * (holdR - xr);
                }
                if (doFilter)
                {
                    const float fl = outL.process (xl), fr = outR.process (xr);
                    xl = fl; xr = fr;
                }
                l[i] = xl; r[i] = xr;
            }
        }
    };

    // ------------------------------------------------------------------------
    //  Événements d'un pas : positions (en fraction de pas) de chaque coup
    // ------------------------------------------------------------------------
    struct HitList { int count = 0; float at[4] {}; int vel[4] {}; };
    inline HitList hitsOf (const Step& s)
    {
        HitList h;
        if (! s.on()) return h;
        if (s.split) { h.count = 2; h.at[0] = 0.0f; h.at[1] = 0.5f; h.vel[0] = s.vel; h.vel[1] = std::max (1, s.vel - 10); return h; }
        h.count = std::max (1, std::min (4, s.ratchet));
        for (int i = 0; i < h.count; ++i) { h.at[i] = (float) i / (float) h.count; h.vel[i] = i == 0 ? s.vel : std::max (1, s.vel - 10); }
        return h;
    }

    // Décalage de swing d'un pas impair, en fraction de pas (swing 0.5 = droit, 0.75 = très chaloupé)
    inline double swingOffset (int step, double swing)
    {
        return (step % 2 == 1) ? (swing - 0.5) * 2.0 : 0.0;
    }

    // ------------------------------------------------------------------------
    //  Fill : remplace la fin de la mesure par un roulement
    //  - longueur : 4, 8 ou 16 pas (1 temps, 2 temps, 1 mesure)
    //  - grille : 0 = 1/8, 1 = 1/16, 2 = 1/32, 3 = 1/16 triolet
    //  - les pistes choisies se partagent le roulement à tour de rôle (façon descente de toms)
    //  - intensité : vélocité de départ et accélération (en dessous de 50 %, la 1re moitié va 2x moins vite)
    // ------------------------------------------------------------------------
    struct FillOptions
    {
        float intensity = 0.5f;
        int lenSteps = 4;
        int grid = 1;
        uint32_t laneMask = 0b10000010;   // snare + perc
        bool othersMute = true;
    };

    // Renvoie true si (piste, position dans la mesure) est pris en charge par le fill ; h reçoit les coups (éventuellement aucun)
    inline bool fillHits (const FillOptions& o, int lane, int posInBar, HitList& h)
    {
        const int len = std::max (1, std::min (16, o.lenSteps));
        const int z = posInBar - (16 - len);
        h.count = 0;
        if (z < 0) return false;

        int sel[numLanes]; int nSel = 0;
        for (int l = 0; l < numLanes; ++l) if (o.laneMask & (1u << l)) sel[nSel++] = l;
        const bool mine = (o.laneMask & (1u << lane)) != 0;
        if (! mine) return o.othersMute && lane != 6;   // le crash garde sa place
        if (nSel == 0) return false;

        // coups (en pas) de la zone, selon la grille
        const double rate = o.grid == 0 ? 0.5 : (o.grid == 1 ? 1.0 : (o.grid == 2 ? 2.0 : 1.5));
        const double spacing = 1.0 / rate;
        const int total = std::max (1, (int) std::round (len * rate));
        const int chunk = (total + nSel - 1) / nSel;
        const bool slowStart = o.intensity < 0.5f;
        for (int g = 0; g < total && h.count < 4; ++g)
        {
            const double t = g * spacing;              // position dans la zone, en pas
            if (t < z || t >= z + 1) continue;
            if (slowStart && g < total / 2 && (g % 2) == 1) continue;   // 1re moitié 2x plus lente
            const int owner = sel[std::min (nSel - 1, g / std::max (1, chunk))];
            if (owner != lane) continue;
            const float prog = total > 1 ? (float) g / (float) (total - 1) : 1.0f;
            const int v0 = 55 + (int) (45.0f * o.intensity);
            h.at[h.count] = (float) (t - z);
            h.vel[h.count] = std::min (127, v0 + (int) ((127 - v0) * prog));
            ++h.count;
        }
        return true;
    }

    // ------------------------------------------------------------------------
    //  Filtre de piste : variable d'état (TPT), passe-bas / passe-haut / passe-bande
    // ------------------------------------------------------------------------
    struct Svf
    {
        float g = 0, k = 1, a1 = 0, a2 = 0, a3 = 0, ic1[2] {}, ic2[2] {};
        float lastF = -1, lastQ = -1; double sr = 48000.0;
        void prepare (double s) { sr = s; lastF = -1; reset(); }
        void reset() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0; }
        void set (float freq, float q)
        {
            if (freq == lastF && q == lastQ) return;
            lastF = freq; lastQ = q;
            const double f = std::min ((double) freq, sr * 0.49);
            g = (float) std::tan (M_PI * f / sr);
            k = 1.0f / std::max (0.5f, q);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        // mode : 0 passe-bas, 1 passe-haut, 2 passe-bande
        void process (float* l, float* r, int n, int mode)
        {
            float* ch[2] = { l, r };
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < n; ++i)
                {
                    const float v0 = ch[c][i];
                    const float v3 = v0 - ic2[c];
                    const float v1 = a1 * ic1[c] + a2 * v3;
                    const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
                    ic1[c] = 2.0f * v1 - ic1[c];
                    ic2[c] = 2.0f * v2 - ic2[c];
                    ch[c][i] = mode == 0 ? v2 : (mode == 1 ? v0 - k * v1 - v2 : v1);
                }
        }
    };

    // ------------------------------------------------------------------------
    //  Granulaire : un « nuage » de grains joué à partir du sample de la piste
    // ------------------------------------------------------------------------
    struct GrainParams
    {
        float pos = 0.3f;        // 0..1 dans le sample
        float sizeMs = 80.0f;
        float density = 24.0f;   // grains / seconde
        float spray = 0.1f;      // 0..1 : dispersion de la position
        float pitch = 0.0f;      // demi-tons
        float pitchRand = 0.0f;  // demi-tons
        float stereo = 0.3f;     // 0..1
        float speed = 0.0f;      // vitesse de défilement de la position (x temps réel, -2..2)
        float durMs = 600.0f;    // durée du nuage
        float reverse = 0.0f;    // probabilité de grain inversé
        bool freeze = false;
    };

    inline const std::vector<float>& hannTable()
    {
        static const std::vector<float> t = [] {
            std::vector<float> v (1025);
            for (int i = 0; i <= 1024; ++i) { const double x = std::sin (M_PI * i / 1024.0); v[(size_t) i] = (float) (x * x); }
            return v;
        }();
        return t;
    }

    struct GrainCloud
    {
        struct Grain { bool on = false; double pos = 0, inc = 1; int len = 1, age = 0; float gL = 1, gR = 1; };
        static constexpr int maxGrains = 32;
        Grain grains[maxGrains];
        bool active = false;
        const Sample* smp = nullptr;
        std::shared_ptr<SampleSet> keep;
        double scan = 0.0, nextIn = 0.0, sr = 48000.0;
        int64_t left = 0, total = 1;
        float gain = 1.0f, fade = 1.0f, fadeStep = 0.0f, gL = 1.0f, gR = 1.0f;
        float pitchOffset = 0.0f;
        bool invertRev = false;   // coup inversé : les grains jouent dans l'autre sens
        int64_t gateLeft = -1; int gateFadeSamples = 64;
        uint32_t rng = 12345u;
        int age = 0;

        float rnd() { rng = rng * 1664525u + 1013904223u; return (float) ((rng >> 8) & 0xffffff) / 16777216.0f; }

        void start (const Sample* s, std::shared_ptr<SampleSet> k, double sampleRate, const GrainParams& p, float g, float posOverride)
        {
            smp = s; keep = std::move (k); sr = sampleRate; gain = g;
            for (auto& gr : grains) gr.on = false;
            scan = std::isnan (posOverride) ? p.pos : posOverride;
            nextIn = 0.0;
            total = left = std::max<int64_t> (64, (int64_t) (p.durMs * 0.001 * sr));
            fade = 1.0f; fadeStep = 0.0f; gateLeft = -1; age = 0;
            active = smp != nullptr && smp->length() > 16;
        }

        void choke (int fadeSamples)
        {
            if (! active) return;
            const float step = fade / (float) std::max (1, fadeSamples);
            if (fadeStep == 0.0f || step > fadeStep) fadeStep = step;
        }

        void render (float* outL, float* outR, int n, const GrainParams& p, int quality)
        {
            if (! active || smp == nullptr) { active = false; return; }
            const auto& win = hannTable();
            const double len = smp->length();
            const int gLen = std::max (16, (int) (p.sizeMs * 0.001 * sr));
            const double period = sr / std::max (0.5f, p.density);
            const double scanStep = p.freeze ? 0.0 : (double) p.speed / len * (smp->rate / sr);
            for (int s = 0; s < n; ++s)
            {
                if (gateLeft == 0) choke (gateFadeSamples);
                if (gateLeft > 0) --gateLeft;
                // nouveau grain
                nextIn -= 1.0;
                if (nextIn <= 0.0 && left > 0)
                {
                    nextIn += period;
                    for (auto& gr : grains)
                    {
                        if (gr.on) continue;
                        double pos = scan + (rnd() * 2.0f - 1.0f) * p.spray * 0.5f;
                        pos = std::max (0.0, std::min (1.0, pos)) * (len - 2);
                        const float pr = p.pitch + pitchOffset + (rnd() * 2.0f - 1.0f) * p.pitchRand;
                        const double inc = smp->rate / sr * std::pow (2.0, pr / 12.0);
                        const bool rev = (rnd() < p.reverse) != invertRev;
                        gr.on = true; gr.age = 0; gr.len = gLen;
                        gr.inc = rev ? -inc : inc;
                        gr.pos = pos;
                        const float pan = (rnd() * 2.0f - 1.0f) * p.stereo;
                        gr.gL = std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f;
                        gr.gR = std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
                        break;
                    }
                }
                // enveloppe du nuage : 5 ms d'entrée, 25 % de sortie
                float envc = 1.0f;
                const double since = (double) (total - left);
                if (since < 0.005 * sr) envc = (float) (since / (0.005 * sr));
                if (left < total / 4) envc *= (float) left / (float) std::max<int64_t> (1, total / 4);
                if (left > 0) --left;

                float l = 0.0f, r = 0.0f; bool any = false;
                for (auto& gr : grains)
                {
                    if (! gr.on) continue;
                    any = true;
                    const float w = win[(size_t) std::min (1024, gr.age * 1024 / gr.len)];
                    const float xl = interp (smp->l, gr.pos, quality == 2 ? 1 : quality);
                    const float xr = smp->r.empty() ? xl : interp (smp->r, gr.pos, quality == 2 ? 1 : quality);
                    l += xl * w * gr.gL; r += xr * w * gr.gR;
                    gr.pos += gr.inc;
                    if (++gr.age >= gr.len || gr.pos < 0.0 || gr.pos >= len - 1) gr.on = false;
                }
                const float g = gain * fade * envc * 0.6f;
                outL[s] += l * g * gL; outR[s] += r * g * gR;
                scan += scanStep;
                if (scan > 1.0) scan -= 1.0;
                if (scan < 0.0) scan += 1.0;
                if (fadeStep > 0.0f) { fade -= fadeStep; if (fade <= 0.0f) { active = false; keep.reset(); return; } }
                if (left <= 0 && ! any) { active = false; keep.reset(); return; }
            }
            ++age;
        }
    };

    // ------------------------------------------------------------------------
    //  Slice : détection des coups d'un break (montées d'énergie)
    //  sensibilité 0..1 ; renvoie au plus maxSlices débuts (le premier est toujours 0)
    // ------------------------------------------------------------------------
    inline std::vector<int> detectSlices (const Sample& s, float sensitivity, int maxSlices)
    {
        std::vector<int> out { 0 };
        const int n = s.length();
        if (n < 1024 || maxSlices < 2) return out;
        const int hop = std::max (64, (int) (s.rate * 0.0025));   // 2,5 ms
        const int frames = n / hop;
        std::vector<float> e ((size_t) frames, 0.0f);
        for (int f = 0; f < frames; ++f)
        {
            // énergie de la dérivée : fait ressortir les attaques (aigus des coups) plutôt que les queues graves
            double acc = 0.0;
            for (int i = std::max (1, f * hop); i < (f + 1) * hop && i < n; ++i)
            {
                const float x = s.l[(size_t) i] + (s.r.empty() ? 0.0f : s.r[(size_t) i]);
                const float xp = s.l[(size_t) i - 1] + (s.r.empty() ? 0.0f : s.r[(size_t) i - 1]);
                acc += (double) (x - xp) * (x - xp);
            }
            e[(size_t) f] = 10.0f * std::log10 ((float) (acc / hop) + 1.0e-8f);
        }
        const float jumpDb = 14.0f - 10.0f * clampf (sensitivity, 0.0f, 1.0f);   // 14 dB (peu sensible) .. 4 dB
        const int minGap = std::max (1, (int) (0.06 * s.rate / hop));
        // fonction d'attaque : montée par rapport au minimum des 10 ms précédentes
        std::vector<float> o ((size_t) frames, 0.0f);
        for (int f = 4; f < frames; ++f)
        {
            float ref = e[(size_t) f - 1];
            for (int k = 2; k <= 4; ++k) ref = std::min (ref, e[(size_t) f - k]);
            o[(size_t) f] = (e[(size_t) f] > -60.0f) ? std::max (0.0f, e[(size_t) f] - ref) : 0.0f;
        }
        struct Cand { int frame; float jump; };
        std::vector<Cand> cands;
        // pics locaux : la plus forte montée dans une fenêtre de +/- 60 ms
        for (int f = 4; f < frames; ++f)
        {
            if (o[(size_t) f] < jumpDb) continue;
            bool isMax = true;
            for (int k = std::max (0, f - minGap); k <= std::min (frames - 1, f + minGap) && isMax; ++k)
                if (k != f && (o[(size_t) k] > o[(size_t) f] || (o[(size_t) k] == o[(size_t) f] && k < f))) isMax = false;
            if (isMax) cands.push_back ({ f, o[(size_t) f] });
        }
        // garde les plus fortes si trop nombreuses
        if ((int) cands.size() > maxSlices - 1)
        {
            std::sort (cands.begin(), cands.end(), [] (const Cand& a, const Cand& b) { return a.jump > b.jump; });
            cands.resize ((size_t) (maxSlices - 1));
        }
        for (auto& c : cands)
        {
            const int pos = std::max (0, c.frame * hop - hop);
            if (pos > (int) (0.03 * s.rate)) out.push_back (pos);
        }
        std::sort (out.begin(), out.end());
        return out;
    }


    // ------------------------------------------------------------------------
    //  STRETCH (rendu hors ligne, jamais sur le fil audio)
    //  mode 1 = simple : WSOLA, garde la hauteur, transitoires propres jusqu'à ~4x
    //  mode 2 = Paulstretch : phases aléatoires sur de grandes fenêtres, nappes extrêmes
    //  window01 : 0..1 = taille de fenêtre (simple 20..80 ms, Paul 60..500 ms)
    // ------------------------------------------------------------------------
    inline void fftInPlace (std::vector<std::complex<float>>& a, bool inverse)
    {
        const size_t n = a.size();
        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap (a[i], a[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1)
        {
            const double ang = 2.0 * 3.14159265358979323846 / (double) len * (inverse ? 1.0 : -1.0);
            const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
            for (size_t i = 0; i < n; i += len)
            {
                std::complex<float> w (1.0f, 0.0f);
                for (size_t k = 0; k < len / 2; ++k)
                {
                    const auto u = a[i + k], v = a[i + k + len / 2] * w;
                    a[i + k] = u + v; a[i + k + len / 2] = u - v;
                    w *= wl;
                }
            }
        }
        if (inverse) for (auto& x : a) x /= (float) n;
    }

    inline int pow2Above (double x) { int n = 64; while (n < x && n < (1 << 18)) n <<= 1; return n; }

    inline Sample stretchSample (const Sample& in, int mode, double factor, float window01,
                                 const std::function<bool()>& cancelled = nullptr, double maxSeconds = 120.0)
    {
        Sample out; out.rate = in.rate;
        const int len = (int) in.l.size();
        if (len == 0 || mode == 0) return in;
        factor = std::max (0.25, std::min (64.0, factor));
        const bool stereo = ! in.r.empty();
        const auto& L = in.l; const auto& R = stereo ? in.r : in.l;
        const size_t outLen = (size_t) std::min ((double) len * factor + 8, maxSeconds * in.rate);
        out.l.assign (outLen, 0.0f); out.r.assign (outLen, 0.0f);
        auto at = [&] (const std::vector<float>& v, long i) { return (i >= 0 && i < (long) v.size()) ? v[(size_t) i] : 0.0f; };

        if (mode == 1)
        {
            // WSOLA : fenêtres de Hann à 50 %, recherche de la meilleure continuité de forme d'onde
            const int N = pow2Above (in.rate * (0.02 + 0.06 * window01)), Hs = N / 2, tol = N / 4;
            std::vector<float> win ((size_t) N);
            for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * (float) std::cos (2.0 * 3.14159265358979323846 * i / N);
            long prev = 0;
            for (size_t k = 0; ; ++k)
            {
                const size_t o = k * (size_t) Hs;
                if (o >= outLen) break;
                if (cancelled && (k & 31) == 0 && cancelled()) return in;
                const long ideal = (long) std::llround ((double) o / factor) - N / 2;
                long best = ideal;
                if (k > 0)
                {
                    const long natural = prev + Hs;
                    double bestC = -1.0e30;
                    for (long d = -tol; d <= tol; d += 2)
                    {
                        const long c = ideal + d;
                        double acc = 0.0;
                        for (int i = 0; i < N; i += 4)
                            acc += (double) (at (L, natural + i) + at (R, natural + i)) * (at (L, c + i) + at (R, c + i));
                        if (acc > bestC) { bestC = acc; best = c; }
                    }
                }
                prev = best;
                for (int i = 0; i < N; ++i)
                {
                    const long oi = (long) o + i - N / 2;
                    if (oi < 0 || oi >= (long) outLen) continue;
                    out.l[(size_t) oi] += at (L, best + i) * win[(size_t) i];
                    out.r[(size_t) oi] += at (R, best + i) * win[(size_t) i];
                }
            }
        }
        else
        {
            // Paulstretch : spectre de chaque fenêtre gardé, phases tirées au hasard (mêmes phases à gauche et à droite)
            const int N = pow2Above (in.rate * (0.06 + 0.44 * window01)), H = N / 2;
            std::vector<float> win ((size_t) N);
            for (int i = 0; i < N; ++i) { const double x = 2.0 * i / (N - 1) - 1.0; win[(size_t) i] = (float) std::pow (std::max (0.0, 1.0 - x * x), 1.25); }
            std::mt19937 rng (12345u);
            std::uniform_real_distribution<float> ph (0.0f, 6.2831853f);
            std::vector<std::complex<float>> fl ((size_t) N), fr ((size_t) N);
            double pos = -N / 2.0;
            for (size_t o = 0; o < outLen + (size_t) N; o += (size_t) H)
            {
                if (cancelled && ((o / (size_t) H) & 15) == 0 && cancelled()) return in;
                const long p = (long) std::floor (pos);
                for (int i = 0; i < N; ++i) { fl[(size_t) i] = at (L, p + i) * win[(size_t) i]; fr[(size_t) i] = at (R, p + i) * win[(size_t) i]; }
                fftInPlace (fl, false); fftInPlace (fr, false);
                for (int i = 0; i <= N / 2; ++i)
                {
                    const auto rot = std::polar (1.0f, i == 0 || i == N / 2 ? 0.0f : ph (rng));
                    fl[(size_t) i] = std::abs (fl[(size_t) i]) * rot; fr[(size_t) i] = std::abs (fr[(size_t) i]) * rot;
                    if (i > 0 && i < N / 2) { fl[(size_t) (N - i)] = std::conj (fl[(size_t) i]); fr[(size_t) (N - i)] = std::conj (fr[(size_t) i]); }
                }
                fftInPlace (fl, true); fftInPlace (fr, true);
                for (int i = 0; i < N; ++i)
                {
                    const long oi = (long) o + i - N / 2;
                    if (oi < 0 || oi >= (long) outLen) continue;
                    out.l[(size_t) oi] += fl[(size_t) i].real() * win[(size_t) i];
                    out.r[(size_t) oi] += fr[(size_t) i].real() * win[(size_t) i];
                }
                pos += H / factor;
            }
        }
        // niveau : même crête que l'original, petits fondus aux bords
        float pin = 0.0f, pout = 0.0f;
        for (int i = 0; i < len; ++i) pin = std::max ({ pin, std::fabs (L[(size_t) i]), std::fabs (R[(size_t) i]) });
        for (size_t i = 0; i < outLen; ++i) pout = std::max ({ pout, std::fabs (out.l[i]), std::fabs (out.r[i]) });
        const float g = pout > 1.0e-9f ? pin / pout : 0.0f;
        const size_t fade = std::min (outLen / 4, (size_t) (0.004 * in.rate));
        for (size_t i = 0; i < outLen; ++i)
        {
            float f = g;
            if (i >= outLen - fade) f *= (float) (outLen - i) / (float) std::max<size_t> (1, fade);
            out.l[i] *= f; out.r[i] *= f;
        }
        if (! stereo) out.r.clear();
        return out;
    }

    inline std::shared_ptr<SampleSet> stretchSet (const SampleSet& in, int mode, double factor, float window01,
                                                 const std::function<bool()>& cancelled = nullptr)
    {
        auto out = std::make_shared<SampleSet>();
        out->name = in.name; out->layerTopVel = in.layerTopVel;
        auto one = [&] (const std::shared_ptr<Sample>& s) { return s ? std::make_shared<Sample> (stretchSample (*s, mode, factor, window01, cancelled)) : s; };
        for (auto& layer : in.layers) { std::vector<std::shared_ptr<Sample>> v; for (auto& s : layer) v.push_back (one (s)); out->layers.push_back (v); }
        for (auto& var : in.variants) { std::vector<std::shared_ptr<Sample>> v; for (auto& s : var) v.push_back (one (s)); out->variants.push_back (v); }
        return out;
    }

}
