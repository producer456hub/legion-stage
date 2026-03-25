#pragma once

#include <JuceHeader.h>
#include <set>
#include "PluginHost.h"
#include "TrackComponent.h"

class PluginEditorWindow : public juce::DocumentWindow
{
public:
    PluginEditorWindow(const juce::String& name, juce::AudioProcessorEditor* editor,
                       std::function<void()> onClose)
        : DocumentWindow(name, juce::Colours::darkgrey, DocumentWindow::closeButton),
          closeCallback(std::move(onClose))
    {
        setUsingNativeTitleBar(true);
        setContentNonOwned(editor, true);
        setVisible(true);
        centreWithSize(getWidth(), getHeight());
    }
    void closeButtonPressed() override { if (closeCallback) closeCallback(); }
private:
    std::function<void()> closeCallback;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditorWindow)
};

class MainComponent : public juce::Component, public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;

private:
    // Computer keyboard MIDI
    bool useComputerKeyboard = false;
    int computerKeyboardOctave = 4;  // C4 = middle C
    std::set<int> keysCurrentlyDown;  // track which keys are held
    int keyToNote(int keyCode) const;
    void sendNoteOn(int note);
    void sendNoteOff(int note);

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioPlayer;
    PluginHost pluginHost;

    // Top controls
    juce::ComboBox midiInputSelector;
    juce::TextButton midiRefreshButton { "Refresh" };
    juce::ComboBox pluginSelector;
    juce::TextButton openEditorButton { "Open Editor" };
    juce::TextButton testNoteButton { "Play Test Note" };
    juce::TextButton audioSettingsButton { "Audio Settings" };

    // Track list
    juce::OwnedArray<TrackComponent> trackComponents;
    juce::Viewport trackViewport;
    juce::Component trackListContainer;
    int selectedTrackIndex = 0;

    // Clip grid buttons — 16 tracks × 4 slots
    juce::OwnedArray<juce::TextButton> clipButtons;
    juce::Component clipGridContainer;
    juce::Viewport clipGridViewport;

    // Transport
    juce::TextButton recordButton { "REC" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::Slider bpmSlider;
    juce::Label bpmLabel;
    juce::Label beatLabel;

    // Status
    juce::Label statusLabel;

    // Plugin editor
    std::unique_ptr<juce::AudioProcessorEditor> currentEditor;
    std::unique_ptr<PluginEditorWindow> editorWindow;

    juce::Array<juce::PluginDescription> pluginDescriptions;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    juce::String currentMidiDeviceId;

    void scanMidiDevices();
    void selectMidiDevice();
    void disableCurrentMidiDevice();
    void scanPlugins();
    void loadSelectedPlugin();
    void openPluginEditor();
    void closePluginEditor();
    void playTestNote();
    void showAudioSettings();
    void updateStatusLabel();

    void selectTrack(int index);
    void onTrackVolumeChanged(int trackIndex, float volume);
    void onTrackMuteChanged(int trackIndex, bool muted);
    void onTrackSoloChanged(int trackIndex, bool soloed);
    void onTrackArmChanged(int trackIndex, bool armed);
    void setupTrackList();
    void setupClipGrid();
    void onClipButtonClicked(int trackIndex, int slotIndex);
    void updateClipButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
