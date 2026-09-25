#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "PluginProcessor.h"

/** Horizontal bar showing how hard the compressor is working right now. Without
    one of these a compressor is almost impossible to learn by ear alone. */
class GainReductionMeter final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit GainReductionMeter (VirtualPedalboardProcessor&);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    VirtualPedalboardProcessor& processor;
    float displayedDb = 0.0f;

    static constexpr float fullScaleDb  = 24.0f;
    static constexpr float fallPerTickDb = 0.8f;   // ~24 dB/second at 30 Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainReductionMeter)
};

//==============================================================================
class VirtualPedalboardEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VirtualPedalboardEditor (VirtualPedalboardProcessor&);
    ~VirtualPedalboardEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    void addKnob (const juce::String& parameterID, const juce::String& text,
                  const juce::String& suffix);

    VirtualPedalboardProcessor& processor;

    juce::Label      titleLabel, meterLabel, inputLabel;
    juce::TextButton bypassButton { "BYPASS" };
    juce::ComboBox   inputBox;
    GainReductionMeter meter;

    std::vector<std::unique_ptr<Knob>>         knobs;
    std::unique_ptr<APVTS::ButtonAttachment>   bypassAttachment;
    std::unique_ptr<APVTS::ComboBoxAttachment> inputAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualPedalboardEditor)
};
