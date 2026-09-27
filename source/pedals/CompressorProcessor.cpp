#include "CompressorProcessor.h"
#include "CompressorEditor.h"

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout CompressorProcessor::createParameterLayout()
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
CompressorProcessor::CompressorProcessor()
    : PedalProcessor (pedalTypeId, pedalName, createParameterLayout())
{
    thresholdParam = parameters.getRawParameterValue ("threshold");
    ratioParam     = parameters.getRawParameterValue ("ratio");
    kneeParam      = parameters.getRawParameterValue ("knee");
    attackParam    = parameters.getRawParameterValue ("attack");
    releaseParam   = parameters.getRawParameterValue ("release");
    makeupParam    = parameters.getRawParameterValue ("makeup");
}

//==============================================================================
void CompressorProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = (juce::uint32) juce::jmax (1, maximumExpectedSamplesPerBlock);
    spec.numChannels      = 1;

    compressor.prepare (spec);
    pushParameters();
    reset();
}

void CompressorProcessor::releaseResources()
{
    reset();
}

void CompressorProcessor::reset()
{
    compressor.reset();
    gainReductionDb.store (0.0f, std::memory_order_relaxed);
}

void CompressorProcessor::pushParameters() noexcept
{
    compressor.setThresholdDb (thresholdParam->load (std::memory_order_relaxed));
    compressor.setRatio       (ratioParam    ->load (std::memory_order_relaxed));
    compressor.setKneeDb      (kneeParam     ->load (std::memory_order_relaxed));
    compressor.setAttackMs    (attackParam   ->load (std::memory_order_relaxed));
    compressor.setReleaseMs   (releaseParam  ->load (std::memory_order_relaxed));
    compressor.setMakeupDb    (makeupParam   ->load (std::memory_order_relaxed));
}

//==============================================================================
void CompressorProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return;

    pushParameters();

    if (isBypassed())
    {
        // Let go completely, so re-engaging starts fresh rather than clamping
        // down with whatever reduction was left over. The audio passes untouched.
        reset();
        return;
    }

    auto* samples = buffer.getWritePointer (0);
    float maxReductionDb = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        samples[i] = compressor.processSample (samples[i]);
        maxReductionDb = juce::jmax (maxReductionDb, compressor.getCurrentReductionDb());
    }

    gainReductionDb.store (maxReductionDb, std::memory_order_relaxed);
}

//==============================================================================
juce::AudioProcessorEditor* CompressorProcessor::createEditor()
{
    return new CompressorEditor (*this);
}
