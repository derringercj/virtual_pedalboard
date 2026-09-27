#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

#include "CompressorProcessor.h"

/** Horizontal bar showing how hard the compressor is working right now. Without
    one of these a compressor is almost impossible to learn by ear alone. */
class GainReductionMeter final : public juce::Component,
                                 private juce::Timer
{
public:
    explicit GainReductionMeter (CompressorProcessor&);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;

    CompressorProcessor& pedal;
    float displayedDb = 0.0f;

    static constexpr float fullScaleDb  = 24.0f;
    static constexpr float fallPerTickDb = 0.8f;   // ~24 dB/second at 30 Hz

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GainReductionMeter)
};

//==============================================================================
/** The compressor's panel on the board: its knobs, meter and bypass switch. */
class CompressorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CompressorEditor (CompressorProcessor&);
    ~CompressorEditor() override = default;

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

    CompressorProcessor& pedal;

    juce::Label      titleLabel, meterLabel;
    juce::TextButton bypassButton { "BYPASS" };
    GainReductionMeter meter;

    std::vector<std::unique_ptr<Knob>>       knobs;
    std::unique_ptr<APVTS::ButtonAttachment> bypassAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompressorEditor)
};
