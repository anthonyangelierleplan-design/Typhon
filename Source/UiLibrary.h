#pragma once
// ============================================================================
//  Page SONS : bibliothèque de boîtes à rythmes (LinnDrum, DMX, 808...)
//  Clic = écouter · double-clic = charger sur la piste conseillée
//  « Charger le kit complet » remplit les 8 pistes avec une machine
// ============================================================================
#include "UiWidgets.h"

namespace tui
{
    class LibraryPage : public juce::Component
    {
    public:
        std::function<void (int)> onLaneLoaded;   // pour rafraîchir le séquenceur

        LibraryPage (Ctx& c) : ctx (c), machineModel (*this, true), soundModel (*this, false),
            chooseBtn (c, "CHOISIR LE DOSSIER"), rescanBtn (c, "RESCANNER"), kitBtn (c, "CHARGER LE KIT COMPLET")
        {
            for (auto* lb : { &machineList, &soundList })
            {
                lb->setRowHeight (34);
                lb->setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
                lb->setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
                addAndMakeVisible (*lb);
            }
            machineList.setModel (&machineModel);
            soundList.setModel (&soundModel);
            for (auto* b : { &chooseBtn, &rescanBtn, &kitBtn }) { addAndMakeVisible (*b); b->textSize = 12.0f; }
            kitBtn.strong = true; kitBtn.textSize = 14.0f;
            chooseBtn.onClick = [this] { chooseFolder(); };
            rescanBtn.onClick = [this] { rescan(); };
            kitBtn.onClick = [this]
            {
                if (auto* m = machine())
                {
                    ctx.proc.loadMachineKit (*m);
                    ctx.proc.undoCommit();
                    status = juce::String::fromUTF8 ("Kit \xc2\xab ") + m->name + juce::String::fromUTF8 (" \xc2\xbb charg\xc3\xa9 sur les 8 pistes.");
                    if (onLaneLoaded) onLaneLoaded (-1);
                    repaint();
                }
            };
            for (int l = 0; l < typhon::numLanes; ++l)
            {
                auto* b = laneBtns.add (new Pill (c, typhon::laneShortName (l)));
                b->textSize = 11.0f;
                b->isOn = [this, l] { auto* s = sound(); return s != nullptr && s->role == l; };
                b->onClick = [this, l] { loadTo (l); };
                addAndMakeVisible (b);
            }
        }

        void visibilityChanged() override { if (isVisible() && ctx.proc.library.empty() && ! scanned) rescan(); }

        void rescan()
        {
            ctx.proc.rescanLibrary();
            scanned = true;
            selMachine = ctx.proc.library.empty() ? -1 : juce::jlimit (0, (int) ctx.proc.library.size() - 1, selMachine);
            selSound = -1; selVariant = 0;
            machineList.updateContent(); soundList.updateContent();
            if (selMachine >= 0) machineList.selectRow (selMachine);
            status = ctx.proc.library.empty() ? juce::String::fromUTF8 ("Aucune machine trouv\xc3\xa9" "e : choisis le dossier qui contient tes bo\xc3\xaetes \xc3\xa0 rythmes.")
                                              : juce::String ((int) ctx.proc.library.size()) + juce::String::fromUTF8 (" machines trouv\xc3\xa9" "es.");
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            for (auto& col : { left, mid, right }) { g.setColour (t.panel); g.fillRoundedRectangle (col.toFloat(), 10.0f); }
            g.setColour (t.sub); g.setFont (font (12.0f, true));
            g.drawText ("MACHINES", lblMachines, juce::Justification::centredLeft);
            g.drawText (machine() ? "SONS DE " + machine()->name.toUpperCase() : juce::String ("SONS"), lblSounds, juce::Justification::centredLeft);
            g.drawText (juce::String::fromUTF8 ("S\xc3\x89LECTION"), lblSel, juce::Justification::centredLeft);
            g.setFont (mono (10.0f)); g.setColour (t.sub);
            g.drawFittedText (ctx.proc.settings.libraryPath(), pathArea, juce::Justification::topLeft, 3);
            g.setFont (font (12.0f)); g.setColour (valueColour (t));
            g.drawFittedText (status, statusArea, juce::Justification::topLeft, 3);

            if (auto* s = sound())
            {
                g.setColour (t.tx); g.setFont (font (20.0f, true));
                g.drawFittedText (s->name, titleArea, juce::Justification::centredLeft, 1);
                g.setColour (t.sub); g.setFont (font (12.0f));
                juce::String info = (s->category.isNotEmpty() ? s->category + juce::String::fromUTF8 (" \xc2\xb7 ") : juce::String())
                                  + juce::String ((int) s->variants.size()) + " variante" + (s->variants.size() > 1 ? "s" : "")
                                  + juce::String::fromUTF8 (" \xc2\xb7 ") + juce::String ((int) s->variants[(size_t) selVariant].size()) + " round robin";
                g.drawText (info, infoArea, juce::Justification::centredLeft);
                g.setColour (t.sub); g.setFont (font (11.0f, true));
                g.drawText (juce::String::fromUTF8 ("VARIANTES (clic = \xc3\xa9" "couter)"), varLbl, juce::Justification::centredLeft);
                g.drawText ("CHARGER SUR LA PISTE", laneLbl, juce::Justification::centredLeft);
                // variantes
                for (int v = 0; v < (int) s->variants.size() && v < 32; ++v)
                {
                    auto r = varRect (v).toFloat();
                    g.setColour (v == selVariant ? t.acc : t.bg); g.fillRoundedRectangle (r, 5.0f);
                    g.setColour (v == selVariant ? t.bg : t.tx); g.setFont (mono (11.0f, true));
                    g.drawText (juce::String (v + 1), r.toNearestInt(), juce::Justification::centred);
                }
            }
            else
            {
                g.setColour (t.sub); g.setFont (font (13.0f));
                g.drawFittedText (juce::String::fromUTF8 ("Choisis une machine, puis un son : un clic l'\xc3\xa9" "coute, un double-clic le charge sur la piste conseill\xc3\xa9" "e "
                                                         "(surlign\xc3\xa9" "e en orange). Les variantes num\xc3\xa9rot\xc3\xa9" "es (accords, versions) se choisissent ensuite piste par piste, "
                                                         "ou pas par pas au clic droit."),
                                  titleArea.withHeight (120), juce::Justification::topLeft, 6);
            }
            // pistes actuelles
            g.setColour (t.sub); g.setFont (font (11.0f, true));
            g.drawText ("PISTES ACTUELLES", curLbl, juce::Justification::centredLeft);
            g.setFont (font (12.0f));
            for (int l = 0; l < typhon::numLanes; ++l)
            {
                auto r = curArea.withHeight (19).translated (0, l * 19);
                g.setColour (t.tx); g.drawText (typhon::laneShortName (l), r.removeFromLeft (60), juce::Justification::centredLeft);
                g.setColour (t.sub); g.drawFittedText (ctx.proc.laneSource (l), r, juce::Justification::centredLeft, 1);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (24, 20);
            left = r.removeFromLeft (280); r.removeFromLeft (20);
            right = r.removeFromRight (380); r.removeFromRight (20);
            mid = r;
            auto L = left.reduced (14);
            lblMachines = L.removeFromTop (18); L.removeFromTop (6);
            auto bottom = L.removeFromBottom (150);
            machineList.setBounds (L);
            bottom.removeFromTop (8);
            pathArea = bottom.removeFromTop (40);
            chooseBtn.setBounds (bottom.removeFromTop (32)); bottom.removeFromTop (6);
            rescanBtn.setBounds (bottom.removeFromTop (32)); bottom.removeFromTop (6);
            statusArea = bottom;
            auto M = mid.reduced (14);
            lblSounds = M.removeFromTop (18); M.removeFromTop (6);
            soundList.setBounds (M);
            auto R = right.reduced (16);
            lblSel = R.removeFromTop (18); R.removeFromTop (4);
            titleArea = R.removeFromTop (30);
            infoArea = R.removeFromTop (18); R.removeFromTop (10);
            varLbl = R.removeFromTop (16); R.removeFromTop (4);
            varArea = R.removeFromTop (62); R.removeFromTop (8);
            laneLbl = R.removeFromTop (16); R.removeFromTop (4);
            auto lr = R.removeFromTop (70);
            const int bw = (lr.getWidth() - 18) / 4;
            for (int l = 0; l < typhon::numLanes; ++l)
                laneBtns[l]->setBounds (lr.getX() + (l % 4) * (bw + 6), lr.getY() + (l / 4) * 36, bw, 32);
            R.removeFromTop (10);
            kitBtn.setBounds (R.removeFromTop (44)); R.removeFromTop (14);
            curLbl = R.removeFromTop (16);
            curArea = R.removeFromTop (8 * 19);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* s = sound();
            if (s == nullptr) return;
            for (int v = 0; v < (int) s->variants.size() && v < 32; ++v)
                if (varRect (v).contains (e.getPosition())) { selVariant = v; ctx.proc.previewSound (*s, v); repaint(); return; }
        }

    private:
        juce::Rectangle<int> varRect (int v) const
        {
            const int w = 36, h = 28;
            return { varArea.getX() + (v % 9) * (w + 5), varArea.getY() + (v / 9) * (h + 5), w, h };
        }
        const typhon::LibMachine* machine() const
        {
            return (selMachine >= 0 && selMachine < (int) ctx.proc.library.size()) ? &ctx.proc.library[(size_t) selMachine] : nullptr;
        }
        const typhon::LibSound* sound() const
        {
            auto* m = machine();
            return (m != nullptr && selSound >= 0 && selSound < (int) m->sounds.size()) ? &m->sounds[(size_t) selSound] : nullptr;
        }
        void loadTo (int lane)
        {
            auto* s = sound();
            if (s == nullptr) return;
            if (ctx.proc.loadLibrarySound (lane, *s))
            {
                if (selVariant > 0) if (auto* p = ctx.proc.apvts.getParameter (typhon::pid (lane, "var"))) p->setValueNotifyingHost (p->convertTo0to1 ((float) selVariant + 1.0f));
                ctx.proc.undoCommit();
                ctx.proc.audition (lane);
                status = s->name + juce::String::fromUTF8 (" \xe2\x86\x92 ") + typhon::laneName (lane);
                if (onLaneLoaded) onLaneLoaded (lane);
            }
            else status = juce::String::fromUTF8 ("Impossible de lire ce son.");
            repaint();
        }
        void chooseFolder()
        {
            chooser = std::make_unique<juce::FileChooser> (juce::String::fromUTF8 ("Dossier des bo\xc3\xaetes \xc3\xa0 rythmes"),
                                                           juce::File (ctx.proc.settings.libraryPath()));
            juce::Component::SafePointer<LibraryPage> safe (this);
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories, [safe] (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                auto f = fc.getResult();
                if (f.isDirectory()) { safe->ctx.proc.settings.setLibraryPath (f.getFullPathName()); safe->rescan(); }
            });
        }

        struct Model : public juce::ListBoxModel
        {
            Model (LibraryPage& p, bool machines) : page (p), isMachines (machines) {}
            int getNumRows() override
            {
                if (isMachines) return (int) page.ctx.proc.library.size();
                auto* m = page.machine(); return m ? (int) m->sounds.size() : 0;
            }
            void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected) override
            {
                const auto& t = page.ctx.th();
                auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (2.0f, 2.0f);
                g.setColour (selected ? t.acc.withAlpha (0.25f) : t.bg); g.fillRoundedRectangle (r, 6.0f);
                if (selected) { g.setColour (t.acc); g.drawRoundedRectangle (r, 6.0f, 1.0f); }
                if (isMachines)
                {
                    if (row >= (int) page.ctx.proc.library.size()) return;
                    auto& m = page.ctx.proc.library[(size_t) row];
                    g.setColour (t.tx); g.setFont (font (13.0f, true));
                    g.drawText (m.name, r.reduced (10, 0).toNearestInt(), juce::Justification::centredLeft);
                    g.setColour (t.sub); g.setFont (mono (10.0f));
                    g.drawText (juce::String ((int) m.sounds.size()), r.reduced (10, 0).toNearestInt(), juce::Justification::centredRight);
                    return;
                }
                auto* m = page.machine();
                if (m == nullptr || row >= (int) m->sounds.size()) return;
                auto& s = m->sounds[(size_t) row];
                auto rr = r.reduced (8, 0).toNearestInt();
                auto badge = rr.removeFromLeft (54).withSizeKeepingCentre (52, 20);
                g.setColour (s.role >= 0 ? t.acc : t.knobEdge); g.fillRoundedRectangle (badge.toFloat(), 4.0f);
                g.setColour (t.bg); g.setFont (font (10.0f, true));
                g.drawText (s.role >= 0 ? typhon::laneShortName (s.role) : juce::String ("?"), badge, juce::Justification::centred);
                rr.removeFromLeft (8);
                g.setColour (t.sub); g.setFont (mono (10.0f));
                const int nv = (int) s.variants.size();
                g.drawText ((nv > 1 ? juce::String (nv) + " var  " : juce::String()) + juce::String (s.totalFiles()) + " fich.", rr.removeFromRight (110), juce::Justification::centredRight);
                g.setColour (t.tx); g.setFont (font (13.0f, true));
                g.drawText (s.name, rr.removeFromLeft (rr.getWidth() / 2 + 40), juce::Justification::centredLeft);
                g.setColour (t.sub); g.setFont (font (11.0f));
                g.drawText (s.category, rr, juce::Justification::centredLeft);
            }
            void listBoxItemClicked (int row, const juce::MouseEvent&) override
            {
                if (isMachines) { page.selMachine = row; page.selSound = -1; page.selVariant = 0; page.soundList.updateContent(); page.soundList.deselectAllRows(); page.repaint(); return; }
                page.selSound = row; page.selVariant = 0;
                if (auto* s = page.sound()) page.ctx.proc.previewSound (*s, 0);
                page.repaint();
            }
            void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override
            {
                if (isMachines) { if (auto* m = page.machine()) { juce::ignoreUnused (row); page.kitBtn.triggerClick(); juce::ignoreUnused (m); } return; }
                page.selSound = row;
                if (auto* s = page.sound()) page.loadTo (s->role >= 0 ? s->role : 7);
            }
            LibraryPage& page;
            bool isMachines;
        };

        Ctx& ctx;
        Model machineModel, soundModel;
        juce::ListBox machineList, soundList;
        Pill chooseBtn, rescanBtn, kitBtn;
        juce::OwnedArray<Pill> laneBtns;
        std::unique_ptr<juce::FileChooser> chooser;
        int selMachine = 0, selSound = -1, selVariant = 0;
        bool scanned = false;
        juce::String status;
        juce::Rectangle<int> left, mid, right, lblMachines, lblSounds, lblSel, pathArea, statusArea, titleArea, infoArea, varLbl, varArea, laneLbl, curLbl, curArea;
    };
}
