#pragma once
// ============================================================================
//  Grille du séquenceur : 8 pistes x 16 pas
//  Clic = allumer / éteindre · clic + glisser vers la droite = lier
//  Clic droit sur un pas = options · clic droit sur une piste = menu de piste
//  Glisser un sample sur une piste = le charger
// ============================================================================
#include "UiWidgets.h"

namespace tui
{
    class SeqGrid : public juce::Component, public juce::FileDragAndDropTarget
    {
    public:
        SeqGrid (Ctx& c) : ctx (c) {}

        std::function<void (int)> onSelectLane;
        int selected = 1;

        static constexpr int headerH = 22, rowH = 44, rowGap = 5;
        static constexpr int nameW = 132, outW = 62, chokeW = 70, gap = 10;

        juce::Rectangle<int> rowRect (int lane) const { return { 0, headerH + lane * (rowH + rowGap), getWidth(), rowH }; }
        juce::Rectangle<int> nameRect (int lane) const { auto r = rowRect (lane).reduced (3, 2); return r.withWidth (nameW); }
        juce::Rectangle<int> chokeRect (int lane) const
        {
            auto r = rowRect (lane);
            return juce::Rectangle<int> (3 + nameW + gap + outW + gap, r.getCentreY() - 15, chokeW, 30);
        }
        int stepsX0() const { return 3 + nameW + gap + outW + gap + chokeW + gap; }
        float stepW() const { return (float) (getWidth() - 3 - stepsX0()) / (float) typhon::numSteps; }
        juce::Rectangle<float> stepRect (int lane, int j) const
        {
            const auto r = rowRect (lane);
            const float w = stepW();
            return { stepsX0() + j * w, (float) r.getY() + 2.0f, w - 4.0f, (float) rowH - 4.0f };
        }
        int stepAt (juce::Point<int> p) const
        {
            if (p.x < stepsX0()) return -1;
            return juce::jlimit (0, typhon::numSteps - 1, (int) ((p.x - stepsX0()) / stepW()));
        }
        int laneAt (juce::Point<int> p) const
        {
            for (int l = 0; l < typhon::numLanes; ++l) if (rowRect (l).contains (0, p.y)) return l;
            return -1;
        }

        void paint (juce::Graphics& g) override
        {
            using namespace typhon;
            const auto& t = ctx.th();
            auto& pat = ctx.proc.pattern;
            const int play = ctx.proc.playingStep();
            const bool multi = ctx.proc.settings.multiOut.load() == 1;

            // en-tête : numéros des pas
            g.setFont (mono (11.0f));
            g.setColour (t.sub);
            g.drawText (juce::String::fromUTF8 ("PISTE \xc2\xb7 NOTE"), 3, 0, nameW, headerH, juce::Justification::centredLeft);
            g.drawText ("SORTIE", 3 + nameW + gap, 0, outW, headerH, juce::Justification::centredLeft);
            g.drawText ("CHOKE", 3 + nameW + gap + outW + gap, 0, chokeW, headerH, juce::Justification::centredLeft);
            for (int j = 0; j < numSteps; ++j)
            {
                auto r = stepRect (0, j);
                g.setColour (j == play ? t.lcdTx : (j % 4 == 0 ? t.tx : t.sub.withAlpha (0.7f)));
                g.drawText (juce::String (j + 1), (int) r.getX(), 0, (int) r.getWidth(), headerH, juce::Justification::centred);
            }

            for (int lane = 0; lane < numLanes; ++lane)
            {
                auto row = rowRect (lane);
                if (lane == selected)
                {
                    g.setColour (t.acc.withAlpha (0.07f));
                    g.fillRoundedRectangle (row.toFloat(), 8.0f);
                    g.setColour (t.acc);
                    g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 8.0f, 1.0f);
                }
                // nom de piste, éclair quand elle joue
                auto nr = nameRect (lane).toFloat();
                const float flash = flashes[lane];
                g.setColour (t.panel2.interpolatedWith (t.acc, flash * 0.5f));
                g.fillRoundedRectangle (nr, 6.0f);
                if (dropLane == lane) { g.setColour (t.lcdTx); g.drawRoundedRectangle (nr, 6.0f, 2.0f); }
                const bool muted = ctx.proc.apvts.getRawParameterValue (pid (lane, "mute"))->load() > 0.5f;
                g.setColour (muted ? t.sub : t.tx);
                g.setFont (font (14.0f, true));
                g.drawText (muted ? laneName (lane) + " (M)" : laneName (lane), nr.reduced (10, 0).toNearestInt(), juce::Justification::centredLeft);
                g.setFont (mono (11.0f));
                g.setColour (t.sub);
                g.drawText (noteName (ctx.proc.noteForLane (lane)), nr.reduced (8, 0).toNearestInt(), juce::Justification::centredRight);

                // sortie
                const int out = (int) ctx.proc.apvts.getRawParameterValue (pid (lane, "out"))->load();
                g.drawText (multi ? "OUT " + juce::String (out * 2 + 1) + "/" + juce::String (out * 2 + 2) : juce::String ("MAIN"),
                            3 + nameW + gap, row.getY(), outW, rowH, juce::Justification::centredLeft);

                // choke de piste
                const bool ch = ctx.proc.apvts.getRawParameterValue (pid (lane, "choke"))->load() > 0.5f;
                auto cr = chokeRect (lane).toFloat();
                if (ch) { g.setColour (t.acc); g.fillRoundedRectangle (cr, 15.0f); g.setColour (t.bg); }
                else { g.setColour (t.knobEdge); g.drawRoundedRectangle (cr.reduced (0.5f), 15.0f, 1.0f); g.setColour (t.sub); }
                g.setFont (font (11.0f, true));
                g.drawText (ch ? "CHOKE" : "OFF", cr.toNearestInt(), juce::Justification::centred);

                // pas
                for (int j = 0; j < numSteps; ++j)
                {
                    auto r = stepRect (lane, j);
                    const Step s = pat.get (lane, j);
                    const int head = s.on() ? -1 : pat.headOf (lane, j);
                    if (head >= 0)
                    {
                        // continuation d'une liaison : barre qui rejoint le pas de tête
                        const Step hs = pat.get (lane, head);
                        const bool last = head + hs.len - 1 == j;
                        auto c = stepColour (t, hs).withMultipliedAlpha (0.8f);
                        auto rr = r.withX (r.getX() - 4.0f).withWidth (r.getWidth() + 4.0f);
                        g.setColour (c);
                        if (last) { g.fillRoundedRectangle (rr, 5.0f); g.fillRect (rr.withWidth (6.0f)); }
                        else g.fillRect (rr);
                        continue;
                    }
                    if (! s.on())
                    {
                        g.setColour (j % 4 == 0 ? t.offBeat : t.off);
                        g.fillRoundedRectangle (r, 5.0f);
                    }
                    else
                    {
                        const auto c = stepColour (t, s);
                        g.setColour (c);
                        if (s.split)
                        {
                            g.fillRoundedRectangle (r.withWidth (r.getWidth() * 0.45f), 4.0f);
                            g.fillRoundedRectangle (r.withTrimmedLeft (r.getWidth() * 0.55f), 4.0f);
                        }
                        else
                        {
                            g.fillRoundedRectangle (r, 5.0f);
                            if (s.len > 1) g.fillRect (r.withTrimmedLeft (r.getWidth() - 6.0f));
                            if (s.ratchet > 1)
                            {
                                g.setColour (t.bg.withAlpha (0.85f));
                                for (int k = 1; k < s.ratchet; ++k)
                                    g.fillRect (r.getX() + r.getWidth() * k / s.ratchet - 1.0f, r.getY() + 4.0f, 2.0f, r.getHeight() - 8.0f);
                            }
                        }
                        g.setColour (t.bg);
                        g.setFont (mono (11.0f, true));
                        if (s.ratchet > 1 && ! s.split) g.drawText (juce::String::fromUTF8 ("\xc3\x97") + juce::String (s.ratchet), r.toNearestInt().removeFromBottom (14), juce::Justification::centred);
                        if (s.choke != 0)
                        {
                            g.setColour (s.choke == 1 ? t.tx : t.lcdTx);
                            g.fillRoundedRectangle (r.withHeight (5.0f), 2.0f);
                        }
                    }
                }
                // tête de lecture
                if (play >= 0)
                {
                    auto r = stepRect (lane, play).expanded (1.0f);
                    g.setColour (t.lcdTx.withAlpha (0.9f));
                    g.drawRoundedRectangle (r, 5.0f, 1.5f);
                }
            }

            g.setColour (t.sub);
            g.setFont (font (12.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Clic : allumer / \xc3\xa9teindre \xc2\xb7 Clic + glisser \xe2\x86\x92 : lier (note tenue) \xc2\xb7 Clic droit : options du pas \xc2\xb7 "
                                                     "Clic sur une piste : \xc3\xa9" "couter \xc2\xb7 Glisse un WAV sur une piste pour le charger"),
                              0, headerH + numLanes * (rowH + rowGap) + 2, getWidth(), 18, juce::Justification::centredLeft, 1);
        }

        static juce::Colour stepColour (const typhon::Theme& t, const typhon::Step& s)
        {
            if (s.vel >= 120) return t.acc;
            const float a = 0.55f + 0.45f * (float) s.vel / 119.0f;
            return t.acc2.withMultipliedAlpha (a);
        }

        // animation
        void tick()
        {
            for (int l = 0; l < typhon::numLanes; ++l)
            {
                const auto c = ctx.proc.laneHitCounter (l);
                if (c != lastHits[l]) { lastHits[l] = c; flashes[l] = 1.0f; }
                else flashes[l] *= 0.8f;
            }
            repaint();
        }

        // --- souris ---------------------------------------------------------
        void mouseDown (const juce::MouseEvent& e) override
        {
            using namespace typhon;
            const auto p = e.getPosition();
            const int lane = laneAt (p);
            if (lane < 0) return;

            if (nameRect (lane).contains (p))
            {
                select (lane);
                if (e.mods.isPopupMenu()) laneMenu (lane);
                else ctx.proc.audition (lane);
                return;
            }
            if (chokeRect (lane).contains (p))
            {
                if (auto* prm = ctx.proc.apvts.getParameter (pid (lane, "choke")))
                    prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
                select (lane);
                return;
            }
            const int j = stepAt (p);
            if (j < 0) return;
            select (lane);
            auto& pat = ctx.proc.pattern;
            if (e.mods.isPopupMenu()) { stepMenu (lane, j); return; }

            Step s = pat.get (lane, j);
            if (! s.on() && pat.headOf (lane, j) >= 0) { dragLane = -1; return; }   // dans une liaison
            dragLane = lane; dragStep = j; dragMoved = false; dragCreated = false;
            if (! s.on()) { s = Step(); s.vel = 100; pat.set (lane, j, s); dragCreated = true; }
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            using namespace typhon;
            if (dragLane < 0) return;
            const int j = stepAt (e.getPosition());
            if (j < 0 || j <= dragStep) { if (j == dragStep && dragMoved) setLen (dragLane, dragStep, 1); return; }
            setLen (dragLane, dragStep, j - dragStep + 1);
            dragMoved = true;
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            using namespace typhon;
            if (dragLane >= 0 && ! dragMoved && ! dragCreated)
                ctx.proc.pattern.set (dragLane, dragStep, Step());
            dragLane = -1;
            repaint();
        }

        // --- glisser-déposer de samples --------------------------------------
        bool isInterestedInFileDrag (const juce::StringArray& files) override
        {
            for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3")) return true;
            return false;
        }
        void fileDragMove (const juce::StringArray&, int x, int y) override { dropLane = laneAt ({ x, y }); repaint(); }
        void fileDragExit (const juce::StringArray&) override { dropLane = -1; repaint(); }
        void filesDropped (const juce::StringArray& files, int x, int y) override
        {
            const int lane = laneAt ({ x, y });
            dropLane = -1;
            if (lane >= 0)
                for (auto& f : files)
                    if (ctx.proc.loadSampleFile (lane, juce::File (f))) { select (lane); ctx.proc.audition (lane); break; }
            repaint();
        }

    private:
        void select (int lane) { selected = lane; if (onSelectLane) onSelectLane (lane); repaint(); }

        void setLen (int lane, int head, int len)
        {
            using namespace typhon;
            auto& pat = ctx.proc.pattern;
            Step h = pat.get (lane, head);
            if (! h.on()) return;
            len = juce::jlimit (1, numSteps - head, len);
            for (int k = head + 1; k < head + len; ++k) pat.set (lane, k, Step());
            h.len = len;
            pat.set (lane, head, h);
        }

        void stepMenu (int lane, int j)
        {
            using namespace typhon;
            auto& pat = ctx.proc.pattern;
            Step s = pat.get (lane, j);
            const int head = s.on() ? j : pat.headOf (lane, j);
            const int at = head >= 0 ? head : j;
            s = pat.get (lane, at);

            juce::PopupMenu m, chokeM;
            m.addSectionHeader (laneName (lane) + juce::String::fromUTF8 (" \xc2\xb7 pas ") + juce::String (at + 1));
            m.addItem (1, "Normal", true, s.on() && s.vel >= 90 && s.vel < 120);
            m.addItem (2, "Accent", true, s.on() && s.vel >= 120);
            m.addItem (3, "Doux (ghost)", true, s.on() && s.vel < 90);
            m.addSeparator();
            m.addItem (4, "Diviser en 2", true, s.split);
            m.addItem (5, juce::String::fromUTF8 ("Ratchet \xc3\x97" "2"), true, ! s.split && s.ratchet == 2);
            m.addItem (6, juce::String::fromUTF8 ("Ratchet \xc3\x97" "3"), true, ! s.split && s.ratchet == 3);
            m.addItem (7, juce::String::fromUTF8 ("Ratchet \xc3\x97" "4"), true, ! s.split && s.ratchet == 4);
            m.addItem (8, juce::String::fromUTF8 ("D\xc3\xa9lier"), s.len > 1);
            chokeM.addItem (20, "Suit la piste", true, s.choke == 0);
            chokeM.addItem (21, "Toujours (coupe la note d'avant)", true, s.choke == 1);
            chokeM.addItem (22, "Laisse sonner", true, s.choke == 2);
            m.addSubMenu ("Choke de ce pas", chokeM);
            m.addSeparator();
            m.addItem (9, "Effacer", s.on());

            juce::Component::SafePointer<SeqGrid> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                [safe, lane, at] (int r)
                {
                    if (safe == nullptr || r == 0) return;
                    auto& pat = safe->ctx.proc.pattern;
                    Step x = pat.get (lane, at);
                    auto turnOn = [&] { if (! x.on()) x.vel = 100; };
                    switch (r)
                    {
                        case 1: turnOn(); x.vel = 100; break;
                        case 2: turnOn(); x.vel = 127; break;
                        case 3: turnOn(); x.vel = 55; break;
                        case 4: turnOn(); x.split = ! x.split; x.ratchet = 1; break;
                        case 5: case 6: case 7: { turnOn(); const int n = r - 3; x.ratchet = x.ratchet == n ? 1 : n; x.split = false; } break;
                        case 8: x.len = 1; break;
                        case 9: x = Step(); break;
                        case 20: case 21: case 22: turnOn(); x.choke = r - 20; break;
                        default: break;
                    }
                    pat.set (lane, at, x);
                    safe->repaint();
                });
        }

        void laneMenu (int lane)
        {
            using namespace typhon;
            auto& ap = ctx.proc.apvts;
            const bool muted = ap.getRawParameterValue (pid (lane, "mute"))->load() > 0.5f;
            const bool gate = ap.getRawParameterValue (pid (lane, "gate"))->load() > 0.5f;
            juce::PopupMenu m;
            m.addSectionHeader (laneName (lane) + juce::String::fromUTF8 (" \xc2\xb7 ") + ctx.proc.laneSource (lane));
            m.addItem (1, juce::String::fromUTF8 ("\xc3\x89" "couter"));
            m.addItem (2, "Charger un sample...");
            m.addItem (3, "Revenir au son de secours");
            m.addSeparator();
            m.addItem (4, "Mute", true, muted);
            m.addItem (5, juce::String::fromUTF8 ("Gate MIDI (la dur\xc3\xa9" "e de la note coupe le son)"), true, gate);
            m.addSeparator();
            m.addItem (6, "Effacer la piste");
            m.addItem (7, juce::String::fromUTF8 ("Copier le pattern de cette piste"));
            m.addItem (8, "Coller", clip.size() == (size_t) numSteps);

            juce::Component::SafePointer<SeqGrid> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                [safe, lane] (int r)
                {
                    if (safe == nullptr || r == 0) return;
                    auto& P = safe->ctx.proc;
                    auto toggle = [&] (const char* w) { if (auto* prm = P.apvts.getParameter (pid (lane, w))) prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f); };
                    switch (r)
                    {
                        case 1: P.audition (lane); break;
                        case 2:
                        {
                            safe->chooser = std::make_unique<juce::FileChooser> ("Charger un sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
                            safe->chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                [safe, lane] (const juce::FileChooser& fc)
                                {
                                    if (safe == nullptr) return;
                                    auto f = fc.getResult();
                                    if (f.existsAsFile() && safe->ctx.proc.loadSampleFile (lane, f)) safe->ctx.proc.audition (lane);
                                    safe->repaint();
                                });
                        } break;
                        case 3: P.resetLaneToFallback (lane); break;
                        case 4: toggle ("mute"); break;
                        case 5: toggle ("gate"); break;
                        case 6: for (int j = 0; j < numSteps; ++j) P.pattern.set (lane, j, Step()); break;
                        case 7: safe->clip.clear(); for (int j = 0; j < numSteps; ++j) safe->clip.push_back (P.pattern.steps[lane][j].load()); break;
                        case 8: for (int j = 0; j < numSteps && j < (int) safe->clip.size(); ++j) P.pattern.steps[lane][j].store (safe->clip[(size_t) j]); break;
                        default: break;
                    }
                    safe->repaint();
                });
        }

        Ctx& ctx;
        int dragLane = -1, dragStep = -1, dropLane = -1;
        bool dragMoved = false, dragCreated = false;
        float flashes[typhon::numLanes] {};
        uint32_t lastHits[typhon::numLanes] {};
        std::vector<uint32_t> clip;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
