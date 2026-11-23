#pragma once
// ============================================================================
//  Panneau de la piste sélectionnée : onglets SAMPLE et TRANSIENT · DYNAMIQUE
// ============================================================================
#include "UiWidgets.h"

namespace tui
{
    class Waveform : public juce::Component
    {
    public:
        Waveform (Ctx& c) : ctx (c) {}
        int lane = 1;
        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            auto r = getLocalBounds().toFloat();
            g.setColour (t.lcdBg); g.fillRoundedRectangle (r, 6.0f);
            auto set = ctx.proc.laneSet (lane);
            const typhon::Sample* s = (set && ! set->empty()) ? set->layers.back()[0].get() : nullptr;
            if (s == nullptr || s->length() < 2)
            {
                g.setColour (t.lcdTx.withAlpha (0.6f)); g.setFont (mono (12.0f));
                g.drawText ("Glisse un sample sur la piste", r, juce::Justification::centred);
                return;
            }
            if (s != cached) rebuild (*s);
            const float mid = r.getCentreY(), hh = r.getHeight() * 0.45f;
            const int W = (int) peaks.size();
            g.setColour (t.lcdTx);
            for (int x = 0; x < W; ++x)
            {
                const float p = peaks[(size_t) x] * hh;
                g.fillRect (r.getX() + (float) x * r.getWidth() / W, mid - p, std::max (1.0f, r.getWidth() / W), std::max (1.0f, p * 2.0f));
            }
            auto& ap = ctx.proc.apvts;
            const float st = ap.getRawParameterValue (typhon::pid (lane, "start"))->load();
            const float en = ap.getRawParameterValue (typhon::pid (lane, "end"))->load();
            g.setColour (t.lcdBg.withAlpha (0.75f));
            g.fillRect (r.withWidth (r.getWidth() * st));
            g.fillRect (r.withTrimmedLeft (r.getWidth() * en));
            g.setColour (t.acc);
            g.fillRect (r.getX() + r.getWidth() * st - 1.0f, r.getY(), 2.0f, r.getHeight());
            g.fillRect (r.getX() + r.getWidth() * en - 1.0f, r.getY(), 2.0f, r.getHeight());
            g.setFont (mono (11.0f));
            const double secs = s->length() / s->rate;
            g.drawText (juce::String (juce::roundToInt (secs * (en - st) * 1000.0)) + " ms", r.reduced (8, 4).toNearestInt(), juce::Justification::topRight);
            g.setColour (t.lcdTx);
            g.drawText (ctx.proc.laneSource (lane), r.reduced (8, 4).toNearestInt(), juce::Justification::bottomLeft);
            if (ap.getRawParameterValue (typhon::pid (lane, "rev"))->load() > 0.5f)
                g.drawText ("REVERSE", r.reduced (8, 4).toNearestInt(), juce::Justification::topLeft);
        }
    private:
        void rebuild (const typhon::Sample& s)
        {
            cached = &s;
            const int W = std::max (50, getWidth());
            peaks.assign ((size_t) W, 0.0f);
            const int n = s.length();
            for (int i = 0; i < n; ++i)
            {
                auto& p = peaks[(size_t) std::min (W - 1, (int) ((juce::int64) i * W / n))];
                p = std::max (p, std::fabs (s.l[(size_t) i]));
            }
            float mx = 0.0f; for (float p : peaks) mx = std::max (mx, p);
            if (mx > 0.0f) for (auto& p : peaks) p /= mx;
        }
        Ctx& ctx;
        const typhon::Sample* cached = nullptr;
        std::vector<float> peaks;
    };

    class DynCurve : public juce::Component
    {
    public:
        DynCurve (Ctx& c) : ctx (c) {}
        int lane = 1;
        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            auto r = getLocalBounds().toFloat();
            g.setColour (t.lcdBg); g.fillRoundedRectangle (r, 6.0f);
            auto& ap = ctx.proc.apvts;
            const int mode = (int) ap.getRawParameterValue (typhon::pid (lane, "dmode"))->load();
            const float thr = ap.getRawParameterValue (typhon::pid (lane, "dthr"))->load();
            const float ratio = ap.getRawParameterValue (typhon::pid (lane, "dratio"))->load();
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
    private:
        Ctx& ctx;
    };

    class LanePanel : public juce::Component
    {
    public:
        LanePanel (Ctx& c) : ctx (c), wave (c), curve (c),
            tabSample (c, "SAMPLE"), tabDyn (c, juce::String::fromUTF8 ("TRANSIENT \xc2\xb7 DYNAMIQUE")),
            chokeBtn (c, "CHOKE", true), revBtn (c, "REVERSE", true), gateBtn (c, "GATE MIDI", true), muteBtn (c, "MUTE", true),
            loadBtn (c, "CHARGER...")
        {
            const char* sampleLbl[8] = { "Pitch", "D\xc3\xa9" "but", "Fin", "D\xc3\xa9" "croiss.", "Fondu choke", "V\xc3\xa9locit\xc3\xa9", "Volume", "Pan" };
            for (int i = 0; i < 8; ++i) { sampleKnobs.add (new Knob (c, juce::String::fromUTF8 (sampleLbl[i]))); addAndMakeVisible (sampleKnobs.getLast()); }
            const char* dynNames[7] = { "Attaque", "Sustain", "Seuil", "Ratio", "Temps att.", "Rel\xc3\xa2" "che", "Mix" };
            for (int i = 0; i < 7; ++i) { dynKnobs.add (new Knob (c, juce::String::fromUTF8 (dynNames[i]))); addChildComponent (dynKnobs.getLast()); }

            sampleKnobs[0]->formatter = fmtSt;
            sampleKnobs[1]->formatter = fmtPct; sampleKnobs[2]->formatter = fmtPct;
            sampleKnobs[3]->formatter = [] (float v) { return v >= 0.999f ? juce::String ("Off") : fmtPct (v); };
            sampleKnobs[4]->formatter = fmtMs; sampleKnobs[5]->formatter = fmtPct; sampleKnobs[6]->formatter = fmtDb;
            sampleKnobs[7]->formatter = [] (float v) { return std::fabs (v) < 0.01f ? juce::String ("C") : (v < 0 ? "G " : "D ") + juce::String (juce::roundToInt (std::fabs (v) * 100)); };
            dynKnobs[0]->formatter = fmtSignedDb; dynKnobs[1]->formatter = fmtSignedDb;
            dynKnobs[2]->formatter = fmtDb; dynKnobs[3]->formatter = [] (float v) { return juce::String (v, 1) + ":1"; };
            dynKnobs[4]->formatter = fmtMs; dynKnobs[5]->formatter = fmtMs; dynKnobs[6]->formatter = fmtPct;

            for (auto* b : { &tabSample, &tabDyn }) { addAndMakeVisible (*b); b->textSize = 11.0f; }
            tabSample.isOn = [this] { return tab == 0; };
            tabDyn.isOn = [this] { return tab == 1; };
            tabSample.onClick = [this] { setTab (0); };
            tabDyn.onClick = [this] { setTab (1); };

            addAndMakeVisible (wave);
            addChildComponent (curve);
            for (auto* b : { &chokeBtn, &revBtn, &gateBtn, &muteBtn }) { addAndMakeVisible (*b); b->setClickingTogglesState (true); b->textSize = 11.0f; }
            addAndMakeVisible (loadBtn); loadBtn.textSize = 11.0f;
            loadBtn.onClick = [this] { chooseFile(); };
            addAndMakeVisible (groupBox);
            addAndMakeVisible (outBox);

            const char* modeLbl[4] = { "COMP \xe2\x86\x93", "COMP \xe2\x86\x91", "EXPAND \xe2\x86\x93", "EXPAND \xe2\x86\x91" };
            for (int k = 0; k < 4; ++k)
            {
                auto* b = modeBtns.add (new Pill (c, juce::String::fromUTF8 (modeLbl[k])));
                b->textSize = 11.0f;
                b->isOn = [this, k] { return (int) ctx.proc.apvts.getRawParameterValue (typhon::pid (lane, "dmode"))->load() == k; };
                b->onClick = [this, k] {
                    if (auto* p = ctx.proc.apvts.getParameter (typhon::pid (lane, "dmode"))) p->setValueNotifyingHost (p->convertTo0to1 ((float) k));
                    repaint();
                };
                addChildComponent (b);
            }
            setLane (1);
        }

        void setLane (int l)
        {
            using typhon::pid;
            lane = l; wave.lane = l; curve.lane = l;
            const char* sp[8] = { "pitch", "start", "end", "decay", "fade", "vsens", "vol", "pan" };
            for (int i = 0; i < 8; ++i) sampleKnobs[i]->attach (pid (l, sp[i]));
            const char* dp[7] = { "tatt", "tsus", "dthr", "dratio", "datt", "drel", "dmix" };
            for (int i = 0; i < 7; ++i) dynKnobs[i]->attach (pid (l, dp[i]));
            auto& ap = ctx.proc.apvts;
            chokeAtt.reset(); revAtt.reset(); gateAtt.reset(); muteAtt.reset(); groupAtt.reset(); outAtt.reset();
            chokeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, pid (l, "choke"), chokeBtn);
            revAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, pid (l, "rev"), revBtn);
            gateAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, pid (l, "gate"), gateBtn);
            muteAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (ap, pid (l, "mute"), muteBtn);
            groupBox.clear (juce::dontSendNotification);
            groupBox.addItemList ({ "Groupe : aucun", "Groupe A", "Groupe B", "Groupe C", "Groupe D" }, 1);
            groupAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, pid (l, "group"), groupBox);
            outBox.clear (juce::dontSendNotification);
            for (int i = 0; i < typhon::numLanes; ++i) outBox.addItem ("Sortie " + juce::String (i * 2 + 1) + "/" + juce::String (i * 2 + 2), i + 1);
            outAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (ap, pid (l, "out"), outBox);
            repaint();
        }

        void setTab (int t)
        {
            tab = t;
            for (auto* k : sampleKnobs) k->setVisible (t == 0);
            for (auto* k : dynKnobs) k->setVisible (t == 1);
            for (auto* b : modeBtns) b->setVisible (t == 1);
            for (juce::Component* c : { (juce::Component*) &wave, (juce::Component*) &chokeBtn, (juce::Component*) &revBtn, (juce::Component*) &gateBtn,
                                        (juce::Component*) &muteBtn, (juce::Component*) &groupBox, (juce::Component*) &outBox, (juce::Component*) &loadBtn })
                c->setVisible (t == 0);
            curve.setVisible (t == 1);
            resized();
            repaint();
        }

        void tick() { wave.repaint(); curve.repaint(); for (auto* b : modeBtns) b->repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            g.setColour (t.panel);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
            auto r = getLocalBounds().reduced (16, 14);
            auto top = r.removeFromTop (28);
            g.setColour (t.tx);
            g.setFont (font (22.0f, true));
            g.drawText (typhon::laneName (lane).toUpperCase(), top, juce::Justification::centredLeft);
            g.setColour (t.sub);
            g.setFont (mono (12.0f));
            g.drawText ("NOTE " + typhon::noteName (ctx.proc.noteForLane (lane)), top, juce::Justification::centredRight);
            if (tab == 1)
            {
                g.setFont (font (12.0f));
                g.setColour (t.sub);
                g.drawFittedText (helpFor ((int) ctx.proc.apvts.getRawParameterValue (typhon::pid (lane, "dmode"))->load()),
                                  helpArea, juce::Justification::topLeft, 3);
                g.setColour (t.sub);
                g.setFont (font (11.0f, true));
                g.drawText ("TRANSIENT", transientLbl, juce::Justification::centredLeft);
                g.drawText ("DYNAMIQUE", dynLbl, juce::Justification::centredLeft);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 14);
            r.removeFromTop (34);
            auto tabs = r.removeFromTop (32);
            tabSample.setBounds (tabs.removeFromLeft (tabs.getWidth() / 2 - 3));
            tabs.removeFromLeft (6);
            tabDyn.setBounds (tabs);
            r.removeFromTop (10);
            if (tab == 0)
            {
                wave.setBounds (r.removeFromTop (88));
                r.removeFromTop (8);
                auto grid = r.removeFromTop (186);
                const int cw = grid.getWidth() / 4, ch = 93;
                for (int i = 0; i < 8; ++i)
                    sampleKnobs[i]->setBounds (grid.getX() + (i % 4) * cw, grid.getY() + (i / 4) * ch, cw, ch);
                r.removeFromTop (8);
                auto row = r.removeFromTop (30);
                const int bw = (row.getWidth() - 18) / 4;
                for (auto* b : { &chokeBtn, &revBtn, &gateBtn, &muteBtn }) { b->setBounds (row.removeFromLeft (bw)); row.removeFromLeft (6); }
                r.removeFromTop (8);
                auto row2 = r.removeFromTop (28);
                const int w3 = (row2.getWidth() - 12) / 3;
                groupBox.setBounds (row2.removeFromLeft (w3)); row2.removeFromLeft (6);
                outBox.setBounds (row2.removeFromLeft (w3)); row2.removeFromLeft (6);
                loadBtn.setBounds (row2);
            }
            else
            {
                transientLbl = r.removeFromTop (16);
                auto tr = r.removeFromTop (92);
                dynKnobs[0]->setBounds (tr.removeFromLeft (tr.getWidth() / 2));
                dynKnobs[1]->setBounds (tr);
                r.removeFromTop (6);
                dynLbl = r.removeFromTop (16);
                auto top = r.removeFromTop (100);
                curve.setBounds (top.removeFromLeft (100));
                top.removeFromLeft (10);
                const int mw = (top.getWidth() - 6) / 2, mh = (top.getHeight() - 6) / 2;
                for (int k = 0; k < 4; ++k)
                    modeBtns[k]->setBounds (top.getX() + (k % 2) * (mw + 6), top.getY() + (k / 2) * (mh + 6), mw, mh);
                r.removeFromTop (6);
                helpArea = r.removeFromTop (44);
                auto kn = r.removeFromTop (92);
                const int kw = kn.getWidth() / 5;
                for (int i = 2; i < 7; ++i) dynKnobs[i]->setBounds (kn.removeFromLeft (kw));
            }
        }

        int lane = 1;

    private:
        static juce::String helpFor (int mode)
        {
            switch (mode)
            {
                case 0: return juce::String::fromUTF8 ("Compression vers le bas : ce qui d\xc3\xa9passe le seuil est \xc3\xa9" "cras\xc3\xa9. Coup plus serr\xc3\xa9.");
                case 1: return juce::String::fromUTF8 ("Compression vers le haut : remonte ce qui est sous le seuil (queue, pi\xc3\xa8" "ce). Effet sample \xc3\xa9" "cras\xc3\xa9.");
                case 2: return juce::String::fromUTF8 ("Expansion vers le bas : enfonce ce qui est sous le seuil. Son sec et punchy, comme un gate doux.");
                default: return juce::String::fromUTF8 ("Expansion vers le haut : pousse ce qui d\xc3\xa9passe le seuil. Redonne de l'attaque.");
            }
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
                    safe->repaint();
                });
        }

        Ctx& ctx;
        int tab = 0;
        Waveform wave;
        DynCurve curve;
        Pill tabSample, tabDyn, chokeBtn, revBtn, gateBtn, muteBtn, loadBtn;
        juce::ComboBox groupBox, outBox;
        juce::OwnedArray<Knob> sampleKnobs, dynKnobs;
        juce::OwnedArray<Pill> modeBtns;
        juce::Rectangle<int> helpArea, transientLbl, dynLbl;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chokeAtt, revAtt, gateAtt, muteAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> groupAtt, outAtt;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
