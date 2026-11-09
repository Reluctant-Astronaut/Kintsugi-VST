cmake_minimum_required(VERSION 3.22)

project(Kintsugi VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Universal binary on macOS (Apple Silicon + Intel)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# Use a local JUCE checkout if JUCE_DIR is given, otherwise download it
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.9
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Kintsugi
    COMPANY_NAME "Reluctant Astronaut"
    BUNDLE_ID "com.reluctantastronaut.kintsugi"
    PLUGIN_MANUFACTURER_CODE Rast
    PLUGIN_CODE Kint
    FORMATS VST3 AU
    PRODUCT_NAME "Kintsugi"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Distortion Mastering)

# Embedded fonts
juce_add_binary_data(KintsugiAssets SOURCES
    Resources/OldStandard-Regular.ttf
    Resources/OldStandard-Bold.ttf)

target_sources(Kintsugi PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(Kintsugi PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Kintsugi
    PRIVATE
        KintsugiAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Optional offline tests: cmake -DKINTSUGI_TESTS=ON
option(KINTSUGI_TESTS "Build the offline render test" OFF)
if(KINTSUGI_TESTS)
    juce_add_console_app(KintsugiTests PRODUCT_NAME "KintsugiTests")
    target_sources(KintsugiTests PRIVATE
        tests/RenderTest.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)
    target_compile_definitions(KintsugiTests PRIVATE
        JucePlugin_Name="Kintsugi"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(KintsugiTests PRIVATE
        KintsugiAssets
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
