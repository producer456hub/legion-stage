#pragma once

#include <JuceHeader.h>
#include <set>
#include "PluginHost.h"
#include "PianoRollComponent.h"
#include "TimelineComponent.h"

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
    void mouseDown(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;

private:
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioPlayer;
    PluginHost pluginHost;

    // Current track
    int selectedTrackIndex = 0;

    // Transport
    double lastSpaceStopTime = 0.0;

    // ── Top Bar ──
    juce::TextButton prevTrackButton { "<" };
    juce::TextButton nextTrackButton { ">" };
    juce::Label trackNameLabel;
    juce::TextButton recordButton { "REC" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton metronomeButton { "MET" };
    juce::Slider bpmSlider;
    juce::Label beatLabel;

    // ── Edit Toolbar ──
    juce::TextButton newClipButton { "New Clip" };
    juce::TextButton deleteClipButton { "Delete" };
    juce::TextButton duplicateClipButton { "Duplicate" };
    juce::TextButton splitClipButton { "Split" };
    juce::TextButton editClipButton { "Edit Notes" };
    juce::TextButton quantizeButton { "Quantize" };
    juce::ComboBox gridSelector;
    juce::TextButton countInButton { "Count-In" };

    // ── Navigation ──
    juce::TextButton zoomInButton { "Zoom +" };
    juce::TextButton zoomOutButton { "Zoom -" };
    juce::TextButton scrollLeftButton { "<<" };
    juce::TextButton scrollRightButton { ">>" };

    // ── Right Panel ──
    juce::ComboBox pluginSelector;
    juce::TextButton openEditorButton { "Open Editor" };
    juce::ComboBox midiInputSelector;
    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::TextButton testNoteButton { "Test Note" };

    // ── Right Panel — Save/Load/Undo ──
    juce::TextButton saveButton { "Save" };
    juce::TextButton loadButton { "Load" };
    juce::TextButton undoButton { "Undo" };
    juce::TextButton redoButton { "Redo" };

    // Undo system — stores snapshots of clip data
    struct ProjectSnapshot {
        struct ClipData {
            juce::MidiMessageSequence events;
            double lengthInBeats = 4.0;
            double timelinePosition = 0.0;
            int trackIndex = 0;
            int slotIndex = 0;
        };
        juce::Array<ClipData> clips;
        double bpm = 120.0;
    };
    juce::Array<ProjectSnapshot> undoHistory;
    int undoIndex = -1;
    void takeSnapshot();
    void restoreSnapshot(const ProjectSnapshot& snap);
    void saveProject();
    void loadProject();


    // ── Plugin Parameters ──
    static constexpr int NUM_PARAM_SLIDERS = 6;
    juce::OwnedArray<juce::Slider> paramSliders;
    juce::OwnedArray<juce::Label> paramLabels;
    void updateParamSliders();

    // ── Right Panel — Mix + Info ──
    juce::Slider volumeSlider;
    juce::Label volumeLabel { {}, "Vol" };
    juce::Slider panSlider;
    juce::Label panLabel { {}, "Pan" };
    juce::Label trackInfoLabel;

    // ── Bottom Bar ──
    juce::Label statusLabel;

    // ── Timeline (arrangement view) ──
    std::unique_ptr<TimelineComponent> timelineComponent;

    // Plugin editor
    std::unique_ptr<juce::AudioProcessorEditor> currentEditor;
    std::unique_ptr<PluginEditorWindow> editorWindow;

    // Data
    juce::Array<juce::PluginDescription> pluginDescriptions;
    juce::Array<juce::MidiDeviceInfo> midiDevices;
    juce::String currentMidiDeviceId;

    // Computer keyboard MIDI
    bool useComputerKeyboard = false;
    int computerKeyboardOctave = 4;
    std::set<int> keysCurrentlyDown;
    int keyToNote(int keyCode) const;
    void sendNoteOn(int note);
    void sendNoteOff(int note);

    // Methods
    void selectTrack(int index);
    void updateTrackDisplay();

    void scanPlugins();
    void loadSelectedPlugin();
    void openPluginEditor();
    void closePluginEditor();
    void playTestNote();

    void scanMidiDevices();
    void selectMidiDevice();
    void disableCurrentMidiDevice();
    void showAudioSettings();
    void updateStatusLabel();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
