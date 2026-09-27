# Pedals

A pedal is one effect on the board. Every pedal is a complete
`juce::AudioProcessor` with its own parameters, saved state, bypass switch and
editor. That's what lets the board hold several of the same pedal, and lets the
`AudioProcessorGraph` run them in any order.

- [`PedalProcessor`](#pedalprocessor): `source/pedals/PedalProcessor.h/.cpp`
- [`PedalRack`](#pedalrack): `source/pedals/PedalRack.h/.cpp`
- [`CompressorProcessor`](#compressorprocessor): `source/pedals/CompressorProcessor.h/.cpp`
- [`CompressorEditor` and `GainReductionMeter`](#compressoreditor): `source/pedals/CompressorEditor.h/.cpp`
- [Adding a new pedal](#adding-a-new-pedal)

---

## PedalProcessor

Abstract base class, derives from `juce::AudioProcessor`. Every pedal derives
from this.

### What it gives every pedal

- **Mono buses.** One mono input and one mono output. `isBusesLayoutSupported`
  accepts nothing else.
- **A `bypass` parameter.** It's added to whatever layout the subclass supplies,
  so no pedal has to declare its own. `getBypassParameter()` returns it, which
  tells JUCE (and the graph) that the pedal handles bypass itself.
- **Its own APVTS**, in the protected `parameters` member. The state's type
  name is the pedal's type ID.
- **Saved state.** `getStateInformation` and `setStateInformation` save and load
  the APVTS as XML.
- **A type ID and display name.**
- **JUCE boilerplate.** No MIDI, zero tail, one program, has an editor.

### What subclasses must provide

- A constructor that passes a type ID, a display name and a parameter layout to
  `PedalProcessor`.
- `prepareToPlay`, `releaseResources` and `processBlock`.
- `createEditor`.

### Bypass is up to each pedal

The base class provides `isBypassed()` but doesn't act on it. What bypass means
depends on the effect: a compressor should let go at once, but a delay might
let its echoes ring out. Each pedal checks `isBypassed()` in its own `processBlock`.

### Public API

| Member | Purpose |
|--------|---------|
| `getTypeId()` | The ID written into saved boards, e.g. `"compressor"`. **Never rename one**, or boards saved with the old name lose that pedal. |
| `getValueTreeState()` | The pedal's APVTS, for editors, tests and save/restore. |
| `isBypassed()` | Reads the cached `bypass` value. Safe on the audio thread. |
| `getName()` | The display name. |

---

## PedalRack

A namespace, not a class. It's the one place that lists every pedal type the
board can create.

```cpp
namespace PedalRack
{
    struct Entry
    {
        juce::String typeId, name;
        std::function<std::unique_ptr<PedalProcessor>()> create;
    };

    const std::vector<Entry>& getEntries();
    std::unique_ptr<PedalProcessor> create (const juce::String& typeId);
}
```

| Function | Purpose |
|----------|---------|
| `getEntries()` | Every pedal type, in the order a rack UI should list them. The list is built once, on first use. |
| `create (typeId)` | A new pedal of that type, or `nullptr` if the type is unknown (for example, from a board saved by a newer version). |

Entries are made with a small template, `entryFor<Pedal>()`, which reads the
pedal class's `pedalTypeId` and `pedalName` constants. Each pedal's ID and name
are written once, in the pedal class.

The board uses `PedalRack::create` whenever it adds or restores a pedal. A
future rack UI would use `getEntries()` to show what's available.

---

## CompressorProcessor

`final`, derives from `PedalProcessor`. The compressor as a pedal: it wraps
the `CompressorPedal` DSP ([dsp.md](dsp.md)) with parameters, bypass and a
meter value.

| Constant | Value |
|----------|-------|
| `pedalTypeId` | `"compressor"` |
| `pedalName` | `"Compressor"` |

### Parameters

`threshold`, `ratio`, `knee`, `attack`, `release`, `makeup`, plus the inherited
`bypass`. Ranges and defaults are in
[architecture.md](architecture.md#parameters). They're defined in
`createParameterLayout()`.

### Methods

| Method | What it does |
|--------|--------------|
| constructor | Caches an `std::atomic<float>*` for every parameter, so the audio thread never looks one up by name. |
| `prepareToPlay` | Prepares the DSP for the sample rate (one channel), pushes the current parameters, and resets. |
| `releaseResources` | Resets. |
| `reset` | Clears the DSP's gain reduction and zeroes the meter value. |
| `processBlock` | See below. |
| `createEditor` | Returns a `CompressorEditor`. |
| `getGainReductionDb` | The largest gain reduction, in dB, during the most recent block. Read by the meter. |

`processBlock`:

1. Pushes every parameter value into the DSP. The DSP ignores values that
   haven't changed, so this is cheap.
2. **If bypassed**, resets (the compressor lets go completely and the meter reads
   zero) and returns without touching the audio.
3. Otherwise runs every sample through `CompressorPedal::processSample`, keeping
   track of the largest gain reduction.
4. Stores that largest reduction in the `gainReductionDb` atomic for the meter.

The meter shows each block's **maximum** rather than its last value, so short
peaks of compression aren't missed between UI frames.

---

## CompressorEditor

`final`, derives from `juce::AudioProcessorEditor`. The compressor's panel on
the board, 780 × 380 pixels.

```
┌────────────────────────────────────────────────────────────┐
│ COMPRESSOR                                                 │
│────────────────────────────────────────────────────────────│
│ Gain reduction  [███████████                    -6.2 dB ]  │
│                                                            │
│ Threshold  Ratio   Knee   Attack  Release  Makeup          │
│   (◯)       (◯)     (◯)    (◯)     (◯)      (◯)            │
│                                                            │
│ [ BYPASS ]                                                 │
└────────────────────────────────────────────────────────────┘
```

| Member | Purpose |
|--------|---------|
| `pedal` | The compressor this panel controls. |
| `titleLabel`, `meterLabel` | Text. |
| `meter` | A `GainReductionMeter`. |
| `bypassButton`, `bypassAttachment` | Toggle button attached to `bypass`. It turns dark red when on. |
| `knobs` | One `Knob` per parameter: a rotary `Slider`, a `Label` and a `SliderAttachment`. |

`addKnob (parameterID, text, suffix)` creates one knob and attaches it to the
named parameter. The attachment keeps the knob and the parameter in sync both
ways, and on the right threads.

### GainReductionMeter

A `juce::Component` and a `juce::Timer`. A horizontal bar showing how many dB
the compressor is taking off right now.

- It reads `CompressorProcessor::getGainReductionDb()` 30 times a second.
- The bar **jumps up** to a new peak immediately, and **falls back** by at most
  0.8 dB per frame (about 24 dB a second), so it stays readable.
- Full scale is 24 dB of reduction.

---

## Adding a new pedal

Here's the whole process, using a simple boost pedal as the example.

### 1. Write the processor

`source/pedals/BoostProcessor.h`:

```cpp
#pragma once

#include "PedalProcessor.h"

class BoostProcessor final : public PedalProcessor
{
public:
    static constexpr const char* pedalTypeId = "boost";   // never change once released
    static constexpr const char* pedalName   = "Boost";

    BoostProcessor();

    using juce::AudioProcessor::processBlock;

    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::atomic<float>* gainParam = nullptr;
    juce::SmoothedValue<float> gain { 1.0f };
};
```

In the `.cpp`, following the pattern in `CompressorProcessor.cpp`:

- `createParameterLayout()` adds a `gain` parameter in dB. **Don't add
  `bypass`**, because the base class does.
- The constructor passes `pedalTypeId`, `pedalName` and the layout to
  `PedalProcessor`, then caches `gainParam`.
- `prepareToPlay` calls `gain.reset (sampleRate, 0.02)` so knob moves don't
  zipper.
- `processBlock` returns early if `isBypassed()`. Otherwise it sets the smoother's
  target from `gainParam` and multiplies each sample of channel 0 by
  `gain.getNextValue()`.

### 2. Give it a panel

To start with, `juce::GenericAudioProcessorEditor` builds a basic panel with a
slider for every parameter:

```cpp
juce::AudioProcessorEditor* BoostProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}
```

Replace it later with a custom editor like `CompressorEditor`, using the
colours in `ui/Theme.h`.

### 3. Register it in the rack

In `PedalRack.cpp`:

```cpp
#include "BoostProcessor.h"
...
static const std::vector<Entry> entries {
    entryFor<CompressorProcessor>(),
    entryFor<BoostProcessor>(),
};
```

### 4. Add the source files to CMake

Add the `.cpp` to `VPB_SOURCES` in `CMakeLists.txt`. The app and the tests
both use that list.

### 5. Test it

- Put the pedal's DSP in `source/dsp/` if it's more than a few lines, and test
  it directly like `tests/CompressorTests.cpp` does.
- Add a check to `tests/ProcessorTests.cpp` that `board->addPedal ("boost")`
  works and changes the output level by the expected amount.

Nothing in the board, the editor or the save format needs to change. Saved
boards pick up the new pedal type automatically.
