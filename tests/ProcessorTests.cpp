/*
    Checks on the processor around the compressor: which of the interface's
    inputs the instrument is taken from, the mono fan-out to both ears, bypass,
    the editor's parameter wiring, and preset save/restore.

    The input routing is the part worth guarding. On a Scarlett Solo the 1/4"
    instrument jack is input 2, which arrives as the *right* channel - process
    the left one and you get silence with nothing obviously broken.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "TestUtils.h"

#include <memory>

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int    blockSize  = 512;

    void setChoice (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, int index)
    {
        auto* parameter = apvts.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
    }

    void setValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float value)
    {
        auto* parameter = apvts.getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    /** Put `left` on channel 0 and `right` on channel 1, run one block, and
        report what came back out of each channel. */
    struct BlockResult { double out0, out1; };

    BlockResult runBlock (VirtualPedalboardProcessor& processor, float left, float right)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int i = 0; i < blockSize; ++i)
        {
            buffer.setSample (0, i, left);
            buffer.setSample (1, i, right);
        }

        processor.processBlock (buffer, midi);

        // Read the last sample so any smoothing has had the whole block to settle.
        return { (double) buffer.getSample (0, blockSize - 1),
                 (double) buffer.getSample (1, blockSize - 1) };
    }

    std::unique_ptr<VirtualPedalboardProcessor> makeProcessor (bool bypassed)
    {
        auto processor = std::make_unique<VirtualPedalboardProcessor>();
        processor->prepareToPlay (sampleRate, blockSize);
        setValue (processor->getValueTreeState(), "bypass", bypassed ? 1.0f : 0.0f);
        return processor;
    }
}

void runProcessorTests()
{
    std::puts ("\nInput routing (bass on input 2, i.e. the right channel):");
    {
        auto processor = makeProcessor (true);
        auto& apvts = processor->getValueTreeState();

        setChoice (apvts, "input", 0);                       // In 1 (left)
        auto result = runBlock (*processor, 0.0f, 0.5f);
        test::check ("In 1 selected, signal only on In 2 -> silence", result.out0, 0.0, 1.0e-6);

        setChoice (apvts, "input", 1);                       // In 2 (right)
        result = runBlock (*processor, 0.0f, 0.5f);
        test::check ("In 2 selected -> signal passes", result.out0, 0.5, 1.0e-6);

        setChoice (apvts, "input", 2);                       // Sum
        result = runBlock (*processor, 0.0f, 0.5f);
        test::check ("Sum selected -> half of each input", result.out0, 0.25, 1.0e-6);

        setChoice (apvts, "input", 0);
        result = runBlock (*processor, 0.3f, 0.0f);
        test::check ("In 1 selected, signal on In 1 -> passes", result.out0, 0.3, 1.0e-6);
    }

    std::puts ("\nMono fan-out:");
    {
        auto processor = makeProcessor (true);
        setChoice (processor->getValueTreeState(), "input", 1);

        const auto result = runBlock (*processor, 0.0f, 0.4f);
        test::check ("both ears get the same signal", result.out1, result.out0, 1.0e-9);
        test::checkTrue ("and it is not silence", std::abs (result.out0) > 0.1);
    }

    std::puts ("\nBypass:");
    {
        auto processor = makeProcessor (false);
        auto& apvts = processor->getValueTreeState();

        setChoice (apvts, "input", 1);
        setValue (apvts, "threshold", -40.0f);
        setValue (apvts, "ratio", 20.0f);
        setValue (apvts, "makeup", 0.0f);

        // Several blocks so the attack has time to clamp down.
        BlockResult compressed { 0.0, 0.0 };
        for (int i = 0; i < 20; ++i)
            compressed = runBlock (*processor, 0.0f, 0.5f);

        test::checkTrue ("engaged, a loud signal is pulled down",
                         std::abs (compressed.out0) < 0.4);
        test::checkTrue ("and the meter reports the reduction",
                         processor->getGainReductionDb() > 1.0f);

        setValue (apvts, "bypass", 1.0f);
        const auto bypassed = runBlock (*processor, 0.0f, 0.5f);
        test::check ("bypassed, the signal passes untouched", bypassed.out0, 0.5, 1.0e-6);
        test::check ("and the meter falls back to zero",
                     (double) processor->getGainReductionDb(), 0.0, 1.0e-9);
    }

    std::puts ("\nEditor wiring (catches a parameter ID that no longer exists):");
    {
        auto processor = makeProcessor (false);
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor->createEditor());

        test::checkTrue ("editor is created", editor != nullptr);
        test::checkTrue ("editor has a sensible size",
                         editor != nullptr && editor->getWidth() > 0 && editor->getHeight() > 0);
    }

    std::puts ("\nPreset state:");
    {
        auto processor = makeProcessor (false);
        auto& apvts = processor->getValueTreeState();

        setValue (apvts, "threshold", -33.5f);
        setValue (apvts, "release", 420.0f);

        juce::MemoryBlock state;
        processor->getStateInformation (state);

        setValue (apvts, "threshold", -6.0f);
        setValue (apvts, "release", 25.0f);

        processor->setStateInformation (state.getData(), (int) state.getSize());

        test::check ("threshold survives a save/restore",
                     (double) apvts.getRawParameterValue ("threshold")->load(), -33.5, 0.05);
        test::check ("release survives a save/restore",
                     (double) apvts.getRawParameterValue ("release")->load(), 420.0, 1.0);
    }
}
