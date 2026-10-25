#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iterator>
#include <vector>

namespace
{
    // Intervalos de cada escala (semitonos sobre la tónica)
    struct ScaleDef { const char* name; std::vector<int> notes; };

    const ScaleDef kScales[] = {
        { "Chromatic",        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } },
        { "Major",            { 0, 2, 4, 5, 7, 9, 11 } },
        { "Minor",            { 0, 2, 3, 5, 7, 8, 10 } },
        { "Harmonic Minor",   { 0, 2, 3, 5, 7, 8, 11 } },
        { "Minor Pentatonic", { 0, 3, 5, 7, 10 } },
        { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
        { "Blues",            { 0, 3, 5, 6, 7, 10 } },
        { "Dorian",           { 0, 2, 3, 5, 7, 9, 10 } },
    };

    // Valores de cada modo
    constexpr float kBoomBapRetune = 110.0f, kBoomBapAmount = 70.0f, kBoomBapDeadZone = 0.25f;
    constexpr float kHardRetune    = 0.0f,   kHardAmount    = 100.0f, kHardDeadZone = 0.0f;
}

const juce::StringArray& NematicTuneAudioProcessor::getKeyNames()
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names;
}

const juce::StringArray& NematicTuneAudioProcessor::getScaleNames()
{
    static const juce::StringArray names = [] {
        juce::StringArray s;
        for (auto& sc : kScales) s.add (sc.name);
        return s;
    }();
    return names;
}

int NematicTuneAudioProcessor::makeScaleMask (int key, int scaleIndex)
{
    scaleIndex = juce::jlimit (0, (int) std::size (kScales) - 1, scaleIndex);
    int mask = 0;
    for (int n : kScales[scaleIndex].notes)
        mask |= 1 << ((key + n) % 12);
    return mask;
}

juce::AudioProcessorValueTreeState::ParameterLayout NematicTuneAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", getKeyNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", getScaleNames(), 0));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Mode",
                                                        StringArray { "Boom Bap", "Hard" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "retune", 1 }, "Retune Speed",
                                                       NormalisableRange<float> (0.0f, 400.0f, 0.1f, 0.45f),
                                                       kBoomBapRetune));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f),
                                                       kBoomBapAmount));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                       NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));
    return layout;
}

NematicTuneAudioProcessor::NematicTuneAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "NEMATIC_TUNE", createLayout())
{
    pKey    = apvts.getRawParameterValue ("key");
    pScale  = apvts.getRawParameterValue ("scale");
    pMode   = apvts.getRawParameterValue ("mode");
    pRetune = apvts.getRawParameterValue ("retune");
    pAmount = apvts.getRawParameterValue ("amount");
    pMix    = apvts.getRawParameterValue ("mix");
    pOutput = apvts.getRawParameterValue ("output");
}

bool NematicTuneAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in  = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void NematicTuneAudioProcessor::prepareToPlay (double sampleRate, int)
{
    engineChannels = juce::jlimit (1, 2, getMainBusNumInputChannels());
    engine.prepare (sampleRate, engineChannels);
    setLatencySamples (engine.getLatency());
}

void NematicTuneAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int nCh = buffer.getNumChannels();
    const int nSamples = buffer.getNumSamples();

    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, nSamples);

    if (nCh < engineChannels || nSamples == 0)
        return;

    const int mode  = (int) std::lround (pMode->load());
    const int key   = juce::jlimit (0, 11, (int) std::lround (pKey->load()));
    const int scale = (int) std::lround (pScale->load());

    nematic::EngineParams p;
    p.scaleMask = makeScaleMask (key, scale);
    p.retuneMs  = pRetune->load();
    p.amount    = pAmount->load() * 0.01f;
    p.deadZone  = mode == 0 ? kBoomBapDeadZone : kHardDeadZone;
    p.wet       = pMix->load() * 0.01f;
    p.outGain   = juce::Decibels::decibelsToGain (pOutput->load());
    engine.setParams (p);

    float* channels[2] = { buffer.getWritePointer (0), engineChannels > 1 ? buffer.getWritePointer (1) : nullptr };
    engine.process (channels, nSamples);

    uiInMidi.store (engine.telemetryInMidi);
    uiOutMidi.store (engine.telemetryOutMidi);
    uiVoiced.store (engine.telemetryVoiced);
    uiScaleMask.store (p.scaleMask);
}

void NematicTuneAudioProcessor::setParameterValue (const juce::String& id, float realValue)
{
    if (auto* param = apvts.getParameter (id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
        param->endChangeGesture();
    }
}

void NematicTuneAudioProcessor::applyModePreset (int modeIndex)
{
    setParameterValue ("mode", (float) modeIndex);
    setParameterValue ("retune", modeIndex == 0 ? kBoomBapRetune : kHardRetune);
    setParameterValue ("amount", modeIndex == 0 ? kBoomBapAmount : kHardAmount);
}

juce::AudioProcessorEditor* NematicTuneAudioProcessor::createEditor()
{
    return new NematicTuneAudioProcessorEditor (*this);
}

void NematicTuneAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void NematicTuneAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NematicTuneAudioProcessor();
}
