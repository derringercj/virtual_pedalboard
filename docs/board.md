# The board

The board is the top level of the app. JUCE's plugin wrappers (the standalone
app and the VST3) create one board and talk only to it.

- [`VirtualPedalboardProcessor`](#virtualpedalboardprocessor): `source/PluginProcessor.h/.cpp`
- [`VirtualPedalboardEditor`](#virtualpedalboardeditor): `source/PluginEditor.h/.cpp`
- [`theme`](#theme): `source/ui/Theme.h`

---

## VirtualPedalboardProcessor

`final`, derives from `juce::AudioProcessor`.

It selects which interface input carries the bass, runs that signal through
the pedal chain, and copies the result to every output. It also owns the chain
and saves and restores the whole board.

### Buses

The board has a stereo input and a stereo output. `isBusesLayoutSupported`
accepts mono or stereo on each side, so it also works with a mono device.
Inside, everything is mono (see [architecture.md](architecture.md#signal-flow)).

### Members

| Member | Purpose |
|--------|---------|
| `parameters` | The board's APVTS. It holds only `input`. Its state type is `Board`. |
| `inputParam` | Cached pointer to the `input` value, for the audio thread. |
| `graph` | The `juce::AudioProcessorGraph` that runs the pedals. It's set to one channel in and one out. |
| `inputNode`, `outputNode` | The graph's audio input and output nodes. Every chain starts at `inputNode` and ends at `outputNode`. |
| `chain` | `std::vector<Node::Ptr>` holding the pedal nodes in signal order. It's the source of truth for the order. Message thread only. |
| `chainListeners` | Who to tell when the chain changes. In practice, the editor. |

### Construction

1. Creates the board's APVTS and caches `inputParam`.
2. Sets the graph to one channel in and one out. **This must come before the I/O
   nodes are added**, because they size themselves from the graph when they join.
3. Adds the input and output nodes.
4. Adds one compressor, so a fresh board behaves like the original app.

### Audio

| Method | What it does |
|--------|--------------|
| `prepareToPlay` | Calls `graph.prepareToPlay`. The graph prepares every pedal now, and any pedal added later as it joins. |
| `releaseResources` | Calls `graph.releaseResources`. |
| `processBlock` | Input selection, then the chain, then fan-out. See [architecture.md](architecture.md#signal-flow). |

`processBlock` in detail:

- Output channels beyond the inputs are cleared, since they hold junk.
- `InputMode::first` leaves channel 0 alone. `second` copies channel 1 into
  channel 0. `sum` averages the two.
- A one-channel `juce::AudioBuffer<float>` is made **pointing at channel 0's
  memory**. That constructor doesn't copy or allocate. The graph processes it in place.
- Channel 0 is copied to every other output channel.

If the graph isn't ready (for example before `prepareToPlay`), JUCE's graph
outputs silence rather than garbage.

### Chain editing

All of these are **message thread only**.

| Method | Behaviour |
|--------|-----------|
| `getNumPedals()` | Number of pedals in the chain. |
| `getPedal (index)` | The pedal at `index`, or `nullptr` if out of range. |
| `addPedal (typeId, index = -1)` | Creates a pedal through `PedalRack` and inserts it at `index`. An out-of-range index, including the default `-1`, means the end. Returns the new pedal, or `nullptr` for an unknown type. |
| `removePedal (index)` | Removes the pedal at `index`. An invalid index does nothing. |
| `movePedal (from, to)` | Moves a pedal so it ends up at `to`. Both indices must be valid. |
| `addChainListener` / `removeChainListener` | Register for `pedalChainChanged()` callbacks. |

Private helpers:

- **`insertPedalNode`** adds a pedal to the graph with `UpdateKind::none` (no
  rebuild yet) and puts its node into `chain`.
- **`replaceChain`** removes every pedal and inserts a new list. Used when
  restoring state.
- **`chainChanged`** does the actual rewiring. It removes every connection, then
  connects `input → pedal 1 → pedal 2 → … → output` on channel 0. Every call
  uses `UpdateKind::none`, and `graph.rebuild()` runs once at the end, so the
  audio thread never picks up a half-wired chain. Finally it calls every
  `ChainListener`.

**Removal order matters.** `removePedal` and `replaceChain` keep a reference to
each removed node until after `chainChanged` has told the listeners. That keeps
the pedal alive while the editor deletes its panel. See
[architecture.md](architecture.md#ownership-and-lifetimes).

### ChainListener

```cpp
struct ChainListener
{
    virtual void pedalChainChanged() = 0;
};
```

Called **synchronously** on the message thread after any add, remove, move or
restore. It's deliberately not JUCE's async `ChangeBroadcaster`. With an async
message, a removed pedal could be deleted before the editor heard about it.

### Saved state

| Method | What it does |
|--------|--------------|
| `getStateInformation` | Writes the `<VirtualPedalboard>` XML described in [architecture.md](architecture.md#saved-state). Each pedal saves itself through its own `getStateInformation`. |
| `setStateInformation` | Reads that XML back, rebuilding the chain through `PedalRack`. Unknown pedal types are skipped. With no `Chain` element it's the old format, so it calls `restoreLegacyState`. |
| `restoreLegacyState` | Builds one compressor from an old flat parameter list. `input` goes to the board, the rest to the compressor. |

### Other overrides

Boilerplate for JUCE: no MIDI, zero tail, one program, and `createEditor`
returns a `VirtualPedalboardEditor`. `createPluginFilter()` at the bottom of the
`.cpp` file is the entry point the plugin wrappers call to create the board.

---

## VirtualPedalboardEditor

`final`, derives from `juce::AudioProcessorEditor` and, privately, from
`VirtualPedalboardProcessor::ChainListener`.

The main window. The top row has the title and the input selector. Below it,
each pedal's own editor is shown as a panel, left to right in signal order.

### Layout

```
┌────────────────────────────────────────────────────────────┐
│ VIRTUAL PEDALBOARD                     Input [In 2 (right)▾]│
│────────────────────────────────────────────────────────────│
│ ┌───────────────┐  ┌───────────────┐                       │
│ │ pedal panel   │  │ pedal panel   │   … (scrolls sideways)│
│ └───────────────┘  └───────────────┘                       │
└────────────────────────────────────────────────────────────┘
```

- The window width fits the panels, between 640 and 1400 pixels. Past 1400 the
  panel row scrolls sideways inside a `juce::Viewport`.
- Panels are 12 pixels apart. The height fits the tallest panel.
- An empty chain shows "No pedals on the board - the bass goes straight through."

### Members

| Member | Purpose |
|--------|---------|
| `board` | The processor this window shows. |
| `titleLabel`, `inputLabel`, `inputBox`, `inputAttachment` | The header. The combo box is attached to the board's `input` parameter. |
| `emptyLabel` | Shown only when there are no pedals. |
| `viewport`, `pedalRow` | `pedalRow` holds the panels side by side. `viewport` scrolls it. |
| `panels` | One `Panel` per pedal, in chain order. |

### Panels

```cpp
using Panel = std::unique_ptr<juce::AudioProcessorEditor, PanelDeleter>;
```

Each panel is created by the pedal's own `createEditorIfNeeded()`, which calls
that pedal's `createEditor()`. The board window doesn't know what kind of pedal
it's showing.

`PanelDeleter` tells the pedal its editor is going (`editorBeingDeleted`), then
deletes it. A JUCE plugin host does the same, and without it JUCE asserts.

### Methods

| Method | What it does |
|--------|--------------|
| constructor | Builds the header, registers as a chain listener, and builds the panels. |
| destructor | Unregisters the listener. `panels` is destroyed after that, which closes every pedal's editor. |
| `pedalChainChanged` | Calls `rebuildPanels`. |
| `rebuildPanels` | Deletes every panel, then makes one per pedal and resizes the window to fit. Rebuilding everything is simple, and chains change too rarely for it to matter. |
| `getNumPedalPanels` | Number of panels showing. Used by the tests. |
| `paint`, `resized` | Background, separator line and header layout. |

---

## Theme

`source/ui/Theme.h`. The `theme` namespace holds the colours shared by the board
and every pedal panel, so new pedals match.

| Name | Colour | Used for |
|------|--------|----------|
| `theme::board` | `#1a1c20` | Main window background |
| `theme::panel` | `#23262b` | Pedal panel background |
| `theme::trough` | `#15171a` | Empty part of a meter |
| `theme::accent` | `#e8b23a` | Knob fill, meter bar |
| `theme::hairline` | `#35393f` | Separator lines, panel border |
