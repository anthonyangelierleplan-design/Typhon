#pragma once
// ============================================================================
//  Kit de secours synthétisé : Typhon sonne dès l'ouverture, même sans
//  capture EZdrummer ni sample glissé.
// ============================================================================
#include "Dsp.h"
#include <random>

namespace typhon
{
    inline std::shared_ptr<SampleSet> makeFallback (int lane, double sr = 48000.0)
    {
        auto smp = std::make_shared<Sample>();
        smp->rate = sr;
        const double pi2 = 6.283185307179586;
        std::mt19937 rng (1234u + (unsigned) lane);
        std::uniform_real_distribution<float> uni (-1.0f, 1.0f);

        auto alloc = [&] (double seconds) { const size_t n = (size_t) (seconds * sr); smp->l.assign (n, 0.0f); };
        float hp = 0, lpA = 0, lpB = 0;

        switch (lane)
        {
            case 0: { // kick
                alloc (0.6); double ph = 0;
                for (size_t i = 0; i < smp->l.size(); ++i) {
                    const double t = (double) i / sr;
                    const double f = 48.0 + 110.0 * std::exp (-t * 28.0);
                    ph += pi2 * f / sr;
                    const double a = std::exp (-t * 6.5);
                    const double click = (t < 0.004) ? (1.0 - t / 0.004) * 0.5 : 0.0;
                    smp->l[i] = (float) (std::tanh (1.6 * std::sin (ph)) * a * 0.9 + click * uni (rng) * 0.3);
                }
            } break;
            case 1: case 2: { // snare / ghost
                alloc (0.35); double ph = 0;
                const float amp = lane == 1 ? 0.85f : 0.35f;
                for (size_t i = 0; i < smp->l.size(); ++i) {
                    const double t = (double) i / sr;
                    ph += pi2 * (190.0 + 40.0 * std::exp (-t * 40.0)) / sr;
                    const float n = uni (rng);
                    hp = n - lpA; lpA += 0.25f * (n - lpA);
                    const float body = (float) (std::sin (ph) * std::exp (-t * 30.0)) * 0.6f;
                    const float noise = hp * (float) std::exp (-t * 14.0);
                    smp->l[i] = amp * std::tanh (body + noise);
                }
            } break;
            case 3: { // rim
                alloc (0.08); double ph = 0;
                for (size_t i = 0; i < smp->l.size(); ++i) {
                    const double t = (double) i / sr;
                    ph += pi2 * 1700.0 / sr;
                    smp->l[i] = (float) ((std::sin (ph) * 0.6 + uni (rng) * 0.3) * std::exp (-t * 70.0)) * 0.7f;
                }
            } break;
            case 4: case 5: case 6: { // hats et crash : bruit métallique filtré
                const double len = lane == 4 ? 0.09 : (lane == 5 ? 0.5 : 1.8);
                const double dec = lane == 4 ? 55.0 : (lane == 5 ? 7.0 : 2.2);
                alloc (len);
                const double fr[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
                double ph[6] = {};
                for (size_t i = 0; i < smp->l.size(); ++i) {
                    const double t = (double) i / sr;
                    double m = 0;
                    for (int k = 0; k < 6; ++k) { ph[k] += pi2 * fr[k] * 1.4 / sr; m += (std::fmod (ph[k], pi2) < 3.14159 ? 1.0 : -1.0); }
                    const float x = (float) (m / 6.0) * 0.6f + uni (rng) * 0.4f;
                    lpB += 0.6f * (x - lpB);
                    const float h = x - lpB;
                    smp->l[i] = h * (float) std::exp (-t * dec) * (lane == 6 ? 0.55f : 0.6f);
                }
            } break;
            default: { // perc : tom
                alloc (0.45); double ph = 0;
                for (size_t i = 0; i < smp->l.size(); ++i) {
                    const double t = (double) i / sr;
                    ph += pi2 * (120.0 + 70.0 * std::exp (-t * 18.0)) / sr;
                    smp->l[i] = (float) (std::sin (ph) * std::exp (-t * 9.0)) * 0.8f;
                }
            } break;
        }
        // petit fondu de fin
        const size_t n = smp->l.size(), f = std::min<size_t> (n, (size_t) (0.003 * sr));
        for (size_t i = 0; i < f; ++i) smp->l[n - 1 - i] *= (float) i / (float) f;
        smp->r = smp->l;

        auto set = std::make_shared<SampleSet>();
        set->layers.push_back ({ smp });
        set->layerTopVel.push_back (127);
        set->name = "Secours";
        return set;
    }
}
