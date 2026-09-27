#pragma once

#include <juce_graphics/juce_graphics.h>

/** Colours shared by the board and every pedal panel, so they look like one app. */
namespace theme
{
    inline const juce::Colour board    { 0xff1a1c20 };
    inline const juce::Colour panel    { 0xff23262b };
    inline const juce::Colour trough   { 0xff15171a };
    inline const juce::Colour accent   { 0xffe8b23a };
    inline const juce::Colour hairline { 0xff35393f };
}
