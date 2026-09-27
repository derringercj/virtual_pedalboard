# Architecture

## Layers

The code is split into four layers. Each one only knows about the layers below it.

```
┌──────────────────────────────────────────────────────────────────┐
│ Board          PluginProcessor / PluginEditor                    │
│                input selection, the chain, save/restore, window  │
├──────────────────────────────────────────────────────────────────┤
│ Registry       pedals/PedalRack                                  │
│                turns a type ID like "compressor" into a pedal    │
├──────────────────────────────────────────────────────────────────┤
│ Pedals         pedals/PedalProcessor (base)                      │
│                pedals/CompressorProcessor + CompressorEditor     │
│                parameters, bypass, panel, one AudioProcessor each│
├──────────────────────────────────────────────────────────────────┤
│ DSP            dsp/CompressorPedal                               │
│                plain signal processing, no JUCE processor types  │
└──────────────────────────────────────────────────────────────────┘
```

- **DSP** classes process samples and nothing else. They have no parameters
  objects, editors or threads, so they are easy to test in isolation
  (`tests/CompressorTests.cpp` does exactly that).
- **Pedals** wrap a DSP class in a `juce::AudioProcessor`. That gives each one
  parameters, saved state, a bypass switch and a panel, and lets the graph run it.
- **The registry** is the only place that knows the full list of pedal types.
  The board asks it for pedals by type ID. The only pedal the board names
  directly is the compressor, which it creates for a fresh board and when
  converting old settings.
- **The board** owns the chain and talks to the audio device (or DAW).

## Signal flow

```
 audio interface
   In 1 (left) ──┐
                 ├─► input selector ─► mono ─► ┌──────── AudioProcessorGraph ────────┐
   In 2 (right) ─┘   (board "input")           │ input ─► pedal ─► pedal ─► … ─► output│
                                               └──────────────────────────────────────┘
                                                                   │
                                            both outputs ◄─ fan-out┘
```

Step by step, in `VirtualPedalboardProcessor::processBlock`:

1. **Pick the bass.** The interface delivers two channels. The `input` parameter
   chooses input 1, input 2, or both averaged, and the result goes into channel 0.
   On a Scarlett Solo the instrument jack is input 2, which is why that's the default.
2. **Run the chain.** A one-channel `AudioBuffer` that points at channel 0 is
   passed to the graph. The graph runs every pedal in order, in place.
3. **Fan out.** Channel 0 is copied to every other output so both ears hear it.

Everything between steps 1 and 3 is **mono**. A bass rig is a mono signal chain,
and keeping pedals mono makes each one simpler.

## Why an AudioProcessorGraph

The player will be able to add, remove and reorder pedals while audio is playing.
That means the chain changes on the message thread while the audio thread is
reading it. Doing this safely by hand is hard: the audio thread must never wait,
and a removed pedal must not be deleted while the audio thread is still using it.

`juce::AudioProcessorGraph` solves both:

- Each edit builds a new "render sequence" on the message thread. At the top of
  its next block, the audio thread takes it with a try-lock, so it never waits.
- Each render sequence holds a reference to every pedal it runs. A removed pedal
  stays alive until the old sequence is released, and that happens on the
  message thread, never the audio thread.

`juce::dsp::ProcessorChain` was the other option. It was ruled out because its
order is fixed when the code compiles, so pedals couldn't be reordered at runtime.

## Threads

| Thread | Runs | Touches |
|--------|------|---------|
| **Audio** | `processBlock` on the board and every pedal | Parameter values (through cached `std::atomic<float>*`), DSP state, the graph's current render sequence, each compressor's `gainReductionDb` atomic |
| **Message** | UI, chain edits, save/restore, `prepareToPlay` for newly added pedals | The chain vector, the graph's nodes and connections, editors, APVTS state |
| **Graph timer** (message thread) | Releases old render sequences every 500 ms | Deletes pedals that have been removed |

Rules the code follows:

- **The audio thread never looks a parameter up by name.** Each processor caches
  its `std::atomic<float>*` pointers in its constructor.
- **The audio thread never allocates.** The mono view in step 2 above points at
  existing memory, and the graph preallocates its buffers.
- **Chain edits are message-thread only.** `insertPedalNode` and `chainChanged`
  assert it with `JUCE_ASSERT_MESSAGE_THREAD`.
- **Meters use atomics.** The audio thread writes each block's gain reduction
  into an atomic, and a 30 Hz UI timer reads it.

## Ownership and lifetimes

```
VirtualPedalboardProcessor
 ├── parameters (APVTS: "input")
 ├── graph (AudioProcessorGraph)
 │    ├── input node   (AudioGraphIOProcessor)
 │    ├── output node  (AudioGraphIOProcessor)
 │    └── pedal nodes  (each owns one PedalProcessor)
 └── chain (std::vector<Node::Ptr>, signal order)

VirtualPedalboardEditor
 └── panels (each owns one pedal's AudioProcessorEditor)
```

- **Nodes are reference counted** (`Node::Ptr`). A node can be held by the graph,
  by `chain`, and by any render sequence still in use. The pedal inside is
  deleted when the last reference goes away.
- **A pedal's panel must be deleted before the pedal.** JUCE asserts this in
  `~AudioProcessor`. Removal can delete the pedal later on the graph's timer, so
  the board tells the editor synchronously, through `ChainListener`, while it
  still holds a reference to the removed node. The editor drops the panel inside
  that call, before the pedal can go.
- **Panels are deleted the way a plugin host deletes an editor.** The pedal is
  told first (`editorBeingDeleted`), then the editor is deleted. That's what
  `VirtualPedalboardEditor::PanelDeleter` does. Skipping the first step trips an
  assertion in `~AudioProcessorEditor`.

## Parameters

Parameters are split by owner. There is no single global list.

| Owner | Parameter ID | Type | Range | Default |
|-------|--------------|------|-------|---------|
| Board | `input` | choice | In 1 (left), In 2 (right), Sum 1 + 2 | In 2 |
| Every pedal | `bypass` | bool | | off |
| Compressor | `threshold` | float, dB | -60 to 0 | -18 |
| Compressor | `ratio` | float, :1 | 1 to 20, skewed around 4 | 4 |
| Compressor | `knee` | float, dB | 0 to 24 | 6 |
| Compressor | `attack` | float, ms | 0.1 to 200, skewed around 15 | 10 |
| Compressor | `release` | float, ms | 10 to 1000, skewed around 150 | 150 |
| Compressor | `makeup` | float, dB | -12 to +24 | 0 |

"Skewed around X" means X sits at the halfway point of the knob, so the useful
part of the range isn't crammed into the first few degrees.

Since each pedal has its own APVTS, two compressors on the board each have their
own `threshold`, and the same IDs can be reused across pedal types.

## Saved state

The board saves itself as XML (inside JUCE's binary wrapper):

```xml
<VirtualPedalboard>
  <Board>
    <PARAM id="input" value="1"/>
  </Board>
  <Chain>
    <Pedal type="compressor" state="…base64…"/>
    <Pedal type="compressor" state="…base64…"/>
  </Chain>
</VirtualPedalboard>
```

- `Board` is the board's APVTS state.
- Each `Pedal` records its type ID and whatever that pedal's own
  `getStateInformation` produced, base64 encoded. The board doesn't need to know
  what's inside, which is the same contract a DAW uses with plugins.
- Pedals are listed in chain order.

When restoring:

- A pedal type this build doesn't know is **skipped** and the rest of the board
  still loads.
- A state with no `Chain` element comes from the **old single-compressor
  version**, which saved one flat list of `PARAM` elements. It is converted into
  a board with one compressor: `input` goes to the board and everything else goes
  to the compressor.

The standalone app saves this state when it quits and restores it on launch
(on Windows, in `%APPDATA%\Juice\Virtual Pedalboard.settings`).

## Design decisions

- **The compressor is hand-written** rather than `juce::dsp::Compressor`. JUCE's
  version has no soft knee, no makeup gain and no gain-reduction readout, and its
  attack and release times are scaled by 1/2π, so a "10 ms" knob acts like about
  1.6 ms. See [dsp.md](dsp.md#compared-with-jucedspcompressor).
- **JUCE's built-ins are used where the hard part is easy to get wrong**: the
  graph for runtime rewiring, `SmoothedValue` for zipper-free knobs, `APVTS` for
  parameters and state.
- **The board doesn't override `reset()`.** JUCE's `AudioProcessorGraph::reset()`
  walks the node list without a lock, so a host calling it on the audio thread
  while a pedal is being added would race.

## Known limitations

- **No rack UI yet.** The board has `addPedal`, `removePedal` and `movePedal`,
  but nothing in the window calls them. The board always starts with one
  compressor.
- **Pedal parameters aren't visible to a DAW.** As a VST3, only the board's own
  `input` parameter is exposed for automation. Pedal parameters live in nested
  processors the host can't see.
- **`setStateInformation` assumes the message thread.** That's true for the
  standalone app and for most VST3 hosts. A host that restores state from
  another thread would trip the message-thread assertions in debug builds.
