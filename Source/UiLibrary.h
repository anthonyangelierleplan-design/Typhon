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
            chooseBtn (c, "CHOISIR LE DOSSIER"), rescanBtn (c, "RESCANNER"), kitBtn (c, "CHARGER LE KIT COMPLET"), saveKitBtn (c, "ENREGISTRER MON KIT"), myKitsBtn (c, "MES KITS"),
              accMinus (c, juce::String::fromUTF8 ("\xe2\x88\x92")), accPlus (c, "+"), zeroBtn (c, juce::String::fromUTF8 ("Z\xc3\x89RO ICI")), autoBtn (c, "AUTO")
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
            for (auto* b : { &chooseBtn, &rescanBtn, &kitBtn, &saveKitBtn, &myKitsBtn }) { addAndMakeVisible (*b); b->textSize = 12.0f; }
            saveKitBtn.onClick = [this] { askKitName(); };
            for (auto* b : { &accMinus, &accPlus, &zeroBtn, &autoBtn }) { addAndMakeVisible (*b); b->textSize = 12.0f; }
            accMinus.onClick = [this] { nudgeAccord (-0.5f); };
            accPlus.onClick = [this] { nudgeAccord (0.5f); };
            zeroBtn.onClick = [this] { if (auto* s = soundMut()) { ctx.proc.setSoundAccord (*s, selVariant, s->tune); status = juce::String::fromUTF8 ("Pitch z\xc3\xa9ro : variante ") + juce::String (selVariant + 1); repaint(); } };
            autoBtn.onClick = [this]
            {
                if (auto* m = machineMut())
                {
                    const int n = ctx.proc.autoTuneMachine (*m);
                    status = n > 0 ? juce::String::fromUTF8 ("Accord auto : ") + juce::String (n) + juce::String::fromUTF8 (" son(s) redescendu(s) vers leur hauteur habituelle (kick ~52 Hz, snare ~185 Hz). Ajuste \xc3\xa0 l'oreille avec \xe2\x88\x92 / +.")
                                   : juce::String::fromUTF8 ("Accord auto : rien \xc3\xa0 corriger sur cette machine.");
                    if (auto* s = soundMut()) selVariant = s->zeroVar;
                    repaint();
                }
            };
            accMinus.setTooltip (juce::String::fromUTF8 ("Accord du son : \xe2\x88\x92 \xc2\xbd demi-ton (gard\xc3\xa9 pour tous tes projets)"));
            accPlus.setTooltip (juce::String::fromUTF8 ("Accord du son : + \xc2\xbd demi-ton"));
            zeroBtn.setTooltip (juce::String::fromUTF8 ("La variante choisie devient le pitch z\xc3\xa9ro de ce son"));
            autoBtn.setTooltip (juce::String::fromUTF8 ("Accorde toute la machine : kicks et snares trop aigus ramen\xc3\xa9s vers leur hauteur habituelle"));
            myKitsBtn.onClick = [this] { showMyKits(); };
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
                const bool tunedOk = s->tuned && (int) s->hz.size() == (int) s->variants.size();
                for (int v = 0; v < (int) s->variants.size() && v < 32; ++v)
                {
                    auto r = varRect (v).toFloat();
                    g.setColour (v == selVariant ? t.acc : t.bg); g.fillRoundedRectangle (r, 5.0f);
                    if (tunedOk && v == s->zeroVar) { g.setColour (t.lcdTx); g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.5f); }
                    g.setColour (v == selVariant ? t.bg : t.tx); g.setFont (mono (11.0f, true));
                    juce::String lab = juce::String (v + 1);
                    // écart en demi-tons par rapport au pitch zéro, quand les deux ont une hauteur nette
                    if (tunedOk && s->pitched[(size_t) v] && s->pitched[(size_t) s->zeroVar])
                    {
                        const int st = (int) std::lround (12.0 * std::log2 (s->hz[(size_t) v] / s->hz[(size_t) s->zeroVar]));
                        lab = v == s->zeroVar ? juce::String ("0") : (st > 0 ? "+" : "") + juce::String (st);
                    }
                    g.drawText (lab, r.toNearestInt(), juce::Justification::centred);
                }
                g.setColour (t.sub); g.setFont (font (11.0f, true));
                g.drawText (juce::String::fromUTF8 ("ACCORD (GARD\xc3\x89 POUR TOUS TES PROJETS)"), accLbl, juce::Justification::centredLeft);
                g.setColour (valueColour (t)); g.setFont (mono (13.0f));
                juce::String hzTxt;
                if (tunedOk && s->pitched[(size_t) s->zeroVar]) hzTxt = "  " + juce::String (juce::roundToInt (s->hz[(size_t) s->zeroVar] * std::pow (2.0f, s->tune / 12.0f))) + " Hz";
                g.drawText ((s->tune > 0.0f ? "+" : "") + juce::String (s->tune, 1) + " st" + hzTxt, accVal, juce::Justification::centred);
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
                auto r = curArea.withHeight (17).translated (0, l * 17);
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
            varArea = R.removeFromTop (62); R.removeFromTop (6);
            accLbl = R.removeFromTop (16); R.removeFromTop (3);
            {
                auto a = R.removeFromTop (30);
                accMinus.setBounds (a.removeFromLeft (34)); a.removeFromLeft (4);
                accVal = a.removeFromLeft (110); a.removeFromLeft (4);
                accPlus.setBounds (a.removeFromLeft (34)); a.removeFromLeft (8);
                autoBtn.setBounds (a.removeFromRight (64)); a.removeFromRight (6);
                zeroBtn.setBounds (a);
            }
            R.removeFromTop (8);
            laneLbl = R.removeFromTop (16); R.removeFromTop (4);
            auto lr = R.removeFromTop (70);
            const int bw = (lr.getWidth() - 18) / 4;
            for (int l = 0; l < typhon::numLanes; ++l)
                laneBtns[l]->setBounds (lr.getX() + (l % 4) * (bw + 6), lr.getY() + (l / 4) * 36, bw, 32);
            R.removeFromTop (10);
            kitBtn.setBounds (R.removeFromTop (44)); R.removeFromTop (14);
            curLbl = R.removeFromTop (16);
            curArea = R.removeFromTop (8 * 17);
            R.removeFromTop (12);
            auto kb = R.removeFromTop (34);
            saveKitBtn.setBounds (kb.removeFromLeft (kb.getWidth() * 3 / 5 - 3)); kb.removeFromLeft (6);
            myKitsBtn.setBounds (kb);
        }

        // « Mes kits » : les 8 pistes telles qu'elles sont (samples glissés, sons de machines, slices...)
        // copiées dans Documents/Foudre Audio/Typhon/Kits/<nom> ; rechargeables partout
        void askKitName()
        {
            nameWin = std::make_unique<juce::AlertWindow> (juce::String::fromUTF8 ("Enregistrer mon kit"),
                                                          juce::String::fromUTF8 ("Les 8 pistes sont copi\xc3\xa9" "es dans ton dossier Kits : le kit reste complet m\xc3\xaame si tu d\xc3\xa9places les samples d'origine."),
                                                          juce::MessageBoxIconType::NoIcon);
            nameWin->addTextEditor ("name", ctx.proc.currentKitName() == "Kit de secours" ? juce::String ("Mon kit") : ctx.proc.currentKitName());
            nameWin->addButton ("ENREGISTRER", 1, juce::KeyPress (juce::KeyPress::returnKey));
            nameWin->addButton ("ANNULER", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            juce::Component::SafePointer<LibraryPage> safe (this);
            nameWin->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int r)
            {
                if (safe == nullptr || safe->nameWin == nullptr) return;
                const auto name = safe->nameWin->getTextEditorContents ("name");
                if (r == 1)
                {
                    auto f = safe->ctx.proc.saveUserKit (name);
                    safe->status = f.exists() ? juce::String::fromUTF8 ("Kit \xc2\xab ") + f.getFileName() + juce::String::fromUTF8 (" \xc2\xbb enregistr\xc3\xa9.")
                                              : juce::String::fromUTF8 ("Impossible d'enregistrer le kit.");
                    if (safe->onLaneLoaded) safe->onLaneLoaded (-1);
                    safe->repaint();
                }
                juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->nameWin.reset(); });
            }), false);
        }

        void showMyKits()
        {
            juce::Array<juce::File> kits;
            for (const auto& e : juce::RangedDirectoryIterator (TyphonProcessor::libraryFolder(), false, "*", juce::File::findDirectories))
                if (e.getFile().getChildFile ("kit.xml").existsAsFile()) kits.add (e.getFile());
            kits.sort();
            juce::PopupMenu m;
            m.addSectionHeader ("MES KITS");
            if (kits.isEmpty()) m.addItem (-1, juce::String::fromUTF8 ("Aucun kit : compose le tien puis \xc2\xab Enregistrer mon kit \xc2\xbb"), false);
            for (int i = 0; i < kits.size(); ++i) m.addItem (i + 1, kits[i].getFileName());
            m.addSeparator();
            m.addItem (10000, "Ouvrir le dossier Kits");
            juce::Component::SafePointer<LibraryPage> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&myKitsBtn), [safe, kits] (int r)
            {
                if (safe == nullptr || r <= 0) return;
                if (r == 10000) { TyphonProcessor::libraryFolder().createDirectory(); TyphonProcessor::libraryFolder().startAsProcess(); return; }
                if (r - 1 < kits.size() && safe->ctx.proc.loadKitFolder (kits[r - 1]))
                {
                    safe->ctx.proc.undoCommit();
                    safe->status = juce::String::fromUTF8 ("Kit \xc2\xab ") + kits[r - 1].getFileName() + juce::String::fromUTF8 (" \xc2\xbb charg\xc3\xa9.");
                    if (safe->onLaneLoaded) safe->onLaneLoaded (-1);
                    safe->repaint();
                }
            });
        }

        void nudgeAccord (float d)
        {
            if (auto* s = soundMut())
            {
                ctx.proc.setSoundAccord (*s, s->zeroVar, s->tune + d);
                ctx.proc.previewSound (*s, selVariant, s->tune);
                repaint();
            }
        }
        typhon::LibMachine* machineMut() { return (selMachine >= 0 && selMachine < (int) ctx.proc.library.size()) ? &ctx.proc.library[(size_t) selMachine] : nullptr; }
        typhon::LibSound* soundMut()
        {
            auto* m = machineMut();
            if (m == nullptr || selSound < 0 || selSound >= (int) m->sounds.size()) return nullptr;
            auto* s = &m->sounds[(size_t) selSound];
            ctx.proc.tuneSound (*s);
            return s;
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            auto* s = soundMut();
            if (s == nullptr) return;
            for (int v = 0; v < (int) s->variants.size() && v < 32; ++v)
                if (varRect (v).contains (e.getPosition()))
                {
                    selVariant = v; ctx.proc.previewSound (*s, v, s->tune); repaint();
                    if (e.mods.isPopupMenu())
                    {
                        juce::PopupMenu m;
                        m.addItem (1, juce::String::fromUTF8 ("D\xc3\xa9" "finir comme pitch z\xc3\xa9ro"), v != s->zeroVar);
                        juce::Component::SafePointer<LibraryPage> safe (this);
                        m.showMenuAsync (juce::PopupMenu::Options(), [safe, v] (int r) { if (safe != nullptr && r == 1) { safe->selVariant = v; safe->zeroBtn.triggerClick(); } });
                    }
                    return;
                }
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
            auto* s = soundMut();
            if (s == nullptr) return;
            if (ctx.proc.loadLibrarySound (lane, *s))
            {
                if (auto* p = ctx.proc.apvts.getParameter (typhon::pid (lane, "var"))) p->setValueNotifyingHost (p->convertTo0to1 ((float) selVariant + 1.0f));
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
                if (auto* s = page.soundMut()) { page.selVariant = s->zeroVar; page.ctx.proc.previewSound (*s, s->zeroVar, s->tune); }
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
        Pill chooseBtn, rescanBtn, kitBtn, saveKitBtn, myKitsBtn;
        std::unique_ptr<juce::AlertWindow> nameWin;
        juce::OwnedArray<Pill> laneBtns;
        std::unique_ptr<juce::FileChooser> chooser;
        int selMachine = 0, selSound = -1, selVariant = 0;
        bool scanned = false;
        juce::String status;
        juce::Rectangle<int> accLbl, accVal;
        Pill accMinus, accPlus, zeroBtn, autoBtn;
        juce::Rectangle<int> left, mid, right, lblMachines, lblSounds, lblSel, pathArea, statusArea, titleArea, infoArea, varLbl, varArea, laneLbl, curLbl, curArea;
    };
}
