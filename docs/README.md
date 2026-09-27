# Virtual Pedalboard documentation

Virtual Pedalboard is a JUCE app for bass guitar. It takes the instrument in
from an audio interface, runs it through a chain of virtual pedals, and sends
the result to your headphones. It builds as a standalone app and as a VST3.

The top-level [README](../README.md) covers building on Windows and setting up
the interface. These docs cover how the code is organised and why.

## Reading order

1. **[Architecture](architecture.md)**: the big picture. Layers, signal flow,
   which thread does what, who owns what, and the saved-state format. Start here.
2. **[The board](board.md)**: `VirtualPedalboardProcessor` and
   `VirtualPedalboardEditor`, the top-level processor and window.
3. **[Pedals](pedals.md)**: the `PedalProcessor` base class, the `PedalRack`
   registry, the compressor pedal, and a walkthrough for adding a new pedal.
4. **[Compressor DSP](dsp.md)**: the maths inside `CompressorPedal`.
5. **[Building](build.md)**: CMake targets, JUCE, ASIO, and the WSL/Windows setup.
6. **[Testing](testing.md)**: the test harness and what each group of tests checks.

## File map

| File | Contains | Documented in |
|------|----------|---------------|
| `source/PluginProcessor.h/.cpp` | `VirtualPedalboardProcessor`: input selection, the pedal chain graph, save/restore | [board.md](board.md) |
| `source/PluginEditor.h/.cpp` | `VirtualPedalboardEditor`: the main window that hosts pedal panels | [board.md](board.md) |
| `source/ui/Theme.h` | `theme::` colours shared by every window | [board.md](board.md#theme) |
| `source/pedals/PedalProcessor.h/.cpp` | `PedalProcessor`: base class for every pedal | [pedals.md](pedals.md#pedalprocessor) |
| `source/pedals/PedalRack.h/.cpp` | `PedalRack`: the list of pedal types the board can create | [pedals.md](pedals.md#pedalrack) |
| `source/pedals/CompressorProcessor.h/.cpp` | `CompressorProcessor`: the compressor as a pedal | [pedals.md](pedals.md#compressorprocessor) |
| `source/pedals/CompressorEditor.h/.cpp` | `CompressorEditor` and `GainReductionMeter`: the compressor's panel | [pedals.md](pedals.md#compressoreditor) |
| `source/dsp/CompressorPedal.h` | `CompressorPedal`: the compressor's signal processing | [dsp.md](dsp.md) |
| `tests/RunTests.cpp` | Test entry point | [testing.md](testing.md) |
| `tests/TestUtils.h` | `test::check` and `test::checkTrue` | [testing.md](testing.md#the-harness) |
| `tests/CompressorTests.cpp` | Numerical checks on `CompressorPedal` | [testing.md](testing.md#compressortestscpp) |
| `tests/ProcessorTests.cpp` | Checks on the board, the chain, the editor and saved state | [testing.md](testing.md#processortestscpp) |
| `CMakeLists.txt` | Build definition | [build.md](build.md) |

## Glossary

- **Board**: the top-level processor and window. It owns the pedal chain.
- **Pedal**: one effect on the board. Each pedal is its own `juce::AudioProcessor`.
- **Chain**: the pedals in signal order, from the input to the output.
- **Rack**: the list of pedal types that can be put on the board (`PedalRack`).
- **Panel**: a pedal's editor, shown inside the board's window.
- **Node**: a pedal's slot in the `juce::AudioProcessorGraph`.
- **APVTS**: `juce::AudioProcessorValueTreeState`, JUCE's parameter manager.
  The board and each pedal have their own.
- **Message thread**: JUCE's UI thread. All chain edits happen here.
- **Audio thread**: the real-time thread that calls `processBlock`. It must never
  block, allocate, or wait on a lock.
