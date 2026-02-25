cmake_minimum_required(VERSION 3.22)
project(SNOT VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── JUCE ──────────────────────────────────────────────────────────────────────
add_subdirectory(JUCE)

# ── Embed HTML as binary data ─────────────────────────────────────────────────
# Generates: BinaryData::SNOT_UI_html  and  BinaryData::SNOT_UI_htmlSize
juce_add_binary_data(SNOTResources
    SOURCES Resources/SNOT_UI.html
)

# ── Plugin ────────────────────────────────────────────────────────────────────
juce_add_plugin(SNOT
    COMPANY_NAME            "SNOT Audio"
    PLUGIN_MANUFACTURER_CODE SNTA
    PLUGIN_CODE             Snot
    FORMATS                 VST3 AU Standalone
    PRODUCT_NAME            "SNOT"
    DESCRIPTION             "Interdimensional Multi-FX Portal"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         "Fx"
    AU_MAIN_TYPE            kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD TRUE
)

# ── Sources ───────────────────────────────────────────────────────────────────
target_sources(SNOT PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# ── Includes ──────────────────────────────────────────────────────────────────
target_include_directories(SNOT PRIVATE
    Source
    Source/dsp
    Source/dsp/modules
    Source/preset
)

# ── Definitions ───────────────────────────────────────────────────────────────
target_compile_definitions(SNOT PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

if(WIN32)
    target_compile_definitions(SNOT PUBLIC _WIN32_WINNT=0x0A00)
endif()

if(APPLE)
    set_target_properties(SNOT PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.snotaudio.snot"
        MACOSX_DEPLOYMENT_TARGET "12.0"
    )
endif()

# ── Link ──────────────────────────────────────────────────────────────────────
target_link_libraries(SNOT PRIVATE
    SNOTResources
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_audio_plugin_client
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_events
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::recommended_warning_flags
)

# ── Optimisations ─────────────────────────────────────────────────────────────
if(MSVC)
    target_compile_options(SNOT PRIVATE /O2 /Ob3 /arch:AVX2 /fp:fast /GL)
    target_link_options   (SNOT PRIVATE /LTCG)
else()
    target_compile_options(SNOT PRIVATE -O3 -march=native -ffast-math -funroll-loops)
endif()

message(STATUS "SNOT | HTML UI embedded | WebView2(Win) WKWebView(Mac)")
