#pragma once
// ============================================================================
//  Page MIX : bus d'effets (A réverbe, B delay), envois par piste, ducking,
//  master (transient + dynamique), et panneau de réglages du fill
// ============================================================================
#include "UiWidgets.h"
#include "UiLane.h"

namespace tui
{
    class MixPage : public juce::Component
    {
    public:
        MixPage (Ctx& c) : ctx (c), pingPong (c, "PING-PONG", true), duckFx (c, "EFFETS AUSSI", true), master (c)
        {
            auto K = [&] (const juce::String& lbl, const juce::String& id, std::function<juce::String (float)> fmt) {
                auto* k = knobs.add (new Knob (c, lbl)); k->attach (id); k->formatter = fmt; addAndMakeVisible (k); return k;
            };
            // bus A
            kA[0] = K ("Taille", "fxa_size", fmtPct); kA[1] = K ("Amorti", "fxa_damp", fmtPct);
            kA[2] = K ("Largeur", "fxa_width", fmtPct); kA[3] = K ("Retour", "fxa_ret", fmtDb);
            // bus B
            kB[0] = K ("Feedback", "fxb_fb", fmtPct); kB[1] = K (juce::String::fromUTF8 ("Tonalit\xc3\xa9"), "fxb_tone", fmtPct);
            kB[2] = K ("Retour", "fxb_ret", fmtDb);
            timeBox.addItemList ({ "1/16", "1/8", juce::String::fromUTF8 ("1/8 point\xc3\xa9"), "1/4", juce::String::fromUTF8 ("1/4 point\xc3\xa9"), "1/2" }, 1);
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (c.proc.apvts, "fxb_time", timeBox));
            addAndMakeVisible (timeBox);
            for (auto* b : { &pingPong, &duckFx }) { addAndMakeVisible (*b); b->setClickingTogglesState (true); b->textSize = 11.0f; }
            btnAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (c.proc.apvts, "fxb_pp", pingPong));
            btnAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (c.proc.apvts, "duck_fx", duckFx));

            // envois et ducking par piste
            for (int l = 0; l < typhon::numLanes; ++l)
            {
                vol[l] = K ("", typhon::pid (l, "vol"), fmtDb);
                pan[l] = K ("", typhon::pid (l, "pan"), [] (float v) { return std::fabs (v) < 0.01f ? juce::String ("C") : (v < 0 ? "G " : "D ") + juce::String (juce::roundToInt (std::fabs (v) * 100)); });
                sendA[l] = K ("", typhon::pid (l, "sendA"), fmtPct);
                sendB[l] = K ("", typhon::pid (l, "sendB"), fmtPct);
                auto* d = duckBtns.add (new Pill (c, "DUCK", true));
                d->setClickingTogglesState (true); d->textSize = 10.0f;
                btnAtts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (c.proc.apvts, typhon::pid (l, "duck"), *d));
                addAndMakeVisible (d);
            }
            // ducking
            srcBox.addItemList ([] { juce::StringArray a; for (int i = 0; i < typhon::numLanes; ++i) a.add (typhon::laneName (i)); return a; }(), 1);
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (c.proc.apvts, "duck_src", srcBox));
            addAndMakeVisible (srcBox);
            kD[0] = K ("Profondeur", "duck_amt", fmtPct);
            kD[1] = K (juce::String::fromUTF8 ("Rel\xc3\xa2" "che"), "duck_rel", fmtMs);
            kMaster = K ("Master", "master", fmtDb);
            addAndMakeVisible (master);
            master.setPrefix ("m_");
        }

        void tick() { master.tick(); repaint (meterArea); }

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            for (auto& col : { left, mid, right }) { g.setColour (t.panel); g.fillRoundedRectangle (col.toFloat(), 10.0f); }
            g.setColour (t.sub); g.setFont (font (12.0f, true));
            g.drawText (juce::String::fromUTF8 ("BUS A \xc2\xb7 R\xc3\x89VERBE"), lblA, juce::Justification::centredLeft);
            g.drawText (juce::String::fromUTF8 ("BUS B \xc2\xb7 DELAY (CAL\xc3\x89 SUR LE TEMPO)"), lblB, juce::Justification::centredLeft);
            g.drawText ("TABLE DE MIXAGE", lblSends, juce::Justification::centredLeft);
            g.drawText ("DUCKING (POMPE)", lblDuck, juce::Justification::centredLeft);
            g.drawText ("MASTER (SORTIE PRINCIPALE)", lblMaster, juce::Justification::centredLeft);
            g.setFont (font (11.0f, true));
            g.drawText ("VOLUME", colV, juce::Justification::centred);
            g.drawText ("PAN", colP, juce::Justification::centred);
            g.drawText ("ENVOI A", colA, juce::Justification::centred);
            g.drawText ("ENVOI B", colB, juce::Justification::centred);
            g.drawText ("DUCK", colD, juce::Justification::centred);
            g.setColour (t.tx); g.setFont (font (14.0f, true));
            for (int l = 0; l < typhon::numLanes; ++l) g.drawText (typhon::laneName (l), laneLbl[l], juce::Justification::centredLeft);
            g.setColour (t.sub); g.setFont (font (12.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Chaque coup de la piste source fait baisser les pistes marqu\xc3\xa9" "es DUCK, puis elles remontent (effet de pompe EDM). Profondeur 0 = coup\xc3\xa9."),
                              duckHelp, juce::Justification::topLeft, 3);
            g.drawText ("Temps", timeLbl, juce::Justification::centredLeft);
            g.drawText ("Source", srcLbl, juce::Justification::centredLeft);
            // vu-mètre master
            auto m = meterArea.toFloat();
            g.setColour (t.lcdBg); g.fillRoundedRectangle (m, 4.0f);
            const float lv = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (ctx.proc.masterLevel(), -60.0f) + 60.0f) / 66.0f);
            g.setColour (lv > 0.91f ? juce::Colours::red : valueColour (t));
            g.fillRoundedRectangle (m.withTop (m.getBottom() - m.getHeight() * lv), 4.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (24, 20);
            left = r.removeFromLeft (300); r.removeFromLeft (16);
            right = r.removeFromRight (370); r.removeFromRight (16);
            mid = r;

            auto L = left.reduced (16);
            lblA = L.removeFromTop (18); L.removeFromTop (6);
            { auto row = L.removeFromTop (90); const int w = row.getWidth() / 4; for (auto* k : kA) k->setBounds (row.removeFromLeft (w)); }
            L.removeFromTop (18);
            lblB = L.removeFromTop (18); L.removeFromTop (6);
            { auto row = L.removeFromTop (28); timeLbl = row.removeFromLeft (60); timeBox.setBounds (row.removeFromLeft (130)); row.removeFromLeft (10); pingPong.setBounds (row); }
            L.removeFromTop (8);
            { auto row = L.removeFromTop (90); const int w = row.getWidth() / 3; for (auto* k : kB) k->setBounds (row.removeFromLeft (w)); }
            L.removeFromTop (18);
            lblDuck = L.removeFromTop (18); L.removeFromTop (6);
            { auto row = L.removeFromTop (28); srcLbl = row.removeFromLeft (60); srcBox.setBounds (row.removeFromLeft (130)); row.removeFromLeft (10); duckFx.setBounds (row); }
            L.removeFromTop (8);
            { auto row = L.removeFromTop (90); const int w = row.getWidth() / 3; kD[0]->setBounds (row.removeFromLeft (w)); kD[1]->setBounds (row.removeFromLeft (w)); }
            duckHelp = L.removeFromTop (50);

            auto M = mid.reduced (16);
            lblSends = M.removeFromTop (18); M.removeFromTop (4);
            auto head = M.removeFromTop (18);
            head.removeFromLeft (96);
            const int cw = (head.getWidth()) / 5;
            colV = head.removeFromLeft (cw); colP = head.removeFromLeft (cw); colA = head.removeFromLeft (cw); colB = head.removeFromLeft (cw); colD = head;
            const int rowH = std::min (70, M.getHeight() / typhon::numLanes);
            for (int l = 0; l < typhon::numLanes; ++l)
            {
                auto row = M.removeFromTop (rowH);
                laneLbl[l] = row.removeFromLeft (96);
                vol[l]->setBounds (row.removeFromLeft (cw).withSizeKeepingCentre (64, rowH - 4));
                pan[l]->setBounds (row.removeFromLeft (cw).withSizeKeepingCentre (64, rowH - 4));
                sendA[l]->setBounds (row.removeFromLeft (cw).withSizeKeepingCentre (64, rowH - 4));
                sendB[l]->setBounds (row.removeFromLeft (cw).withSizeKeepingCentre (64, rowH - 4));
                duckBtns[l]->setBounds (row.withSizeKeepingCentre (std::min (70, row.getWidth() - 4), 26));
            }

            auto R = right.reduced (16);
            lblMaster = R.removeFromTop (18); R.removeFromTop (6);
            auto mrow = R.removeFromTop (100);
            meterArea = mrow.removeFromRight (16);
            mrow.removeFromRight (10);
            kMaster->setBounds (mrow.removeFromRight (80));
            R.removeFromTop (6);
            master.setBounds (R.removeFromTop (370));
        }

    private:
        Ctx& ctx;
        juce::OwnedArray<Knob> knobs;
        Knob* kA[4] {}; Knob* kB[3] {}; Knob* kD[2] {}; Knob* kMaster = nullptr;
        Knob* sendA[typhon::numLanes] {}; Knob* sendB[typhon::numLanes] {}; Knob* vol[typhon::numLanes] {}; Knob* pan[typhon::numLanes] {};
        juce::OwnedArray<Pill> duckBtns;
        Pill pingPong, duckFx;
        juce::ComboBox timeBox, srcBox;
        DynBlock master;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> btnAtts;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> boxAtts;
        juce::Rectangle<int> left, mid, right, lblA, lblB, lblSends, lblDuck, lblMaster, colV, colP, colA, colB, colD, duckHelp, timeLbl, srcLbl, meterArea;
        juce::Rectangle<int> laneLbl[typhon::numLanes];
    };

    // ------------------------------------------------------------------------
    //  Réglages du fill (bulle ouverte depuis la barre du bas)
    // ------------------------------------------------------------------------
    class FillPanel : public juce::Component
    {
    public:
        FillPanel (Ctx& c) : ctx (c), crash (c, "CRASH AU RETOUR", true)
        {
            auto& ap = c.proc.apvts;
            for (int l = 0; l < typhon::numLanes; ++l)
            {
                auto* b = lanes.add (new Pill (c, typhon::laneName (l), true));
                b->setClickingTogglesState (true); b->textSize = 11.0f;
                atts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (ap, "fill_l" + juce::String (l), *b));
                addAndMakeVisible (b);
            }
            lenBox.addItemList ({ "1 temps", "2 temps", "1 mesure" }, 1);
            gridBox.addItemList ({ "1/8", "1/16", "1/32", "1/16 triolet" }, 1);
            othersBox.addItemList ({ "Gardent leur pattern", "Se taisent" }, 1);
            srcBox.addItemList ({ juce::String::fromUTF8 ("G\xc3\xa9n\xc3\xa9r\xc3\xa9 (roulement)"), juce::String::fromUTF8 ("Pattern FILL (\xc3\xa9" "crit)") }, 1);
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (ap, "fill_src", srcBox));
            addAndMakeVisible (srcBox);
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (ap, "fill_len", lenBox));
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (ap, "fill_grid", gridBox));
            boxAtts.add (new juce::AudioProcessorValueTreeState::ComboBoxAttachment (ap, "fill_others", othersBox));
            for (auto* b : { &lenBox, &gridBox, &othersBox }) addAndMakeVisible (*b);
            crash.setClickingTogglesState (true); crash.textSize = 11.0f;
            atts.add (new juce::AudioProcessorValueTreeState::ButtonAttachment (ap, "fill_crash", crash));
            addAndMakeVisible (crash);
            setSize (420, 300);
        }
        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            g.fillAll (t.panel);
            g.setColour (t.tx); g.setFont (font (16.0f, true));
            g.drawText ("FILL", 16, 10, 200, 22, juce::Justification::centredLeft);
            g.setColour (t.sub); g.setFont (font (12.0f));
            g.drawText (juce::String::fromUTF8 ("Pistes qui jouent le roulement (elles alternent)"), 16, 34, 400, 18, juce::Justification::centredLeft);
            g.drawText ("Longueur", 16, 150, 120, 18, juce::Justification::centredLeft);
            g.drawText ("Grille", 146, 150, 120, 18, juce::Justification::centredLeft);
            g.drawText ("Les autres pistes", 276, 150, 140, 18, juce::Justification::centredLeft);
            g.drawText ("Source du fill", 196, 202, 200, 18, juce::Justification::centredLeft);
            g.setFont (font (11.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Pattern FILL : choisis FILL en haut de la grille, \xc3\xa9" "cris ton fill, reviens sur A..H."), 16, 262, 400, 30, juce::Justification::topLeft, 2);
        }
        void resized() override
        {
            for (int l = 0; l < typhon::numLanes; ++l)
                lanes[l]->setBounds (16 + (l % 4) * 98, 56 + (l / 4) * 40, 92, 32);
            lenBox.setBounds (16, 170, 120, 28);
            gridBox.setBounds (146, 170, 120, 28);
            othersBox.setBounds (276, 170, 130, 28);
            crash.setBounds (16, 220, 160, 32);
            srcBox.setBounds (196, 222, 210, 28);
        }
    private:
        Ctx& ctx;
        juce::OwnedArray<Pill> lanes;
        juce::ComboBox lenBox, gridBox, othersBox, srcBox;
        Pill crash;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ButtonAttachment> atts;
        juce::OwnedArray<juce::AudioProcessorValueTreeState::ComboBoxAttachment> boxAtts;
    };
}
