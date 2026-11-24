cmake_minimum_required(VERSION 3.22)
project(Typhon VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE : soit un dossier local (JUCE_DIR), soit téléchargé automatiquement
if(DEFINED JUCE_DIR)
    add_subdirectory(${JUCE_DIR} JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.4
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(Typhon
    COMPANY_NAME "Foudre Audio"
    BUNDLE_ID com.foudreaudio.typhon
    PLUGIN_MANUFACTURER_CODE Fdra
    PLUGIN_CODE Tyhn
    FORMATS VST3
    PRODUCT_NAME "Typhon"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Drum)

juce_generate_juce_header(Typhon)

target_sources(Typhon PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Capture.cpp
    Source/Diag.cpp)

target_compile_definitions(Typhon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_PLUGINHOST_VST3=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

# Symboles de débogage (Typhon.pdb livré dans le .vst3) pour lire les rapports de plantage
if(MSVC)
    target_compile_options(Typhon PRIVATE /Zi)
    target_link_options(Typhon_VST3 PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    target_link_libraries(Typhon PRIVATE psapi)
endif()

target_link_libraries(Typhon
    PRIVATE
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
