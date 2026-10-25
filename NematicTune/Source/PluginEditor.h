#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "NeonLookAndFeel.h"
#include "PitchDisplay.h"

class NematicTuneAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit NematicTuneAudioProcessorEditor (NematicTuneAudioProcessor&);
    ~NematicTuneAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::String caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob& k, const juce::String& caption, const juce::String& paramId, double doubleClickValue);
    void updateModeButtons (int mode);
    void paintBackdrop (juce::Graphics&) const;

    NematicTuneAudioProcessor& processor;
    NeonLookAndFeel look;

    ModeButton boomBapButton { "BOOM BAP", neon::blue, neon::violet };
    ModeButton hardButton    { "HARD",     neon::magenta, neon::pink };
    juce::ParameterAttachment modeAttachment;

    juce::ComboBox keyBox, scaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttachment, scaleAttachment;

    Knob retuneKnob, amountKnob, mixKnob, outputKnob;
    PitchDisplay display;

    juce::Rectangle<int> modeArea, displayArea, knobsArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NematicTuneAudioProcessorEditor)
};
