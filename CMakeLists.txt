cmake_minimum_required(VERSION 3.22)

# Runtime de Visual C++ enlazado estáticamente: el plugin no necesita instalar nada más en Windows.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(NematicTune VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.14
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(NematicTune
    COMPANY_NAME              "Nematic"
    PLUGIN_MANUFACTURER_CODE  Nmtc
    PLUGIN_CODE               Ntun
    PRODUCT_NAME              "Nematic Tune"
    DESCRIPTION               "Vocal pitch correction - Boom Bap / Hard"
    FORMATS                   VST3
    VST3_CATEGORIES           Fx "Pitch Shift"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE)

target_sources(NematicTune PRIVATE
    Source/AutotuneEngine.h
    Source/NeonLookAndFeel.h
    Source/PitchDisplay.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h)

target_compile_definitions(NematicTune PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(NematicTune
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
