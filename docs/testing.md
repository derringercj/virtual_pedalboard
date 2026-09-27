# Testing

The tests are a single program, `vpb_tests`, that prints one line per check and
exits with 0 if everything passes. It doesn't use a test framework.

```bash
cmake --build build-linux --target vpb_tests
./build-linux/vpb_tests
```

On Windows, build the `vpb_tests` target in Visual Studio or use
`cmake --build build --target vpb_tests --config Release`.

`vpb_tests` is left out of the default build, so building the app never waits on
it. Run it after touching a pedal, the DSP, or the board.

## The harness

### RunTests.cpp

`main()` creates a `juce::ScopedJuceInitialiser_GUI`, runs each test file's
function, then prints `ALL PASS` or `FAILURES ABOVE`.

The JUCE initialiser is needed because the editor tests create real components
and parameter attachments, which need JUCE's message manager. The test program
runs on the message thread, so board methods that assert they're on it work.

### TestUtils.h

| Function | Passes when |
|----------|-------------|
| `test::check (what, actual, expected, tolerance)` | `|actual − expected| ≤ tolerance`. Prints the actual and expected values. |
| `test::checkTrue (what, condition)` | `condition` is true. |

Each failure increments `test::failures`, and `main` returns 1 if that's non-zero.

### Catching JUCE assertions

In a Debug build, a failed `jassert` inside JUCE doesn't stop the program
unless a debugger is attached. It does print `JUCE Assertion failure in <file>:<line>`
to stderr. After changing anything to do with editors or the chain, check the
output for that line:

```bash
./build-linux/vpb_tests 2>&1 | grep Assertion
```

No output means no assertions fired. The editor tests rely on this: they
exercise the cases where getting the order of deletion wrong makes JUCE assert.

## CompressorTests.cpp

Tests `CompressorPedal` on its own, with no processor or graph. The expected
values are **worked out by hand** from the formulas in [dsp.md](dsp.md), not
copied from whatever the code produced.

Most checks feed a **constant** level. A sine would make a peak detector ripple
(see [dsp.md](dsp.md#release-ripple-on-low-notes)), so the measured reduction
would land slightly under the curve. The ripple has its own group.

| Group | Checks |
|-------|--------|
| Static curve, hard knee | 9 dB of reduction for 12 dB over at 4:1. Nothing below threshold. 1:1 is transparent. 11.4 dB at 20:1. Ratios below 1 clamp to transparent. Makeup lands on the output. |
| Soft knee | 0 dB at the lower knee edge. 1.125 dB at the threshold. The upper edge rejoins the hard-knee line (4.5 dB). Normal above the knee. The curve never goes negative from -90 to 0 dBFS. |
| Ballistics | One attack time constant covers 63.2% of the move, and so does one release time constant. |
| Release ripple | On a 100 Hz sine, ripple shrinks as release goes from 20 ms to 1 s, and is under 0.05 dB at 1 s. It also prints the ripple for each release time. |
| Housekeeping | `reset()` clears the reduction. Silence stays silent and finite with 12 dB of makeup. Full-scale input followed by silence never produces NaN or infinity. |

## ProcessorTests.cpp

Tests the board as a whole: input routing, the graph, editors, and saved state.
Every test here goes through `VirtualPedalboardProcessor::processBlock`, the
same path the audio device uses.

Helpers at the top of the file:

| Helper | Does |
|--------|------|
| `makeBoard (bypassed)` | A prepared board (48 kHz, 512-sample blocks) reading input 2, with its compressor bypassed or engaged. |
| `runBlock (board, left, right)` | Fills a block with constant values on each input, processes it, and returns the last sample of each output. |
| `settledOutputDb (board, inputDb)` | Runs 40 blocks of a constant level into input 2 and returns the output level in dB. |
| `setCompressor (pedal, threshold, ratio, makeup)` | Configures a compressor with a hard knee and a 10 ms release, so settled levels are easy to calculate by hand. |
| `setValue`, `setChoice`, `getValue` | Set or read a parameter the same way a UI or host would. |

| Group | Checks |
|-------|--------|
| Input routing | Input 1, input 2 and the sum each reach the output. Selecting input 1 while the bass is on input 2 gives silence. This guards the Scarlett Solo's jack being input 2. |
| Mono fan-out | Both outputs get the same, non-silent signal. |
| Bypass | An engaged compressor pulls a loud signal down and the meter shows it. Bypassed, the signal passes untouched and the meter reads zero. |
| Pedal chain | A new board has one compressor. Unknown types are refused. An empty chain passes audio straight through. A pedal added while running is prepared at the board's sample rate. Two 4:1 compressors in series land on the hand-calculated -17.25 dB. |
| Pedal order | A +12 dB boost before a 20:1 limiter gives -17.7 dB. After `movePedal`, the limiter comes first and the result is -12 dB. Inserting at an index and removing from the middle keep the order right, and the audio is wired back up afterwards. |
| Editor | The board editor is created with a sensible size. There's one panel per pedal. Adding a pedal adds its panel immediately. Removing a pedal with its panel open drops the panel without asserting. Closing the board window closes every pedal's editor. |
| Saved state | A two-pedal board with different settings, one pedal bypassed, and a non-default input survives save, scramble and restore, in order. The restored chain passes audio. |
| Unknown pedal in saved state | A saved board containing a pedal type this build doesn't know restores everything else. |
| Settings from before the chain existed | An old flat parameter list becomes a board with one compressor, carrying over the input, threshold, release and bypass. |

## Writing new tests

- **Work out the expected value by hand** from how the effect should behave, and
  say how in a comment. A test that copies the code's current output only
  proves nothing changed.
- **Test DSP directly** when you can. It's faster and failures point straight at
  the maths.
- **Let levels settle.** Anything with smoothing or ballistics needs enough
  blocks to reach steady state before checking. `settledOutputDb` runs 40
  blocks (about 0.4 s).
- Add a new file's run function to `RunTests.cpp` and the file to `vpb_tests`
  in `CMakeLists.txt`.
