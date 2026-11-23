#include "PluginEditor.h"
#include "UiWidgets.h"
#include "UiSeq.h"
#include "UiLane.h"
#include "UiCapture.h"
#include "UiSettings.h"

using namespace tui;

static constexpr int baseW = 1280, baseH = 800;
static constexpr int headerH = 76, bottomH = 168;

// ============================================================================
//  Bouton « glisser le pattern dans Ableton »
// ============================================================================
class DragMidiButton : public juce::Component
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
        g.drawFittedText (juce::String::fromUTF8 ("\xe2\x87\xa9 GLISSER LE PATTERN DANS ABLETON"), getLocalBounds().reduced (8, 0), juce::Justification::centred, 2);
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

// ============================================================================
struct TyphonEditor::Ui : public juce::Component, private juce::Timer
{
    Ui (TyphonProcessor& p)
        : ctx { p }, look (ctx), grid (ctx), lane (ctx), capture (ctx), settingsPage (ctx),
          tabSeq (ctx, juce::String::fromUTF8 ("S\xc3\x89QUENCEUR")), tabCap (ctx, "CAPTURE"),
          chokeAllBtn (ctx, "CHOKE GLOBAL"), settingsBtn (ctx, juce::String::fromUTF8 ("PARAM\xc3\x88TRES")),
          dragBtn (ctx), fillBtn (ctx)
    {
        setLookAndFeel (&look);
        addAndMakeVisible (grid);
        addAndMakeVisible (lane);
        addChildComponent (capture);
        addChildComponent (settingsPage);
        for (auto* b : { &tabSeq, &tabCap, &settingsBtn, &chokeAllBtn }) { addAndMakeVisible (*b); b->textSize = 13.0f; }
        tabSeq.isOn = [this] { return page == 0; };
        tabCap.isOn = [this] { return page == 1; };
        settingsBtn.isOn = [this] { return page == 2; };
        tabSeq.onClick = [this] { setPage (0); };
        tabCap.onClick = [this] { setPage (1); };
        settingsBtn.onClick = [this] { setPage (page == 2 ? 0 : 2); };
        chokeAllBtn.setClickingTogglesState (true);
        chokeAllAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, "choke_all", chokeAllBtn);
        settingsPage.onClose = [this] { setPage (0); };
        settingsPage.onChanged = [this] { applySettings(); };

        grid.onSelectLane = [this] (int l) { lane.setLane (l); };
        grid.selected = 1;

        for (int i = 0; i < typhon::numLanes; ++i)
        {
            auto* s = faders.add (new juce::Slider());
            s->setSliderStyle (juce::Slider::LinearVertical);
            s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            faderAtts.add (new juce::AudioProcessorValueTreeState::SliderAttachment (p.apvts, typhon::pid (i, "vol"), *s));
            s->setDoubleClickReturnValue (true, 0.0);
            addAndMakeVisible (s);
        }
        struct K { const char* label; const char* id; std::function<juce::String (float)> fmt; };
        const K ks[] = {
            { "Swing", "swing", [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; } },
            { "Grain SP", "sp_amt", fmtPct },
            { "Bits", "sp_bits", [] (float v) { return juce::String (juce::roundToInt (v)) + " bits"; } },
            { juce::CharPointer_UTF8 ("Fr\xc3\xa9quence") .getAddress(), "sp_rate", [] (float v) { return juce::String (v / 1000.0f, 1) + " kHz"; } },
            { "Filtre SP", "sp_filt", [] (float v) { return v < 0.005f ? juce::String ("Off") : fmtPct (v); } },
            { "Master", "master", fmtDb },
            { juce::CharPointer_UTF8 ("Intensit\xc3\xa9").getAddress(), "fill_int", fmtPct }
        };
        for (auto& k : ks)
        {
            auto* kn = masterKnobs.add (new Knob (ctx, juce::String::fromUTF8 (k.label)));
            kn->attach (k.id);
            kn->formatter = k.fmt;
            kn->onBottom = true;
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
        stopTimer();
        setLookAndFeel (nullptr);
    }

    void setPage (int pg)
    {
        page = pg;
        const bool seq = pg == 0;
        grid.setVisible (seq); lane.setVisible (seq);
        for (auto* f : faders) f->setVisible (seq);
        for (auto* k : masterKnobs) k->setVisible (seq);
        fillAuto.setVisible (seq); dragBtn.setVisible (seq); fillBtn.setVisible (seq);
        capture.setVisible (pg == 1);
        settingsPage.setVisible (pg == 2);
        if (pg == 1) capture.refresh();
        resized();
        repaint();
    }

    void applySettings()
    {
        sendLookAndFeelChange();
        const int sc = ctx.proc.settings.uiScale.load();
        if (sc != lastScale && onScaleChanged) { lastScale = sc; onScaleChanged(); }
        repaint();
    }
    std::function<void()> onScaleChanged;

    void timerCallback() override
    {
        if (page == 0) { grid.tick(); lane.tick(); fillBtn.repaint(); }
        else if (page == 1) capture.tick();
        repaint (0, 0, baseW, headerH);
        if (page == 0) repaint (0, baseH - bottomH, baseW, bottomH);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& t = ctx.th();
        g.fillAll (t.bg);
        // en-tête
        auto head = juce::Rectangle<int> (0, 0, baseW, headerH);
        g.setColour (t.head); g.fillRect (head);
        g.setColour (t.acc); g.fillRect (head.removeFromBottom (4));
        g.setColour (t.headTx); g.setFont (font (34.0f, true));
        g.drawText ("TYPHON", 24, 8, 220, 40, juce::Justification::centredLeft);
        g.setColour (t.headSub); g.setFont (font (11.0f, true));
        g.drawText (juce::String::fromUTF8 ("FOUDRE AUDIO \xc2\xb7 DRUM MACHINE"), 24, 46, 240, 16, juce::Justification::centredLeft);

        // écran
        auto lcd = lcdArea.toFloat();
        g.setColour (t.lcdBg); g.fillRoundedRectangle (lcd, 6.0f);
        g.setColour (t.lcdTx); g.setFont (mono (14.0f));
        auto lr = lcdArea.reduced (14, 0);
        if (page == 1)
        {
            auto& eng = ctx.proc.capture();
            g.drawText (eng.busy() ? juce::String::fromUTF8 ("CAPTURE \xc2\xb7 ") + juce::String (juce::roundToInt (eng.progress() * 100.0f)) + " %"
                                   : juce::String (juce::String::fromUTF8 ("CAPTURE \xc2\xb7 PR\xc3\x8aT")), lr, juce::Justification::centredLeft);
        }
        else
        {
            g.drawText (juce::String::fromUTF8 ("KIT \xe2\x96\xb8 ") + ctx.proc.currentKitName().toUpperCase(), lr, juce::Justification::centredLeft);
            const float sw = ctx.proc.apvts.getRawParameterValue ("swing")->load();
            juce::String right = juce::String ("SWING ") + juce::String (juce::roundToInt (sw * 100.0f)) + " %   ";
            right += ctx.proc.hostPlaying() ? juce::String::fromUTF8 ("\xe2\x96\xb6 PAS ") + juce::String (ctx.proc.playingStep() + 1)
                                            : juce::String::fromUTF8 ("\xe2\x96\xa0 STOP");
            if (ctx.proc.fillActive()) right = "FILL   " + right;
            g.drawText (right, lr, juce::Justification::centredRight);
        }

        if (page == 0)
        {
            // barre du bas
            auto bot = juce::Rectangle<int> (0, baseH - bottomH, baseW, bottomH);
            g.setColour (t.bottom); g.fillRect (bot);
            g.setColour (t.bottomTx); g.setFont (font (11.0f, true));
            for (int i = 0; i < typhon::numLanes; ++i)
            {
                auto fr = faders[i]->getBounds();
                g.drawFittedText (typhon::laneShortName (i), fr.getX() - 6, fr.getBottom() + 4, fr.getWidth() + 12, 14, juce::Justification::centred, 1);
                const float m = ctx.proc.laneMeter (i);
                const float h = juce::jlimit (0.0f, 1.0f, std::sqrt (m)) * (float) fr.getHeight();
                g.setColour (t.acc.withAlpha (0.8f));
                g.fillRect ((float) fr.getRight() - 3.0f, (float) fr.getBottom() - h, 3.0f, h);
                g.setColour (t.bottomTx);
            }
            g.setColour (t.bottomTx.withAlpha (0.3f));
            g.fillRect (sep1); g.fillRect (sep2);
            g.setColour (t.bottomTx.withAlpha (0.8f)); g.setFont (font (11.0f));
            g.drawFittedText (juce::String::fromUTF8 ("Clips MIDI : Kick C1, Snare D1, Ghost D#1, Rim C#1, Hat F#1 / A#1, Crash C#2, Perc E1 \xc2\xb7 C0 coupe tout"),
                              midiHint, juce::Justification::centredLeft, 2);
        }
    }

    void resized() override
    {
        // en-tête
        tabSeq.setBounds (268, 16, 150, 40);
        tabCap.setBounds (424, 16, 120, 40);
        settingsBtn.setBounds (baseW - 24 - 140, 18, 140, 36);
        chokeAllBtn.setBounds (settingsBtn.getX() - 10 - 140, 18, 140, 36);
        lcdArea = { 560, 14, chokeAllBtn.getX() - 16 - 560, 44 };

        auto body = juce::Rectangle<int> (0, headerH, baseW, baseH - headerH);
        capture.setBounds (body);
        settingsPage.setBounds (body);

        auto main = juce::Rectangle<int> (24, headerH + 18, baseW - 48, baseH - headerH - bottomH - 30);
        lane.setBounds (main.removeFromRight (340));
        main.removeFromRight (20);
        grid.setBounds (main);

        // barre du bas
        auto bot = juce::Rectangle<int> (24, baseH - bottomH + 16, baseW - 48, bottomH - 32);
        for (int i = 0; i < typhon::numLanes; ++i)
        {
            auto c = bot.removeFromLeft (50);
            faders[i]->setBounds (c.withTrimmedBottom (20).withSizeKeepingCentre (40, c.getHeight() - 22));
        }
        bot.removeFromLeft (14);
        sep1 = bot.removeFromLeft (2).toFloat(); bot.removeFromLeft (14);
        for (int i = 0; i < 6; ++i) masterKnobs[i]->setBounds (bot.removeFromLeft (66).withSizeKeepingCentre (66, 96));
        bot.removeFromLeft (10);
        sep2 = bot.removeFromLeft (2).toFloat(); bot.removeFromLeft (14);
        auto fillCol = bot.removeFromLeft (96);
        fillBtn.setBounds (fillCol.removeFromTop (62));
        fillCol.removeFromTop (8);
        fillAuto.setBounds (fillCol.removeFromTop (28));
        masterKnobs[6]->setBounds (bot.removeFromLeft (70).withSizeKeepingCentre (70, 96));
        bot.removeFromLeft (10);
        dragBtn.setBounds (bot.removeFromTop (54));
        bot.removeFromTop (8);
        midiHint = bot;
    }

    Ctx ctx;
    Look look;
    SeqGrid grid;
    LanePanel lane;
    CapturePage capture;
    SettingsPage settingsPage;
    Pill tabSeq, tabCap, chokeAllBtn, settingsBtn;
    DragMidiButton dragBtn;
    FillButton fillBtn;
    juce::OwnedArray<juce::Slider> faders;
    juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> faderAtts;
    juce::OwnedArray<Knob> masterKnobs;
    juce::ComboBox fillAuto;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> fillAutoAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chokeAllAtt;
    juce::Rectangle<int> lcdArea, midiHint;
    juce::Rectangle<float> sep1, sep2;
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
    setResizeLimits (baseW / 2, baseH / 2, baseW * 2, baseH * 2);
    applyScaleSetting();
}

TyphonEditor::~TyphonEditor() = default;

void TyphonEditor::applyScaleSetting()
{
    const float scales[4] = { 0.75f, 1.0f, 1.25f, 1.5f };
    const float s = scales[juce::jlimit (0, 3, proc.settings.uiScale.load())];
    setSize ((int) (baseW * s), (int) (baseH * s));
}

void TyphonEditor::paint (juce::Graphics& g) { g.fillAll (juce::Colours::black); }

void TyphonEditor::resized()
{
    const float s = (float) getWidth() / (float) baseW;
    ui->setTransform (juce::AffineTransform::scale (s));
    ui->setBounds (0, 0, baseW, baseH);
}
