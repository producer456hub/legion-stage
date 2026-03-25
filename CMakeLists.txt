cmake_minimum_required(VERSION 3.22)
project(Sequencer VERSION 0.1.0)

# ASIO SDK path — JUCE needs this to compile ASIO support
set(JUCE_ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-sdk" CACHE PATH "ASIO SDK location")

add_subdirectory(libs/JUCE)

juce_add_gui_app(Sequencer
    PRODUCT_NAME "Sequencer"
    COMPANY_NAME "Dev"
)

juce_generate_juce_header(Sequencer)

target_sources(Sequencer PRIVATE
    src/Main.cpp
    src/MainComponent.h
    src/MainComponent.cpp
    src/PluginHost.h
    src/PluginHost.cpp
    src/GainProcessor.h
    src/GainProcessor.cpp
    src/TrackComponent.h
    src/TrackComponent.cpp
    src/MidiClip.h
    src/ClipPlayerNode.h
    src/ClipPlayerNode.cpp
    src/SequencerEngine.h
    src/SequencerEngine.cpp
    src/PianoRollComponent.h
    src/PianoRollComponent.cpp
)

target_compile_definitions(Sequencer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_ASIO=1
    JUCE_WASAPI=1
    JUCE_DIRECTSOUND=1
    JUCE_PLUGINHOST_VST3=1
)

# Point JUCE to the ASIO SDK headers
target_include_directories(Sequencer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-sdk/common
)

target_link_libraries(Sequencer PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)
