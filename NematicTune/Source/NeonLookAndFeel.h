#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>

// Paleta synthwave de Nematic
namespace neon
{
    const juce::Colour bgDeep   (0xff07040f);
    const juce::Colour bgTop    (0xff160a35);
    const juce::Colour bgMid    (0xff0d0722);
    const juce::Colour panel    (0xff120a2b);
    const juce::Colour violet   (0xff8f5bff);
    const juce::Colour magenta  (0xffff3df2);
    const juce::Colour pink     (0xffff6ec7);
    const juce::Colour cyan     (0xff3df3ff);
    const juce::Colour blue     (0xff4d7cff);
    const juce::Colour text     (0xffeee6ff);
    const juce::Colour dim      (0xff8372b8);

    inline juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    // trazo con halo de neón
    inline void glowStroke (juce::Graphics& g, const juce::Path& p, juce::Colour c, float core)
    {
        const juce::PathStrokeType::JointStyle js = juce::PathStrokeType::curved;
        const juce::PathStrokeType::EndCapStyle cs = juce::PathStrokeType::rounded;
        g.setColour (c.withAlpha (0.07f)); g.strokePath (p, juce::PathStrokeType (core * 5.5f, js, cs));
        g.setColour (c.withAlpha (0.14f)); g.strokePath (p, juce::PathStrokeType (core * 3.4f, js, cs));
        g.setColour (c.withAlpha (0.30f)); g.strokePath (p, juce::PathStrokeType (core * 2.0f, js, cs));
        g.setColour (c.brighter (0.35f));  g.strokePath (p, juce::PathStrokeType (core, js, cs));
    }
}

//==============================================================================
class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, neon::panel);
        setColour (juce::ComboBox::textColourId, neon::text);
        setColour (juce::ComboBox::outlineColourId, neon::violet.withAlpha (0.7f));
        setColour (juce::ComboBox::arrowColourId, neon::cyan);
        setColour (juce::PopupMenu::backgroundColourId, neon::bgMid);
        setColour (juce::PopupMenu::textColourId, neon::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, neon::violet.withAlpha (0.55f));
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Slider::textBoxTextColourId, neon::cyan);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxHighlightColourId, neon::violet.withAlpha (0.5f));
        setColour (juce::Label::textColourId, neon::text);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (10.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float angle = startAngle + sliderPos * (endAngle - startAngle);
        const float arcR = radius;

        // pista
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (neon::violet.withAlpha (0.16f));
        g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // cuerpo de la perilla
        const float bodyR = radius * 0.72f;
        juce::ColourGradient body (juce::Colour (0xff2a1660), centre.x - bodyR * 0.4f, centre.y - bodyR * 0.6f,
                                   juce::Colour (0xff0a0519), centre.x + bodyR * 0.5f, centre.y + bodyR * 0.8f, true);
        g.setGradientFill (body);
        g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
        g.setColour (neon::violet.withAlpha (0.55f));
        g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.4f);

        // arco de valor con degradado y halo
        if (sliderPos > 0.001f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);

            static const std::pair<float, float> glows[] = { { 15.0f, 0.06f }, { 11.0f, 0.10f }, { 7.5f, 0.2f } };
            for (const auto& [w, a] : glows)
            {
                g.setColour (neon::magenta.withAlpha (a));
                g.strokePath (value, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            juce::ColourGradient grad (neon::cyan, centre.x - arcR, centre.y + arcR,
                                       neon::magenta, centre.x + arcR, centre.y - arcR, false);
            grad.addColour (0.5, neon::violet);
            g.setGradientFill (grad);
            g.strokePath (value, juce::PathStrokeType (4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // aguja
        const float inner = bodyR * 0.25f, outer = bodyR * 0.88f;
        juce::Path needle;
        needle.startNewSubPath (centre.x + inner * std::sin (angle), centre.y - inner * std::cos (angle));
        needle.lineTo (centre.x + outer * std::sin (angle), centre.y - outer * std::cos (angle));
        neon::glowStroke (g, needle, neon::cyan, 2.2f);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int,
                       juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height).reduced (0.5f);
        g.setColour (neon::panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (neon::violet.withAlpha (box.isPopupActive() ? 1.0f : 0.65f));
        g.drawRoundedRectangle (r, 8.0f, 1.4f);

        juce::Path arrow;
        const float cx = (float) width - 16.0f, cy = (float) height * 0.5f;
        arrow.addTriangle (cx - 4.5f, cy - 2.5f, cx + 4.5f, cy - 2.5f, cx, cy + 3.5f);
        g.setColour (neon::cyan);
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (10, 1, box.getWidth() - 30, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return neon::font (14.0f, true); }
    juce::Font getPopupMenuFont() override { return neon::font (14.0f, true); }

    juce::Font getLabelFont (juce::Label&) override { return neon::font (13.0f, true); }
};

//==============================================================================
// Botón de modo (pastilla neón). "accent" define el color del brillo.
class ModeButton : public juce::Button
{
public:
    ModeButton (const juce::String& name, juce::Colour accentA, juce::Colour accentB)
        : juce::Button (name), a (accentA), b (accentB)
    {
        setClickingTogglesState (false);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (3.0f);
        const bool on = getToggleState();
        const float cr = r.getHeight() * 0.5f;

        if (on)
        {
            static const std::pair<float, float> halos[] = { { 5.0f, 0.10f }, { 3.0f, 0.16f }, { 1.5f, 0.28f } };
            for (const auto& [grow, al] : halos)
            {
                g.setColour (a.withAlpha (al));
                g.fillRoundedRectangle (r.expanded (grow), cr + grow);
            }
            juce::ColourGradient grad (a, r.getX(), r.getY(), b, r.getRight(), r.getBottom(), false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (r, cr);
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            g.drawRoundedRectangle (r, cr, 1.3f);
        }
        else
        {
            g.setColour (neon::panel.brighter (highlighted ? 0.25f : 0.0f));
            g.fillRoundedRectangle (r, cr);
            g.setColour (neon::violet.withAlpha (highlighted ? 0.9f : 0.5f));
            g.drawRoundedRectangle (r, cr, 1.3f);
        }

        g.setColour (on ? juce::Colours::white : neon::dim.brighter (highlighted ? 0.4f : 0.0f));
        g.setFont (neon::font (14.0f, true));
        g.drawText (getButtonText(), getLocalBounds().translated (0, down ? 1 : 0), juce::Justification::centred);
    }

private:
    juce::Colour a, b;
};
