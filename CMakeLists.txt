cmake_minimum_required(VERSION 3.22)

project(VirtualPedalboard VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ------------------------------------------------------------------------------
# JUCE. Fetched into the build tree, so the source tree stays small and each
# machine (WSL for editing, Windows for building) keeps its own copy.
# ------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

# ------------------------------------------------------------------------------
# Optional ASIO support (Windows only). JUCE cannot ship the SDK, so download it
# from Steinberg yourself and point this at the unzipped folder:
#   cmake -B build -DVPB_ASIO_SDK_DIR="C:/SDKs/asiosdk"
# ------------------------------------------------------------------------------
set(VPB_ASIO_SDK_DIR "" CACHE PATH "Path to the Steinberg ASIO SDK (Windows, optional)")

# ------------------------------------------------------------------------------
# The app itself. FORMATS Standalone gives a runnable .exe; VST3 is there so the
# same code can be dropped into a DAW later without any restructuring.
# ------------------------------------------------------------------------------
juce_add_plugin(VirtualPedalboard
    PRODUCT_NAME                "Virtual Pedalboard"
    COMPANY_NAME                "Juice"
    PLUGIN_MANUFACTURER_CODE    Vpbd
    PLUGIN_CODE                 Cmp1
    FORMATS                     Standalone VST3
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE)

target_sources(VirtualPedalboard PRIVATE
    source/PluginProcessor.cpp
    source/PluginEditor.cpp)

target_include_directories(VirtualPedalboard PRIVATE source)

target_compile_definitions(VirtualPedalboard PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(VPB_ASIO_SDK_DIR)
    message(STATUS "Virtual Pedalboard: ASIO enabled from ${VPB_ASIO_SDK_DIR}")
    target_compile_definitions(VirtualPedalboard PUBLIC JUCE_ASIO=1)
    target_include_directories(VirtualPedalboard PRIVATE "${VPB_ASIO_SDK_DIR}/common")
endif()

target_link_libraries(VirtualPedalboard
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
