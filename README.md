# Virtual Pedalboard

A standalone JUCE app that takes your bass in from a Focusrite Scarlett Solo,
runs it through a chain of pedals, and sends it straight back out to your headphones.
The board starts with one compressor.

Also builds as a VST3, so the same code can be loaded in a DAW later.

```
source/
  PluginProcessor.*            the board: input selector, the pedal chain, save/restore
  PluginEditor.*               the board's window: input selector plus each pedal's panel
  pedals/PedalProcessor.*      base class every pedal derives from (mono, own parameters, bypass)
  pedals/PedalRack.*           the list of pedals the board can create
  pedals/CompressorProcessor.* the compressor pedal: parameters, bypass, meter value
  pedals/CompressorEditor.*    its panel: knobs and the gain-reduction meter
  dsp/CompressorPedal.h        the compressor's DSP - detector, gain computer, ballistics
  ui/Theme.h                   shared colours
```

The chain is a `juce::AudioProcessorGraph` with one node per pedal, wired in a
line from input to output. Adding, removing or reordering pedals rewires it on
the message thread, and the graph hands the new wiring to the audio thread
without making it wait.

To add a new pedal type: derive from `PedalProcessor`, give it an editor, and
add one line to `PedalRack::getEntries()`.

## Building on Windows

Needs Visual Studio 2022 with the **Desktop development with C++** workload,
CMake 3.22+, and Git. JUCE is downloaded automatically into `build/`.

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The app lands at:

```
build\VirtualPedalboard_artefacts\Release\Standalone\Virtual Pedalboard.exe
```

Use **Release** for playing. Debug builds work but can glitch at small buffer sizes.

### ASIO (recommended, optional)

The Focusrite ASIO driver is the low-latency path on Windows. JUCE can't ship the
SDK, so download the ASIO SDK from Steinberg, unzip it, and point CMake at it:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DVPB_ASIO_SDK_DIR="C:/SDKs/asiosdk"
```

Without it, pick **Windows Audio (Exclusive Mode)** in the app's audio settings -
higher latency, but playable, and needs no extra downloads.

## Scarlett Solo setup

1. **Direct Monitor off.** If it's on you hear the dry bass blended in and the
   compressor will seem to do nothing.
2. **INST engaged** on the 1/4" input for a passive bass.
3. Launch the app, click **Options -> Audio/MIDI Settings**, and choose the
   Scarlett as both input and output device. Buffer size 128 samples (or 64 on ASIO).
4. Set the gain so your hardest notes peak around -10 dBFS.
5. The Solo's instrument jack is **input 2**, so the Input selector defaults to
   `In 2 (right)`. If you get silence, try `In 1 (left)`.

Settings persist in `%APPDATA%\Juice\Virtual Pedalboard.settings`.

## Starting points for the knobs

Bass, fairly obvious compression so you can hear what each control does:

| Threshold | Ratio | Knee | Attack | Release | Makeup |
|-----------|-------|------|--------|---------|--------|
| -18 dB    | 4:1   | 6 dB | 10 ms  | 150 ms  | +6 dB  |

Watch the gain-reduction meter while you play - 3 to 6 dB on the hard notes is a
healthy amount. Then try attack at 0.5 ms (kills the pick attack) versus 50 ms
(lets it through); that single control is most of what a compressor "sounds like".

## Editing from WSL

The source lives in WSL; the build happens on Windows. Keep them in sync with git:

```bash
# in WSL, after editing
git commit -am "..."
```

```bat
:: on Windows, first time only
git clone \\wsl.localhost\Ubuntu\home\juice\programming\personal\virtual_pedalboard C:\dev\virtual_pedalboard

:: after that
cd C:\dev\virtual_pedalboard && git pull
```

Optionally also configure a Linux build in WSL for compiler errors and clangd
intellisense without leaving the editor (the audio side is unusable under WSL2,
but it compiles):

```bash
sudo apt install cmake ninja-build libasound2-dev libx11-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxcursor-dev libxcomposite-dev \
  libfreetype6-dev libfontconfig1-dev libglu1-mesa-dev mesa-common-dev
cmake -B build-linux -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-linux
```
