#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

/**
    Base for every pedal on the board.

    Each pedal is a complete AudioProcessor with its own parameters, state and
    editor. That is what lets the board hold several of the same pedal - two
    compressors don't fight over one "threshold" - and lets an
    AudioProcessorGraph wire them together in whatever order the player picks.

    Pedals are mono in, mono out, like the real thing. Every pedal gets a
    "bypass" parameter for free; subclasses supply the rest of their layout.
    What bypass *means* is left to each pedal - a compressor lets go at once,
    but a delay might let its echoes ring out.
*/
class PedalProcessor : public juce::AudioProcessor
{
public:
    PedalProcessor (const juce::String& typeId, const juce::String& displayName,
                    juce::AudioProcessorValueTreeState::ParameterLayout layout);

    /** Written into saved boards to say which pedal to rebuild, e.g. "compressor".
        Renaming one breaks every board saved with it. */
    const juce::String& getTypeId() const noexcept { return typeId; }

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }

    bool isBypassed() const noexcept { return bypassValue->load (std::memory_order_relaxed) >= 0.5f; }

    //==============================================================================
    juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParameter; }
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    const juce::String getName() const override                { return displayName; }
    bool hasEditor() const override                            { return true; }
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

protected:
    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout
        withBypass (juce::AudioProcessorValueTreeState::ParameterLayout);

    const juce::String typeId, displayName;

    juce::AudioProcessorParameter* bypassParameter = nullptr;
    std::atomic<float>*            bypassValue     = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PedalProcessor)
};
