#pragma once
// ============================================================================
//  Page CAPTURE : EZdrummer -> kit Typhon, un kit ou par lot, bibliothèque
// ============================================================================
#include "UiWidgets.h"
#include "Capture.h"

namespace tui
{
    // Liste simple peinte à la main (file d'attente ou bibliothèque)
    class SimpleList : public juce::Component
    {
    public:
        SimpleList (Ctx& c) : ctx (c) {}
        struct Row { juce::String name, status; juce::Colour statusColour; bool highlight = false; };
        std::vector<Row> rows;
        std::function<void (int)> onClick, onRemove;
        juce::String emptyText;
        static constexpr int rowH = 38;

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            if (rows.empty())
            {
                g.setColour (t.sub); g.setFont (font (12.0f));
                g.drawFittedText (emptyText, getLocalBounds().reduced (6), juce::Justification::topLeft, 3);
                return;
            }
            for (int i = 0; i < (int) rows.size() && (i + 1) * (rowH + 6) - 6 <= getHeight() + rowH; ++i)
            {
                auto r = juce::Rectangle<int> (0, i * (rowH + 6), getWidth(), rowH);
                if (r.getBottom() > getHeight()) break;
                g.setColour (t.bg); g.fillRoundedRectangle (r.toFloat(), 7.0f);
                if (rows[(size_t) i].highlight) { g.setColour (t.acc); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, 1.0f); }
                g.setColour (t.tx); g.setFont (font (13.0f, true));
                g.drawText (rows[(size_t) i].name, r.reduced (12, 0).withTrimmedRight (onRemove ? 100 : 80), juce::Justification::centredLeft);
                g.setColour (rows[(size_t) i].statusColour); g.setFont (mono (11.0f));
                g.drawText (rows[(size_t) i].status, r.reduced (onRemove ? 30 : 12, 0), juce::Justification::centredRight);
                if (onRemove) { g.setColour (t.sub); g.drawText (juce::String::fromUTF8 ("\xc3\x97"), r.reduced (10, 0), juce::Justification::centredRight); }
            }
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            const int i = e.y / (rowH + 6);
            if (i < 0 || i >= (int) rows.size()) return;
            if (onRemove && e.x > getWidth() - 28) { onRemove (i); return; }
            if (onClick) onClick (i);
        }
    private:
        Ctx& ctx;
    };

    class CapturePage : public juce::Component
    {
    public:
        CapturePage (Ctx& c) : ctx (c), eng (c.proc.capture()),
            loadBtn (c, "CHARGER EZDRUMMER"), openBtn (c, "OUVRIR EZDRUMMER"),
            captureBtn (c, "CAPTURER LE KIT"), stopBtn (c, juce::String::fromUTF8 ("ARR\xc3\x8aTER")),
            addBtn (c, juce::String::fromUTF8 ("+ AJOUTER L'\xc3\x89TAT ACTUEL \xc3\x80 LA FILE")), batchBtn (c, "TOUT CAPTURER"),
            clearBtn (c, "RETIRER LES KITS FAITS"), folderBtn (c, "OUVRIR LE DOSSIER"), queueList (c), libList (c)
        {
            for (auto* b : { &loadBtn, &openBtn, &captureBtn, &stopBtn, &addBtn, &batchBtn, &clearBtn, &folderBtn }) { addAndMakeVisible (*b); b->textSize = 12.0f; }
            captureBtn.strong = true; batchBtn.strong = true;
            captureBtn.textSize = 15.0f;
            loadBtn.onClick = [this] { juce::String err; if (eng.load (err)) eng.showEditor(); refresh(); };
            openBtn.onClick = [this] { eng.showEditor(); };
            captureBtn.onClick = [this] { eng.captureNow (nameEd.getText()); };
            stopBtn.onClick = [this] { eng.stop(); };
            addBtn.onClick = [this] { eng.addCurrentToQueue (nameEd.getText()); refresh(); };
            batchBtn.onClick = [this] { eng.startBatch(); };
            clearBtn.onClick = [this] { eng.clearDone(); refresh(); };
            folderBtn.onClick = [] { auto f = TyphonProcessor::libraryFolder(); f.createDirectory(); f.startAsProcess(); };

            nameEd.setText ("Mon kit 01 (sec)", false);
            nameEd.setFont (font (14.0f));
            nameEd.setIndents (10, 8);
            addAndMakeVisible (nameEd);

            const int layers[4] = { 1, 2, 4, 8 };
            for (int k = 0; k < 4; ++k)
            {
                auto* b = layerBtns.add (new Pill (c, juce::String (layers[k])));
                b->isOn = [this, v = layers[k]] { return eng.velLayers.load() == v; };
                b->onClick = [this, v = layers[k]] { eng.velLayers.store (v); repaint(); };
                addAndMakeVisible (b);
            }
            for (int k = 1; k <= 4; ++k)
            {
                auto* b = rrBtns.add (new Pill (c, juce::String (k)));
                b->isOn = [this, k] { return eng.roundRobin.load() == k; };
                b->onClick = [this, k] { eng.roundRobin.store (k); repaint(); };
                addAndMakeVisible (b);
            }
            for (auto* s : { &lenSlider, &waitSlider })
            {
                s->setSliderStyle (juce::Slider::LinearBar);
                s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 20);
                s->setColour (juce::Slider::trackColourId, ctx.th().acc.withAlpha (0.6f));
                addAndMakeVisible (*s);
            }
            lenSlider.setRange (0.5, 4.0, 0.1); lenSlider.setValue (eng.maxSeconds.load(), juce::dontSendNotification);
            lenSlider.setTextValueSuffix (" s");
            lenSlider.onValueChange = [this] { eng.maxSeconds.store ((float) lenSlider.getValue()); };
            waitSlider.setRange (3.0, 40.0, 1.0); waitSlider.setValue (eng.loadWaitSeconds.load(), juce::dontSendNotification);
            waitSlider.setTextValueSuffix (" s");
            waitSlider.onValueChange = [this] { eng.loadWaitSeconds.store ((float) waitSlider.getValue()); };

            for (int l = 0; l < typhon::numLanes; ++l)
            {
                auto* tb = laneOn.add (new Pill (c, typhon::laneName (l)));
                tb->isOn = [this, l] { return eng.laneEnabled[l].load(); };
                tb->onClick = [this, l] { eng.laneEnabled[l].store (! eng.laneEnabled[l].load()); repaint(); };
                tb->textSize = 13.0f;
                addAndMakeVisible (tb);
                auto* cb = noteBoxes.add (new juce::ComboBox());
                for (int n = 24; n <= 96; ++n) cb->addItem (typhon::noteName (n) + "  (" + juce::String (n) + ")", n + 1);
                cb->setSelectedId (ctx.proc.captureNote (l) + 1, juce::dontSendNotification);
                cb->onChange = [this, l, cb] { ctx.proc.setCaptureNote (l, cb->getSelectedId() - 1); };
                addAndMakeVisible (cb);
            }

            queueList.emptyText = juce::String::fromUTF8 ("Charge un kit dans EZdrummer, puis \xc2\xab Ajouter l'\xc3\xa9tat actuel \xc3\xa0 la file \xc2\xbb. R\xc3\xa9p\xc3\xa8te pour chaque kit, puis \xc2\xab Tout capturer \xc2\xbb.");
            queueList.onRemove = [this] (int i) { eng.removeFromQueue (i); refresh(); };
            libList.emptyText = juce::String::fromUTF8 ("Aucun kit captur\xc3\xa9 pour l'instant.");
            libList.onClick = [this] (int i)
            {
                if (i >= 0 && i < kits.size())
                {
                    ctx.proc.loadKitFolder (kits[i]);
                    refresh();
                }
            };
            addAndMakeVisible (queueList);
            addAndMakeVisible (libList);
            refresh();
        }

        void refresh()
        {
            const auto& t = ctx.th();
            const auto q = eng.queue();
            queueList.rows.clear();
            const char* st[4] = { "EN ATTENTE", "EN COURS", "FAIT", "ERREUR" };
            for (auto& i : q)
                queueList.rows.push_back ({ i.name, st[juce::jlimit (0, 3, i.status)],
                                            i.status == 2 ? valueColour (t) : (i.status == 1 ? t.acc : (i.status == 3 ? juce::Colours::red : t.sub)), i.status == 1 });
            kits.clear();
            auto lib = TyphonProcessor::libraryFolder();
            for (const auto& e : juce::RangedDirectoryIterator (lib, false, "*", juce::File::findDirectories))
                if (e.getFile().getChildFile ("kit.xml").existsAsFile()) kits.add (e.getFile());
            kits.sort();
            libList.rows.clear();
            const auto cur = ctx.proc.currentKitName();
            for (auto& k : kits)
                libList.rows.push_back ({ k.getFileName(), k.getFileName() == juce::File::createLegalFileName (cur) ? juce::String (juce::String::fromUTF8 ("CHARG\xc3\x89")) : juce::String ("charger"),
                                          valueColour (t), k.getFileName() == juce::File::createLegalFileName (cur) });
            queueList.repaint(); libList.repaint();
            const bool loaded = eng.isLoaded(), busy = eng.busy();
            openBtn.setEnabled (loaded);
            captureBtn.setEnabled (loaded && ! busy);
            addBtn.setEnabled (loaded && ! busy);
            batchBtn.setEnabled (loaded && ! busy && ! q.isEmpty());
            stopBtn.setEnabled (busy);
            repaint();
        }

        void tick()
        {
            if (++counter % 8 == 0) refresh();
            else repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            for (auto& col : { left, mid, right }) { g.setColour (t.panel); g.fillRoundedRectangle (col.toFloat(), 10.0f); }
            g.setColour (t.sub); g.setFont (font (12.0f, true));
            auto lab = [&] (juce::Rectangle<int> r, const juce::String& s) { g.drawText (s, r, juce::Justification::centredLeft); };
            lab (lblSource, juce::String::fromUTF8 ("1 \xc2\xb7 SOURCE"));
            lab (lblSettings, juce::String::fromUTF8 ("2 \xc2\xb7 R\xc3\x89GLAGES"));
            lab (lblLanes, juce::String::fromUTF8 ("3 \xc2\xb7 \xc3\x89L\xc3\x89MENTS : PISTE TYPHON \xe2\x86\x90 NOTE EZDRUMMER"));
            lab (lblQueue, juce::String::fromUTF8 ("4 \xc2\xb7 CAPTURE PAR LOT"));
            lab (lblLib, juce::String::fromUTF8 ("BIBLIOTH\xc3\x88QUE TYPHON"));
            g.setFont (font (13.0f));
            g.setColour (t.tx);
            lab (lblName, "Nom du kit");
            lab (lblLayers, juce::String::fromUTF8 ("Couches de v\xc3\xa9locit\xc3\xa9"));
            lab (lblRR, "Variantes par coup (round robin)");
            lab (lblLen, juce::String::fromUTF8 ("Dur\xc3\xa9" "e max d'un coup"));
            lab (lblWait, "Attente du chargement d'un kit (lot)");

            // état d'EZdrummer
            g.setFont (font (17.0f, true));
            g.setColour (t.tx);
            g.drawText (eng.isLoaded() ? eng.pluginName() : juce::String ("EZdrummer"), statusRow, juce::Justification::centredLeft);
            g.setFont (mono (11.0f));
            g.setColour (eng.isLoaded() ? valueColour (t) : t.sub);
            g.drawText (eng.isLoaded() ? juce::String::fromUTF8 ("\xe2\x97\x8f CHARG\xc3\x89") : juce::String (juce::String::fromUTF8 ("NON CHARG\xc3\x89")), statusRow, juce::Justification::centredRight);
            g.setFont (font (12.0f));
            g.setColour (t.sub);
            g.drawFittedText (juce::String::fromUTF8 ("Choisis le kit dans EZdrummer (effets coup\xc3\xa9s pour un son sec). Tant que cette page est ouverte, EZdrummer s'entend dans Typhon : teste ses pads."), hint, juce::Justification::topLeft, 3);

            // progression
            auto pr = progressRow.toFloat();
            g.setColour (t.lcdBg); g.fillRoundedRectangle (pr, 6.0f);
            if (eng.busy())
            {
                g.setColour (t.lcdTx.withAlpha (0.35f));
                g.fillRoundedRectangle (pr.withWidth (pr.getWidth() * juce::jlimit (0.0f, 1.0f, eng.progress())), 6.0f);
            }
            g.setColour (t.lcdTx); g.setFont (mono (12.0f));
            g.drawText (eng.statusText(), progressRow.reduced (10, 0), juce::Justification::centredLeft);

            // estimation pour la file
            const auto q = eng.queue();
            int pending = 0; for (auto& i : q) if (i.status != 2) ++pending;
            const double secs = eng.estimateSecondsPerKit() * pending;
            g.setColour (t.tx); g.setFont (font (12.0f));
            g.drawText (juce::String::fromUTF8 ("Temps estim\xc3\xa9 restant"), estRow.withTrimmedRight (90), juce::Justification::centredLeft);
            g.setColour (valueColour (t)); g.setFont (mono (12.0f));
            g.drawText (pending == 0 ? juce::String ("-") : juce::String::fromUTF8 ("\xe2\x89\x88 ") + juce::String (std::max (1, juce::roundToInt (secs / 60.0))) + " min",
                        estRow, juce::Justification::centredRight);
            g.setColour (t.sub); g.setFont (mono (10.0f));
            g.drawText (TyphonProcessor::libraryFolder().getFullPathName(), libPath, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (24, 20);
            left = r.removeFromLeft (300); r.removeFromLeft (20);
            right = r.removeFromRight (330); r.removeFromRight (20);
            mid = r;

            auto L = left.reduced (16);
            lblSource = L.removeFromTop (18); L.removeFromTop (6);
            statusRow = L.removeFromTop (24); L.removeFromTop (6);
            loadBtn.setBounds (L.removeFromTop (38)); L.removeFromTop (6);
            openBtn.setBounds (L.removeFromTop (38)); L.removeFromTop (6);
            hint = L.removeFromTop (48); L.removeFromTop (6);
            lblSettings = L.removeFromTop (18); L.removeFromTop (6);
            lblName = L.removeFromTop (18);
            nameEd.setBounds (L.removeFromTop (36)); L.removeFromTop (8);
            lblLayers = L.removeFromTop (18);
            { auto row = L.removeFromTop (32); const int w = (row.getWidth() - 18) / 4; for (auto* b : layerBtns) { b->setBounds (row.removeFromLeft (w)); row.removeFromLeft (6); } }
            L.removeFromTop (8);
            lblRR = L.removeFromTop (18);
            { auto row = L.removeFromTop (32); const int w = (row.getWidth() - 18) / 4; for (auto* b : rrBtns) { b->setBounds (row.removeFromLeft (w)); row.removeFromLeft (6); } }
            L.removeFromTop (8);
            lblLen = L.removeFromTop (18);
            lenSlider.setBounds (L.removeFromTop (26)); L.removeFromTop (8);
            lblWait = L.removeFromTop (18);
            waitSlider.setBounds (L.removeFromTop (26));

            auto M = mid.reduced (16);
            lblLanes = M.removeFromTop (18); M.removeFromTop (10);
            for (int l = 0; l < typhon::numLanes; ++l)
            {
                auto row = M.removeFromTop (40); M.removeFromTop (8);
                laneOn[l]->setBounds (row.removeFromLeft (row.getWidth() / 2 - 6));
                row.removeFromLeft (12);
                noteBoxes[l]->setBounds (row.reduced (0, 4));
            }
            M.removeFromTop (4);
            progressRow = M.removeFromTop (34); M.removeFromTop (10);
            auto btns = M.removeFromTop (52);
            stopBtn.setBounds (btns.removeFromRight (150)); btns.removeFromRight (10);
            captureBtn.setBounds (btns);

            auto R = right.reduced (16);
            lblQueue = R.removeFromTop (18); R.removeFromTop (8);
            addBtn.setBounds (R.removeFromTop (36)); R.removeFromTop (8);
            queueList.setBounds (R.removeFromTop (5 * (SimpleList::rowH + 6))); R.removeFromTop (4);
            estRow = R.removeFromTop (20); R.removeFromTop (6);
            auto qb = R.removeFromTop (40);
            clearBtn.setBounds (qb.removeFromRight (qb.getWidth() / 2 - 4)); qb.removeFromRight (8);
            batchBtn.setBounds (qb);
            R.removeFromTop (14);
            lblLib = R.removeFromTop (18); R.removeFromTop (8);
            libPath = R.removeFromBottom (14);
            folderBtn.setBounds (R.removeFromBottom (32)); R.removeFromBottom (8);
            libList.setBounds (R);
        }

    private:
        Ctx& ctx;
        CaptureEngine& eng;
        Pill loadBtn, openBtn, captureBtn, stopBtn, addBtn, batchBtn, clearBtn, folderBtn;
        juce::TextEditor nameEd;
        juce::OwnedArray<Pill> layerBtns, rrBtns, laneOn;
        juce::OwnedArray<juce::ComboBox> noteBoxes;
        juce::Slider lenSlider, waitSlider;
        SimpleList queueList, libList;
        juce::Array<juce::File> kits;
        juce::Rectangle<int> left, mid, right, lblSource, lblSettings, lblLanes, lblQueue, lblLib, lblName, lblLayers, lblRR, lblLen, lblWait,
                             statusRow, hint, progressRow, estRow, libPath;
        int counter = 0;
    };
}
