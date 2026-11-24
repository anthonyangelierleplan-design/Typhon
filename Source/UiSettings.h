#pragma once
// ============================================================================
//  Page PARAMÈTRES (réglages globaux, gardés pour tous les projets)
// ============================================================================
#include "UiWidgets.h"

namespace tui
{
    class SettingsPage : public juce::Component, private juce::Timer
    {
    public:
        std::function<void()> onChanged, onClose;

        SettingsPage (Ctx& c) : ctx (c), closeBtn (c, "FERMER")
        {
            auto& S = ctx.proc.settings;
            addCard ("QUALIT\xc3\x89 DU MOTEUR",
                     juce::String::fromUTF8 ("Interpolation du pitch : lin\xc3\xa9" "aire (\xc3\x89" "co), Hermite (Haute) ou sinc 16 points (Ultra). En rendu, Typhon peut passer en Ultra tout seul."),
                     { { "Qualit\xc3\xa9 en lecture", { "\xc3\x89" "co", "Haute", "Ultra" }, &S.quality },
                       { "Qualit\xc3\xa9 au rendu (export)", { "Comme la lecture", "Ultra" }, &S.renderQuality } });
            addCard ("CARACT\xc3\x88RE SP-1200",
                     juce::String::fromUTF8 ("Authentique : maintien sans filtre anti-repliement, comme la machine. Propre : m\xc3\xaame grain, aigus moins agressifs. Le dosage se r\xc3\xa8gle avec Grain SP."),
                     { { "Mode", { "Authentique", "Propre" }, &S.spMode } });
            addCard ("CAPTURE",
                     juce::String::fromUTF8 ("Format des kits captur\xc3\xa9s depuis EZdrummer. Les samples sont charg\xc3\xa9s en m\xc3\xa9moire : z\xc3\xa9ro latence."),
                     { { "Format", { "48 kHz / 24 bits", "96 kHz / 32 bits" }, &S.captureRate } });
            addCard ("SORTIES",
                     juce::String::fromUTF8 ("Multi-out : chaque piste sort sur sa sortie (Kick 1/2, Snare 3/4...). Dans Ableton, cr\xc3\xa9" "e une piste audio par sortie, entr\xc3\xa9" "e = Typhon. Une sortie non branch\xc3\xa9" "e revient sur Main."),
                     { { "Routage", { "St\xc3\xa9r\xc3\xa9o", "Multi-out" }, &S.multiOut } });
            addCard ("MIDI",
                     juce::String::fromUTF8 ("Notes qui jouent les pistes depuis tes clips ou ton clavier. C0 coupe tous les sons."),
                     { { "Carte des notes", { "Typhon", "General MIDI", "EZdrummer" }, &S.midiMap },
                       { "Canal d'entr\xc3\xa9" "e", { "Omni", "10" }, &S.midiChannel, { 0, 10 } } });
            addCard ("AKAI APC64",
                     juce::String::fromUTF8 ("Typhon pilote l'APC64 en direct : la grille 8\xc3\x97" "8 devient le s\xc3\xa9quenceur. Dans Ableton, mets la surface de contr\xc3\xb4le APC64 sur \xc2\xab Aucun \xc2\xbb. Repasse sur Off pour la rendre \xc3\xa0 Ableton."),
                     { { "Piloter l'APC64", { "Off", "On" }, &S.apcOn } });
            cards.back().status = [this] { return ctx.proc.apcStatus(); };
            addCard ("INTERFACE", {},
                     { { "Th\xc3\xa8me", { "Atelier", "Nuit", "Phosph.", "Bel Air", "808", "Linn" }, &S.theme },
                       { "Taille de la fen\xc3\xaatre", { "75 %", "100 %", "125 %", "150 %" }, &S.uiScale },
                       { "Cran magn\xc3\xa9tique au z\xc3\xa9ro", { "Off", "On" }, &S.snapZero } });
            startTimerHz (2);
            addAndMakeVisible (closeBtn);
            closeBtn.strong = true;
            closeBtn.textSize = 14.0f;
            closeBtn.onClick = [this] { if (onClose) onClose(); };
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            g.fillAll (t.bg);
            g.setColour (t.tx); g.setFont (font (22.0f, true));
            g.drawText (juce::String::fromUTF8 ("PARAM\xc3\x88TRES"), getLocalBounds().reduced (24, 0).removeFromTop (60), juce::Justification::centredLeft);
            for (auto& c : cards)
            {
                g.setColour (t.panel); g.fillRoundedRectangle (c.area.toFloat(), 10.0f);
                g.setColour (t.sub); g.setFont (font (12.0f, true));
                g.drawText (c.title, c.area.reduced (16, 12).removeFromTop (16), juce::Justification::centredLeft);
                g.setFont (font (13.0f)); g.setColour (t.tx);
                for (auto& row : c.rows) g.drawText (row.labelText, row.labelArea, juce::Justification::centredLeft);
                g.setColour (t.sub); g.setFont (font (12.0f));
                g.drawFittedText (c.note, c.noteArea, juce::Justification::topLeft, 5);
                if (c.status)
                {
                    g.setColour (valueColour (t)); g.setFont (font (12.0f, true));
                    g.drawFittedText (c.status(), c.noteArea.withTrimmedTop (c.noteArea.getHeight() - 48), juce::Justification::bottomLeft, 3);
                    g.setColour (t.sub); g.setFont (font (12.0f));
                }
            }
        }

        void timerCallback() override { if (isShowing()) repaint(); }

        void resized() override
        {
            auto r = getLocalBounds().reduced (24, 0);
            auto top = r.removeFromTop (60);
            closeBtn.setBounds (top.removeFromRight (140).withSizeKeepingCentre (140, 40));
            r.removeFromBottom (20);
            const int cols = cards.size() > 6 ? 4 : 3;
            const int cw = (r.getWidth() - (cols - 1) * 16) / cols, ch = (r.getHeight() - 16) / 2;
            for (size_t i = 0; i < cards.size(); ++i)
            {
                auto& c = cards[i];
                c.area = { r.getX() + (int) (i % (size_t) cols) * (cw + 16), r.getY() + (int) (i / (size_t) cols) * (ch + 16), cw, ch };
                auto in = c.area.reduced (16, 12);
                in.removeFromTop (24);
                for (auto& row : c.rows)
                {
                    row.labelArea = in.removeFromTop (20);
                    auto br = in.removeFromTop (34);
                    const int n = (int) row.buttons.size();
                    const int bw = (br.getWidth() - (n - 1) * 6) / n;
                    for (auto* b : row.buttons) { b->setBounds (br.removeFromLeft (bw)); br.removeFromLeft (6); }
                    in.removeFromTop (10);
                }
                c.noteArea = in;
            }
        }

    private:
        struct RowDef { const char* label; std::vector<const char*> opts; std::atomic<int>* target; std::vector<int> values {}; };
        struct Row { juce::String labelText; juce::Rectangle<int> labelArea; std::vector<Pill*> buttons; };
        struct Card { juce::String title, note; std::vector<Row> rows; juce::Rectangle<int> area, noteArea; std::function<juce::String()> status; };

        void addCard (const char* title, const juce::String& note, std::vector<RowDef> defs)
        {
            Card c; c.title = juce::String::fromUTF8 (title); c.note = note;
            for (auto& d : defs)
            {
                Row row; row.labelText = juce::String::fromUTF8 (d.label);
                for (size_t k = 0; k < d.opts.size(); ++k)
                {
                    const int value = d.values.empty() ? (int) k : d.values[k];
                    auto* b = pills.add (new Pill (ctx, juce::String::fromUTF8 (d.opts[k])));
                    auto* target = d.target;
                    b->isOn = [target, value] { return target->load() == value; };
                    b->onClick = [this, target, value]
                    {
                        target->store (value);
                        ctx.proc.settings.save();
                        if (onChanged) onChanged();
                        repaint();
                    };
                    addAndMakeVisible (b);
                    row.buttons.push_back (b);
                }
                c.rows.push_back (row);
            }
            cards.push_back (c);
        }

        Ctx& ctx;
        juce::OwnedArray<Pill> pills;
        std::vector<Card> cards;
        Pill closeBtn;
    };
}
