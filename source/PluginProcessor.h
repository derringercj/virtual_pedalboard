#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>
#include <vector>

#include "pedals/PedalProcessor.h"

/**
    The board: picks the bass out of the interface's inputs, runs it through
    the pedal chain, and sends the result to both ears.

    The chain lives in a juce::AudioProcessorGraph, one node per pedal, wired in
    a line from the graph's input to its output. Adding, removing or reordering
    pedals rewires the graph on the message thread; the graph hands the new
    arrangement to the audio thread without ever making it wait.
*/
class VirtualPedalboardProcessor final : public juce::AudioProcessor
{
public:
    /** Which of the interface's inputs the instrument is actually plugged into.
        On a Scarlett Solo the 1/4" instrument jack is input 2, i.e. "right". */
    enum class InputMode { first = 0, second, sum };

    /** Told whenever pedals are added, removed or reordered. Called synchronously
        on the message thread, before a removed pedal can be deleted, so a
        listener can safely let go of anything that points at it. */
    struct ChainListener
    {
        virtual ~ChainListener() = default;
        virtual void pedalChainChanged() = 0;
    };

    VirtualPedalboardProcessor();
    ~VirtualPedalboardProcessor() override = default;

    //==============================================================================
    // We only do single precision; this keeps the unused double overload visible
    // so the compiler stops warning that we hid it.
    using juce::AudioProcessor::processBlock;

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
    /** Board-wide settings, i.e. the input selector. Each pedal has its own. */
    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }

    //==============================================================================
    // The pedal chain, in signal order. Message thread only.
    int getNumPedals() const noexcept { return (int) chain.size(); }
    PedalProcessor* getPedal (int index) const noexcept;

    /** Puts a new pedal of this type into the chain at `index`, or at the end
        if `index` is out of range. Returns nullptr for an unknown type. */
    PedalProcessor* addPedal (const juce::String& typeId, int index = -1);
    void removePedal (int index);

    /** Takes the pedal at `fromIndex` out and puts it back so it ends up at `toIndex`. */
    void movePedal (int fromIndex, int toIndex);

    void addChainListener (ChainListener* listener)    { chainListeners.add (listener); }
    void removeChainListener (ChainListener* listener) { chainListeners.remove (listener); }

private:
    //==============================================================================
    using Graph      = juce::AudioProcessorGraph;
    using Node       = Graph::Node;
    using UpdateKind = Graph::UpdateKind;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void insertPedalNode (std::unique_ptr<PedalProcessor>, int index);
    void replaceChain (std::vector<std::unique_ptr<PedalProcessor>>);
    void chainChanged();
    std::unique_ptr<PedalProcessor> restoreLegacyState (const juce::ValueTree&);

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* inputParam = nullptr;   // cached for the audio thread

    Graph graph;
    Node::Ptr inputNode, outputNode;
    std::vector<Node::Ptr> chain;

    juce::ListenerList<ChainListener> chainListeners;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VirtualPedalboardProcessor)
};
