cmake_minimum_required(VERSION 3.22)
project(Sequencer VERSION 1.1.0)

# ASIO SDK path — JUCE needs this to compile ASIO support
set(JUCE_ASIO_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/libs/asio-sdk" CACHE PATH "ASIO SDK location")

add_subdirectory(libs/JUCE)

juce_add_gui_app(Sequencer
    PRODUCT_NAME "Legion Stage"
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
    src/MidiClip.h
    src/ClipPlayerNode.h
    src/ClipPlayerNode.cpp
    src/SequencerEngine.h
    src/SequencerEngine.cpp
    src/PianoRollComponent.h
    src/PianoRollComponent.cpp
    src/Midi2Handler.h
    src/Midi2Handler.cpp
    src/QuickKeysHandler.h
    src/QuickKeysHandler.cpp
    src/DawTheme.h
    src/DawLookAndFeel.h
    src/WalkmanLookAndFeel.h
    src/GlassLookAndFeel.h
    src/NoirLookAndFeel.h
    src/TerminalLookAndFeel.h
    src/JournalLookAndFeel.h
    src/LinenLookAndFeel.h
    src/IceLookAndFeel.h
    src/IndigoLookAndFeel.h
    src/FrostLookAndFeel.h
    src/KeystageLookAndFeel.h
    src/BlueprintLookAndFeel.h
    src/SpectrumComponent.h
    src/LissajousComponent.h
    src/GForceComponent.h
    src/GeissComponent.h
    src/ProjectMComponent.h
    src/TouchPianoComponent.h
    src/MixerComponent.h
    src/SplashComponent.h
    src/ThemeManager.h
    src/TimelineComponent.h
    src/TimelineComponent.cpp
    src/UpdateChecker.h
    src/UpdateChecker.cpp
    src/UpdateDialog.h
    src/UpdateDialog.cpp
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
    juce::juce_dsp
    juce::juce_opengl
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
    opengl32
)
