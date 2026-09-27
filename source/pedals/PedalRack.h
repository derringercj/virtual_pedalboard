#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "PedalProcessor.h"

/**
    Every pedal that can be put on the board. The rack UI lists these, and a
    saved board is rebuilt by looking each pedal's type ID up here.

    Adding a pedal to the app means adding one line to getEntries().
*/
namespace PedalRack
{
    struct Entry
    {
        juce::String typeId, name;
        std::function<std::unique_ptr<PedalProcessor>()> create;
    };

    /** In the order the rack shows them. */
    const std::vector<Entry>& getEntries();

    /** nullptr for a type this build doesn't know, e.g. from a board saved by a
        newer version of the app. */
    std::unique_ptr<PedalProcessor> create (const juce::String& typeId);
}
