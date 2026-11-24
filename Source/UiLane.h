#pragma once
// ============================================================================
//  Panneau de la piste sélectionnée : SAMPLE · GRAIN · SLICE · DYN
//  + éditeur de waveform zoomable (molette = zoom, glisser = déplacer,
//    marqueurs de début / fin à la souris, double-clic = éditeur en grand)
//  + mode verrou : les potards règlent un seul pas
// ============================================================================
#include "UiWidgets.h"

namespace tui
{
    // ------------------------------------------------------------------------
    //  Waveform interactive
    // ------------------------------------------------------------------------
    class Waveform : public juce::Component
    {
    public:
        enum Mode { sampleMode, grainMode, sliceMode };
        Waveform (Ctx& c) : ctx (c) {}

        int lane = 1;
        Mode mode = sampleMode;
        std::vector<int> slices;           // mode slice : débuts (échantillons)
        std::function<void()> onDoubleClick;
        // verrou de début (mode verrou) : si défini, le marqueur de début règle le verrou
        std::function<float()> lockStartGet;
        std::function<void (float)> lockStartSet;
        bool big = false;

        void resetZoom() { viewStart = 0.0; viewLen = 1.0; repaint(); }
        void zoomBy (double factor, double centreFrac = 0.5)
        {
            const double c = viewStart + centreFrac * viewLen;
            viewLen = juce::jlimit (0.0005, 1.0, viewLen / factor);
            viewStart = juce::jlimit (0.0, 1.0 - viewLen, c - centreFrac * viewLen);
            repaint();
        }
        void zoomToSelection()
        {
            const float st = startValue(), en = paramValue ("end");
            const double pad = (en - st) * 0.05;
            viewStart = juce::jlimit (0.0, 1.0, (double) st - pad);
            viewLen = juce::jlimit (0.0005, 1.0 - viewStart, (double) (en - st) + 2 * pad);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            auto r = getLocalBounds().toFloat();
            g.setColour (t.lcdBg); g.fillRoundedRectangle (r, 6.0f);
            const typhon::Sample* s = currentSample();
            if (s == nullptr)
            {
                g.setColour (t.lcdTx.withAlpha (0.6f)); g.setFont (mono (12.0f));
                g.drawText ("Glisse un sample sur la piste", r, juce::Justification::centred);
                return;
            }
            if (s != cached) rebuild (*s);
            const int n = s->length();
            const double secs = n / s->rate;
            auto area = r.reduced (2.0f, big ? 20.0f : 14.0f);
            const float mid = area.getCentreY(), hh = area.getHeight() * 0.48f;
            const int W = std::max (1, (int) area.getWidth());
            const double spp = viewLen * n / W;   // échantillons par pixel

            // règle en millisecondes
            g.setFont (mono (10.0f));
            const double visMs = viewLen * secs * 1000.0;
            const double steps[] = { 0.1, 0.2, 0.5, 1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000 };
            double tick = 1000.0;
            for (double sp : steps) if (visMs / sp <= (big ? 14.0 : 6.0)) { tick = sp; break; }
            const double t0 = viewStart * secs * 1000.0;
            for (double ms = std::ceil (t0 / tick) * tick; ms <= t0 + visMs; ms += tick)
            {
                const float x = area.getX() + (float) ((ms - t0) / visMs) * area.getWidth();
                g.setColour (t.lcdTx.withAlpha (0.18f)); g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
                g.setColour (t.lcdTx.withAlpha (0.7f));
                g.drawText (tick < 1.0 ? juce::String (ms, 1) : juce::String (juce::roundToInt (ms)), (int) x + 2, (int) r.getY() + 1, 50, 12, juce::Justification::left);
            }

            // forme d'onde (min / max par pixel, via un résumé par blocs de 64)
            g.setColour (t.lcdTx);
            for (int x = 0; x < W; ++x)
            {
                const double a = (viewStart * n) + x * spp, b = a + std::max (1.0, spp);
                float lo = 0, hi = 0;
                if (spp > 64.0)
                {
                    for (int k = (int) (a / 64); k <= std::min ((int) mins.size() - 1, (int) (b / 64)); ++k) { lo = std::min (lo, mins[(size_t) k]); hi = std::max (hi, maxs[(size_t) k]); }
                }
                else
                {
                    for (int i = (int) a; i <= std::min (n - 1, (int) b); ++i) { const float v = s->l[(size_t) i]; lo = std::min (lo, v); hi = std::max (hi, v); }
                }
                lo *= norm; hi *= norm;
                g.fillRect (area.getX() + x, mid - hi * hh, 1.0f, std::max (1.0f, (hi - lo) * hh));
            }

            auto fx = [&] (double frac) { return area.getX() + (float) ((frac - viewStart) / viewLen) * area.getWidth(); };
            if (mode == sampleMode)
            {
                const float st = startValue(), en = paramValue ("end");
                g.setColour (t.lcdBg.withAlpha (0.72f));
                if (st > viewStart) g.fillRect (juce::Rectangle<float>::leftTopRightBottom (area.getX(), r.getY(), std::min (area.getRight(), fx (st)), r.getBottom()));
                if (en < viewStart + viewLen) g.fillRect (juce::Rectangle<float>::leftTopRightBottom (std::max (area.getX(), fx (en)), r.getY(), area.getRight(), r.getBottom()));
                g.setColour (t.acc);
                for (float v : { st, en })
                {
                    const float x = fx (v);
                    if (x < area.getX() - 1 || x > area.getRight() + 1) continue;
                    g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
                    juce::Path flag; flag.addTriangle (x, r.getY(), x + (v == st ? 8.0f : -8.0f), r.getY(), x, r.getY() + 10.0f);
                    g.fillPath (flag);
                }
                g.setFont (mono (11.0f));
                g.drawText (juce::String (juce::roundToInt (secs * (en - st) * 1000.0)) + " ms", r.reduced (8, 3).toNearestInt(), juce::Justification::bottomRight);
                if (lockStartGet && ! std::isnan (lockStartGet())) g.drawText (juce::String::fromUTF8 ("\xe2\x97\x8f d\xc3\xa9" "but verrouill\xc3\xa9"), r.reduced (8, 3).toNearestInt(), juce::Justification::bottomLeft);
            }
            else if (mode == grainMode)
            {
                const float pos = paramValue ("gpos"), spray = paramValue ("gspray");
                const float sizeFrac = (float) (paramValue ("gsize") * 0.001 / secs);
                const float a = juce::jlimit (0.0f, 1.0f, pos - spray * 0.5f), b = juce::jlimit (0.0f, 1.0f, pos + spray * 0.5f + sizeFrac);
                g.setColour (t.acc.withAlpha (0.18f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (fx (a), r.getY(), fx (b), r.getBottom()));
                g.setColour (t.acc);
                g.fillRect (fx (pos) - 1.0f, r.getY(), 2.0f, r.getHeight());
                g.setFont (mono (11.0f));
                g.drawText ("POSITION " + juce::String (juce::roundToInt (pos * 100)) + " %", r.reduced (8, 3).toNearestInt(), juce::Justification::bottomRight);
            }
            else
            {
                g.setFont (mono (11.0f, true));
                for (size_t k = 0; k < slices.size(); ++k)
                {
                    const float x = fx ((double) slices[k] / n);
                    if (x < area.getX() - 1 || x > area.getRight() + 1) continue;
                    g.setColour (t.acc); g.fillRect (x - 1.0f, r.getY(), 2.0f, r.getHeight());
                    g.fillRoundedRectangle (x, r.getY() + 2.0f, 16.0f, 14.0f, 3.0f);
                    g.setColour (t.lcdBg); g.drawText (juce::String ((int) k + 1), (int) x, (int) r.getY() + 2, 16, 14, juce::Justification::centred);
                }
            }
            // variantes du son (machines) : ◀ 2/5 ▶
            const int nv = setRef ? setRef->numVariants() : 0;
            varPrev = varNext = {};
            if (nv > 1 && ! big)
            {
                auto vr = juce::Rectangle<int> (6, getHeight() - 20, 120, 16);
                varPrev = vr.removeFromLeft (18); varNext = vr.removeFromRight (18);
                g.setColour (t.acc); g.setFont (mono (11.0f, true));
                g.drawText (juce::String::fromUTF8 ("â"), varPrev, juce::Justification::centred);
                g.drawText (juce::String::fromUTF8 ("â¶"), varNext, juce::Justification::centred);
                g.drawText ("VAR " + juce::String ((int) paramValue ("var")) + "/" + juce::String (nv), vr, juce::Justification::centred);
            }
            g.setColour (t.lcdTx); g.setFont (mono (11.0f));
            juce::String info = ctx.proc.laneSource (lane);
            if (viewLen < 0.999) info += juce::String::fromUTF8 ("  \xc2\xb7 zoom ") + juce::String (1.0 / viewLen, 1) + "x";
            g.drawText (info, r.reduced (8, 3).toNearestInt(), juce::Justification::topRight);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            dragWhat = 0;
            if (e.mods.isPopupMenu()) { menu(); return; }
            if (! varPrev.isEmpty() && (varPrev.contains (e.getPosition()) || varNext.contains (e.getPosition())))
            {
                const int nv = setRef ? setRef->numVariants() : 1;
                int v = (int) paramValue ("var") + (varNext.contains (e.getPosition()) ? 1 : -1);
                if (v < 1) v = nv; if (v > nv) v = 1;
                setParam ("var", (float) v);
                ctx.proc.audition (lane);
                repaint();
                return;
            }
            const typhon::Sample* s = currentSample();
            if (s == nullptr) return;
            const float x = (float) e.x;
            if (mode == sampleMode)
            {
                if (std::fabs (x - toX (startValue())) < 8.0f) dragWhat = 1;
                else if (std::fabs (x - toX (paramValue ("end"))) < 8.0f) dragWhat = 2;
            }
            else if (mode == grainMode) dragWhat = 3;
            if (dragWhat == 0) { dragWhat = 4; panFrom = viewStart; }
            if (dragWhat == 1 && ! lockStartGet) begin ("start");
            if (dragWhat == 2) begin ("end");
            if (dragWhat == 3) { begin ("gpos"); setParam ("gpos", (float) toFrac (x)); }
        }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            const double f = juce::jlimit (0.0, 1.0, toFrac ((float) e.x));
            if (dragWhat == 1)
            {
                const float v = (float) std::min (f, (double) paramValue ("end") - 0.0005);
                if (lockStartSet) lockStartSet (v); else setParam ("start", v);
            }
            else if (dragWhat == 2) setParam ("end", (float) std::max (f, (double) startValue() + 0.0005));
            else if (dragWhat == 3) setParam ("gpos", (float) f);
            else if (dragWhat == 4 && viewLen < 1.0)
            {
                const double d = (double) e.getDistanceFromDragStartX() / std::max (1, getWidth()) * viewLen;
                viewStart = juce::jlimit (0.0, 1.0 - viewLen, panFrom - d);
            }
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragWhat == 1 && ! lockStartGet) end ("start");
            if (dragWhat == 2) end ("end");
            if (dragWhat == 3) end ("gpos");
            dragWhat = 0;
        }
        void mouseDoubleClick (const juce::MouseEvent&) override { if (onDoubleClick) onDoubleClick(); }
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (e.mods.isShiftDown() || std::fabs (w.deltaX) > std::fabs (w.deltaY))
            {
                const float d = std::fabs (w.deltaX) > 0.0f ? w.deltaX : w.deltaY;
                viewStart = juce::jlimit (0.0, 1.0 - viewLen, viewStart - d * viewLen * 0.5);
                repaint();
                return;
            }
            zoomBy (w.deltaY > 0 ? 1.3 : 1.0 / 1.3, juce::jlimit (0.0, 1.0, (double) e.x / std::max (1, getWidth())));
        }

    private:
        const typhon::Sample* currentSample()
        {
            setRef = ctx.proc.laneSet (lane);
            if (setRef == nullptr || setRef->empty()) return nullptr;
            const auto* s = setRef->display ((int) paramValue ("var") - 1);
            return (s != nullptr && s->length() >= 2) ? s : nullptr;
        }
        float paramValue (const char* w) const
        {
            if (auto* p = ctx.proc.apvts.getParameter (typhon::pid (lane, w))) return p->convertFrom0to1 (p->getValue());
            return 0.0f;
        }
        float startValue() const
        {
            if (lockStartGet) { const float v = lockStartGet(); if (! std::isnan (v)) return v; }
            return paramValue ("start");
        }
        void setParam (const char* w, float v)
        {
            if (auto* p = ctx.proc.apvts.getParameter (typhon::pid (lane, w))) p->setValueNotifyingHost (p->convertTo0to1 (v));
        }
        void begin (const char* w) { if (auto* p = ctx.proc.apvts.getParameter (typhon::pid (lane, w))) p->beginChangeGesture(); }
        void end (const char* w) { if (auto* p = ctx.proc.apvts.getParameter (typhon::pid (lane, w))) p->endChangeGesture(); }
        float inner() const { return (float) getWidth() - 4.0f; }
        float toX (double frac) const { return 2.0f + (float) ((frac - viewStart) / viewLen) * inner(); }
        double toFrac (float x) const { return viewStart + (x - 2.0f) / inner() * viewLen; }

        void menu()
        {
            juce::PopupMenu m;
            m.addItem (1, juce::String::fromUTF8 ("Zoomer sur la s\xc3\xa9lection"), mode == sampleMode);
            m.addItem (2, "Tout afficher", viewLen < 0.999);
            m.addItem (3, "Zoom +"); m.addItem (4, "Zoom -");
            m.addSeparator();
            m.addItem (5, "Reverse (toute la piste)", true, paramValue ("rev") > 0.5f);
            m.addItem (6, juce::String::fromUTF8 ("R\xc3\xa9initialiser d\xc3\xa9" "but et fin"), mode == sampleMode);
            if (! big) m.addItem (7, juce::String::fromUTF8 ("Ouvrir l'\xc3\xa9" "diteur en grand"));
            juce::Component::SafePointer<Waveform> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int r)
            {
                if (safe == nullptr) return;
                switch (r)
                {
                    case 1: safe->zoomToSelection(); break;
                    case 2: safe->resetZoom(); break;
                    case 3: safe->zoomBy (1.6); break;
                    case 4: safe->zoomBy (1.0 / 1.6); break;
                    case 5: safe->setParam ("rev", safe->paramValue ("rev") > 0.5f ? 0.0f : 1.0f); break;
                    case 6: safe->setParam ("start", 0.0f); safe->setParam ("end", 1.0f); break;
                    case 7: if (safe->onDoubleClick) safe->onDoubleClick(); break;
                    default: break;
                }
                safe->repaint();
            });
        }

        void rebuild (const typhon::Sample& s)
        {
            cached = &s;
            const int n = s.length();
            const int blocks = (n + 63) / 64;
            mins.assign ((size_t) blocks, 0.0f); maxs.assign ((size_t) blocks, 0.0f);
            float mx = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float v = s.l[(size_t) i];
                auto& lo = mins[(size_t) (i / 64)]; auto& hi = maxs[(size_t) (i / 64)];
                lo = std::min (lo, v); hi = std::max (hi, v);
                mx = std::max (mx, std::fabs (v));
            }
            norm = mx > 0.0f ? 1.0f / mx : 1.0f;
            viewStart = 0.0; viewLen = 1.0;
        }

        Ctx& ctx;
        std::shared_ptr<typhon::SampleSet> setRef;
        const typhon::Sample* cached = nullptr;
        std::vector<float> mins, maxs;
        float norm = 1.0f;
        double viewStart = 0.0, viewLen = 1.0, panFrom = 0.0;
        int dragWhat = 0;
        juce::Rectangle<int> varPrev, varNext;
    };

    // ------------------------------------------------------------------------
    class DynCurve : public juce::Component
    {
    public:
        DynCurve (Ctx& c, juce::String prefix) : pre (std::move (prefix)), ctx (c) {}
        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            auto r = getLocalBounds().toFloat();
            g.setColour (t.lcdBg); g.fillRoundedRectangle (r, 6.0f);
            auto& ap = ctx.proc.apvts;
            const int mode = (int) ap.getRawParameterValue (pre + "dmode")->load();
            const float thr = ap.getRawParameterValue (pre + "dthr")->load();
            const float ratio = ap.getRawParameterValue (pre + "dratio")->load();
            auto in = r.reduced (6.0f);
            auto toX = [&] (float db) { return in.getX() + (db + 60.0f) / 60.0f * in.getWidth(); };
            auto toY = [&] (float db) { return in.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f) * in.getHeight(); };
            g.setColour (t.lcdTx.withAlpha (0.25f));
            g.drawLine (toX (-60), toY (-60), toX (0), toY (0), 1.0f);
            juce::Path p;
            for (int i = 0; i <= 60; ++i)
            {
                const float x = -60.0f + i;
                const float y = x + typhon::Dynamics::gainDb (mode, x, thr, ratio);
                if (i == 0) p.startNewSubPath (toX (x), toY (y)); else p.lineTo (toX (x), toY (y));
            }
            g.setColour (t.lcdTx); g.strokePath (p, juce::PathStrokeType (2.0f));
            g.setColour (t.acc); g.drawVerticalLine ((int) toX (thr), in.getY(), in.getBottom());
            g.setFont (mono (10.0f)); g.setColour (t.lcdTx);
            g.drawText (juce::String::fromUTF8 ("ENTR\xc3\x89" "E \xe2\x86\x92 SORTIE"), r.reduced (6, 4).toNearestInt(), juce::Justification::topLeft);
        }
        juce::String pre;
    private:
        Ctx& ctx;
    };

    inline juce::String dynHelp (int mode)
    {
        switch (mode)
        {
            case 0: return juce::String::fromUTF8 ("Compression vers le bas : ce qui d\xc3\xa9passe le seuil est \xc3\xa9" "cras\xc3\xa9. Plus serr\xc3\xa9.");
            case 1: return juce::String::fromUTF8 ("Compression vers le haut : remonte ce qui est sous le seuil (queue, pi\xc3\xa8" "ce). Effet sample \xc3\xa9" "cras\xc3\xa9.");
            case 2: return juce::String::fromUTF8 ("Expansion vers le bas : enfonce ce qui est sous le seuil. Sec et punchy, comme un gate doux.");
            default: return juce::String::fromUTF8 ("Expansion vers le haut : pousse ce qui d\xc3\xa9passe le seuil. Redonne de l'attaque.");
        }
    }

    // Bloc « dynamique » réutilisé par la piste et par le master (préfixe des paramètres)
    class DynBlock : public juce::Component
    {
    public:
        DynBlock (Ctx& c) : ctx (c), curve (c, "m_")
        {
            const char* names[7] = { "Attaque", "Sustain", "Seuil", "Ratio", "Temps att.", "Rel\xc3\xa2" "che", "Mix" };
            for (int i = 0; i < 7; ++i) { knobs.add (new Knob (c, juce::String::fromUTF8 (names[i]))); addAndMakeVisible (knobs.getLast()); }
            knobs[0]->formatter = fmtSignedDb; knobs[1]->formatter = fmtSignedDb;
            knobs[2]->formatter = fmtDb; knobs[3]->formatter = [] (float v) { return juce::String (v, 1) + ":1"; };
            knobs[4]->formatter = fmtMs; knobs[5]->formatter = fmtMs; knobs[6]->formatter = fmtPct;
            addAndMakeVisible (curve);
            const char* modeLbl[4] = { "COMP \xe2\x86\x93", "COMP \xe2\x86\x91", "EXPAND \xe2\x86\x93", "EXPAND \xe2\x86\x91" };
            for (int k = 0; k < 4; ++k)
            {
                auto* b = modeBtns.add (new Pill (c, juce::String::fromUTF8 (modeLbl[k])));
                b->textSize = 11.0f;
                b->isOn = [this, k] { return (int) ctx.proc.apvts.getRawParameterValue (pre + "dmode")->load() == k; };
                b->onClick = [this, k] {
                    if (auto* p = ctx.proc.apvts.getParameter (pre + "dmode")) p->setValueNotifyingHost (p->convertTo0to1 ((float) k));
                    repaint();
                };
                addAndMakeVisible (b);
            }
            setPrefix ("m_");
        }
        // pre : "l3_" pour une piste, "m_" pour le master
        void setPrefix (const juce::String& p)
        {
            pre = p; curve.pre = p;
            const char* ids[7] = { "tatt", "tsus", "dthr", "dratio", "datt", "drel", "dmix" };
            for (int i = 0; i < 7; ++i) knobs[i]->attach (p + ids[i]);
            repaint();
        }
        void tick() { curve.repaint(); for (auto* b : modeBtns) b->repaint(); repaint (helpArea); }
        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            g.setColour (t.sub); g.setFont (font (11.0f, true));
            g.drawText ("TRANSIENT", trLbl, juce::Justification::centredLeft);
            g.drawText ("DYNAMIQUE", dyLbl, juce::Justification::centredLeft);
            g.setFont (font (12.0f));
            g.drawFittedText (dynHelp ((int) ctx.proc.apvts.getRawParameterValue (pre + "dmode")->load()), helpArea, juce::Justification::topLeft, 3);
        }
        void resized() override
        {
            auto r = getLocalBounds();
            trLbl = r.removeFromTop (16);
            auto tr = r.removeFromTop (86);
            knobs[0]->setBounds (tr.removeFromLeft (tr.getWidth() / 2));
            knobs[1]->setBounds (tr);
            r.removeFromTop (4);
            dyLbl = r.removeFromTop (16);
            auto top = r.removeFromTop (96);
            curve.setBounds (top.removeFromLeft (96));
            top.removeFromLeft (10);
            const int mw = (top.getWidth() - 6) / 2, mh = (top.getHeight() - 6) / 2;
            for (int k = 0; k < 4; ++k) modeBtns[k]->setBounds (top.getX() + (k % 2) * (mw + 6), top.getY() + (k / 2) * (mh + 6), mw, mh);
            r.removeFromTop (4);
            helpArea = r.removeFromTop (34);
            auto kn = r.removeFromTop (86);
            const int kw = kn.getWidth() / 5;
            for (int i = 2; i < 7; ++i) knobs[i]->setBounds (kn.removeFromLeft (kw));
        }
    private:
        Ctx& ctx;
        juce::String pre { "m_" };
        DynCurve curve;
        juce::OwnedArray<Knob> knobs;
        juce::OwnedArray<Pill> modeBtns;
        juce::Rectangle<int> trLbl, dyLbl, helpArea;
    };

    // ------------------------------------------------------------------------
    class LanePanel : public juce::Component
    {
    public:
        std::function<void()> onOpenEditor, onLockEnded;

        LanePanel (Ctx& c) : wave (c), ctx (c), dyn (c),
            chokeBtn (c, "CHOKE", true), revBtn (c, "REVERSE", true), gateBtn (c, "GATE MIDI", true), muteBtn (c, "MUTE", true), stepLenBtn (c, juce::String::fromUTF8 ("DUR\xc3\x89" "E = PAS"), true),
            loadBtn (c, "CHARGER..."), bigBtn (c, juce::String::fromUTF8 ("\xe2\xa4\xa2 AGRANDIR")),
            granOn (c, "GRANULAIRE ON", true), freezeBtn (c, "FREEZE", true),
            sliceGo (c, juce::String::fromUTF8 ("R\xc3\x89PARTIR SUR LES PISTES")), slicePat (c, juce::String::fromUTF8 ("R\xc3\x89PARTIR + PATTERN")),
            lockDone (c, "TERMINER"), lockClear (c, "EFFACER")
        {
            const char* tabNames[5] = { "SAMPLE", "GRAIN", "SLICE", "STRETCH", "DYN" };
            for (int k = 0; k < 5; ++k)
            {
                auto* b = tabs.add (new Pill (c, tabNames[k]));
                b->textSize = 10.5f;
                b->isOn = [this, k] { return tab == k; };
                b->onClick = [this, k] { setTab (k); };
                addAndMakeVisible (b);
            }
            const char* sampleLbl[12] = { "Pitch", "D\xc3\xa9" "but", "Fin", "D\xc3\xa9" "croiss.", "Fondu choke", "V\xc3\xa9locit\xc3\xa9",
                                          "Volume", "Pan", "Filtre", "R\xc3\xa9sonance", "Envoi A", "Envoi B" };
            for (int i = 0; i < 12; ++i) { sampleKnobs.add (new Knob (c, juce::String::fromUTF8 (sampleLbl[i]))); addAndMakeVisible (sampleKnobs.getLast()); }
            sampleKnobs[0]->formatter = fmtSt; sampleKnobs[0]->slider.ctrlInteger = true;
            sampleKnobs[1]->formatter = fmtPct; sampleKnobs[2]->formatter = fmtPct;
            sampleKnobs[3]->formatter = [] (float v) { return v >= 0.999f ? juce::String ("Off") : fmtPct (v); };
            sampleKnobs[4]->formatter = fmtMs; sampleKnobs[5]->formatter = fmtPct; sampleKnobs[6]->formatter = fmtDb;
            sampleKnobs[7]->formatter = [] (float v) { return std::fabs (v) < 0.01f ? juce::String ("C") : (v < 0 ? "G " : "D ") + juce::String (juce::roundToInt (std::fabs (v) * 100)); };
            sampleKnobs[8]->formatter = [] (float v) { return v >= 1000.0f ? juce::String (v / 1000.0f, 1) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz"; };
            sampleKnobs[9]->formatter = [] (float v) { return juce::String (v, 2); };
            sampleKnobs[10]->formatter = fmtPct; sampleKnobs[11]->formatter = fmtPct;

            const char* grainLbl[10] = { "Position", "Taille", "Densit\xc3\xa9", "Spray", "Pitch grain", "Al\xc3\xa9" "a pitch", "St\xc3\xa9r\xc3\xa9o", "Vitesse", "Dur\xc3\xa9" "e", "Invers\xc3\xa9s" };
            for (int i = 0; i < 10; ++i) { grainKnobs.add (new Knob (c, juce::String::fromUTF8 (grainLbl[i]))); addChildComponent (grainKnobs.getLast()); }
            grainKnobs[0]->formatter = fmtPct; grainKnobs[1]->formatter = fmtMs;
            grainKnobs[2]->formatter = [] (float v) { return juce::String (v, 1) + " /s"; };
            grainKnobs[3]->formatter = fmtPct; grainKnobs[4]->formatter = fmtSt; grainKnobs[4]->slider.ctrlInteger = true;
            grainKnobs[5]->formatter = fmtSt; grainKnobs[5]->slider.ctrlInteger = true;
            grainKnobs[6]->formatter = fmtPct;
            grainKnobs[7]->formatter = [] (float v) { return (v > 0.005f ? "+" : "") + juce::String (v, 2) + "x"; };
            grainKnobs[8]->formatter = fmtMs; grainKnobs[9]->formatter = fmtPct;

            addAndMakeVisible (wave);
            wave.onDoubleClick = [this] { if (onOpenEditor) onOpenEditor(); };
            addChildComponent (dyn);
            for (auto* b : { &chokeBtn, &revBtn, &gateBtn, &muteBtn, &stepLenBtn, &granOn, &freezeBtn }) { addAndMakeVisible (*b); b->setClickingTogglesState (true); b->textSize = 10.0f; }
            stepLenBtn.setTooltip (juce::String::fromUTF8 ("Le son s'arr\xc3\xaate \xc3\xa0 la fin de son pas (ou des pas fusionn\xc3\xa9s). \xc3\x89teint : il joue comme r\xc3\xa9gl\xc3\xa9."));
            for (auto* b : { &loadBtn, &bigBtn, &sliceGo, &slicePat, &lockDone, &lockClear }) { addAndMakeVisible (*b); b->textSize = 11.0f; }
            loadBtn.onClick = [this] { chooseFile(); };
            bigBtn.onClick = [this] { if (onOpenEditor) onOpenEditor(); };
            addAndMakeVisible (groupBox); addAndMakeVisible (outBox); addAndMakeVisible (ftypeBox);

            // slice
            sliceSens.setSliderStyle (juce::Slider::LinearBar);
            sliceSens.textFromValueFunction = [] (double v) { return juce::String::fromUTF8 ("Sensibilit\xc3\xa9 ") + juce::String (juce::roundToInt (v * 100)) + " %"; };
            sliceSens.setRange (0.0, 1.0, 0.01); sliceSens.setValue (0.6, juce::dontSendNotification);
            sliceSens.updateText();
            sliceSens.onValueChange = [this] { updateSlices(); };
            addAndMakeVisible (sliceSens);
            for (int k = 0; k < 3; ++k)
            {
                const int v = k == 0 ? 4 : (k == 1 ? 6 : 8);
                auto* b = sliceCount.add (new Pill (c, juce::String (v) + " tranches"));
                b->textSize = 11.0f;
                b->isOn = [this, v] { return maxSlices == v; };
                b->onClick = [this, v] { maxSlices = v; updateSlices(); };
                addAndMakeVisible (b);
            }
            sliceGo.onClick = [this] { doSlice (false); };
            slicePat.onClick = [this] { doSlice (true); };
            slicePat.strong = true;

            lockDone.strong = true;
            lockDone.onClick = [this] { setLockStep (-1); };
            lockClear.onClick = [this] { if (lockStep >= 0) ctx.proc.pattern.clearLocks (lane, lockStep); setLockStep (lockStep); };
            // stretch
            const char* modeNames[3] = { "OFF", "SIMPLE", "PAULSTRETCH" };
            for (int k = 0; k < 3; ++k)
            {
                auto* b = strModes.add (new Pill (c, modeNames[k]));
                b->textSize = 11.0f;
                b->isOn = [this, k] { return (int) param ("strmode") == k; };
                b->onClick = [this, k] { setChoice ("strmode", k); };
                addChildComponent (b);
            }
            const char* fitNames[7] = { "LIBRE", "1", "2", "4", "8", "16", "32" };
            for (int k = 0; k < 7; ++k)
            {
                auto* b = strFits.add (new Pill (c, fitNames[k]));
                b->textSize = 11.0f;
                b->isOn = [this, k] { return (int) param ("strfit") == k; };
                b->onClick = [this, k] { setChoice ("strfit", k); };
                addChildComponent (b);
            }
            strAmt.formatter = [this] (float v) { return (int) param ("strfit") > 0 ? juce::String::fromUTF8 ("\xc3\x97") + juce::String (ctx.proc.stretchFactor (lane), 2)
                                                                                    : juce::String::fromUTF8 ("\xc3\x97") + juce::String (v, 2); };
            strWin.formatter = fmtPct;
            addChildComponent (strAmt); addChildComponent (strWin);
            setLane (1);
            setTab (0);
        }

        void setLane (int l)
        {
            using typhon::pid;
            if (lockStep >= 0 && l != lane) lockStep = -1;
            lane = l; wave.lane = l;
            const char* sp[12] = { "pitch", "start", "end", "decay", "fade", "vsens", "vol", "pan", "fcut", "fres", "sendA", "sendB" };
            for (int i = 0; i < 12; ++i) sampleKnobs[i]->attach (pid (l, sp[i]));
            const char* gp[10] = { "gpos", "gsize", "gdens", "gspray", "gpitch", "gprand", "gstereo", "gspeed", "gdur", "grev" };
            for (int i = 0; i < 10; ++i) grainKnobs[i]->attach (pid (l, gp[i]));
            strAmt.attach (pid (l, "stramt")); strWin.attach (pid (l, "strwin"));
            dyn.setPrefix ("l" + juce::String (l) + "_");
            auto& ap = ctx.proc.apvts;
            btnAtts.clear(); boxAtts.clear();
            auto bt = [&] (const char* w, juce::Button& b) { btnAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (ap, pid (l, w), b)); };
            bt ("choke", chokeBtn); bt ("rev", revBtn); bt ("gate", gateBtn); bt ("mute", muteBtn); bt ("steplen", stepLenBtn); bt ("gran", granOn); bt ("gfreeze", freezeBtn);
            groupBox.clear (juce::dontSendNotification);
            groupBox.addItemList ({ "Groupe : aucun", "Groupe A", "Groupe B", "Groupe C", "Groupe D" }, 1);
            outBox.clear (juce::dontSendNotification);
            for (int i = 0; i < typhon::numLanes; ++i) outBox.addItem ("Sortie " + juce::String (i * 2 + 1) + "/" + juce::String (i * 2 + 2), i + 1);
            ftypeBox.clear (juce::dontSendNotification);
            ftypeBox.addItemList ({ "LPF", "HPF", "BPF" }, 1);
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (ap, pid (l, "group"), groupBox));
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (ap, pid (l, "out"), outBox));
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (ap, pid (l, "ftype"), ftypeBox));
            applyLockMode();
            updateSlices();
            repaint();
        }

        // Mode verrou : step = -1 pour sortir
        void setLockStep (int step)
        {
            lockStep = step;
            if (step >= 0 && tab != 0 && tab != 1) setTab (0);
            applyLockMode();
            if (step < 0 && onLockEnded) onLockEnded();
            resized(); repaint();
        }
        int getLockStep() const { return lockStep; }

        void setTab (int t)
        {
            tab = t;
            for (auto* k : sampleKnobs) k->setVisible (t == 0);
            for (auto* k : grainKnobs) k->setVisible (t == 1);
            for (juce::Component* c : { (juce::Component*) &chokeBtn, (juce::Component*) &revBtn, (juce::Component*) &gateBtn, (juce::Component*) &muteBtn, (juce::Component*) &stepLenBtn,
                                        (juce::Component*) &groupBox, (juce::Component*) &outBox, (juce::Component*) &ftypeBox, (juce::Component*) &loadBtn })
                c->setVisible (t == 0);
            wave.setVisible (t != 4);
            bigBtn.setVisible (t != 4);
            for (auto* b : strModes) b->setVisible (t == 3);
            for (auto* b : strFits) b->setVisible (t == 3);
            strAmt.setVisible (t == 3); strWin.setVisible (t == 3);
            granOn.setVisible (t == 1); freezeBtn.setVisible (t == 1);
            sliceSens.setVisible (t == 2); sliceGo.setVisible (t == 2); slicePat.setVisible (t == 2);
            for (auto* b : sliceCount) b->setVisible (t == 2);
            dyn.setVisible (t == 4);
            wave.mode = t == 1 ? Waveform::grainMode : (t == 2 ? Waveform::sliceMode : Waveform::sampleMode);
            if (t == 2) updateSlices();
            resized();
            repaint();
        }

        void tick()
        {
            wave.repaint();
            if (tab == 4) dyn.tick();
            if (tab == 3) { strAmt.setAlpha ((int) param ("strfit") > 0 ? 0.45f : 1.0f); strAmt.repaint(); repaint (strInfo); }
            if (tab == 0) for (auto* k : sampleKnobs) if (k->isLocked()) k->repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            g.setColour (t.panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
            auto r = getLocalBounds().reduced (16, 14);
            auto top = r.removeFromTop (28);
            if (lockStep >= 0)
            {
                g.setColour (t.lcdBg); g.fillRoundedRectangle (top.toFloat().expanded (6, 3), 6.0f);
                g.setColour (t.lcdTx); g.setFont (mono (13.0f, true));
                g.drawText (juce::String::fromUTF8 ("VERROU \xc2\xb7 ") + typhon::laneName (lane).toUpperCase() + " PAS " + juce::String (lockStep + 1), top, juce::Justification::centredLeft);
            }
            else
            {
                g.setColour (t.tx); g.setFont (font (22.0f, true));
                g.drawText (typhon::laneName (lane).toUpperCase(), top, juce::Justification::centredLeft);
                g.setColour (t.sub); g.setFont (mono (12.0f));
                g.drawText ("NOTE " + typhon::noteName (ctx.proc.noteForLane (lane)), top, juce::Justification::centredRight);
            }
            if (tab == 2)
            {
                g.setColour (t.sub); g.setFont (font (12.0f));
                g.drawFittedText (juce::String::fromUTF8 ("Glisse un break sur cette piste, r\xc3\xa8gle la sensibilit\xc3\xa9, puis r\xc3\xa9partis les coups sur les 8 pistes. "
                                                         "\xc2\xab R\xc3\xa9partir + pattern \xc2\xbb r\xc3\xa9\xc3\xa9" "crit aussi le rythme d'origine (au tempo du projet)."),
                                  sliceHelp, juce::Justification::topLeft, 5);
                g.setColour (valueColour (t)); g.setFont (mono (12.0f));
                g.drawText (juce::String ((int) wave.slices.size()) + juce::String::fromUTF8 (" coups d\xc3\xa9tect\xc3\xa9s"), sliceInfo, juce::Justification::centredLeft);
            }
            if (tab == 3)
            {
                g.setColour (t.sub); g.setFont (font (12.0f, true));
                g.drawText (juce::String::fromUTF8 ("CALER LA DUR\xc3\x89" "E SUR (PAS, AU TEMPO DU PROJET)"), strFitLbl, juce::Justification::centredLeft);
                auto base = ctx.proc.baseSet (lane); auto cur = ctx.proc.laneSet (lane);
                auto secs = [] (const std::shared_ptr<typhon::SampleSet>& x) { auto* d = x ? x->display (0) : nullptr; return d ? d->length() / d->rate : 0.0; };
                g.setColour (valueColour (t)); g.setFont (mono (12.0f));
                g.drawText (ctx.proc.stretchRendering (lane) ? juce::String::fromUTF8 ("Calcul en cours\xe2\x80\xa6")
                                                             : juce::String (secs (base), 2) + juce::String::fromUTF8 (" s \xe2\x86\x92 ") + juce::String (secs (cur), 2) + " s",
                            strInfo, juce::Justification::centredLeft);
                g.setColour (t.sub); g.setFont (font (12.0f));
                g.drawFittedText (juce::String::fromUTF8 ("Simple : allonge sans changer la hauteur, garde le punch jusqu'\xc3\xa0 x4. "
                                                         "Paulstretch : transforme le coup en nappe (x8 \xc3\xa0 x32). Fen\xc3\xaatre : grain fin \xe2\x86\x94 flou. "
                                                         "Le calcul se fait hors du son, l'original est gard\xc3\xa9."),
                                  strHelp, juce::Justification::topLeft, 4);
            }
            if (tab == 1 && lockStep < 0)
            {
                g.setColour (t.sub); g.setFont (font (11.0f));
                g.drawFittedText (juce::String::fromUTF8 ("Clique dans la waveform pour placer la position."), grainHint, juce::Justification::centredLeft, 1);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 14);
            auto top = r.removeFromTop (28);
            lockDone.setVisible (lockStep >= 0); lockClear.setVisible (lockStep >= 0);
            if (lockStep >= 0)
            {
                lockDone.setBounds (top.removeFromRight (84).reduced (0, 2)); top.removeFromRight (6);
                lockClear.setBounds (top.removeFromRight (74).reduced (0, 2));
            }
            r.removeFromTop (8);
            auto tb = r.removeFromTop (30);
            const int tw = (tb.getWidth() - 24) / 5;
            for (auto* b : tabs) { b->setBounds (tb.removeFromLeft (tw)); tb.removeFromLeft (6); }
            r.removeFromTop (8);
            if (tab != 4)
            {
                auto wr = r.removeFromTop (tab == 2 ? 120 : 76);
                wave.setBounds (wr);
                bigBtn.setBounds (wr.getRight() - 78, wr.getBottom() + 3, 78, 18);
                r.removeFromTop (24);
            }
            if (tab == 0)
            {
                auto grid = r.removeFromTop (3 * 80);
                const int cw = grid.getWidth() / 4;
                for (int i = 0; i < 12; ++i) sampleKnobs[i]->setBounds (grid.getX() + (i % 4) * cw, grid.getY() + (i / 4) * 80, cw, 80);
                r.removeFromTop (6);
                auto row = r.removeFromTop (28);
                const int bw = (row.getWidth() - 16) / 5;
                for (auto* b : { &chokeBtn, &revBtn, &gateBtn, &muteBtn, &stepLenBtn }) { b->setBounds (row.removeFromLeft (bw)); row.removeFromLeft (4); }
                r.removeFromTop (6);
                auto row2 = r.removeFromTop (28);
                const int w4 = (row2.getWidth() - 18) / 4;
                groupBox.setBounds (row2.removeFromLeft (w4)); row2.removeFromLeft (6);
                outBox.setBounds (row2.removeFromLeft (w4)); row2.removeFromLeft (6);
                ftypeBox.setBounds (row2.removeFromLeft (w4)); row2.removeFromLeft (6);
                loadBtn.setBounds (row2);
            }
            else if (tab == 1)
            {
                auto row = r.removeFromTop (28);
                granOn.setBounds (row.removeFromLeft (row.getWidth() / 2 - 3)); row.removeFromLeft (6);
                freezeBtn.setBounds (row);
                grainHint = r.removeFromTop (18);
                auto grid = r.removeFromTop (3 * 80);
                const int cw = grid.getWidth() / 4;
                for (int i = 0; i < 10; ++i) grainKnobs[i]->setBounds (grid.getX() + (i % 4) * cw, grid.getY() + (i / 4) * 80, cw, 80);
            }
            else if (tab == 2)
            {
                sliceSens.setBounds (r.removeFromTop (26)); r.removeFromTop (8);
                auto row = r.removeFromTop (30);
                const int w3 = (row.getWidth() - 12) / 3;
                for (auto* b : sliceCount) { b->setBounds (row.removeFromLeft (w3)); row.removeFromLeft (6); }
                r.removeFromTop (6);
                sliceInfo = r.removeFromTop (20);
                r.removeFromTop (4);
                sliceGo.setBounds (r.removeFromTop (34)); r.removeFromTop (6);
                slicePat.setBounds (r.removeFromTop (38)); r.removeFromTop (8);
                sliceHelp = r.removeFromTop (80);
            }
            else if (tab == 3)
            {
                auto row = r.removeFromTop (30);
                const int w3 = (row.getWidth() - 12) / 3;
                for (auto* b : strModes) { b->setBounds (row.removeFromLeft (w3)); row.removeFromLeft (6); }
                r.removeFromTop (8);
                auto kr = r.removeFromTop (84);
                strAmt.setBounds (kr.removeFromLeft (kr.getWidth() / 2)); strWin.setBounds (kr);
                r.removeFromTop (4);
                strFitLbl = r.removeFromTop (18);
                auto fr = r.removeFromTop (28);
                const int w7 = (fr.getWidth() - 24) / 7;
                for (auto* b : strFits) { b->setBounds (fr.removeFromLeft (w7)); fr.removeFromLeft (4); }
                r.removeFromTop (8);
                strInfo = r.removeFromTop (22);
                strHelp = r.removeFromTop (70);
            }
            else dyn.setBounds (r);
        }

        Waveform wave;
        int lane = 1;

    private:
        void applyLockMode()
        {
            using namespace typhon;
            // potards verrouillables : volume, pan, début, décroissance, filtre, envois A / B, position du grain
            struct Map { Knob* k; int lock; const char* id; };
            const Map maps[] = { { sampleKnobs[6], lkVol, "vol" }, { sampleKnobs[7], lkPan, "pan" }, { sampleKnobs[1], lkStart, "start" },
                                 { sampleKnobs[3], lkDecay, "decay" }, { sampleKnobs[8], lkCut, "fcut" }, { sampleKnobs[10], lkSendA, "sendA" },
                                 { sampleKnobs[11], lkSendB, "sendB" }, { grainKnobs[0], lkGpos, "gpos" } };
            for (auto& m : maps)
            {
                if (lockStep >= 0)
                {
                    const int l = lane, s = lockStep, k = m.lock;
                    auto* pat = &ctx.proc.pattern;
                    m.k->setLockMode ([pat, l, s, k] { return pat->lock (l, s, k); },
                                      [pat, l, s, k] (float v) { pat->setLock (l, s, k, v); });
                }
                else m.k->attach (pid (lane, m.id));
            }
            if (lockStep >= 0)
            {
                const int l = lane, s = lockStep;
                auto* pat = &ctx.proc.pattern;
                wave.lockStartGet = [pat, l, s] { return pat->lock (l, s, lkStart); };
                wave.lockStartSet = [pat, l, s] (float v) { pat->setLock (l, s, lkStart, v); };
            }
            else { wave.lockStartGet = nullptr; wave.lockStartSet = nullptr; }
            // potards non verrouillables : grisés pendant le mode verrou
            for (auto* k : sampleKnobs)
            {
                bool lockable = false;
                for (auto& m : maps) lockable |= m.k == k;
                k->setAlpha (lockStep >= 0 && ! lockable ? 0.35f : 1.0f);
                k->setEnabled (lockStep < 0 || lockable);
            }
            for (auto* k : grainKnobs)
            {
                const bool lockable = k == grainKnobs[0];
                k->setAlpha (lockStep >= 0 && ! lockable ? 0.35f : 1.0f);
                k->setEnabled (lockStep < 0 || lockable);
            }
        }

        void updateSlices()
        {
            wave.slices.clear();
            auto set = ctx.proc.laneSet (lane);
            if (set && ! set->empty())
                if (auto* d = set->display ((int) ctx.proc.apvts.getRawParameterValue (typhon::pid (lane, "var"))->load() - 1))
                    wave.slices = typhon::detectSlices (*d, (float) sliceSens.getValue(), maxSlices);
            repaint();
        }

        void doSlice (bool withPattern)
        {
            ctx.proc.sliceLane (lane, (float) sliceSens.getValue(), maxSlices, withPattern);
            updateSlices();
            if (auto* p = getParentComponent()) p->repaint();
        }

        void chooseFile()
        {
            chooser = std::make_unique<juce::FileChooser> ("Charger un sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
            juce::Component::SafePointer<LanePanel> safe (this);
            const int l = lane;
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [safe, l] (const juce::FileChooser& fc)
                {
                    if (safe == nullptr) return;
                    auto f = fc.getResult();
                    if (f.existsAsFile() && safe->ctx.proc.loadSampleFile (l, f)) safe->ctx.proc.audition (l);
                    safe->updateSlices();
                    safe->repaint();
                });
        }

        Ctx& ctx;
        int tab = 0, lockStep = -1, maxSlices = 8;
        DynBlock dyn;
        juce::OwnedArray<Pill> tabs, sliceCount;
        Pill chokeBtn, revBtn, gateBtn, muteBtn, stepLenBtn, loadBtn, bigBtn, granOn, freezeBtn, sliceGo, slicePat, lockDone, lockClear;
        juce::ComboBox groupBox, outBox, ftypeBox;
        juce::Slider sliceSens;
        juce::OwnedArray<Knob> sampleKnobs, grainKnobs;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> btnAtts;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> boxAtts;
        juce::Rectangle<int> sliceHelp, sliceInfo, grainHint, strFitLbl, strInfo, strHelp;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::OwnedArray<Pill> strModes, strFits;
        Knob strAmt { ctx, "Facteur" }, strWin { ctx, juce::String::fromUTF8 ("Fen\xc3\xaatre") };
        float param (const char* w) const { return ctx.proc.apvts.getRawParameterValue (typhon::pid (lane, w))->load(); }
        void setChoice (const char* w, int idx)
        {
            if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (ctx.proc.apvts.getParameter (typhon::pid (lane, w))))
            { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) idx)); p->endChangeGesture(); ctx.proc.undoCommit(); }
            repaint();
        }
    };

    // ------------------------------------------------------------------------
    //  Éditeur de sample en grand (par-dessus le séquenceur)
    // ------------------------------------------------------------------------
    class SampleEditor : public juce::Component
    {
    public:
        std::function<void()> onClose;
        SampleEditor (Ctx& c) : ctx (c), wave (c), closeBtn (c, "FERMER"), zin (c, "ZOOM +"), zout (c, "ZOOM -"), zsel (c, juce::String::fromUTF8 ("ZOOM S\xc3\x89LECTION")),
                                zall (c, "TOUT"), revBtn (c, "REVERSE", true), resetBtn (c, juce::String::fromUTF8 ("R\xc3\x89INITIALISER"))
        {
            wave.big = true;
            addAndMakeVisible (wave);
            for (auto* b : { &closeBtn, &zin, &zout, &zsel, &zall, &revBtn, &resetBtn }) { addAndMakeVisible (*b); b->textSize = 12.0f; }
            closeBtn.strong = true;
            closeBtn.onClick = [this] { if (onClose) onClose(); };
            zin.onClick = [this] { wave.zoomBy (1.6); };
            zout.onClick = [this] { wave.zoomBy (1.0 / 1.6); };
            zsel.onClick = [this] { wave.zoomToSelection(); };
            zall.onClick = [this] { wave.resetZoom(); };
            revBtn.setClickingTogglesState (true);
            resetBtn.onClick = [this]
            {
                for (auto w : { "start", "end" })
                    if (auto* p = ctx.proc.apvts.getParameter (typhon::pid (lane, w))) p->setValueNotifyingHost (p->getDefaultValue());
            };
            const char* lbl[6] = { "D\xc3\xa9" "but", "Fin", "Pitch", "D\xc3\xa9" "croiss.", "Fondu choke", "Volume" };
            for (int i = 0; i < 6; ++i) { knobs.add (new Knob (c, juce::String::fromUTF8 (lbl[i]))); addAndMakeVisible (knobs.getLast()); }
            knobs[0]->formatter = fmtPct; knobs[1]->formatter = fmtPct; knobs[2]->formatter = fmtSt; knobs[2]->slider.ctrlInteger = true;
            knobs[3]->formatter = [] (float v) { return v >= 0.999f ? juce::String ("Off") : fmtPct (v); };
            knobs[4]->formatter = fmtMs; knobs[5]->formatter = fmtDb;
        }
        void setLane (int l)
        {
            lane = l; wave.lane = l;
            const char* ids[6] = { "start", "end", "pitch", "decay", "fade", "vol" };
            for (int i = 0; i < 6; ++i) knobs[i]->attach (typhon::pid (l, ids[i]));
            revAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ctx.proc.apvts, typhon::pid (l, "rev"), revBtn);
            wave.resetZoom();
            repaint();
        }
        void tick() { wave.repaint(); }
        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            g.setColour (t.panel); g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
            g.setColour (t.tx); g.setFont (font (22.0f, true));
            g.drawText (juce::String::fromUTF8 ("\xc3\x89" "DITEUR \xc2\xb7 ") + typhon::laneName (lane).toUpperCase(), titleArea, juce::Justification::centredLeft);
            g.setColour (t.sub); g.setFont (font (12.0f));
            g.drawText (juce::String::fromUTF8 ("Molette : zoom \xc2\xb7 Maj + molette ou glisser : d\xc3\xa9placer \xc2\xb7 Saisis les drapeaux orange pour d\xc3\xa9placer le d\xc3\xa9" "but et la fin \xc2\xb7 Clic droit : options"),
                        hintArea, juce::Justification::centredLeft);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (18, 14);
            auto top = r.removeFromTop (34);
            closeBtn.setBounds (top.removeFromRight (110));
            titleArea = top;
            r.removeFromTop (6);
            hintArea = r.removeFromTop (18);
            r.removeFromTop (6);
            auto bottom = r.removeFromBottom (96);
            auto bar = r.removeFromBottom (34); r.removeFromBottom (8);
            for (auto* b : { &zin, &zout, &zsel, &zall }) { b->setBounds (bar.removeFromLeft (b == &zsel ? 150 : 90)); bar.removeFromLeft (8); }
            resetBtn.setBounds (bar.removeFromRight (130)); bar.removeFromRight (8);
            revBtn.setBounds (bar.removeFromRight (110));
            wave.setBounds (r);
            const int kw = std::min (110, bottom.getWidth() / 6);
            for (auto* k : knobs) k->setBounds (bottom.removeFromLeft (kw));
        }
    private:
        Ctx& ctx;
        int lane = 1;
        Waveform wave;
        Pill closeBtn, zin, zout, zsel, zall, revBtn, resetBtn;
        juce::OwnedArray<Knob> knobs;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> revAtt;
        juce::Rectangle<int> titleArea, hintArea;
    };
}
