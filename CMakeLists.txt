cmake_minimum_required(VERSION 3.22)

cmake_policy(SET CMP0091 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(Morphex VERSION 0.5.0)

# Optimized builds by default
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set (CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # This disables the STL1003 compiler check entirely in Microsoft's yvals_core.h
    add_compile_definitions(_STL_COMPILER_PREPROCESSOR_CHECK_H)
endif()

enable_language(C)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.1
)
FetchContent_MakeAvailable(JUCE)

# CLAP wrapper (pinned development revision).
FetchContent_Declare(
    clap_juce_extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        c1a5ad0
)
FetchContent_MakeAvailable(clap_juce_extensions)

option(MORPHEX_COPY_AFTER_BUILD "Copy built plugins into the system plugin folders" ON)

if (MORPHEX_COPY_AFTER_BUILD)
    set_property(GLOBAL PROPERTY JUCE_COPY_PLUGIN_AFTER_BUILD TRUE)
endif()

# 2. Assets & Binary Data (VT323 only; legacy logo images removed --
#    nothing referenced them).
juce_add_binary_data(Morphex_BinaryData
    SOURCES
        Source/Fonts/VT323-Regular.ttf
)

# 3. Plugin Definition
juce_add_plugin(Morphex
    COMPANY_NAME "Morphex"
    PLUGIN_MANUFACTURER_CODE "Mrph"  # 4-character manufacturer code
    PLUGIN_CODE "Mrpx"               # 4-character plugin code
    FORMATS Standalone VST3 AU
    PRODUCT_NAME "Morphex"
    VERSION ${PROJECT_VERSION}
    PLUGIN_DESCRIPTION "Four-way HPS morphing synth"
    VST3_CATEGORIES "Instrument" "Synth"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
)

clap_juce_extensions_plugin(TARGET Morphex
    CLAP_ID "com.balamdsp.Morphex"
    CLAP_FEATURES "instrument" "synthesizer")

# 1. Allow legacy JUCE alert windows and modal loops
target_compile_definitions(Morphex PUBLIC JUCE_MODAL_LOOPS_PERMITTED=1)

# We don't build a VST2 format (see FORMATS above), so the safety check in
# juce_audio_plugin_client_VST3.cpp about mismatched VST2/VST3 parameter IDs
# doesn't apply here - disable it.
target_compile_definitions(Morphex PUBLIC JUCE_VST3_CAN_REPLACE_VST2=0)

target_compile_definitions(Morphex PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

# 2. Force MSVC to include the standard Mac/Linux logic operators (and, or)
# NOTE: we do NOT use target_compile_options(... /FI"ciso646") here. CMake's
# Visual Studio generator hoists any /FI flag out of the raw options list and
# re-emits it as the dedicated <ForcedIncludeFiles> MSBuild property - and it
# does this WITHOUT preserving a $<COMPILE_LANGUAGE:CXX> condition wrapped
# around it, so the forced include ends up applied unconditionally to every
# file in the project, including the plain-C JUCE module files (zlib,
# libjpg, libpng, flac, etc.). ciso646 pulls in yvals_core.h, which then
# throws STL1003 on those C files. Instead, attach the forced include only
# to the specific .cpp files we own, once they've been added below.

juce_generate_juce_header(Morphex)

# 4. Source Files
target_sources(Morphex PRIVATE
    Source/Core/Instrument.cpp
    Source/Core/Sound.cpp
    Source/Core/Synthesis.cpp
    Source/Entities/MorphexSynth.cpp
    Source/Managers/PresetManager.cpp
    Source/Helpers/MorphexFrameExtents.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(Morphex PRIVATE X11)
endif()

if(MSVC)
    set_source_files_properties(
        Source/Core/Instrument.cpp
        Source/Core/Sound.cpp
        Source/Core/Synthesis.cpp
        Source/Entities/MorphexSynth.cpp
        Source/Managers/PresetManager.cpp
        Source/Helpers/MorphexFrameExtents.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        PROPERTIES COMPILE_OPTIONS "/FI\"ciso646\""
    )
endif()

# 5. Link JUCE Modules
target_link_libraries(Morphex PRIVATE
    Morphex_BinaryData
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_cryptography
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_opengl
)

# Set C++17 standard as specified in the Jucer file[cite: 1]
target_compile_features(Morphex PUBLIC cxx_std_17)

# Fix macOS to Windows MSVC compiler differences
if(MSVC)
    # Enable the M_PI math constant
    target_compile_definitions(Morphex PRIVATE _USE_MATH_DEFINES)
endif()