#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include "AutotuneEngine.h"

class NematicTuneAudioProcessor : public juce::AudioProcessor
{
public:
    NematicTuneAudioProcessor();
    ~NematicTuneAudioProcessor() override = default;

    // ---- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Nematic Tune"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Parámetros
    juce::AudioProcessorValueTreeState apvts;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static const juce::StringArray& getKeyNames();
    static const juce::StringArray& getScaleNames();

    // Llamado desde la interfaz al tocar un botón de modo: cambia el modo y deja los
    // mandos (retune/cantidad) en los valores de ese modo.
    void applyModePreset (int modeIndex);

    // ---- Telemetría para la interfaz (se lee desde el hilo de mensajes)
    struct Telemetry
    {
        float inMidi = 60.0f, outMidi = 60.0f;
        bool  voiced = false;
        int   scaleMask = 0xFFF;
    };
    Telemetry getTelemetry() const
    {
        Telemetry t;
        t.inMidi = uiInMidi.load();  t.outMidi = uiOutMidi.load();
        t.voiced = uiVoiced.load();  t.scaleMask = uiScaleMask.load();
        return t;
    }

private:
    static int makeScaleMask (int key, int scaleIndex);
    void setParameterValue (const juce::String& id, float realValue);

    nematic::AutotuneEngine engine;
    int engineChannels = 2;

    std::atomic<float> *pKey = nullptr, *pScale = nullptr, *pMode = nullptr, *pRetune = nullptr,
                       *pAmount = nullptr, *pMix = nullptr, *pOutput = nullptr;

    std::atomic<float> uiInMidi { 60.0f }, uiOutMidi { 60.0f };
    std::atomic<bool>  uiVoiced { false };
    std::atomic<int>   uiScaleMask { 0xFFF };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NematicTuneAudioProcessor)
};
