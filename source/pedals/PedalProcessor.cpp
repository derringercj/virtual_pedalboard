#include "PedalProcessor.h"

PedalProcessor::PedalProcessor (const juce::String& type, const juce::String& name,
                                juce::AudioProcessorValueTreeState::ParameterLayout layout)
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::mono(), true)
                                .withOutput ("Output", juce::AudioChannelSet::mono(), true)),
      parameters (*this, nullptr, juce::Identifier (type), withBypass (std::move (layout))),
      typeId (type),
      displayName (name)
{
    bypassParameter = parameters.getParameter ("bypass");
    bypassValue     = parameters.getRawParameterValue ("bypass");
}

juce::AudioProcessorValueTreeState::ParameterLayout
PedalProcessor::withBypass (juce::AudioProcessorValueTreeState::ParameterLayout layout)
{
    layout.add (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return layout;
}

bool PedalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet()  == juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

void PedalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PedalProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}
