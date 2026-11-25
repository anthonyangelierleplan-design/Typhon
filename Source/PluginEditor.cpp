#include "PluginEditor.h"
#include "UiWidgets.h"
#include "UiSeq.h"
#include "UiLane.h"
#include "UiCapture.h"
#include "UiSettings.h"
#include "UiMix.h"
#include "UiLibrary.h"

using namespace tui;

static constexpr int baseW = 1280, baseH = 800;
static constexpr int headerH = 76, bottomH = 172;

// ============================================================================
//  Bouton « glisser le pattern dans Ableton »
// ============================================================================
class DragMidiButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragMidiButton (Ctx& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        auto r = getLocalBounds().toFloat();
        g.setColour (t.panel2); g.fillRoundedRectangle (r, 8.0f);
        g.setColour (t.acc.withAlpha (0.6f)); g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (t.acc); g.setFont (font (13.0f, true));
        g.drawFittedText (juce::String::fromUTF8 ("\xe2\x87\xa9 MIDI\nGLISSER"), getLocalBounds().reduced (6, 0), juce::Justification::centred, 2);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6) return;
        dragging = true;
        auto f = ctx.proc.writePatternMidi();
        if (f.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this, [this] { dragging = false; });
        else dragging = false;
    }
    void mouseUp (const juce::MouseEvent&) override { dragging = false; }
private:
    Ctx& ctx;
    bool dragging = false;
};

// Bouton FILL : actif tant qu'on appuie (clic droit = verrouillé)
class FillButton : public juce::Component
{
public:
    FillButton (Ctx& c) : ctx (c) {}
    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        const bool on = param() != nullptr && param()->getValue() > 0.5f;
        const bool playing = ctx.proc.fillActive();
        auto r = getLocalBounds().toFloat();
        g.setColour (on ? t.acc : t.panel2); g.fillRoundedRectangle (r, 8.0f);
        if (playing && ! on) { g.setColour (t.acc); g.drawRoundedRectangle (r.reduced (1.5f), 8.0f, 3.0f); }
        g.setColour (on ? t.panel2 : t.acc); g.setFont (font (18.0f, true));
        g.drawText ("FILL", getLocalBounds().withTrimmedBottom (14), juce::Justification::centred);
        g.setFont (font (10.0f));
        g.drawText (latched ? juce::String::fromUTF8 ("verrouill\xc3\xa9") : juce::String ("maintenir"), getLocalBounds().withTrimmedTop (30), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { latched = ! latched; set (latched); }
        else if (! latched) set (true);
        repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (! e.mods.isPopupMenu() && ! latched) set (false);
        repaint();
    }
private:
    juce::RangedAudioParameter* param() const { return ctx.proc.apvts.getParameter ("fill"); }
    void set (bool v)
    {
        if (auto* p = param()) { p->beginChangeGesture(); p->setValueNotifyingHost (v ? 1.0f : 0.0f); p->endChangeGesture(); }
    }
    Ctx& ctx;
    bool latched = false;
};

// Bouton de performance à maintenir (ROLL, STEP LOOP) ; clic droit = réglage
class HoldButton : public juce::Component, public juce::SettableTooltipClient
{
public:
    HoldButton (Ctx& c, const juce::String& t) : ctx (c), title (t) {}
    std::function<void (bool)> onHold;
    std::function<juce::String()> sub;
    std::function<void()> onMenu;
    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (held ? t.acc : t.bottom.darker (0.08f)); g.fillRoundedRectangle (r, 9.0f);
        g.setColour (t.bottomTx); g.drawRoundedRectangle (r, 9.0f, 2.0f);
        if (held) paintBevel (g, r, 9.0f, false);
        const auto fg = held ? t.bottom : t.bottomTx;
        g.setColour (fg);
        auto f = font (14.0f, true); f.setExtraKerningFactor (0.06f);
        g.setFont (f);
        g.drawFittedText (title, getLocalBounds().withTrimmedBottom (20).reduced (4, 0), juce::Justification::centredBottom, 2);
        g.setColour (fg.withAlpha (0.75f)); g.setFont (mono (11.0f));
        g.drawText (sub ? sub() : juce::String(), getLocalBounds().withTrimmedTop (getHeight() - 26), juce::Justification::centred);
    }
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu()) { if (onMenu) onMenu(); return; }
        held = true; if (onHold) onHold (true); repaint();
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || ! held) return;
        held = false; if (onHold) onHold (false); repaint();
    }
private:
    Ctx& ctx;
    juce::String title;
    bool held = false;
};

// Languette du panneau de piste rangé (texte vertical, clic = déplier)
class PanelTab : public juce::Component
{
public:
    PanelTab (Ctx& c) : ctx (c) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    std::function<void()> onClick;
    juce::String text;
    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        auto r = getLocalBounds().toFloat();
        g.setColour (t.panel); g.fillRoundedRectangle (r, 8.0f);
        paintBevel (g, r, 8.0f);
        g.setColour (t.sub);
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
        auto f = font (12.0f, true); f.setExtraKerningFactor (0.12f);
        g.setFont (f);
        g.drawText (text, juce::Rectangle<float> (r.getHeight(), r.getWidth()).withCentre (r.getCentre()), juce::Justification::centred);
    }
    void mouseUp (const juce::MouseEvent&) override { if (onClick) onClick(); }
private:
    Ctx& ctx;
};

// ============================================================================
struct TyphonEditor::Ui : public juce::Component, private juce::Timer
{
    Ui (TyphonProcessor& p)
        : ctx { p }, look (ctx), grid (ctx), lane (ctx), capture (ctx), settingsPage (ctx), mixPage (ctx), libPage (ctx), editor (ctx),
          tabSeq (ctx, juce::String::fromUTF8 ("S\xc3\x89QUENCE")), tabCap (ctx, "CAPTURE EZDRUMMER"), subLib (ctx, juce::String::fromUTF8 ("BIBLIOTH\xc3\x88QUE")), tabMix (ctx, "MIX"), tabLib (ctx, "SONS"), fillSet (ctx, juce::String::fromUTF8 ("R\xc3\x89GLAGES FILL")),
          chokeAllBtn (ctx, "CHOKE GLOBAL"), undoBtn (ctx, juce::String::fromUTF8 ("\xe2\x86\xb6")), redoBtn (ctx, juce::String::fromUTF8 ("\xe2\x86\xb7")), settingsBtn (ctx, juce::String::fromUTF8 ("\xe2\x9a\x99")),
          dragBtn (ctx), fillBtn (ctx), spAuth (ctx, "AUTHENTIQUE"), spClean (ctx, "PROPRE"), panelBtn (ctx, juce::String::fromUTF8 ("RANGER \xe2\x96\xb8")), panelTab (ctx), rollBtn (ctx, "ROLL"), loopBtn (ctx, "STEP LOOP")
    {
        setLookAndFeel (&look);
        addAndMakeVisible (grid);
        addAndMakeVisible (lane);
        addChildComponent (capture);
        addChildComponent (settingsPage);
        addChildComponent (mixPage);
        addChildComponent (libPage);
        libPage.onLaneLoaded = [this] (int l) { if (l >= 0) { grid.selected = l; lane.setLane (l); } else lane.setLane (lane.lane); grid.repaint(); };
        addChildComponent (editor);
        for (auto* b : { &tabSeq, &tabMix, &tabLib, &settingsBtn }) { addAndMakeVisible (*b); b->textSize = 13.0f; }
        // CAPTURE est une source de sons : elle vit dans SONS, à côté de la bibliothèque ; CHOKE GLOBAL vit dans MIX
        for (auto* b : { &tabCap, &subLib, &chokeAllBtn }) { addChildComponent (*b); b->textSize = 11.5f; }
        tabLib.isOn = [this] { return page == 4 || page == 1; };
        tabLib.onClick = [this] { setPage (sonsPage); };
        subLib.isOn = [this] { return page == 4; };
        subLib.onClick = [this] { setPage (4); };
        chokeAllBtn.setTooltip (juce::String::fromUTF8 ("Chaque coup coupe toutes les autres pistes (comme une seule voix)"));
        tabMix.isOn = [this] { return page == 3; };
        for (auto* b : { &undoBtn, &redoBtn }) { addAndMakeVisible (*b); b->textSize = 20.0f; }
        undoBtn.setTooltip ("Annuler (Ctrl+Z)"); redoBtn.setTooltip (juce::String::fromUTF8 ("R\xc3\xa9tablir (Ctrl+Y)"));
        undoBtn.onClick = [this] { doUndo (false); };
        redoBtn.onClick = [this] { doUndo (true); };
        setWantsKeyboardFocus (true);
        addMouseListener (this, true);
        tabMix.onClick = [this] { setPage (3); };
        addAndMakeVisible (fillSet); fillSet.textSize = 10.0f;
        fillSet.onClick = [this]
        {
            juce::CallOutBox::launchAsynchronously (std::make_unique<FillPanel> (ctx), fillSet.getBounds(), this);
        };
        lane.onOpenEditor = [this] { editor.setLane (lane.lane); editorOpen = true; setPage (0); };
        editor.onClose = [this] { editorOpen = false; setPage (0); };
        grid.onLockStep = [this] (int l, int st)
        {
            grid.selected = l; lane.setLane (l);
            grid.lockLane = l; grid.lockStep = st;
            lane.setLockStep (st);
            editorOpen = false; setPage (0);
        };
        lane.onLockEnded = [this] { grid.lockLane = -1; grid.lockStep = -1; grid.repaint(); };
        tabSeq.isOn = [this] { return page == 0; };
        tabCap.isOn = [this] { return page == 1; };
        settingsBtn.isOn = [this] { return page == 2; };
        tabSeq.onClick = [this] { setPage (0); };
        tabCap.onClick = [this] { setPage (1); };
        settingsBtn.onClick = [this] { setPage (page == 2 ? 0 : 2); };
        settingsBtn.setTooltip (juce::String::fromUTF8 ("Param\xc3\xa8tres"));
        settingsBtn.textSize = 20.0f;
        for (auto* b : { &spAuth, &spClean }) { addAndMakeVisible (*b); b->textSize = 9.5f; }
        spAuth.isOn = [this] { return ctx.proc.settings.spMode.load() == 0; };
        spClean.isOn = [this] { return ctx.proc.settings.spMode.load() == 1; };
        spAuth.onClick = [this] { ctx.proc.settings.spMode.store (0); ctx.proc.settings.save(); repaint(); };
        spClean.onClick = [this] { ctx.proc.settings.spMode.store (1); ctx.proc.settings.save(); repaint(); };
        spAuth.setTooltip (juce::String::fromUTF8 ("Comme la machine : maintien sans filtre anti-repliement"));
        spClean.setTooltip (juce::String::fromUTF8 ("M\xc3\xaame grain, aigus moins agressifs"));
        addAndMakeVisible (panelBtn); panelBtn.textSize = 11.0f;
        panelBtn.onClick = [this] { setPanelOpen (false); };
        addChildComponent (panelTab);
        addAndMakeVisible (rollBtn); addAndMakeVisible (loopBtn);
        rollBtn.sub = [this] { const char* n[4] = { "", "1/8", "1/16", "1/32" }; return juce::String (n[rollRate]); };
        rollBtn.onHold = [this] (bool on) { ctx.proc.uiRollLane.store (grid.selected); ctx.proc.uiRollDiv.store (on ? rollRate : 0); };
        rollBtn.onMenu = [this]
        {
            juce::PopupMenu m; m.addSectionHeader ("ROLL (piste choisie)");
            m.addItem (1, "1/8", true, rollRate == 1); m.addItem (2, "1/16", true, rollRate == 2); m.addItem (3, "1/32", true, rollRate == 3);
            juce::Component::SafePointer<Ui> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&rollBtn), [safe] (int r) { if (safe != nullptr && r > 0) { safe->rollRate = r; safe->rollBtn.repaint(); } });
        };
        rollBtn.setTooltip (juce::String::fromUTF8 ("Maintenir : roulement sur la piste choisie, cal\xc3\xa9 sur le tempo \xc2\xb7 clic droit : cadence"));
        loopBtn.sub = [this] { return juce::String (loopSize) + " PAS"; };
        loopBtn.onHold = [this] (bool on) { ctx.proc.stepLoopLen.store (on ? loopSize : 0); };
        loopBtn.onMenu = [this]
        {
            juce::PopupMenu m; m.addSectionHeader ("STEP LOOP");
            for (int v : { 1, 2, 4, 8 }) m.addItem (v, juce::String (v) + " pas", true, loopSize == v);
            juce::Component::SafePointer<Ui> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loopBtn), [safe] (int r) { if (safe != nullptr && r > 0) { safe->loopSize = r; safe->loopBtn.repaint(); } });
        };
        loopBtn.setTooltip (juce::String::fromUTF8 ("Maintenir pendant la lecture : rejoue en boucle les pas en cours \xc2\xb7 clic droit : longueur"));
        panelTab.onClick = [this] { setPanelOpen (true); };
        dragBtn.setTooltip (juce::String::fromUTF8 ("Clips MIDI : Kick C1, Snare D1, Ghost D#1, Rim C#1, Hat F#1 / A#1, Crash C#2, Perc E1 \xc2\xb7 C0 coupe tout"));
        chokeAllBtn.setClickingTogglesState (true);
        chokeAllAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "choke_all", chokeAllBtn);
        settingsPage.onClose = [this] { setPage (0); };
        settingsPage.onChanged = [this] { applySettings(); };
        { static const float ts[3] = { 1.0f, 1.12f, 1.25f }; textScale() = ts[juce::jlimit (0, 2, ctx.proc.settings.textSize.load())]; SeqGrid::toggleModifierRef() = ctx.proc.settings.toggleMod.load(); }

        grid.onSelectLane = [this] (int l) { lane.setLane (l); panelTab.text = juce::String::fromUTF8 ("\xe2\x97\x82  ") + upperFr (typhon::laneName (l)) + juce::String::fromUTF8 ("  \xc2\xb7  PISTE"); panelTab.repaint(); if (lane.getLockStep() < 0) { grid.lockLane = -1; grid.lockStep = -1; } if (editorOpen) editor.setLane (l); };
        grid.selected = 1;
        grid.onFocusStep = [this] (int l, int st, bool reveal)
        {
            if (reveal)
            {
                if (ctx.proc.settings.panelOpen.load() == 0) setPanelOpen (true);
                int sl, s0, s1;
                if (grid.singleLaneSelection (sl, s0, s1) && sl == l && st >= s0 && st <= s1 && s1 > s0) lane.showRange (l, s0, s1, true);
                else lane.showStep (l, st);
            }
            else if (lane.lane == l) lane.inspector.setStep (l, st);
        };
        grid.onSelectionChanged = [this]
        {
            int l, s0, s1;
            if (grid.singleLaneSelection (l, s0, s1) && s1 > s0) lane.showRange (l, s0, s1, false);
            else if (lane.inspector.isRange()) lane.inspector.setStep (lane.lane, -1);
        };
        lane.inspector.onLock = [this] (int l, int st) { if (grid.onLockStep) grid.onLockStep (l, st); };
        lane.inspector.onEdited = [this] { grid.repaint(); };
        lane.onAltLock = [this] { grid.repaint(); };

        struct K { const char* label; const char* id; std::function<juce::String (float)> fmt; };
        const K ks[] = {
            { "Grain SP", "sp_amt", fmtPct },
            { "Bits", "sp_bits", [] (float v) { return juce::String (juce::roundToInt (v)) + " bits"; } },
            { "Fr\xc3\xa9quence", "sp_rate", [] (float v) { return juce::String (v / 1000.0f, 2) + " kHz"; } },
            { "Filtre SP", "sp_filt", [] (float v) { return v < 0.005f ? juce::String ("OUVERT") : fmtPct (v); } },
            { "Swing", "swing", [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; } },
            { "Master", "master", fmtDb },
            { "Intensit\xc3\xa9", "fill_int", fmtPct }
        };
        for (auto& k : ks)
        {
            auto* kn = masterKnobs.add (new Knob (ctx, juce::String::fromUTF8 (k.label)));
            kn->attach (k.id);
            kn->formatter = k.fmt;
            kn->setHardware();
            addAndMakeVisible (kn);
        }
        fillAuto.addItemList ({ "Fill : jamais", "Fill : 4 mes.", "Fill : 8 mes." }, 1);
        fillAutoAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "fill_auto", fillAuto);
        addAndMakeVisible (fillAuto);
        addAndMakeVisible (dragBtn);
        addAndMakeVisible (fillBtn);

        setSize (baseW, baseH);
        startTimerHz (30);
    }

    ~Ui() override
    {
        diag::uiBeat().store (0);
        ctx.proc.capture().monitor.store (false);
        stopTimer();
        setLookAndFeel (nullptr);
    }

    // --- annuler / rétablir ----------------------------------------------------
    void doUndo (bool redo)
    {
        const bool ok = redo ? ctx.proc.redo() : ctx.proc.undo();
        if (! ok) return;
        lane.setLane (lane.lane);
        if (editorOpen) editor.setLane (lane.lane);
        repaint();
        grid.repaint();
    }
    bool keyPressed (const juce::KeyPress& k) override
    {
        const auto m = k.getModifiers();
        const int c = k.getKeyCode();
        if (page == 0 && ! editorOpen)
        {
            if (c == juce::KeyPress::escapeKey) { grid.clearSelectionMark(); return true; }
            if (c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey)
            {
                if (grid.hasSelection()) grid.clearSelectionSteps();
                else grid.clearFocusedStep();
                lane.inspector.refresh(); ctx.proc.undoCommit(); return true;
            }
            if (m.isCommandDown() && (c == 'D' || c == 'd'))
            {
                if (m.isShiftDown())
                {
                    const int cur = ctx.proc.currentPattern();
                    if (cur < TyphonProcessor::fillPatternIndex - 1) { ctx.proc.copyPattern (cur, cur + 1); ctx.proc.selectPattern (cur + 1); }
                }
                else if (grid.hasSelection()) grid.duplicateSelection();
                else grid.duplicateFocusedStep();
                ctx.proc.undoCommit(); grid.repaint(); return true;
            }
            if (m.isCommandDown() && (c == 'C' || c == 'c')) { grid.copySelectionOrFocus(); return true; }
            if (m.isCommandDown() && (c == 'V' || c == 'v')) { grid.pasteAtSelection(); grid.repaint(); ctx.proc.undoCommit(); return true; }
            if (! m.isCommandDown() && ! m.isAltDown() && handleShortcut (c)) return true;
        }
        if (! m.isCommandDown()) return false;
        if ((c == 'Z' || c == 'z') && ! m.isShiftDown()) { doUndo (false); return true; }
        if (c == 'Y' || c == 'y' || ((c == 'Z' || c == 'z') && m.isShiftDown())) { doUndo (true); return true; }
        return false;
    }
    bool handleShortcut (int code)
    {
        if (code >= 'a' && code <= 'z') code -= 32;
        int action = -1;
        for (int k = 0; k < TyphonSettings::numShortcuts; ++k) if (ctx.proc.settings.shortcutKeys[k].load() == code) { action = k; break; }
        if (action < 0) return false;
        if (action == 13) { if (grid.focusLane >= 0) grid.onFocusStep (grid.focusLane, grid.focusStep, true); return true; }
        auto targets = grid.targetSteps();
        if (targets.empty()) return false;
        auto& pat = ctx.proc.pattern;
        // les cycles suivent le premier pas visé, pour que tous les pas sélectionnés finissent pareils
        const auto ref = pat.get (targets[0].first, targets[0].second);
        for (auto [l, st] : targets)
        {
            auto s = pat.get (l, st);
            if (! s.on()) continue;
            switch (action)
            {
                case 0: s.vel = ref.vel >= 115 ? 90 : 120; break;
                case 1: s.vel = 40; break;
                case 2: s.ratchet = ref.split ? 1 : (ref.ratchet % 4) + 1; s.split = false; break;
                case 3: s.flam = ! ref.flam; break;
                case 4: s.split = ! ref.split; s.ratchet = 1; break;
                case 5: s.rev = ! ref.rev; break;
                case 6: s.prob = ref.prob > 75 ? 75 : (ref.prob > 50 ? 50 : (ref.prob > 25 ? 25 : 100)); break;
                case 7: s.pitch = std::max (-12, s.pitch - 1); break;
                case 8: s.pitch = std::min (12, s.pitch + 1); break;
                case 9: s.nudge = std::max (-50, s.nudge - 5); break;
                case 10: s.nudge = std::min (50, s.nudge + 5); break;
                case 11: s.dur = (ref.dur + 1) % 3; break;
                case 12: s.choke = (ref.choke + 1) % 3; break;
                default: break;
            }
            pat.set (l, st, s);
        }
        lane.inspector.refresh();
        grid.repaint();
        ctx.proc.undoCommit();
        return true;
    }

    // chaque action à la souris (et les menus qui suivent) devient une étape d'annulation
    void mouseDown (const juce::MouseEvent&) override
    {
        uiActiveUntil = juce::Time::getMillisecondCounter() + 4000;
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        uiActiveUntil = juce::Time::getMillisecondCounter() + 4000;
        juce::Component::SafePointer<Ui> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->ctx.proc.undoCommit(); });
    }

    void setPage (int pg)
    {
        page = pg;
        if (pg == 1 || pg == 4) sonsPage = pg;
        tabCap.setVisible (pg == 1 || pg == 4); subLib.setVisible (pg == 1 || pg == 4); chokeAllBtn.setVisible (pg == 3);
        const bool seq = pg == 0;
        grid.setVisible (seq && ! editorOpen); lane.setVisible (seq && ! editorOpen);
        editor.setVisible (seq && editorOpen);
        for (auto* k : masterKnobs) k->setVisible (seq);
        spAuth.setVisible (seq); spClean.setVisible (seq); rollBtn.setVisible (seq); loopBtn.setVisible (seq);
        const bool open = ctx.proc.settings.panelOpen.load() == 1;
        lane.setVisible (seq && ! editorOpen && open);
        panelBtn.setVisible (seq && ! editorOpen && open);
        panelTab.setVisible (seq && ! editorOpen && ! open);
        fillAuto.setVisible (seq); dragBtn.setVisible (seq); fillBtn.setVisible (seq); fillSet.setVisible (seq);
        mixPage.setVisible (pg == 3);
        libPage.setVisible (pg == 4);
        capture.setVisible (pg == 1);
        ctx.proc.capture().monitor.store (pg == 1);
        settingsPage.setVisible (pg == 2);
        if (pg == 1) capture.refresh();
        resized();
        repaint();
    }

    void setPanelOpen (bool open)
    {
        ctx.proc.settings.panelOpen.store (open ? 1 : 0);
        ctx.proc.settings.save();
        setPage (page);
    }

    void applySettings()
    {
        bgKey = -1;
        static const float ts[3] = { 1.0f, 1.12f, 1.25f };
        textScale() = ts[juce::jlimit (0, 2, ctx.proc.settings.textSize.load())];
        SeqGrid::toggleModifierRef() = ctx.proc.settings.toggleMod.load();
        sendLookAndFeelChange();
        const int sc = ctx.proc.settings.uiScale.load();
        if (sc != lastScale && onScaleChanged) { lastScale = sc; onScaleChanged(); }
        repaint();
    }
    std::function<void()> onScaleChanged;

    void timerCallback() override
    {
        diag::uiBeat().store (std::max<juce::uint32> (1, juce::Time::getMillisecondCounter()));
        diag::ui ("minuterie de l'interface");
        if (++undoTick % 10 == 0 && juce::Time::getMillisecondCounter() < uiActiveUntil && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
            ctx.proc.undoCommit();
        undoBtn.setEnabled (ctx.proc.canUndo() || juce::Time::getMillisecondCounter() < uiActiveUntil);
        redoBtn.setEnabled (ctx.proc.canRedo());
        if (page == 0) { if (editorOpen) editor.tick(); else { grid.tick(); lane.tick(); } fillBtn.repaint(); }
        else if (page == 1) capture.tick();
        else if (page == 3) mixPage.tick();
        repaint (lcdArea);
    }

    // Fond (façades, matières, sérigraphie) dessiné une fois dans une image, à la résolution de l'écran
    juce::Image bgCache;
    int bgKey = -1;
    float bgScale = 0.0f;

    void paint (juce::Graphics& g) override
    {
        diag::ui ("dessin de la fenetre");
        const float sc = std::max (1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
        const int key = ctx.proc.settings.theme.load() * 10 + (page == 0 ? 1 : 0);
        if (bgCache.isNull() || key != bgKey || std::abs (sc - bgScale) > 0.01f)
        {
            bgCache = juce::Image (juce::Image::RGB, juce::roundToInt (baseW * sc), juce::roundToInt (baseH * sc), true);
            juce::Graphics cg (bgCache);
            cg.addTransform (juce::AffineTransform::scale (sc));
            paintStatic (cg);
            bgKey = key; bgScale = sc;
        }
        g.drawImageTransformed (bgCache, juce::AffineTransform::scale (1.0f / sc));
        if (page == 1 || page == 3 || page == 4)
        {
            const auto& t = ctx.th();
            g.setColour (t.sub.withAlpha (0.3f));
            g.fillRect (subBar.getX() + 24, subBar.getBottom() - 1, subBar.getWidth() - 48, 1);
            g.setFont (font (11.0f, true)); g.setColour (t.sub);
            g.drawText (page == 3 ? juce::String::fromUTF8 ("MIX \xc2\xb7 ENVOIS, SORTIES ET CHOKE") : juce::String::fromUTF8 ("SONS \xc2\xb7 MACHINES, MES KITS ET CAPTURE"),
                        subBar.withTrimmedRight (24).withTrimmedLeft (400), juce::Justification::centredRight);
        }
        paintLcd (g);
    }

    void paintLcd (juce::Graphics& g)
    {
        const auto& t = ctx.th();
        g.setColour (t.lcdTx); g.setFont (mono (16.0f));
        auto lr = lcdArea.reduced (12, 0);
        if (page == 1)
        {
            auto& eng = ctx.proc.capture();
            g.drawText (eng.busy() ? juce::String::fromUTF8 ("CAPTURE \xc2\xb7 ") + juce::String (juce::roundToInt (eng.progress() * 100.0f)) + " %"
                                   : juce::String (juce::String::fromUTF8 ("CAPTURE \xc2\xb7 PR\xc3\x8aT")), lr, juce::Justification::centredLeft);
            return;
        }
        const float sw = ctx.proc.apvts.getRawParameterValue ("swing")->load();
        juce::String right = juce::String (ctx.proc.hostBpm(), 1) + "  SW" + juce::String (juce::roundToInt (sw * 100.0f)) + "  ";
        right += ctx.proc.hostPlaying() ? juce::String::fromUTF8 ("\xe2\x96\xb6 ") + juce::String (ctx.proc.playingStep() + 1).paddedLeft ('0', 2)
                                        : juce::String::fromUTF8 ("\xe2\x96\xa0 STOP");
        if (ctx.proc.fillActive()) right = "FILL  " + right;
        const int rw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), right)) + 6;
        g.drawText (right, lr.removeFromRight (rw), juce::Justification::centredRight);
        const juce::String leftText = page == 3 ? "MASTER " + juce::String (juce::Decibels::gainToDecibels (ctx.proc.masterLevel(), -60.0f), 1) + " dB"
                                                : ctx.proc.currentKitName().toUpperCase();
        g.drawFittedText (leftText, lr.withTrimmedRight (8), juce::Justification::centredLeft, 1, 0.7f);
    }

    void paintStatic (juce::Graphics& g)
    {
        const auto& t = ctx.th();
        g.fillAll (t.bg);
        paintGrain (g, getLocalBounds().toFloat(), t.grain * 0.5f);

        // --- en-tête : façade sérigraphiée ---
        auto head = juce::Rectangle<float> (0, 0, (float) baseW, (float) headerH);
        g.setColour (t.head); g.fillRect (head);
        paintGrain (g, head, t.grain);
        g.setColour (juce::Colours::white.withAlpha (0.25f)); g.drawLine (0, 1.0f, (float) baseW, 1.0f, 1.0f);
        g.setColour (t.acc); g.fillRect (head.removeFromBottom (3.0f));
        paintEmblem (g, { 22.0f, 14.0f, 46.0f, 46.0f }, t.headTx, t.acc);
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.addTransform (juce::AffineTransform::shear (-0.14f, 0.0f).translated (0.14f * 40.0f, 0.0f));
            auto lf = logoFont (27.0f); lf.setExtraKerningFactor (0.16f);
            g.setFont (lf); g.setColour (t.headTx);
            g.drawText ("TYPHON", 80, 12, 220, 34, juce::Justification::centredLeft);
        }
        auto sf = font (11.0f, true); sf.setExtraKerningFactor (0.22f);
        g.setFont (sf); g.setColour (t.headSub);
        g.drawText (juce::String::fromUTF8 ("TY-12 \xc2\xb7 FOUDRE AUDIO"), 82, 46, 220, 14, juce::Justification::centredLeft);
        // logement des onglets
        auto tabs = tabSeq.getBounds().getUnion (tabMix.getBounds()).toFloat().expanded (4.0f);
        paintInset (g, tabs, 9.0f, t.headTx.withAlpha (t.head.getPerceivedBrightness() > 0.5f ? 0.9f : 0.25f));

        // écran
        auto lcd = lcdArea.toFloat();
        paintInset (g, lcd, 6.0f, t.lcdBg);
        g.setColour (t.lcdTx.withAlpha (0.05f));
        for (float yy = lcd.getY() + 2.0f; yy < lcd.getBottom(); yy += 3.0f) g.drawHorizontalLine ((int) yy, lcd.getX() + 2.0f, lcd.getRight() - 2.0f);
        if (t.wood)
        {
            paintWood (g, { 0.0f, 0.0f, 10.0f, (float) baseH });
            paintWood (g, { (float) baseW - 10.0f, 0.0f, 10.0f, (float) baseH });
        }

        if (page == 0)
        {
            // --- façade SP GRAIN ---
            auto bot = juce::Rectangle<float> (0, (float) (baseH - bottomH), (float) baseW, (float) bottomH);
            g.setColour (t.bottom); g.fillRect (bot);
            paintGrain (g, bot, t.grain * 1.2f);
            g.setColour (t.acc); g.fillRect (bot.withHeight (3.0f));
            g.setColour (juce::Colours::white.withAlpha (0.2f)); g.drawLine (0, bot.getY() + 3.5f, (float) baseW, bot.getY() + 3.5f, 1.0f);
            const float inset = t.wood ? 22.0f : 12.0f;
            for (auto p : { juce::Point<float> (inset, bot.getY() + 16.0f), juce::Point<float> ((float) baseW - inset, bot.getY() + 16.0f),
                            juce::Point<float> (inset, bot.getBottom() - 12.0f), juce::Point<float> ((float) baseW - inset, bot.getBottom() - 12.0f) })
                paintScrew (g, p);
            if (t.wood) { paintWood (g, { 0.0f, bot.getY(), 10.0f, bot.getHeight() }); paintWood (g, { (float) baseW - 10.0f, bot.getY(), 10.0f, bot.getHeight() }); }

            // plaque SP GRAIN
            auto badge = badgeArea.toFloat();
            g.setColour (t.bottomTx); g.drawRoundedRectangle (badge, 8.0f, 2.0f);
            {
                auto lf = logoFont (17.0f); lf.setExtraKerningFactor (0.14f);
                g.setFont (lf);
                g.drawText ("SP GRAIN", badge.reduced (14.0f, 0.0f).withTrimmedTop (12.0f).withHeight (24.0f), juce::Justification::centredLeft);
            }
            auto bf = font (9.5f, true); bf.setExtraKerningFactor (0.08f);
            g.setFont (bf); g.setColour (t.bottomTx.withAlpha (0.7f));
            g.drawFittedText (juce::String::fromUTF8 ("12-BIT \xc2\xb7 26.04 kHz \xc2\xb7 SANS FILTRE ANTI-REPLIEMENT"),
                              badge.reduced (14.0f, 0.0f).withTrimmedTop (40.0f).withHeight (26.0f).toNearestInt(), juce::Justification::topLeft, 2);
            // filets sérigraphiés entre les groupes
            g.setColour (t.bottomTx.withAlpha (0.35f));
            for (auto& sp : { sep1, sep2, sep3 }) g.fillRect (sp);
            auto gf = font (10.0f, true); gf.setExtraKerningFactor (0.2f);
            g.setFont (gf); g.setColour (t.bottomTx.withAlpha (0.6f));
            g.drawText (juce::String::fromUTF8 ("GRAIN 12 BITS"), grpLabel1, juce::Justification::centred);
            g.drawText (juce::String::fromUTF8 ("GROOVE \xc2\xb7 SORTIE"), grpLabel2, juce::Justification::centred);
            g.drawText (juce::String::fromUTF8 ("FILL \xc2\xb7 PERFORMANCE"), grpLabel3.withWidth (grpLabel3.getWidth() + 200), juce::Justification::centred);
        }
    }

    void resized() override
    {
        // en-tête
        tabSeq.setBounds (300, 18, 116, 38);
        tabLib.setBounds (420, 18, 80, 38);
        tabMix.setBounds (504, 18, 68, 38);
        settingsBtn.setBounds (baseW - 24 - 44, 18, 44, 38);
        redoBtn.setBounds (settingsBtn.getX() - 14 - 42, 18, 42, 38);
        undoBtn.setBounds (redoBtn.getX() - 6 - 42, 18, 42, 38);
        lcdArea = { 594, 16, undoBtn.getX() - 12 - 594, 42 };

        auto body = juce::Rectangle<int> (0, headerH, baseW, baseH - headerH);
        settingsPage.setBounds (body);
        // sous-barre des pages SONS (bibliothèque / capture) et MIX (choke global)
        auto sub = body.removeFromTop (46);
        subBar = sub;
        subLib.setBounds (24, sub.getY() + 10, 150, 30);
        tabCap.setBounds (180, sub.getY() + 10, 190, 30);
        chokeAllBtn.setBounds (24, sub.getY() + 10, 150, 30);
        capture.setBounds (body);
        mixPage.setBounds (body);
        libPage.setBounds (body);

        auto main = juce::Rectangle<int> (24, headerH + 16, baseW - 48, baseH - headerH - bottomH - 28);
        editor.setBounds (main);
        if (ctx.proc.settings.panelOpen.load() == 1)
        {
            auto lr = main.removeFromRight (340);
            lane.setBounds (lr);
            main.removeFromRight (18);
            panelBtn.setBounds (main.getRight() - 108, main.getY(), 108, 30);
        }
        else
        {
            panelTab.setBounds (main.removeFromRight (34));
            panelTab.text = juce::String::fromUTF8 ("\xe2\x97\x82  ") + typhon::laneName (lane.lane).toUpperCase() + juce::String::fromUTF8 ("  \xc2\xb7  PISTE");
            main.removeFromRight (14);
        }
        grid.setBounds (main);

        // façade du bas
        auto bot = juce::Rectangle<int> (34, baseH - bottomH + 14, baseW - 68, bottomH - 24);
        badgeArea = bot.removeFromLeft (196).withSizeKeepingCentre (196, 112).translated (0, -4);
        spAuth.setBounds (badgeArea.getX() + 14, badgeArea.getBottom() - 32, 92, 20);
        spClean.setBounds (spAuth.getRight() + 6, spAuth.getY(), 64, 20);
        bot.removeFromLeft (14);
        auto knobRow = [&] (int from, int to, int w)
        {
            auto area = bot.removeFromLeft ((to - from) * w);
            for (int i = from; i < to; ++i) masterKnobs[i]->setBounds (area.removeFromLeft (w).withTrimmedTop (16));
            return area;
        };
        auto g1 = bot; knobRow (0, 4, 86); grpLabel1 = g1.withWidth (4 * 86).withHeight (14);
        bot.removeFromLeft (10); sep1 = bot.removeFromLeft (2).toFloat().withTrimmedTop (10).withTrimmedBottom (10); bot.removeFromLeft (10);
        auto g2 = bot; knobRow (4, 6, 86); grpLabel2 = g2.withWidth (2 * 86).withHeight (14);
        bot.removeFromLeft (10); sep2 = bot.removeFromLeft (2).toFloat().withTrimmedTop (10).withTrimmedBottom (10); bot.removeFromLeft (14);
        auto fillCol = bot.removeFromLeft (100);
        grpLabel3 = fillCol.withHeight (14).withWidth (100 + 80);
        fillCol.removeFromTop (16);
        fillBtn.setBounds (fillCol.removeFromTop (56));
        fillCol.removeFromTop (6);
        fillAuto.setBounds (fillCol.removeFromTop (26));
        fillCol.removeFromTop (5);
        fillSet.setBounds (fillCol.removeFromTop (24));
        masterKnobs[6]->setBounds (bot.removeFromLeft (80).withTrimmedTop (16));
        bot.removeFromLeft (8);
        sep3 = bot.removeFromLeft (2).toFloat().withTrimmedTop (10).withTrimmedBottom (10);
        bot.removeFromLeft (14);
        auto perf = bot.withSizeKeepingCentre (bot.getWidth(), 78);
        rollBtn.setBounds (perf.removeFromLeft (72)); perf.removeFromLeft (6);
        loopBtn.setBounds (perf.removeFromLeft (76)); perf.removeFromLeft (8);
        dragBtn.setBounds (perf);
    }

    Ctx ctx;
    Look look;
    SeqGrid grid;
    LanePanel lane;
    CapturePage capture;
    SettingsPage settingsPage;
    MixPage mixPage;
    LibraryPage libPage;
    SampleEditor editor;
    Pill tabSeq, tabCap, subLib, tabMix, tabLib, fillSet, chokeAllBtn, undoBtn, redoBtn, settingsBtn;
    int sonsPage = 4;
    juce::Rectangle<int> subBar;
    juce::uint32 uiActiveUntil = 0;
    int undoTick = 0;
    juce::SharedResourcePointer<juce::TooltipWindow> tooltips;
    bool editorOpen = false;
    DragMidiButton dragBtn;
    FillButton fillBtn;
    Pill spAuth, spClean, panelBtn;
    PanelTab panelTab;
    HoldButton rollBtn, loopBtn;
    int rollRate = 2, loopSize = 2;
    juce::OwnedArray<Knob> masterKnobs;
    juce::ComboBox fillAuto;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fillAutoAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chokeAllAtt;
    juce::Rectangle<int> lcdArea, badgeArea, grpLabel1, grpLabel2, grpLabel3;
    juce::Rectangle<float> sep1, sep2, sep3;
    int page = 0;
    int lastScale = -1;
};

// ============================================================================
TyphonEditor::TyphonEditor (TyphonProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    ui = std::make_unique<Ui> (p);
    ui->onScaleChanged = [this] { applyScaleSetting(); };
    ui->lastScale = p.settings.uiScale.load();
    addAndMakeVisible (*ui);
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseW / baseH);
    setResizeLimits (baseW / 2, baseH / 2, baseW * 5 / 2, baseH * 5 / 2);
    applyScaleSetting();
}

TyphonEditor::~TyphonEditor() = default;

void TyphonEditor::applyScaleSetting()
{
    const float scales[6] = { 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f };
    const float s = scales[juce::jlimit (0, 5, proc.settings.uiScale.load())];
    setSize ((int) (baseW * s), (int) (baseH * s));
}

void TyphonEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

void TyphonEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    ui->setTransform (juce::AffineTransform::scale (s));
    ui->setBounds (0, 0, baseW, baseH);
}
