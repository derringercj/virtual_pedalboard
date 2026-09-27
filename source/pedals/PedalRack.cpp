#include "PedalRack.h"

#include "CompressorProcessor.h"

namespace
{
    template <typename Pedal>
    PedalRack::Entry entryFor()
    {
        return { Pedal::pedalTypeId, Pedal::pedalName, [] { return std::make_unique<Pedal>(); } };
    }
}

const std::vector<PedalRack::Entry>& PedalRack::getEntries()
{
    static const std::vector<Entry> entries {
        entryFor<CompressorProcessor>(),
    };

    return entries;
}

std::unique_ptr<PedalProcessor> PedalRack::create (const juce::String& typeId)
{
    for (const auto& entry : getEntries())
        if (entry.typeId == typeId)
            return entry.create();

    return nullptr;
}
