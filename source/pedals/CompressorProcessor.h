#pragma once

#include <atomic>

#include "PedalProcessor.h"
#include "dsp/CompressorPedal.h"

/** The compressor as a pedal on the board: CompressorPedal's DSP, plus the
    knobs, bypass and gain-reduction meter that go with it. */
class CompressorProcessor final : public PedalProcessor
{
public:
    static constexpr const char* pedalTypeId = "compressor";
    static constexpr const char* pedalName   = "Compressor";

    CompressorProcessor();

    //==============================================================================
    // We only do single precision; this keeps the unused double overload visible
    // so the compiler stops warning that we hid it.
    using juce::AudioProcessor::processBlock;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;

    //==============================================================================
    /** Largest gain reduction, in dB, seen during the most recent block. Written
        on the audio thread and read by the editor, hence the atomic. */
    float getGainReductionDb() const noexcept
    {
        return gainReductionDb.load (std::memory_order_relaxed);
    }

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushParameters() noexcept;

    // Cached so the audio thread never has to look parameters up by name.
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* ratioParam     = nullptr;
    std::atomic<float>* kneeParam      = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* releaseParam   = nullptr;
    std::atomic<float>* makeupParam    = nullptr;

    CompressorPedal compressor;
    std::atomic<float> gainReductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorProcessor)
};
