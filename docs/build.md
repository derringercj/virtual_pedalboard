# Building

Step-by-step Windows instructions are in the top-level [README](../README.md).
This page explains what `CMakeLists.txt` sets up.

## Requirements

- CMake 3.22 or newer
- A C++17 compiler (Visual Studio 2022 on Windows, GCC or Clang on Linux)
- Git, which CMake uses to download JUCE

## JUCE

JUCE **8.0.4** is downloaded by CMake's `FetchContent` into the build folder
(`build/_deps/juce-src`). It isn't stored in the repository, so each machine
(WSL and Windows) keeps its own copy. To change the version, edit `GIT_TAG`.

Reading JUCE's source there is often the fastest way to learn how a class really
behaves. For example, `modules/juce_audio_processors/processors/juce_AudioProcessorGraph.cpp`
shows how the graph swaps render sequences without blocking.

## Targets

`juce_add_plugin(VirtualPedalboard ...)` creates several targets:

| Target | Output |
|--------|--------|
| `VirtualPedalboard` | A static library of the app's code, shared by the formats below |
| `VirtualPedalboard_Standalone` | The runnable app |
| `VirtualPedalboard_VST3` | The VST3 plugin, for loading in a DAW later |
| `vpb_tests` | The test program. Only built when asked for by name. |

Outputs go under `<build dir>/VirtualPedalboard_artefacts/<Config>/`, for example
`build\VirtualPedalboard_artefacts\Release\Standalone\Virtual Pedalboard.exe`.

### Plugin settings

| Setting | Value | Notes |
|---------|-------|-------|
| `PRODUCT_NAME` | Virtual Pedalboard | Window title and file names |
| `COMPANY_NAME` | Juice | Also the settings folder name |
| `PLUGIN_MANUFACTURER_CODE` | `Vpbd` | Four-character ID a DAW uses |
| `PLUGIN_CODE` | `Cmp1` | Kept from when this was only a compressor. Changing it would make a DAW see a different plugin. |
| `FORMATS` | Standalone, VST3 | |

## Source list

```cmake
set(VPB_SOURCES
    source/PluginProcessor.cpp
    source/PluginEditor.cpp
    source/pedals/PedalProcessor.cpp
    source/pedals/PedalRack.cpp
    source/pedals/CompressorProcessor.cpp
    source/pedals/CompressorEditor.cpp)
```

The app and `vpb_tests` both compile this one list, so the tests check exactly
what ships. **Add every new `.cpp` file here.** Header-only files like
`dsp/CompressorPedal.h` and `ui/Theme.h` don't need listing.

`source/` is an include directory, so includes are written from there, e.g.
`#include "pedals/PedalRack.h"`.

## Compile definitions

| Definition | Why |
|------------|-----|
| `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0` | Not needed. Leaving them out avoids extra system dependencies. |
| `JUCE_VST3_CAN_REPLACE_VST2=0` | There was never a VST2 version to replace. |
| `JUCE_STRICT_REFCOUNTEDPOINTER=1` | Stops `ReferenceCountedObjectPtr` (such as `Node::Ptr`) converting silently to a raw pointer. |
| `JUCE_ASIO=1` | Only when `VPB_ASIO_SDK_DIR` is set. See below. |
| `JUCE_STANDALONE_APPLICATION=1` | Tests only. Lets the test program use JUCE without a plugin wrapper. |

Both targets link `juce_audio_utils` and `juce_dsp`, plus JUCE's recommended
config and warning flags. The code currently builds with no warnings under
those flags.

## ASIO (Windows, optional)

ASIO is the low-latency driver path on Windows. JUCE can't include Steinberg's
SDK, so download it and pass its folder:

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DVPB_ASIO_SDK_DIR="C:/SDKs/asiosdk"
```

CMake prints `Virtual Pedalboard: ASIO enabled from ...` when it's picked up.

## Editing in WSL, building on Windows

The source lives in WSL and is edited there. The real build and audio happen on
Windows, which pulls the changes through git. See the README for the commands.

A Linux build in WSL is still useful for compiler errors, clangd, and running
the tests:

```bash
cmake -B build-linux -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-linux --target vpb_tests
./build-linux/vpb_tests
```

The standalone app builds and starts under WSL, but its audio doesn't work there.

`build/` and `build-*/` are in `.gitignore`.
