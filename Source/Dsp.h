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

namespace typhon
{
    static constexpr int numLanes = 8;
    static constexpr int numSteps = 16;
    static constexpr int maxVoicesPerLane = 6;

    inline float dbToGain (float db) { return db <= -99.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
    inline float gainToDb (float g)  { return g <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (g); }
    inline float clampf (float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

    // ------------------------------------------------------------------------
    //  Un pas du séquenceur, rangé dans un entier 32 bits (lecture sans verrou)
    //  bits 0..6   vélocité (0 = pas éteint)
    //  bits 7..8   ratchet - 1 (1..4 coups)
    //  bit  9      divisé en deux
    //  bits 10..14 longueur de liaison - 1 (1..16 pas)
    //  bits 15..16 choke du pas : 0 = suit la piste, 1 = toujours, 2 = laisse sonner
    // ------------------------------------------------------------------------
    struct Step
    {
        int vel = 0, ratchet = 1, len = 1, choke = 0;
        bool split = false;

        bool on() const { return vel > 0; }

        uint32_t pack() const
        {
            return (uint32_t) (vel & 127)
                 | ((uint32_t) ((std::max (1, std::min (4, ratchet)) - 1) & 3) << 7)
                 | ((uint32_t) (split ? 1 : 0) << 9)
                 | ((uint32_t) ((std::max (1, std::min (16, len)) - 1) & 31) << 10)
                 | ((uint32_t) (choke & 3) << 15);
        }
        static Step unpack (uint32_t v)
        {
            Step s;
            s.vel = (int) (v & 127);
            s.ratchet = (int) ((v >> 7) & 3) + 1;
            s.split = ((v >> 9) & 1) != 0;
            s.len = (int) ((v >> 10) & 31) + 1;
            s.choke = (int) ((v >> 15) & 3);
            return s;
        }
    };

    struct Pattern
    {
        std::atomic<uint32_t> steps[numLanes][numSteps];
        Pattern() { clear(); }
        void clear() { for (auto& l : steps) for (auto& s : l) s.store (0); }
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
        std::vector<int> layerTopVel;                             // vélocité max de chaque couche
        std::string name;

        bool empty() const { return layers.empty() || layers[0].empty(); }

        const Sample* pick (int vel, uint32_t& rr) const
        {
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
        float gain = 1.0f;
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
                outL[s] += l * g;
                outR[s] += r * g;

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
    //  Fill : génère une variante du pattern sur le dernier temps (pas 12..15)
    // ------------------------------------------------------------------------
    // Rôle des pistes : 0 kick, 1 snare, 2 ghost, 3 rim, 4 hat fermé, 5 hat ouvert, 6 crash, 7 perc
    inline Step fillStep (const Pattern& p, int lane, int step, float intensity)
    {
        Step s = p.get (lane, step);
        if (step < 12 || intensity <= 0.0f) return s;
        const int k = step - 12; // 0..3
        auto crescendo = [&] (int base) { return std::min (127, base + k * 8); };
        if (lane == 1) // snare : roulement qui s'accélère
        {
            if (intensity > 0.15f || k >= 2)
            {
                s.vel = crescendo (85);
                s.split = false;
                s.len = 1;
                s.ratchet = intensity > 0.7f ? (k >= 2 ? 4 : 3) : (intensity > 0.35f ? (k >= 2 ? 3 : 2) : (k == 3 ? 2 : 1));
            }
        }
        else if (lane == 7) // perc / tom : réponse à contretemps
        {
            if (intensity > 0.3f && (k % 2 == 1)) { s.vel = crescendo (80); s.ratchet = intensity > 0.75f ? 2 : 1; s.len = 1; }
        }
        else if (lane == 4 || lane == 5) // hats : on dégage la place
        {
            if (intensity > 0.5f) s.vel = 0;
        }
        else if (lane == 2) // ghost : se tait sous le roulement
        {
            if (intensity > 0.2f) s.vel = 0;
        }
        else if (lane == 0) // kick : un coup sur le dernier pas à fond
        {
            if (intensity > 0.6f && k == 3) { s.vel = 110; s.ratchet = 1; s.len = 1; }
        }
        return s;
    }

    // ------------------------------------------------------------------------
    //  Événements d'un pas : positions (en fraction de pas) de chaque coup
    // ------------------------------------------------------------------------
    struct HitList { int count = 0; float at[4] {}; };
    inline HitList hitsOf (const Step& s)
    {
        HitList h;
        if (! s.on()) return h;
        if (s.split) { h.count = 2; h.at[0] = 0.0f; h.at[1] = 0.5f; return h; }
        h.count = std::max (1, std::min (4, s.ratchet));
        for (int i = 0; i < h.count; ++i) h.at[i] = (float) i / (float) h.count;
        return h;
    }

    // Décalage de swing d'un pas impair, en fraction de pas (swing 0.5 = droit, 0.75 = très chaloupé)
    inline double swingOffset (int step, double swing)
    {
        return (step % 2 == 1) ? (swing - 0.5) * 2.0 : 0.0;
    }
}
