#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VirtualPedalboardProcessor::createParameterLayout()
{
    using namespace juce;

    // A range whose midpoint sits at `centre` rather than halfway, so the useful
    // part of an attack or ratio control isn't crammed into the first few degrees.
    auto skewed = [] (float minimum, float maximum, float centre, float interval)
    {
        NormalisableRange<float> range { minimum, maximum, interval };
        range.setSkewForCentre (centre);
        return range;
    };

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "input", 1 }, "Input",
        StringArray { "In 1 (left)", "In 2 (right)", "Sum 1 + 2" }, 1));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { "bypass", 1 }, "Bypass", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "threshold", 1 }, "Threshold",
        NormalisableRange<float> { -60.0f, 0.0f, 0.1f }, -18.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "ratio", 1 }, "Ratio", skewed (1.0f, 20.0f, 4.0f, 0.1f), 4.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "knee", 1 }, "Knee",
        NormalisableRange<float> { 0.0f, 24.0f, 0.5f }, 6.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "attack", 1 }, "Attack", skewed (0.1f, 200.0f, 15.0f, 0.1f), 10.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "release", 1 }, "Release", skewed (10.0f, 1000.0f, 150.0f, 1.0f), 150.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { "makeup", 1 }, "Makeup",
        NormalisableRange<float> { -12.0f, 24.0f, 0.1f }, 0.0f));

    return layout;
}

//==============================================================================
VirtualPedalboardProcessor::VirtualPedalboardProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("VirtualPedalboard"), createParameterLayout())
{
    inputParam     = parameters.getRawParameterValue ("input");
    bypassParam    = parameters.getRawParameterValue ("bypass");
    thresholdParam = parameters.getRawParameterValue ("threshold");
    ratioParam     = parameters.getRawParameterValue ("ratio");
    kneeParam      = parameters.getRawParameterValue ("knee");
    attackParam    = parameters.getRawParameterValue ("attack");
    releaseParam   = parameters.getRawParameterValue ("release");
    makeupParam    = parameters.getRawParameterValue ("makeup");
}

//==============================================================================
void VirtualPedalboardProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, maximumExpectedSamplesPerBlock);
    spec.numChannels      = 1;                 // the pedal chain is mono, like the real thing

    compressor.prepare (spec);
    pushParametersToPedals();
    compressor.reset();

    gainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void VirtualPedalboardProcessor::releaseResources()
{
    compressor.reset();
}

bool VirtualPedalboardProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void VirtualPedalboardProcessor::pushParametersToPedals() noexcept
{
    compressor.setThresholdDb (thresholdParam->load (std::memory_order_relaxed));
    compressor.setRatio       (ratioParam    ->load (std::memory_order_relaxed));
    compressor.setKneeDb      (kneeParam     ->load (std::memory_order_relaxed));
    compressor.setAttackMs    (attackParam   ->load (std::memory_order_relaxed));
    compressor.setReleaseMs   (releaseParam  ->load (std::memory_order_relaxed));
    compressor.setMakeupDb    (makeupParam   ->load (std::memory_order_relaxed));
}

//==============================================================================
void VirtualPedalboardProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();
    const auto numIns     = getTotalNumInputChannels();
    const auto numOuts    = getTotalNumOutputChannels();

    if (numSamples <= 0 || numOuts <= 0)
        return;

    // Any output channel the host gave us beyond the inputs holds junk - clear it.
    for (auto ch = numIns; ch < numOuts; ++ch)
        buffer.clear (ch, 0, numSamples);

    pushParametersToPedals();

    // --- 1. Collapse the interface's inputs down to the one mono bass signal ---
    // Input and output share this buffer, so channel 0 doubles as our workspace.
    auto* mono = buffer.getWritePointer (0);

    if (numIns > 1)
    {
        const auto mode = static_cast<InputMode> ((int) inputParam->load (std::memory_order_relaxed));
        const auto* second = buffer.getReadPointer (1);

        if (mode == InputMode::second)
        {
            juce::FloatVectorOperations::copy (mono, second, numSamples);
        }
        else if (mode == InputMode::sum)
        {
            juce::FloatVectorOperations::add (mono, second, numSamples);
            juce::FloatVectorOperations::multiply (mono, 0.5f, numSamples);
        }
    }

    // --- 2. The pedal chain. One pedal today; the rest go here later. ---
    float maxReductionDb = 0.0f;
    const auto bypassed = bypassParam->load (std::memory_order_relaxed) >= 0.5f;

    if (bypassed)
    {
        compressor.reset();
    }
    else
    {
        for (int i = 0; i < numSamples; ++i)
        {
            mono[i] = compressor.processSample (mono[i]);
            maxReductionDb = juce::jmax (maxReductionDb, compressor.getCurrentReductionDb());
        }
    }

    gainReductionDb.store (maxReductionDb, std::memory_order_relaxed);

    // --- 3. Fan the mono result back out to every output the device gives us ---
    for (auto ch = 1; ch < numOuts; ++ch)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), mono, numSamples);
}

//==============================================================================
juce::AudioProcessorEditor* VirtualPedalboardProcessor::createEditor()
{
    return new VirtualPedalboardEditor (*this);
}

void VirtualPedalboardProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VirtualPedalboardProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
// Entry point the plugin wrappers (including the standalone app) call.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VirtualPedalboardProcessor();
}
