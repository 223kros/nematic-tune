#include "PluginEditor.h"

namespace
{
    constexpr int kWidth = 720, kHeight = 512;
    constexpr int kMargin = 24;
}

NematicTuneAudioProcessorEditor::NematicTuneAudioProcessorEditor (NematicTuneAudioProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      modeAttachment (*p.apvts.getParameter ("mode"), [this] (float v) { updateModeButtons ((int) std::lround (v)); }),
      display ([&p]
               {
                   const auto t = p.getTelemetry();
                   return PitchDisplay::Frame { t.inMidi, t.outMidi, t.voiced, t.scaleMask };
               })
{
    setLookAndFeel (&look);
    setSize (kWidth, kHeight);

    // --- modo
    addAndMakeVisible (boomBapButton);
    addAndMakeVisible (hardButton);
    boomBapButton.onClick = [this] { processor.applyModePreset (0); };
    hardButton.onClick    = [this] { processor.applyModePreset (1); };
    modeAttachment.sendInitialUpdate();

    // --- tonalidad / escala
    keyBox.addItemList (NematicTuneAudioProcessor::getKeyNames(), 1);
    scaleBox.addItemList (NematicTuneAudioProcessor::getScaleNames(), 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAttachment   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "key", keyBox);
    scaleAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "scale", scaleBox);

    // --- mandos
    setupKnob (retuneKnob, "RETUNE SPEED", "retune", 110.0);
    setupKnob (amountKnob, "AMOUNT", "amount", 70.0);
    setupKnob (mixKnob, "MIX", "mix", 100.0);
    setupKnob (outputKnob, "OUTPUT", "output", 0.0);

    retuneKnob.slider.textFromValueFunction = [] (double v) { return v < 0.5 ? juce::String ("INSTANT") : juce::String (juce::roundToInt (v)) + " ms"; };
    retuneKnob.slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    amountKnob.slider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
    amountKnob.slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    mixKnob.slider.textFromValueFunction    = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
    mixKnob.slider.valueFromTextFunction    = [] (const juce::String& t) { return t.getDoubleValue(); };
    outputKnob.slider.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    outputKnob.slider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    for (auto* k : { &retuneKnob, &amountKnob, &mixKnob, &outputKnob })
        k->slider.updateText();

    addAndMakeVisible (display);
}

NematicTuneAudioProcessorEditor::~NematicTuneAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void NematicTuneAudioProcessorEditor::setupKnob (Knob& k, const juce::String& caption, const juce::String& paramId,
                                                 double doubleClickValue)
{
    k.caption = caption;
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 100, 22);
    k.slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
    addAndMakeVisible (k.slider);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, k.slider);
    k.slider.setDoubleClickReturnValue (true, doubleClickValue);
}

void NematicTuneAudioProcessorEditor::updateModeButtons (int mode)
{
    boomBapButton.setToggleState (mode == 0, juce::dontSendNotification);
    hardButton.setToggleState (mode == 1, juce::dontSendNotification);
}

//==============================================================================
void NematicTuneAudioProcessorEditor::paintBackdrop (juce::Graphics& g) const
{
    const auto bounds = getLocalBounds().toFloat();
    const float w = bounds.getWidth(), h = bounds.getHeight();

    // degradado de fondo
    juce::ColourGradient bg (neon::bgTop, 0.0f, 0.0f, neon::bgDeep, 0.0f, h, false);
    bg.addColour (0.55, neon::bgMid);
    g.setGradientFill (bg);
    g.fillAll();

    // resplandores
    g.setGradientFill (juce::ColourGradient (neon::violet.withAlpha (0.28f), w * 0.12f, -20.0f,
                                             neon::violet.withAlpha (0.0f), w * 0.12f, 260.0f, true));
    g.fillRect (bounds);
    g.setGradientFill (juce::ColourGradient (neon::magenta.withAlpha (0.20f), w * 0.92f, h * 0.98f,
                                             neon::magenta.withAlpha (0.0f), w * 0.92f, h * 0.98f - 300.0f, true));
    g.fillRect (bounds);
    g.setGradientFill (juce::ColourGradient (neon::cyan.withAlpha (0.10f), w * 0.55f, h * 0.55f,
                                             neon::cyan.withAlpha (0.0f), w * 0.55f, h * 0.55f - 220.0f, true));
    g.fillRect (bounds);

    // grilla en perspectiva (zona de los mandos)
    {
        const float top = (float) knobsArea.getY() - 6.0f, bottom = h - 34.0f;
        const float vx = w * 0.5f, spread = w * 1.9f;
        g.setColour (neon::violet.withAlpha (0.10f));
        for (int i = -14; i <= 14; ++i)
            g.drawLine (vx, top - 50.0f, vx + (float) i * spread / 28.0f, bottom, 1.0f);
        for (int i = 1; i <= 9; ++i)
        {
            const float t = std::pow ((float) i / 9.0f, 2.0f);
            const float y = (top - 4.0f) + t * (bottom - top + 4.0f);
            g.drawHorizontalLine ((int) y, 0.0f, w);
        }
    }

    // scanlines suaves
    g.setColour (juce::Colours::black.withAlpha (0.10f));
    for (int y = 0; y < (int) h; y += 3)
        g.drawHorizontalLine (y, 0.0f, w);
}

void NematicTuneAudioProcessorEditor::paint (juce::Graphics& g)
{
    paintBackdrop (g);

    const float w = (float) getWidth();

    // --- título con neón
    juce::String title = "N E M A T I C";
    const auto titleBox = juce::Rectangle<int> (kMargin, 12, 330, 40);

    g.setFont (neon::font (34.0f, true));
    for (auto [dx, al] : std::initializer_list<std::pair<int, float>> { { 3, 0.05f }, { 2, 0.09f }, { 1, 0.16f } })
    {
        g.setColour (neon::magenta.withAlpha (al));
        for (int sx = -dx; sx <= dx; sx += dx)
            for (int sy = -dx; sy <= dx; sy += dx)
                g.drawText (title, titleBox.translated (sx, sy), juce::Justification::centredLeft);
    }
    g.setGradientFill (juce::ColourGradient (neon::cyan, (float) titleBox.getX(), 0.0f,
                                             neon::pink, (float) titleBox.getX() + 300.0f, 0.0f, false));
    g.drawText (title, titleBox, juce::Justification::centredLeft);

    g.setFont (neon::font (15.0f, true));
    g.setColour (neon::violet.brighter (0.35f));
    g.drawText ("T U N E", juce::Rectangle<int> (kMargin + 2, 50, 200, 18), juce::Justification::centredLeft);

    g.setFont (neon::font (11.0f, true));
    g.setColour (neon::dim);
    g.drawText ("VOCAL PITCH CORRECTION", juce::Rectangle<int> (getWidth() - kMargin - 260, 24, 260, 16),
                juce::Justification::centredRight);
    g.drawText ("BOOM BAP  /  HARD", juce::Rectangle<int> (getWidth() - kMargin - 260, 40, 260, 16),
                juce::Justification::centredRight);

    // línea de neón bajo la cabecera
    {
        juce::ColourGradient line (neon::cyan.withAlpha (0.0f), 0.0f, 0.0f, neon::cyan.withAlpha (0.0f), w, 0.0f, false);
        line.addColour (0.25, neon::violet);
        line.addColour (0.6, neon::magenta);
        g.setGradientFill (line);
        g.fillRect (0.0f, 74.0f, w, 2.0f);
        line = juce::ColourGradient (neon::violet.withAlpha (0.0f), 0.0f, 0.0f, neon::violet.withAlpha (0.0f), w, 0.0f, false);
        line.addColour (0.5, neon::magenta.withAlpha (0.25f));
        g.setGradientFill (line);
        g.fillRect (0.0f, 71.0f, w, 8.0f);
    }

    // etiquetas de tonalidad/escala
    g.setFont (neon::font (11.0f, true));
    g.setColour (neon::dim);
    g.drawText ("KEY", keyBox.getX(), keyBox.getY() - 15, keyBox.getWidth(), 14, juce::Justification::centredLeft);
    g.drawText ("SCALE", scaleBox.getX(), scaleBox.getY() - 15, scaleBox.getWidth(), 14, juce::Justification::centredLeft);

    // etiquetas de los mandos
    g.setFont (neon::font (12.0f, true));
    for (auto* k : { &retuneKnob, &amountKnob, &mixKnob, &outputKnob })
    {
        g.setColour (neon::text.withAlpha (0.85f));
        g.drawText (k->caption, k->slider.getX(), k->slider.getY() - 14, k->slider.getWidth(), 14, juce::Justification::centred);
    }

    // pie
    g.setFont (neon::font (11.0f, true));
    g.setColour (neon::dim);
    g.drawText ("NEMATIC  |  TUNE  v1.0", kMargin, getHeight() - 26, 300, 16, juce::Justification::centredLeft);
    g.drawText ("doble clic en un mando = valor por defecto", getWidth() - kMargin - 300, getHeight() - 26, 300, 16,
                juce::Justification::centredRight);
}

void NematicTuneAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (kMargin, 0);

    // fila de modo / tonalidad / escala
    modeArea = area.withTrimmedTop (92).withHeight (40);
    boomBapButton.setBounds (modeArea.getX(), modeArea.getY(), 128, 40);
    hardButton.setBounds (modeArea.getX() + 134, modeArea.getY(), 104, 40);

    scaleBox.setBounds (modeArea.getRight() - 170, modeArea.getY() + 6, 170, 30);
    keyBox.setBounds (scaleBox.getX() - 84, modeArea.getY() + 6, 74, 30);

    // visor
    displayArea = juce::Rectangle<int> (kMargin, 144, getWidth() - kMargin * 2, 160);
    display.setBounds (displayArea);

    // mandos
    knobsArea = juce::Rectangle<int> (kMargin, 332, getWidth() - kMargin * 2, 150);
    const int cell = knobsArea.getWidth() / 4;
    int i = 0;
    for (auto* k : { &retuneKnob, &amountKnob, &mixKnob, &outputKnob })
    {
        k->slider.setBounds (knobsArea.getX() + i * cell + (cell - 130) / 2, knobsArea.getY() + 16, 130, 132);
        ++i;
    }
}
