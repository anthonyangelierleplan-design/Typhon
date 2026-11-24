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
        SeqGrid (Ctx& c) : ctx (c), len16 (c, "16 PAS"), len32 (c, "32 PAS"), page1 (c, "1-16"), page2 (c, "17-32"), followBtn (c, "SUIVRE", true)
        {
            for (auto* b : { &len16, &len32, &page1, &page2, &followBtn }) { addAndMakeVisible (*b); b->textSize = 11.0f; }
            len16.isOn = [this] { return ctx.proc.seqLength() == 16; };
            len32.isOn = [this] { return ctx.proc.seqLength() == 32; };
            auto setLen = [this] (int v) { if (auto* p = ctx.proc.apvts.getParameter ("seq_len")) p->setValueNotifyingHost (v == 32 ? 1.0f : 0.0f); if (v == 16) page = 0; repaint(); };
            len16.onClick = [setLen] { setLen (16); };
            len32.onClick = [setLen] { setLen (32); };
            page1.isOn = [this] { return page == 0; };
            page2.isOn = [this] { return page == 1; };
            page1.onClick = [this] { page = 0; follow = false; repaint(); };
            page2.onClick = [this] { if (ctx.proc.seqLength() == 32) { page = 1; follow = false; repaint(); } };
            followBtn.isOn = [this] { return follow; };
            followBtn.onClick = [this] { follow = ! follow; repaint(); };
        }

        std::function<void (int)> onSelectLane;
        std::function<void (int, int)> onLockStep;   // (piste, pas) : passer le panneau en mode verrou
        int selected = 1;
        int lockLane = -1, lockStep = -1;
        int page = 0;
        bool follow = true;

        static constexpr int topH = 36, rowH = 44, rowGap = 5;
        static constexpr int headerH = topH + 22;
        int pageOffset() const { return ctx.proc.seqLength() == 32 ? page * 16 : 0; }

        void resized() override
        {
            auto r = juce::Rectangle<int> (3, 0, getWidth() - 6, topH - 8);
            len16.setBounds (r.removeFromLeft (70)); r.removeFromLeft (6);
            len32.setBounds (r.removeFromLeft (70)); r.removeFromLeft (20);
            page1.setBounds (r.removeFromLeft (70)); r.removeFromLeft (6);
            page2.setBounds (r.removeFromLeft (70)); r.removeFromLeft (6);
            followBtn.setBounds (r.removeFromLeft (80));
        }
        static constexpr int nameW = 132, outW = 62, chokeW = 70, gap = 10;

        juce::Rectangle<int> rowRect (int lane) const { return { 0, headerH + lane * (rowH + rowGap), getWidth(), rowH }; }
        juce::Rectangle<int> nameRect (int lane) const { auto r = rowRect (lane).reduced (3, 2); return r.withWidth (nameW); }
        juce::Rectangle<int> chokeRect (int lane) const
        {
            auto r = rowRect (lane);
            return juce::Rectangle<int> (3 + nameW + gap + outW + gap, r.getCentreY() - 15, chokeW, 30);
        }
        int stepsX0() const { return 3 + nameW + gap + outW + gap + chokeW + gap; }
        float stepW() const { return (float) (getWidth() - 3 - stepsX0()) / 16.0f; }
        // j = indice réel du pas (0..31) ; dessiné dans la colonne j - pageOffset()
        juce::Rectangle<float> stepRect (int lane, int j) const
        {
            const auto r = rowRect (lane);
            const float w = stepW();
            return { stepsX0() + (j - pageOffset()) * w, (float) r.getY() + 2.0f, w - 4.0f, (float) rowH - 4.0f };
        }
        int stepAt (juce::Point<int> p) const
        {
            if (p.x < stepsX0()) return -1;
            return pageOffset() + juce::jlimit (0, 15, (int) ((p.x - stepsX0()) / stepW()));
        }
        int laneAt (juce::Point<int> p) const
        {
            for (int l = 0; l < typhon::numLanes; ++l) if (rowRect (l).contains (0, p.y)) return l;
            return -1;
        }

        void paint (juce::Graphics& g) override
        {
            using namespace typhon;
            diag::ui ("dessin de la grille");
            const auto& t = ctx.th();
            auto& pat = ctx.proc.pattern;
            const int play = ctx.proc.playingStep();
            const bool multi = ctx.proc.settings.multiOut.load() == 1;

            // en-tête : numéros des pas
            g.setFont (mono (11.0f));
            g.setColour (t.sub);
            const int off = pageOffset();
            const int hy = topH, hh = headerH - topH;
            g.drawText (juce::String::fromUTF8 ("PISTE \xc2\xb7 NOTE"), 3, hy, nameW, hh, juce::Justification::centredLeft);
            g.drawText ("SORTIE", 3 + nameW + gap, hy, outW, hh, juce::Justification::centredLeft);
            g.drawText ("CHOKE", 3 + nameW + gap + outW + gap, hy, chokeW, hh, juce::Justification::centredLeft);
            for (int j = off; j < off + 16; ++j)
            {
                auto r = stepRect (0, j);
                if (j % 4 == 0)
                {
                    // temps forts (1, 5, 9, 13) : bande verticale sur toute la grille + numéro sur pastille
                    auto band = juce::Rectangle<float> (r.getX() - 2.0f, (float) hy, r.getWidth() + 4.0f,
                                                        stepRect (typhon::numLanes - 1, j).getBottom() + 2.0f - (float) hy);
                    g.setColour (t.tx.withAlpha (0.06f)); g.fillRoundedRectangle (band, 6.0f);
                    g.setColour (t.tx.withAlpha (0.14f)); g.fillRoundedRectangle (r.withY ((float) hy + 3.0f).withHeight ((float) hh - 6.0f), 5.0f);
                }
                g.setColour (j == play ? t.lcdTx : (j % 4 == 0 ? t.tx : t.sub.withAlpha (0.7f)));
                g.drawText (juce::String (j + 1), (int) r.getX(), hy, (int) r.getWidth(), hh, juce::Justification::centred);
            }
            g.setColour (t.sub); g.setFont (font (11.0f));
            g.drawText (ctx.proc.seqLength() == 32 ? juce::String::fromUTF8 ("Page affich\xc3\xa9" "e : ") + (page == 0 ? "1-16" : "17-32") : juce::String(),
                        followBtn.getRight() + 16, 0, 220, topH - 8, juce::Justification::centredLeft);

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
                // nom du sample chargé (glisser-déposer, bibliothèque, kit) ; le rôle de la piste passe en petit au-dessus
                const auto src = ctx.proc.laneSource (lane);
                const bool custom = src.isNotEmpty() && src != "Secours";
                auto tr = nr.reduced (10, 0).toNearestInt();
                tr.removeFromRight (34);
                if (custom)
                {
                    auto top = tr.removeFromTop (tr.getHeight() / 2 - 1);
                    g.setFont (font (10.0f, true)); g.setColour (t.sub);
                    g.drawText (laneName (lane).toUpperCase() + (muted ? " (M)" : ""), top.withTrimmedBottom (-2), juce::Justification::bottomLeft);
                    g.setFont (font (13.5f, true)); g.setColour (muted ? t.sub : t.tx);
                    g.drawFittedText (src, tr.withTrimmedTop (1), juce::Justification::topLeft, 1, 0.8f);
                }
                else
                {
                    g.setColour (muted ? t.sub : t.tx);
                    g.setFont (font (14.0f, true));
                    g.drawText (muted ? laneName (lane) + " (M)" : laneName (lane), tr, juce::Justification::centredLeft);
                }
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
                for (int j = off; j < off + 16; ++j)
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
                        g.setColour (j % 4 == 0 ? t.offBeat.interpolatedWith (t.tx, 0.10f) : t.off);
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
                        if (s.rev)
                        {
                            juce::Path tri; const float bx = r.getX() + 5.0f, by = r.getBottom() - 6.0f;
                            tri.addTriangle (bx, by, bx + 6.0f, by - 4.0f, bx + 6.0f, by + 4.0f);
                            g.setColour (t.bg); g.fillPath (tri);
                        }
                        if (s.var > 0)
                        {
                            g.setColour (t.bg); g.setFont (mono (9.0f, true));
                            g.drawText ("v" + juce::String (s.var), r.toNearestInt().reduced (2, 3), juce::Justification::centredBottom);
                        }
                        if (s.dur != 0)
                        {
                            g.setColour (s.dur == 1 ? t.bg : t.lcdTx);
                            if (s.dur == 1) g.fillRect (r.getRight() - 4.0f, r.getY() + 5.0f, 2.0f, r.getHeight() - 10.0f);   // coupé à la fin du pas
                            else { g.setFont (mono (10.0f, true)); g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), r.toNearestInt().reduced (3, 4), juce::Justification::topRight); }
                        }
                        if (s.pitch != 0)
                        {
                            g.setColour (t.bg); g.setFont (mono (10.0f, true));
                            g.drawText ((s.pitch > 0 ? "+" : "") + juce::String (s.pitch), r.toNearestInt().reduced (3, 4), juce::Justification::topLeft);
                        }
                    }
                    if (pat.hasLocks (lane, j))
                    {
                        g.setColour (valueColour (t));
                        g.fillEllipse (r.getRight() - 9.0f, r.getBottom() - 9.0f, 6.0f, 6.0f);
                    }
                    if (lane == lockLane && j == lockStep)
                    {
                        g.setColour (t.lcdTx);
                        g.drawRoundedRectangle (r.expanded (1.5f), 5.0f, 2.5f);
                    }
                }
                // tête de lecture
                if (play >= off && play < off + 16)
                {
                    auto r = stepRect (lane, play).expanded (1.0f);
                    g.setColour (t.lcdTx.withAlpha (0.9f));
                    g.drawRoundedRectangle (r, 5.0f, 1.5f);
                }
            }

            // zone sélectionnée
            if (hasSel)
            {
                const int a = std::max (selS0, off), b = std::min (selS1, off + 15);
                if (a <= b)
                {
                    auto r0 = stepRect (selL0, a), r1 = stepRect (selL1, b);
                    auto box = r0.getUnion (r1).expanded (3.0f);
                    g.setColour (t.lcdTx.withAlpha (0.12f)); g.fillRoundedRectangle (box, 6.0f);
                    g.setColour (t.lcdTx); g.drawRoundedRectangle (box, 6.0f, 2.0f);
                }
            }

            g.setColour (t.sub);
            g.setFont (font (12.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Maj + glisser : s\xc3\xa9lectionner \xc2\xb7 Ctrl+D : dupliquer \xc2\xb7 Ctrl+C / V : copier / coller \xc2\xb7 Suppr : effacer \xc2\xb7 "
                                                     "Ctrl+Z : annuler \xc2\xb7 ") + juce::String::fromUTF8 ("Clic : allumer / \xc3\xa9teindre \xc2\xb7 Clic + glisser \xe2\x86\x92 : lier (note tenue) \xc2\xb7 Clic droit : options du pas \xc2\xb7 "
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
            if (ctx.proc.seqLength() == 16) page = 0;
            else if (follow && ctx.proc.playingStep() >= 0) page = ctx.proc.playingStep() / 16;
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
            if (e.mods.isPopupMenu())
            {
                if (inSelection (lane, j)) selectionMenu (lane, j);
                else stepMenu (lane, j);
                return;
            }
            // Maj + glisser : sélection d'une zone
            if (e.mods.isShiftDown())
            {
                hasSel = true; selAnchorLane = lane; selAnchorStep = j;
                setSel (lane, j, lane, j);
                selecting = true; dragLane = -1;
                return;
            }
            if (hasSel) { hasSel = false; repaint(); }

            Step s = pat.get (lane, j);
            if (! s.on() && pat.headOf (lane, j) >= 0) { dragLane = -1; return; }   // dans une liaison
            dragLane = lane; dragStep = j; dragMoved = false; dragCreated = false;
            if (! s.on()) { s = Step(); s.vel = 100; pat.set (lane, j, s); dragCreated = true; }
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            using namespace typhon;
            if (selecting)
            {
                const int l = juce::jlimit (0, numLanes - 1, laneAt (e.getPosition()) < 0 ? (e.y < headerH ? 0 : numLanes - 1) : laneAt (e.getPosition()));
                const int j = stepAt ({ std::max (e.x, stepsX0()), e.y });
                if (j >= 0) setSel (selAnchorLane, selAnchorStep, l, j);
                return;
            }
            if (dragLane < 0) return;
            const int j = stepAt (e.getPosition());
            if (j < 0 || j <= dragStep) { if (j == dragStep && dragMoved) setLen (dragLane, dragStep, 1); return; }
            setLen (dragLane, dragStep, j - dragStep + 1);
            dragMoved = true;
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (selecting) { selecting = false; return; }
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

        // --- sélection, copier / coller / dupliquer ---------------------------------
        bool hasSelection() const { return hasSel; }
        void clearSelectionMark() { hasSel = false; repaint(); }

        void copySelection()
        {
            if (! hasSel) return;
            auto& pat = ctx.proc.pattern;
            clipBlock.clear();
            clipH = selL1 - selL0 + 1; clipW = selS1 - selS0 + 1;
            for (int l = selL0; l <= selL1; ++l)
                for (int st = selS0; st <= selS1; ++st)
                {
                    Cell c; c.step = pat.steps[l][st].load();
                    for (int k = 0; k < typhon::numLocks; ++k) c.locks[k] = pat.lock (l, st, k);
                    clipBlock.push_back (c);
                }
        }
        void pasteAt (int lane, int step)
        {
            if (clipBlock.empty()) return;
            auto& pat = ctx.proc.pattern;
            const int len = ctx.proc.seqLength();
            for (int dl = 0; dl < clipH; ++dl)
                for (int ds = 0; ds < clipW; ++ds)
                {
                    const int l = lane + dl, st = step + ds;
                    if (l >= typhon::numLanes || st >= len) continue;
                    const auto& c = clipBlock[(size_t) (dl * clipW + ds)];
                    pat.steps[l][st].store (c.step);
                    for (int k = 0; k < typhon::numLocks; ++k) pat.setLock (l, st, k, c.locks[k]);
                }
            hasSel = true;
            setSel (lane, step, std::min (typhon::numLanes - 1, lane + clipH - 1), std::min (len - 1, step + clipW - 1));
        }
        void pasteAtSelection() { if (hasSel) pasteAt (selL0, selS0); }
        // Dupliquer : copie la zone juste après elle-même (passe en 32 pas si besoin), la sélection suit la copie
        void duplicateSelection()
        {
            if (! hasSel) return;
            const int w = selS1 - selS0 + 1;
            const int dest = selS1 + 1;
            if (ctx.proc.seqLength() == 16 && dest + w <= 32 && dest + w > 16)
                if (auto* p = ctx.proc.apvts.getParameter ("seq_len")) p->setValueNotifyingHost (1.0f);
            if (dest >= ctx.proc.seqLength()) return;
            copySelection();
            pasteAt (selL0, dest);
            if (ctx.proc.seqLength() == 32) { page = selS0 / 16; follow = false; }
            repaint();
        }
        void clearSelectionSteps()
        {
            if (! hasSel) return;
            auto& pat = ctx.proc.pattern;
            for (int l = selL0; l <= selL1; ++l)
                for (int st = selS0; st <= selS1; ++st) { pat.set (l, st, typhon::Step()); pat.clearLocks (l, st); }
            repaint();
        }

    private:
        struct Cell { uint32_t step = 0; float locks[typhon::numLocks] {}; };
        std::vector<Cell> clipBlock;
        int clipW = 0, clipH = 0;
        bool hasSel = false, selecting = false;
        int selL0 = 0, selL1 = 0, selS0 = 0, selS1 = 0, selAnchorLane = 0, selAnchorStep = 0;

        void setSel (int l0, int s0, int l1, int s1)
        {
            selL0 = std::min (l0, l1); selL1 = std::max (l0, l1);
            selS0 = std::min (s0, s1); selS1 = std::max (s0, s1);
            repaint();
        }
        bool inSelection (int lane, int step) const { return hasSel && lane >= selL0 && lane <= selL1 && step >= selS0 && step <= selS1; }

        void selectionMenu (int lane, int step)
        {
            juce::ignoreUnused (lane, step);
            juce::PopupMenu m;
            m.addSectionHeader (juce::String::fromUTF8 ("S\xc3\xa9lection : ") + juce::String (selL1 - selL0 + 1) + " piste(s) x " + juce::String (selS1 - selS0 + 1) + " pas");
            m.addItem (1, "Dupliquer (Ctrl+D)");
            m.addItem (2, "Copier (Ctrl+C)");
            m.addItem (3, "Coller ici (Ctrl+V)", ! clipBlock.empty());
            m.addSeparator();
            m.addItem (4, "Effacer (Suppr)");
            m.addItem (5, juce::String::fromUTF8 ("D\xc3\xa9s\xc3\xa9lectionner (Echap)"));
            juce::Component::SafePointer<SeqGrid> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe] (int r)
            {
                if (safe == nullptr) return;
                switch (r)
                {
                    case 1: safe->duplicateSelection(); break;
                    case 2: safe->copySelection(); break;
                    case 3: safe->pasteAtSelection(); break;
                    case 4: safe->clearSelectionSteps(); break;
                    case 5: safe->hasSel = false; break;
                    default: break;
                }
                safe->repaint();
            });
        }

        void select (int lane) { selected = lane; if (onSelectLane) onSelectLane (lane); repaint(); }

        void setLen (int lane, int head, int len)
        {
            using namespace typhon;
            auto& pat = ctx.proc.pattern;
            Step h = pat.get (lane, head);
            if (! h.on()) return;
            len = juce::jlimit (1, ctx.proc.seqLength() - head, len);
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
            const auto times = juce::String::fromUTF8 ("\xc3\x97");

            // Menu court : vélocités en haut, le reste rangé par thème
            juce::PopupMenu m, rythm, pitchM, chokeM, lockM;
            m.addSectionHeader (laneName (lane) + juce::String::fromUTF8 (" \xc2\xb7 pas ") + juce::String (at + 1));
            m.addItem (1, "Normal", true, s.on() && s.vel >= 90 && s.vel < 120);
            m.addItem (2, "Accent", true, s.on() && s.vel >= 120);
            m.addItem (3, "Doux (ghost)", true, s.on() && s.vel < 90);
            m.addSeparator();

            rythm.addItem (4, "Diviser en 2", true, s.split);
            rythm.addItem (5, "Ratchet " + times + "2", true, ! s.split && s.ratchet == 2);
            rythm.addItem (6, "Ratchet " + times + "3", true, ! s.split && s.ratchet == 3);
            rythm.addItem (7, "Ratchet " + times + "4", true, ! s.split && s.ratchet == 4);
            rythm.addSeparator();
            rythm.addItem (8, juce::String::fromUTF8 ("D\xc3\xa9lier"), s.len > 1);
            juce::String rTag = s.split ? " (1/2)" : (s.ratchet > 1 ? " (" + times + juce::String (s.ratchet) + ")" : juce::String());
            m.addSubMenu ("Rythme" + rTag, rythm);

            for (int p = 12; p >= -12; --p)
                pitchM.addItem (200 + p + 12, (p > 0 ? "+" : "") + juce::String (p) + (p == 0 ? " (normal)" : " st"), true, s.pitch == p);
            m.addSubMenu ("Pitch" + (s.pitch != 0 ? "  (" + juce::String (s.pitch > 0 ? "+" : "") + juce::String (s.pitch) + ")" : juce::String()), pitchM);

            chokeM.addItem (20, "Suit la piste", true, s.choke == 0);
            chokeM.addItem (21, "Toujours (coupe la note d'avant)", true, s.choke == 1);
            chokeM.addItem (22, "Laisse sonner", true, s.choke == 2);
            m.addSubMenu ("Choke" + juce::String (s.choke == 1 ? " (toujours)" : (s.choke == 2 ? " (laisse sonner)" : "")), chokeM);

            juce::PopupMenu durM;
            durM.addItem (40, "Suit la piste", true, s.dur == 0);
            durM.addItem (41, juce::String::fromUTF8 ("Limit\xc3\xa9" "e au pas"), true, s.dur == 1);
            durM.addItem (42, juce::String::fromUTF8 ("Libre (jou\xc3\xa9" "e comme r\xc3\xa9gl\xc3\xa9" "e)"), true, s.dur == 2);
            m.addSubMenu (juce::String::fromUTF8 ("Dur\xc3\xa9" "e") + juce::String (s.dur == 1 ? juce::String::fromUTF8 (" (limit\xc3\xa9" "e)") : (s.dur == 2 ? juce::String (" (libre)") : juce::String())), durM);
            const int nVar = ctx.proc.laneVariants (lane);
            if (nVar > 1)
            {
                juce::PopupMenu varM;
                varM.addItem (300, "Suit la piste", true, s.var == 0);
                for (int v = 1; v <= std::min (15, nVar); ++v) varM.addItem (300 + v, "Variante " + juce::String (v), true, s.var == v);
                m.addSubMenu ("Variante" + juce::String (s.var > 0 ? " (" + juce::String (s.var) + ")" : juce::String()), varM);
            }
            m.addItem (10, juce::String::fromUTF8 ("Jouer \xc3\xa0 l'envers (reverse)"), true, s.rev);

            lockM.addItem (30, juce::String::fromUTF8 ("R\xc3\xa9gler ce pas..."));
            lockM.addItem (31, "Effacer les verrous", pat.hasLocks (lane, at));
            m.addSubMenu ("Verrous" + juce::String (pat.hasLocks (lane, at) ? juce::String::fromUTF8 (" \xe2\x97\x8f") : juce::String()), lockM);
            m.addSeparator();
            m.addItem (9, "Effacer le pas", s.on() || pat.hasLocks (lane, at));
            if (! clipBlock.empty()) m.addItem (50, juce::String::fromUTF8 ("Coller ici (Ctrl+V)"));

            juce::Component::SafePointer<SeqGrid> safe (this);
            diag::ui ("menu du pas : ouverture");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                [safe, lane, at] (int r)
                {
                    diag::ui ("menu du pas : choix recu");
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
                        case 9: x = Step(); pat.clearLocks (lane, at); break;
                        case 10: turnOn(); x.rev = ! x.rev; break;
                        case 20: case 21: case 22: turnOn(); x.choke = r - 20; break;
                        case 30: turnOn(); pat.set (lane, at, x); if (safe->onLockStep) safe->onLockStep (lane, at); safe->repaint(); return;
                        case 40: case 41: case 42: turnOn(); x.dur = r - 40; break;
                        case 50: safe->pasteAt (lane, at); return;
                        case 31: pat.clearLocks (lane, at); break;
                        default:
                            if (r >= 200 && r <= 224) { turnOn(); x.pitch = r - 212; }
                            if (r >= 300 && r <= 315) { turnOn(); x.var = r - 300; }
                            break;
                    }
                    pat.set (lane, at, x);
                    safe->repaint();
                    diag::ui ("menu du pas : applique");
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
            m.addItem (9, juce::String::fromUTF8 ("Reverse (toute la piste)"), true, ap.getRawParameterValue (pid (lane, "rev"))->load() > 0.5f);
            m.addItem (10, juce::String::fromUTF8 ("Dur\xc3\xa9" "e limit\xc3\xa9" "e au pas"), true, ap.getRawParameterValue (pid (lane, "steplen"))->load() > 0.5f);
            m.addItem (11, juce::String::fromUTF8 ("Dur\xc3\xa9" "e limit\xc3\xa9" "e au pas : toutes les pistes"));
            m.addItem (12, juce::String::fromUTF8 ("Dur\xc3\xa9" "e libre : toutes les pistes"));
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
                        case 9: toggle ("rev"); break;
                        case 10: toggle ("steplen"); break;
                        case 11: case 12:
                            for (int o = 0; o < numLanes; ++o)
                                if (auto* prm = P.apvts.getParameter (pid (o, "steplen"))) prm->setValueNotifyingHost (r == 11 ? 1.0f : 0.0f);
                            break;
                        case 6: for (int j = 0; j < numSteps; ++j) { P.pattern.set (lane, j, Step()); P.pattern.clearLocks (lane, j); } break;
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
        Pill len16, len32, page1, page2, followBtn;
        std::unique_ptr<juce::FileChooser> chooser;
    };
}
