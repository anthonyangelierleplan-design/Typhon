#pragma once
// ============================================================================
//  Briques d'interface : apparence, boutons pilule, potards, faders
// ============================================================================
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Shared.h"
#include "FontData.h"

namespace tui
{
    using typhon::Theme;

    // Polices embarquées : Saira Semi Condensed (sérigraphie), Share Tech Mono (écran), Michroma (logo, titres)
    struct Typefaces
    {
        juce::Typeface::Ptr medium, bold, lcd, logo;
        Typefaces()
        {
            medium = juce::Typeface::createSystemTypefaceFor (fontdata::sairaMedium, (size_t) fontdata::sairaMediumSize);
            bold = juce::Typeface::createSystemTypefaceFor (fontdata::sairaBold, (size_t) fontdata::sairaBoldSize);
            lcd = juce::Typeface::createSystemTypefaceFor (fontdata::shareTechMono, (size_t) fontdata::shareTechMonoSize);
            logo = juce::Typeface::createSystemTypefaceFor (fontdata::michroma, (size_t) fontdata::michromaSize);
        }
    };
    inline const Typefaces& faces() { static Typefaces t; return t; }
    inline juce::Font withFace (juce::Typeface::Ptr tf, float size, juce::Font fallback)
    {
        return tf != nullptr ? juce::Font (juce::FontOptions (tf).withHeight (size)) : fallback;
    }
    inline juce::Font font (float size, bool bold = false)
    {
        return withFace (bold ? faces().bold : faces().medium, size * 1.24f, juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)));
    }
    inline juce::Font mono (float size, bool bold = false)
    {
        juce::ignoreUnused (bold);
        return withFace (faces().lcd, size * 1.1f, juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain)));
    }
    inline juce::Font logoFont (float size)
    {
        return withFace (faces().logo, size, font (size, true));
    }

    // Couleur des valeurs chiffrées : vert écran sur fond sombre, couleur d'en-tête sur fond clair
    inline juce::Colour valueColour (const typhon::Theme& t)
    {
        return t.panel.getPerceivedBrightness() > 0.5f ? t.head : t.lcdTx;
    }

    // Accès au thème courant
    struct Ctx
    {
        TyphonProcessor& proc;
        const Theme& th() const { return typhon::theme (proc.settings.theme.load()); }
        bool snap() const { return proc.settings.snapZero.load() != 0; }
    };

    // ------------------------------------------------------------------------
    class Look : public juce::LookAndFeel_V4
    {
    public:
        explicit Look (Ctx& c) : ctx (c) {}

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                               float a0, float a1, juce::Slider& s) override
        {
            const auto& t = ctx.th();
            if (s.getProperties()["hw"]) { drawHardwareKnob (g, x, y, w, h, pos, a0, a1, s); return; }
            auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
            const float d = std::min (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (d, d);
            const float ang = a0 + pos * (a1 - a0);

            // arc depuis le zéro (ou le minimum) jusqu'à la valeur
            float zeroPos = 0.0f;
            if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0) zeroPos = (float) s.valueToProportionOfLength (0.0);
            const float az = a0 + zeroPos * (a1 - a0);
            juce::Path bg; bg.addCentredArc (r.getCentreX(), r.getCentreY(), d * 0.5f, d * 0.5f, 0.0f, a0, a1, true);
            g.setColour (t.knobEdge.withAlpha (0.6f));
            g.strokePath (bg, juce::PathStrokeType (2.5f));
            if (std::fabs (ang - az) > 0.01f)
            {
                juce::Path arc; arc.addCentredArc (r.getCentreX(), r.getCentreY(), d * 0.5f, d * 0.5f, 0.0f, std::min (az, ang), std::max (az, ang), true);
                g.setColour (t.acc);
                g.strokePath (arc, juce::PathStrokeType (2.5f));
            }
            auto body = r.reduced (5.0f);
            g.setColour (t.knob);
            g.fillEllipse (body);
            g.setColour (t.knobEdge);
            g.drawEllipse (body, 1.5f);
            juce::Path needle;
            const float rad = body.getWidth() * 0.5f;
            needle.addRoundedRectangle (-1.5f, -rad + 3.0f, 3.0f, rad * 0.45f, 1.5f);
            g.setColour (t.acc);
            g.fillPath (needle, juce::AffineTransform::rotation (ang).translated (body.getCentreX(), body.getCentreY()));
        }

        // Potard de façade : graduations sérigraphiées, capuchon bombé, trait d'index
        void drawHardwareKnob (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
        {
            const auto& t = ctx.th();
            auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
            const float d = std::min (r.getWidth(), r.getHeight());
            r = r.withSizeKeepingCentre (d, d);
            const auto c = r.getCentre();
            const float R = d * 0.5f;
            // graduations
            g.setColour (t.bottomTx.withAlpha (0.85f));
            for (int k = 0; k <= 10; ++k)
            {
                const float a = a0 + (a1 - a0) * (float) k / 10.0f;
                const float r0 = R - (k % 5 == 0 ? 7.0f : 5.0f), r1 = R - 1.0f;
                g.drawLine (c.x + r0 * std::sin (a), c.y - r0 * std::cos (a), c.x + r1 * std::sin (a), c.y - r1 * std::cos (a), k % 5 == 0 ? 2.0f : 1.2f);
            }
            // repère du zéro pour les réglages bipolaires
            if (s.getMinimum() < 0.0 && s.getMaximum() > 0.0)
            {
                const float az = a0 + (float) s.valueToProportionOfLength (0.0) * (a1 - a0);
                g.setColour (t.acc);
                g.fillEllipse (c.x + (R - 2.0f) * std::sin (az) - 2.0f, c.y - (R - 2.0f) * std::cos (az) - 2.0f, 4.0f, 4.0f);
            }
            // ombre portée + capuchon
            auto cap = r.reduced (d * 0.17f);
            g.setColour (juce::Colours::black.withAlpha (0.35f));
            g.fillEllipse (cap.translated (0.0f, 3.0f).expanded (1.0f));
            const auto base = t.knob;
            juce::ColourGradient grad (base.brighter (0.55f), cap.getX() + cap.getWidth() * 0.3f, cap.getY() + cap.getHeight() * 0.25f,
                                       base.darker (0.35f), cap.getRight(), cap.getBottom(), true);
            g.setGradientFill (grad);
            g.fillEllipse (cap);
            g.setColour (t.bottomTx.withAlpha (0.9f));
            g.drawEllipse (cap, 1.6f);
            // index
            const float ang = a0 + pos * (a1 - a0);
            const float cr = cap.getWidth() * 0.5f;
            juce::Path needle;
            needle.addRoundedRectangle (-2.0f, -cr + 3.0f, 4.0f, cr * 0.55f, 2.0f);
            g.setColour (base.getPerceivedBrightness() > 0.6f ? juce::Colour (0xff1a1a1a) : t.acc);
            g.fillPath (needle, juce::AffineTransform::rotation (ang).translated (c.x, c.y));
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float,
                               juce::Slider::SliderStyle style, juce::Slider& s) override
        {
            const auto& t = ctx.th();
            if (style != juce::Slider::LinearVertical) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, s); return; }
            const float cx = x + w * 0.5f;
            g.setColour (t.bottomTx.withAlpha (0.85f));
            g.fillRoundedRectangle (cx - 5.0f, (float) y, 10.0f, (float) h, 5.0f);
            const float capH = 14.0f;
            auto cap = juce::Rectangle<float> (cx - 19.0f, sliderPos - capH * 0.5f, 38.0f, capH);
            g.setColour (t.knob.darker (0.2f));
            g.fillRoundedRectangle (cap, 3.0f);
            g.setColour (t.acc);
            g.fillRect (cap.withHeight (3.0f));
        }

        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
        {
            const auto& t = ctx.th();
            auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
            g.setColour (t.bg); g.fillRoundedRectangle (r, 6.0f);
            g.setColour (t.knobEdge); g.drawRoundedRectangle (r, 6.0f, 1.0f);
            juce::Path p; const float ax = w - 14.0f, ay = h * 0.5f;
            p.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
            g.setColour (t.sub); g.fillPath (p);
        }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
        void positionComboBoxText (juce::ComboBox& b, juce::Label& l) override
        {
            l.setBounds (8, 0, b.getWidth() - 24, b.getHeight());
            l.setFont (getComboBoxFont (b));
            l.setColour (juce::Label::textColourId, ctx.th().tx);
        }
        juce::Font getPopupMenuFont() override { return font (14.0f); }
        void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
        {
            g.fillAll (ctx.th().panel2);
            g.setColour (ctx.th().knobEdge); g.drawRect (0, 0, w, h);
        }
        void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcut,
                                const juce::Drawable* icon, const juce::Colour* textColour) override
        {
            const auto& t = ctx.th();
            if (isSeparator) { g.setColour (t.knobEdge); g.fillRect (area.reduced (6, 0).withHeight (1).withY (area.getCentreY())); return; }
            auto r = area.reduced (3, 1);
            if (isHighlighted && isActive) { g.setColour (t.acc); g.fillRoundedRectangle (r.toFloat(), 4.0f); }
            g.setColour (isHighlighted && isActive ? t.bg : (isActive ? t.tx : t.sub));
            g.setFont (font (14.0f, isTicked));
            auto tr = r.reduced (10, 0);
            if (isTicked) { g.drawText (juce::String::fromUTF8 ("\xe2\x9c\x93"), tr.removeFromLeft (16), juce::Justification::centredLeft); }
            else tr.removeFromLeft (16);
            g.drawText (text, tr, juce::Justification::centredLeft);
            if (hasSubMenu) g.drawText (">", r.reduced (8, 0), juce::Justification::centredRight);
            juce::ignoreUnused (shortcut, icon, textColour);
        }

        void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor&) override
        {
            g.setColour (ctx.th().bg); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 6.0f);
        }
        void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor&) override
        {
            g.setColour (ctx.th().knobEdge); g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 6.0f, 1.0f);
        }

        Ctx& ctx;
    };

    // ------------------------------------------------------------------------
    //  Bouton pilule : actif = couleur d'accent
    // ------------------------------------------------------------------------
    class Pill : public juce::Button
    {
    public:
        Pill (Ctx& c, const juce::String& txt, bool round = false) : juce::Button (txt), ctx (c), rounded (round) {}
        std::function<bool()> isOn;      // si défini, prime sur l'état du bouton
        bool strong = false;             // bouton d'action principal (toujours plein)
        float textSize = 12.0f;

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            const auto& t = ctx.th();
            const bool on = strong || (isOn ? isOn() : getToggleState());
            if (! isEnabled()) g.beginTransparencyLayer (0.35f);
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float rad = rounded ? r.getHeight() * 0.5f : 6.0f;
            if (on) { g.setColour (down ? t.acc.darker (0.2f) : (over ? t.acc.brighter (0.1f) : t.acc)); g.fillRoundedRectangle (r, rad); }
            else
            {
                g.setColour (t.bg.withAlpha (0.9f)); g.fillRoundedRectangle (r, rad);
                g.setColour (over ? t.sub : t.knobEdge); g.drawRoundedRectangle (r, rad, 1.0f);
            }
            g.setColour (on ? t.bg : (isEnabled() ? t.tx : t.sub));
            g.setFont (font (textSize, true));
            g.drawFittedText (getButtonText(), getLocalBounds().reduced (6, 0), juce::Justification::centred, 1);
            if (! isEnabled()) g.endTransparencyLayer();
        }
    private:
        Ctx& ctx;
        bool rounded;
    };

    // ------------------------------------------------------------------------
    //  Potard : cran magnétique au zéro, double clic = valeur par défaut
    // ------------------------------------------------------------------------
    class SnapSlider : public juce::Slider
    {
    public:
        explicit SnapSlider (Ctx& c) : ctx (c) {}
        bool ctrlInteger = false;   // Ctrl maintenu = valeurs entières (demi-tons)
        double snapValue (double v, DragMode) override
        {
            if (ctrlInteger && juce::ModifierKeys::currentModifiers.isCtrlDown()) return std::round (v);
            if (! ctx.snap()) return v;
            const double lo = getMinimum(), hi = getMaximum();
            const double z = (lo < 0.0 && hi > 0.0) ? 0.0 : std::numeric_limits<double>::quiet_NaN();
            if (! std::isnan (z) && std::fabs (v - z) < (hi - lo) * 0.025) return z;
            return v;
        }
    private:
        Ctx& ctx;
    };

    class Knob : public juce::Component, private juce::Slider::Listener
    {
    public:
        Knob (Ctx& c, const juce::String& lbl) : slider (c), ctx (c), label (lbl)
        {
            slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            slider.setMouseDragSensitivity (220);
            slider.setVelocityModeParameters (1.0, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
            slider.addListener (this);
            addAndMakeVisible (slider);
        }

        void attach (const juce::String& paramId)
        {
            locked = false; lockGet = nullptr; lockSet = nullptr;
            slider.onValueChange = nullptr;
            att.reset();
            param = ctx.proc.apvts.getParameter (paramId);
            if (param == nullptr) return;
            att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (ctx.proc.apvts, paramId, slider);
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            repaint();
        }
        void setLabel (const juce::String& l) { label = l; repaint(); }

        // Mode verrou : le potard règle une valeur propre à un pas (get renvoie NaN = pas de verrou)
        void setLockMode (std::function<float()> get, std::function<void (float)> set)
        {
            if (param == nullptr) return;
            att.reset();
            locked = true; lockGet = std::move (get); lockSet = std::move (set);
            const auto& r = param->getNormalisableRange();
            slider.setNormalisableRange (juce::NormalisableRange<double> (r.start, r.end, r.interval, r.skew));
            const float v = lockGet();
            slider.setValue (std::isnan (v) ? param->convertFrom0to1 (param->getValue()) : v, juce::dontSendNotification);
            slider.onValueChange = [this] { if (lockSet) lockSet ((float) slider.getValue()); repaint(); };
            repaint();
        }
        bool isLocked() const { return locked && lockGet && ! std::isnan (lockGet()); }

        std::function<juce::String (float)> formatter;
        bool onBottom = false;   // posé sur la barre du bas (couleurs adaptées)
        bool hardware = false;   // potard de façade (sérigraphie, capuchon)
        void setHardware() { hardware = true; onBottom = true; slider.getProperties().set ("hw", true); label = label.toUpperCase(); repaint(); }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromBottom (label.isEmpty() ? 16 : 30);
            const int d = std::min (r.getWidth(), r.getHeight());
            slider.setBounds (r.withSizeKeepingCentre (d, d));
        }
        void paint (juce::Graphics& g) override
        {
            const auto& t = ctx.th();
            auto r = getLocalBounds().removeFromBottom (label.isEmpty() ? 16 : 30);
            g.setColour (onBottom ? t.bottomTx : t.tx.withAlpha (0.85f));
            auto lf = font (hardware ? 12.5f : 12.0f, onBottom);
            if (hardware) lf.setExtraKerningFactor (0.12f);
            g.setFont (lf);
            if (label.isNotEmpty()) g.drawText (label, r.removeFromTop (15), juce::Justification::centred);
            g.setColour (onBottom ? t.bottomTx.withAlpha (0.75f) : valueColour (t));
            g.setFont (mono (11.0f));
            juce::String v;
            if (locked && lockGet)
            {
                const float lv = lockGet();
                const float shown = std::isnan (lv) ? (param != nullptr ? param->convertFrom0to1 (param->getValue()) : 0.0f) : lv;
                v = formatter ? formatter (shown) : juce::String (shown, 2);
                if (! std::isnan (lv)) { g.setColour (t.acc); v = juce::String::fromUTF8 ("\xe2\x97\x8f ") + v; }
            }
            else if (param != nullptr)
                v = formatter ? formatter (param->convertFrom0to1 (param->getValue())) : param->getCurrentValueAsText();
            g.drawText (v, r, juce::Justification::centred);
        }
        SnapSlider slider;
    private:
        void sliderValueChanged (juce::Slider*) override { repaint(); }
        Ctx& ctx;
        juce::String label;
        juce::RangedAudioParameter* param = nullptr;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
        bool locked = false;
        std::function<float()> lockGet;
        std::function<void (float)> lockSet;
    };

    // ------------------------------------------------------------------------
    //  Matières : grain de peinture, contours de panneau, vis, emblème
    // ------------------------------------------------------------------------
    inline const juce::Image& grainImage()
    {
        static juce::Image img = []
        {
            juce::Image im (juce::Image::ARGB, 160, 160, true);
            juce::Random rnd (1795);
            for (int y = 0; y < 160; ++y)
                for (int x = 0; x < 160; ++x)
                {
                    const float v = rnd.nextFloat();
                    im.setPixelAt (x, y, v < 0.5f ? juce::Colours::black.withAlpha ((0.5f - v) * 0.9f) : juce::Colours::white.withAlpha ((v - 0.5f) * 0.55f));
                }
            return im;
        }();
        return img;
    }
    inline void paintGrain (juce::Graphics& g, juce::Rectangle<float> area, float amount)
    {
        if (amount <= 0.0f) return;
        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (area.toNearestInt());
        g.setOpacity (amount * 2.2f);
        g.setTiledImageFill (grainImage(), 0, 0, amount * 2.2f);
        g.fillRect (area);
    }
    // relief d'un panneau : arête claire en haut, ombre en bas, contour sombre
    inline void paintBevel (juce::Graphics& g, juce::Rectangle<float> r, float radius, bool raised = true)
    {
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (raised ? 0.18f : 0.05f));
        g.drawLine (r.getX() + radius, r.getY() + 1.5f, r.getRight() - radius, r.getY() + 1.5f, 1.0f);
        g.setColour (juce::Colours::black.withAlpha (raised ? 0.3f : 0.15f));
        g.drawLine (r.getX() + radius, r.getBottom() - 1.5f, r.getRight() - radius, r.getBottom() - 1.5f, 1.5f);
    }
    // écran encastré
    inline void paintInset (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour fill)
    {
        g.setColour (fill); g.fillRoundedRectangle (r, radius);
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.55f), r.getX(), r.getY(), juce::Colours::transparentBlack, r.getX(), r.getY() + 9.0f, false);
        g.setGradientFill (sh); g.fillRoundedRectangle (r, radius);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.drawLine (r.getX() + radius, r.getBottom() + 0.5f, r.getRight() - radius, r.getBottom() + 0.5f, 1.0f);
    }
    inline void paintScrew (juce::Graphics& g, juce::Point<float> c, float rad = 6.5f)
    {
        auto r = juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c);
        juce::ColourGradient gr (juce::Colour (0xfff2f2ef), r.getX() + rad * 0.6f, r.getY() + rad * 0.5f, juce::Colour (0xff6d6e6b), r.getRight(), r.getBottom(), true);
        g.setGradientFill (gr); g.fillEllipse (r);
        g.setColour (juce::Colours::black.withAlpha (0.45f)); g.drawEllipse (r, 1.0f);
        juce::Path slot; slot.addRoundedRectangle (-rad * 0.7f, -0.9f, rad * 1.4f, 1.8f, 0.9f);
        g.setColour (juce::Colour (0xff4b4c49));
        g.fillPath (slot, juce::AffineTransform::rotation (0.6f).translated (c.x, c.y));
    }
    // L'œil du typhon : trois bras en rotation + l'éclair Foudre Audio
    inline void paintEmblem (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour body, juce::Colour bolt)
    {
        const float s = std::min (r.getWidth(), r.getHeight()) / 100.0f;
        const auto tr = juce::AffineTransform::scale (s).translated (r.getCentreX() - 50.0f * s, r.getCentreY() - 50.0f * s);
        juce::Path arm;
        arm.startNewSubPath (50, 50);
        arm.cubicTo (49, 26, 66, 9, 92, 14);
        arm.cubicTo (72, 17, 61, 29, 57, 47);
        arm.closeSubPath();
        juce::Path all;
        for (int k = 0; k < 3; ++k) all.addPath (arm, juce::AffineTransform::rotation (juce::MathConstants<float>::twoPi * k / 3.0f, 50.0f, 50.0f));
        all.addEllipse (37, 37, 26, 26);
        g.setColour (body); g.fillPath (all, tr);
        juce::Path b;
        b.startNewSubPath (53, 39); b.lineTo (44, 52); b.lineTo (50, 52); b.lineTo (46, 62); b.lineTo (57, 47); b.lineTo (51, 47); b.closeSubPath();
        g.setColour (bolt); g.fillPath (b, tr);
    }
    // flanc en bois (thème Linn)
    inline void paintWood (juce::Graphics& g, juce::Rectangle<float> r)
    {
        juce::ColourGradient gr (juce::Colour (0xff6b4428), r.getX(), r.getY(), juce::Colour (0xff4a2c18), r.getRight(), r.getBottom(), false);
        g.setGradientFill (gr); g.fillRect (r);
        juce::Random rnd (42);
        for (float x = r.getX(); x < r.getRight(); x += 1.0f + rnd.nextFloat() * 2.5f)
        {
            g.setColour (juce::Colours::black.withAlpha (0.06f + rnd.nextFloat() * 0.12f));
            g.drawLine (x, r.getY(), x + rnd.nextFloat() * 2.0f - 1.0f, r.getBottom(), 0.8f);
        }
    }

    // Formatages lisibles
    inline juce::String fmtDb (float v)   { return v <= -59.9f ? juce::String ("-inf") : juce::String (v, 1) + " dB"; }
    inline juce::String fmtSt (float v)   { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st"; }
    inline juce::String fmtPct (float v)  { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
    inline juce::String fmtMs (float v)   { return v < 10.0f ? juce::String (v, 1) + " ms" : juce::String (juce::roundToInt (v)) + " ms"; }
    inline juce::String fmtSignedDb (float v) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
}
