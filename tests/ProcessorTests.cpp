/*
    Checks on the board around the pedals: which of the interface's inputs the
    instrument is taken from, the mono fan-out to both ears, bypass, the pedal
    chain in the AudioProcessorGraph, the editor's panels, and save/restore.

    The input routing is the part worth guarding. On a Scarlett Solo the 1/4"
    instrument jack is input 2, which arrives as the *right* channel - process
    the left one and you get silence with nothing obviously broken.
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "pedals/CompressorProcessor.h"
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

    double getValue (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
    {
        return (double) apvts.getRawParameterValue (id)->load();
    }

    /** Put `left` on channel 0 and `right` on channel 1, run one block, and
        report what came back out of each channel. */
    struct BlockResult { double out0, out1; };

    BlockResult runBlock (VirtualPedalboardProcessor& board, float left, float right)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;

        for (int i = 0; i < blockSize; ++i)
        {
            buffer.setSample (0, i, left);
            buffer.setSample (1, i, right);
        }

        board.processBlock (buffer, midi);

        // Read the last sample so any smoothing has had the whole block to settle.
        return { (double) buffer.getSample (0, blockSize - 1),
                 (double) buffer.getSample (1, blockSize - 1) };
    }

    /** Feed a steady level into input 2 long enough for every pedal to settle,
        and return the output level in dB. */
    double settledOutputDb (VirtualPedalboardProcessor& board, double inputDb, int blocks = 40)
    {
        const auto level = (float) juce::Decibels::decibelsToGain (inputDb);
        BlockResult result { 0.0, 0.0 };

        for (int i = 0; i < blocks; ++i)
            result = runBlock (board, 0.0f, level);

        return juce::Decibels::gainToDecibels (std::abs (result.out0), -200.0);
    }

    CompressorProcessor* compressorAt (VirtualPedalboardProcessor& board, int index)
    {
        return dynamic_cast<CompressorProcessor*> (board.getPedal (index));
    }

    /** A board reading input 2, with its one compressor bypassed or engaged. */
    std::unique_ptr<VirtualPedalboardProcessor> makeBoard (bool bypassed)
    {
        auto board = std::make_unique<VirtualPedalboardProcessor>();
        board->prepareToPlay (sampleRate, blockSize);
        setChoice (board->getValueTreeState(), "input", 1);
        setValue (board->getPedal (0)->getValueTreeState(), "bypass", bypassed ? 1.0f : 0.0f);
        return board;
    }

    /** Hard knee so the settled levels are easy to work out by hand. */
    void setCompressor (PedalProcessor& pedal, float thresholdDb, float ratio, float makeupDb)
    {
        auto& apvts = pedal.getValueTreeState();
        setValue (apvts, "threshold", thresholdDb);
        setValue (apvts, "ratio", ratio);
        setValue (apvts, "knee", 0.0f);
        setValue (apvts, "makeup", makeupDb);
        setValue (apvts, "release", 10.0f);
    }
}

void runProcessorTests()
{
    std::puts ("\nInput routing (bass on input 2, i.e. the right channel):");
    {
        auto board = makeBoard (true);
        auto& apvts = board->getValueTreeState();

        setChoice (apvts, "input", 0);                       // In 1 (left)
        auto result = runBlock (*board, 0.0f, 0.5f);
        test::check ("In 1 selected, signal only on In 2 -> silence", result.out0, 0.0, 1.0e-6);

        setChoice (apvts, "input", 1);                       // In 2 (right)
        result = runBlock (*board, 0.0f, 0.5f);
        test::check ("In 2 selected -> signal passes", result.out0, 0.5, 1.0e-6);

        setChoice (apvts, "input", 2);                       // Sum
        result = runBlock (*board, 0.0f, 0.5f);
        test::check ("Sum selected -> half of each input", result.out0, 0.25, 1.0e-6);

        setChoice (apvts, "input", 0);
        result = runBlock (*board, 0.3f, 0.0f);
        test::check ("In 1 selected, signal on In 1 -> passes", result.out0, 0.3, 1.0e-6);
    }

    std::puts ("\nMono fan-out:");
    {
        auto board = makeBoard (true);

        const auto result = runBlock (*board, 0.0f, 0.4f);
        test::check ("both ears get the same signal", result.out1, result.out0, 1.0e-9);
        test::checkTrue ("and it is not silence", std::abs (result.out0) > 0.1);
    }

    std::puts ("\nBypass:");
    {
        auto board = makeBoard (false);
        auto* compressor = compressorAt (*board, 0);
        auto& apvts = compressor->getValueTreeState();

        setValue (apvts, "threshold", -40.0f);
        setValue (apvts, "ratio", 20.0f);
        setValue (apvts, "makeup", 0.0f);

        // Several blocks so the attack has time to clamp down.
        BlockResult compressed { 0.0, 0.0 };
        for (int i = 0; i < 20; ++i)
            compressed = runBlock (*board, 0.0f, 0.5f);

        test::checkTrue ("engaged, a loud signal is pulled down",
                         std::abs (compressed.out0) < 0.4);
        test::checkTrue ("and the meter reports the reduction",
                         compressor->getGainReductionDb() > 1.0f);

        setValue (apvts, "bypass", 1.0f);
        const auto bypassed = runBlock (*board, 0.0f, 0.5f);
        test::check ("bypassed, the signal passes untouched", bypassed.out0, 0.5, 1.0e-6);
        test::check ("and the meter falls back to zero",
                     (double) compressor->getGainReductionDb(), 0.0, 1.0e-9);
    }

    std::puts ("\nPedal chain:");
    {
        auto board = makeBoard (false);

        test::checkTrue ("a new board holds one compressor",
                         board->getNumPedals() == 1
                             && board->getPedal (0)->getTypeId() == CompressorProcessor::pedalTypeId);

        test::checkTrue ("an unknown pedal type is refused",
                         board->addPedal ("fuzz-from-the-future") == nullptr && board->getNumPedals() == 1);

        board->removePedal (0);
        test::check ("an empty board passes the bass straight through",
                     runBlock (*board, 0.0f, 0.5f).out0, 0.5, 1.0e-6);

        auto* first = board->addPedal (CompressorProcessor::pedalTypeId);
        test::check ("a pedal added while running is prepared at the board's rate",
                     first->getSampleRate(), sampleRate, 0.0);

        // Two identical 4:1 compressors in series: the second one squeezes what
        // the first let through. Worked out from the static curve by hand.
        auto* second = board->addPedal (CompressorProcessor::pedalTypeId);
        setCompressor (*first,  -18.0f, 4.0f, 0.0f);
        setCompressor (*second, -18.0f, 4.0f, 0.0f);

        const auto inputDb  = -6.0;
        const auto afterOne = inputDb  - 0.75 * (inputDb  + 18.0);
        const auto afterTwo = afterOne - 0.75 * (afterOne + 18.0);
        test::check ("two 4:1 compressors in series stack up",
                     settledOutputDb (*board, inputDb), afterTwo, 0.02);
    }

    std::puts ("\nPedal order:");
    {
        // A +12 dB boost and a 20:1 limiter at -18 dB, fed -24 dB. Boost first
        // and the limiter catches it; limiter first and it has nothing to do.
        auto board = makeBoard (false);
        auto* boost   = board->getPedal (0);
        auto* limiter = board->addPedal (CompressorProcessor::pedalTypeId);
        setCompressor (*boost,     0.0f,  1.0f, 12.0f);
        setCompressor (*limiter, -18.0f, 20.0f,  0.0f);

        test::check ("boost -> limiter", settledOutputDb (*board, -24.0), -18.0 + 6.0 / 20.0, 0.02);

        board->movePedal (1, 0);
        test::checkTrue ("moving puts the limiter first",
                         board->getPedal (0) == limiter && board->getPedal (1) == boost);
        test::check ("limiter -> boost", settledOutputDb (*board, -24.0), -12.0, 0.02);

        auto* third = board->addPedal (CompressorProcessor::pedalTypeId, 1);
        test::checkTrue ("adding at an index inserts there",
                         board->getPedal (1) == third && board->getPedal (2) == boost);

        board->removePedal (1);
        test::checkTrue ("removing from the middle keeps the rest in order",
                         board->getNumPedals() == 2
                             && board->getPedal (0) == limiter && board->getPedal (1) == boost);
        test::check ("and the audio is wired back up", settledOutputDb (*board, -24.0), -12.0, 0.02);
    }

    std::puts ("\nEditor (catches a parameter ID that no longer exists):");
    {
        auto board = makeBoard (false);
        std::unique_ptr<juce::AudioProcessorEditor> editor (board->createEditor());
        auto* boardEditor = dynamic_cast<VirtualPedalboardEditor*> (editor.get());

        test::checkTrue ("editor is created", boardEditor != nullptr);
        test::checkTrue ("editor has a sensible size",
                         editor != nullptr && editor->getWidth() > 0 && editor->getHeight() > 0);
        test::checkTrue ("one panel for the one pedal",
                         boardEditor != nullptr && boardEditor->getNumPedalPanels() == 1);

        board->addPedal (CompressorProcessor::pedalTypeId);
        test::checkTrue ("adding a pedal adds its panel straight away",
                         boardEditor != nullptr && boardEditor->getNumPedalPanels() == 2);
        test::checkTrue ("and the new pedal knows its panel is open",
                         board->getPedal (1)->getActiveEditor() != nullptr);

        // Would trip JUCE's "editor outlived its processor" assertion if the
        // panel weren't torn down before the pedal is released.
        board->removePedal (0);
        test::checkTrue ("removing a pedal with its panel open drops the panel",
                         boardEditor != nullptr && boardEditor->getNumPedalPanels() == 1);

        editor.reset();
        test::checkTrue ("closing the board closes every pedal's panel",
                         board->getPedal (0)->getActiveEditor() == nullptr);
    }

    std::puts ("\nSaved state:");
    {
        auto board = makeBoard (false);
        setChoice (board->getValueTreeState(), "input", 2);
        board->addPedal (CompressorProcessor::pedalTypeId);

        setValue (board->getPedal (0)->getValueTreeState(), "threshold", -33.5f);
        setValue (board->getPedal (0)->getValueTreeState(), "release", 420.0f);
        setValue (board->getPedal (1)->getValueTreeState(), "threshold", -12.0f);
        setValue (board->getPedal (1)->getValueTreeState(), "bypass", 1.0f);

        juce::MemoryBlock state;
        board->getStateInformation (state);

        // Scramble everything, then restore.
        setChoice (board->getValueTreeState(), "input", 0);
        board->removePedal (1);
        setValue (board->getPedal (0)->getValueTreeState(), "threshold", -6.0f);
        board->addPedal (CompressorProcessor::pedalTypeId);
        board->addPedal (CompressorProcessor::pedalTypeId);

        board->setStateInformation (state.getData(), (int) state.getSize());

        test::checkTrue ("the chain comes back with two pedals", board->getNumPedals() == 2);

        if (board->getNumPedals() == 2)
        {
            auto& first  = board->getPedal (0)->getValueTreeState();
            auto& second = board->getPedal (1)->getValueTreeState();

            test::check ("input selector survives",       getValue (board->getValueTreeState(), "input"), 2.0, 0.0);
            test::check ("pedal 1 threshold survives",    getValue (first,  "threshold"), -33.5, 0.05);
            test::check ("pedal 1 release survives",      getValue (first,  "release"),   420.0, 1.0);
            test::check ("pedal 2 threshold survives",    getValue (second, "threshold"), -12.0, 0.05);
            test::checkTrue ("pedal 2 is still bypassed", board->getPedal (1)->isBypassed());
            test::checkTrue ("pedal 1 is still engaged",  ! board->getPedal (0)->isBypassed());
        }

        // Summed input halves it; -46 dB sits under both thresholds, so it passes clean.
        test::check ("and the restored chain is wired up",
                     runBlock (*board, 0.0f, 0.01f).out0, 0.005, 1.0e-6);
    }

    std::puts ("\nSaved state with a pedal this build doesn't know:");
    {
        auto board = makeBoard (false);

        juce::MemoryBlock state;
        board->getStateInformation (state);

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        auto* chain = xml->getChildByName ("Chain");
        auto* unknown = chain->createNewChildElement ("Pedal");
        unknown->setAttribute ("type", "fuzz-from-the-future");
        juce::AudioProcessor::copyXmlToBinary (*xml, state);

        board->setStateInformation (state.getData(), (int) state.getSize());
        test::checkTrue ("the unknown pedal is skipped, the rest restored",
                         board->getNumPedals() == 1
                             && board->getPedal (0)->getTypeId() == CompressorProcessor::pedalTypeId);
    }

    std::puts ("\nSettings saved before the pedal chain existed:");
    {
        // What the app used to save: one flat parameter list for everything.
        juce::ValueTree legacy { "VirtualPedalboard" };

        for (const auto& [id, value] : { std::pair { "input", 0.0f }, { "bypass", 1.0f },
                                         { "threshold", -33.5f }, { "release", 420.0f } })
            legacy.appendChild (juce::ValueTree { "PARAM", { { "id", id }, { "value", value } } }, nullptr);

        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (*legacy.createXml(), state);

        auto board = makeBoard (false);
        board->addPedal (CompressorProcessor::pedalTypeId);
        board->setStateInformation (state.getData(), (int) state.getSize());

        test::checkTrue ("become a board with one compressor",
                         board->getNumPedals() == 1
                             && board->getPedal (0)->getTypeId() == CompressorProcessor::pedalTypeId);

        if (board->getNumPedals() == 1)
        {
            auto& compressor = board->getPedal (0)->getValueTreeState();

            test::check ("input selector carries over", getValue (board->getValueTreeState(), "input"), 0.0, 0.0);
            test::check ("threshold carries over",      getValue (compressor, "threshold"), -33.5, 0.05);
            test::check ("release carries over",        getValue (compressor, "release"),   420.0, 1.0);
            test::checkTrue ("bypass carries over",     board->getPedal (0)->isBypassed());
        }
    }
}
