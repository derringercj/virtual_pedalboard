#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include <atomic>

#include "dsp/CompressorPedal.h"

class VirtualPedalboardProcessor final : public juce::AudioProcessor
{
public:
    /** Which of the interface's inputs the instrument is actually plugged into.
        On a Scarlett Solo the 1/4" instrument jack is input 2, i.e. "right". */
    enum class InputMode { first = 0, second, sum };

    VirtualPedalboardProcessor();
    ~VirtualPedalboardProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                            { return true; }

    const juce::String getName() const override                { return "Virtual Pedalboard"; }
    bool acceptsMidi() const override                          { return false; }
    bool producesMidi() const override                         { return false; }
    bool isMidiEffect() const override                         { return false; }
    double getTailLengthSeconds() const override               { return 0.0; }

    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }

    /** Largest gain reduction, in dB, seen during the most recent block. Written
        on the audio thread and read by the editor, hence the atomic. */
    float getGainReductionDb() const noexcept
    {
        return gainReductionDb.load (std::memory_order_relaxed);
    }

private:
    //==============================================================================
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void pushParametersToPedals() noexcept;

    juce::AudioProcessorValueTreeState parameters;

    // Cached so the audio thread never has to look parameters up by name.
    std::atomic<float>* inputParam     = nullptr;
    std::atomic<float>* bypassParam    = nullptr;
    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* ratioParam     = nullptr;
    std::atomic<float>* kneeParam      = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* releaseParam   = nullptr;
    std::atomic<float>* makeupParam    = nullptr;

    CompressorPedal compressor;
    std::atomic<float> gainReductionDb { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualPedalboardProcessor)
};
