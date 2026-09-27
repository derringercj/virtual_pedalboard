#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "pedals/CompressorProcessor.h"
#include "pedals/PedalRack.h"

#include <utility>

namespace
{
    // Saved-state layout:
    //   <VirtualPedalboard>
    //     <Board> ...board parameters... </Board>
    //     <Chain>
    //       <Pedal type="compressor" state="...pedal's own state, base64..."/>
    //     </Chain>
    //   </VirtualPedalboard>
    const juce::Identifier stateTag     { "VirtualPedalboard" };
    const juce::Identifier chainTag     { "Chain" };
    const juce::Identifier pedalTag     { "Pedal" };
    const juce::Identifier typeProperty { "type" };
    const juce::Identifier dataProperty { "state" };
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VirtualPedalboardProcessor::createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { "input", 1 }, "Input",
        StringArray { "In 1 (left)", "In 2 (right)", "Sum 1 + 2" }, 1));

    return layout;
}

//==============================================================================
VirtualPedalboardProcessor::VirtualPedalboardProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, juce::Identifier ("Board"), createParameterLayout())
{
    inputParam = parameters.getRawParameterValue ("input");

    // The chain is mono, like the real thing: one channel in from the input
    // selector, one channel out to the fan-out. This has to be set before the
    // I/O nodes are added, because they size themselves from the graph.
    graph.setPlayConfigDetails (1, 1, getSampleRate(), getBlockSize());

    using IO = Graph::AudioGraphIOProcessor;
    inputNode  = graph.addNode (std::make_unique<IO> (IO::audioInputNode),  std::nullopt, UpdateKind::none);
    outputNode = graph.addNode (std::make_unique<IO> (IO::audioOutputNode), std::nullopt, UpdateKind::none);

    addPedal (CompressorProcessor::pedalTypeId);
}

//==============================================================================
void VirtualPedalboardProcessor::prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock)
{
    // Prepares every pedal now, and any pedal added later as it joins.
    graph.prepareToPlay (sampleRate, maximumExpectedSamplesPerBlock);
}

void VirtualPedalboardProcessor::releaseResources()
{
    graph.releaseResources();
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

//==============================================================================
void VirtualPedalboardProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
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

    // --- 2. The pedal chain ---
    // A one-channel view onto channel 0. It points at the same memory, so the
    // graph works on the bass in place with no copying or allocation here.
    juce::AudioBuffer<float> monoBuffer (buffer.getArrayOfWritePointers(), 1, numSamples);
    graph.processBlock (monoBuffer, midi);

    // --- 3. Fan the mono result back out to every output the device gives us ---
    for (auto ch = 1; ch < numOuts; ++ch)
        juce::FloatVectorOperations::copy (buffer.getWritePointer (ch), mono, numSamples);
}

//==============================================================================
PedalProcessor* VirtualPedalboardProcessor::getPedal (int index) const noexcept
{
    if (! juce::isPositiveAndBelow (index, getNumPedals()))
        return nullptr;

    // Only PedalRack puts processors into the chain, so this always succeeds.
    return dynamic_cast<PedalProcessor*> (chain[(size_t) index]->getProcessor());
}

PedalProcessor* VirtualPedalboardProcessor::addPedal (const juce::String& typeId, int index)
{
    auto pedal = PedalRack::create (typeId);

    if (pedal == nullptr)
        return nullptr;

    if (! juce::isPositiveAndNotGreaterThan (index, getNumPedals()))
        index = getNumPedals();

    auto* added = pedal.get();
    insertPedalNode (std::move (pedal), index);
    chainChanged();
    return added;
}

void VirtualPedalboardProcessor::removePedal (int index)
{
    if (! juce::isPositiveAndBelow (index, getNumPedals()))
        return;

    // Holding this keeps the pedal alive until listeners have heard about it.
    const auto removed = chain[(size_t) index];

    chain.erase (chain.begin() + index);
    graph.removeNode (removed->nodeID, UpdateKind::none);
    chainChanged();
}

void VirtualPedalboardProcessor::movePedal (int fromIndex, int toIndex)
{
    if (! juce::isPositiveAndBelow (fromIndex, getNumPedals())
        || ! juce::isPositiveAndBelow (toIndex, getNumPedals())
        || fromIndex == toIndex)
        return;

    auto node = chain[(size_t) fromIndex];
    chain.erase (chain.begin() + fromIndex);
    chain.insert (chain.begin() + toIndex, std::move (node));
    chainChanged();
}

void VirtualPedalboardProcessor::insertPedalNode (std::unique_ptr<PedalProcessor> pedal, int index)
{
    JUCE_ASSERT_MESSAGE_THREAD

    if (auto node = graph.addNode (std::move (pedal), std::nullopt, UpdateKind::none))
        chain.insert (chain.begin() + index, std::move (node));
}

void VirtualPedalboardProcessor::replaceChain (std::vector<std::unique_ptr<PedalProcessor>> pedals)
{
    // Holding these keeps the old pedals alive until listeners have heard about it.
    const auto oldChain = std::exchange (chain, {});

    for (const auto& node : oldChain)
        graph.removeNode (node->nodeID, UpdateKind::none);

    for (auto& pedal : pedals)
        insertPedalNode (std::move (pedal), getNumPedals());

    chainChanged();
}

void VirtualPedalboardProcessor::chainChanged()
{
    JUCE_ASSERT_MESSAGE_THREAD

    // Rewire the whole line from scratch: input -> pedal -> pedal -> ... -> output.
    // Every step is UpdateKind::none so the graph is rebuilt once at the end,
    // rather than the audio thread briefly hearing a half-wired chain.
    for (const auto& connection : graph.getConnections())
        graph.removeConnection (connection, UpdateKind::none);

    auto previous = inputNode->nodeID;

    for (const auto& node : chain)
    {
        [[maybe_unused]] const auto connected = graph.addConnection ({ { previous, 0 }, { node->nodeID, 0 } },
                                                                     UpdateKind::none);
        jassert (connected);
        previous = node->nodeID;
    }

    [[maybe_unused]] const auto connected = graph.addConnection ({ { previous, 0 }, { outputNode->nodeID, 0 } },
                                                                 UpdateKind::none);
    jassert (connected);

    graph.rebuild();

    chainListeners.call ([] (ChainListener& listener) { listener.pedalChainChanged(); });
}

//==============================================================================
juce::AudioProcessorEditor* VirtualPedalboardProcessor::createEditor()
{
    return new VirtualPedalboardEditor (*this);
}

void VirtualPedalboardProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree chainState { chainTag };

    for (int i = 0; i < getNumPedals(); ++i)
    {
        auto* pedal = getPedal (i);

        // Each pedal saves itself, the same way a plugin saves itself for a DAW.
        juce::MemoryBlock pedalData;
        pedal->getStateInformation (pedalData);

        chainState.appendChild (juce::ValueTree { pedalTag, { { typeProperty, pedal->getTypeId() },
                                                              { dataProperty, pedalData.toBase64Encoding() } } },
                                nullptr);
    }

    juce::ValueTree state { stateTag };
    state.appendChild (parameters.copyState(), nullptr);
    state.appendChild (chainState, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void VirtualPedalboardProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (stateTag.toString()))
        return;

    const auto state      = juce::ValueTree::fromXml (*xml);
    const auto chainState = state.getChildWithName (chainTag);

    std::vector<std::unique_ptr<PedalProcessor>> pedals;

    if (chainState.isValid())
    {
        if (const auto board = state.getChildWithName (parameters.state.getType()); board.isValid())
            parameters.replaceState (board);

        for (const auto& pedalState : chainState)
        {
            // Skips a pedal this build doesn't know, rather than failing the whole board.
            auto pedal = PedalRack::create (pedalState[typeProperty].toString());

            if (pedal == nullptr)
                continue;

            juce::MemoryBlock pedalData;

            if (pedalData.fromBase64Encoding (pedalState[dataProperty].toString()))
                pedal->setStateInformation (pedalData.getData(), (int) pedalData.getSize());

            pedals.push_back (std::move (pedal));
        }
    }
    else
    {
        pedals.push_back (restoreLegacyState (state));
    }

    replaceChain (std::move (pedals));
}

std::unique_ptr<PedalProcessor> VirtualPedalboardProcessor::restoreLegacyState (const juce::ValueTree& state)
{
    // Before the pedal chain existed, the app saved one flat list of parameters:
    // the input selector plus the single compressor's knobs. That becomes a
    // board holding one compressor, so an upgrade doesn't lose anyone's settings.
    auto compressor = PedalRack::create (CompressorProcessor::pedalTypeId);

    for (const auto& parameterState : state)
    {
        const auto id    = parameterState["id"].toString();
        const auto value = (float) parameterState["value"];

        for (auto* apvts : { &parameters, &compressor->getValueTreeState() })
            if (auto* parameter = apvts->getParameter (id))
                parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    return compressor;
}

//==============================================================================
// Entry point the plugin wrappers (including the standalone app) call.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VirtualPedalboardProcessor();
}
