#pragma once

#include <JuceHeader.h>
#include <set>
#include "PluginHost.h"
#include "PianoRollComponent.h"
#include "TimelineComponent.h"
#include "Midi2Handler.h"
#include "ThemeManager.h"
#include "SpectrumComponent.h"
#include "LissajousComponent.h"
#include "GForceComponent.h"
#include "MilkDropComponent.h"
#include "WorkflowGuideWindow.h"

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

class MainComponent : public juce::Component, public juce::Timer, public juce::MidiInputCallback
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
    ThemeManager themeManager;
    juce::ComboBox themeSelector;
    SpectrumComponent spectrumDisplay;
    LissajousComponent lissajousDisplay;
    GForceComponent gforceDisplay;
    MilkDropComponent milkdropDisplay;
    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer audioPlayer;
    PluginHost pluginHost;

    // Current track
    int selectedTrackIndex = 0;

    // Transport
    double lastSpaceStopTime = 0.0;

    // ── Top Bar ──
    juce::TextButton midiLearnButton { "LEARN" };
    juce::Label trackNameLabel;
    juce::TextButton recordButton { "REC" };
    juce::TextButton playButton { "PLAY" };
    juce::TextButton stopButton { "STOP" };
    juce::TextButton metronomeButton { "MET" };
    juce::TextButton bpmDownButton { "-" };
    juce::Label bpmLabel;
    juce::TextButton bpmUpButton { "+" };
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
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton panicButton { "PANIC" };

    // ── Navigation ──
    juce::TextButton zoomInButton { "Zoom +" };
    juce::TextButton zoomOutButton { "Zoom -" };
    juce::TextButton scrollLeftButton { "<<" };
    juce::TextButton scrollRightButton { ">>" };

    // ── Right Panel ──
    juce::ComboBox pluginSelector;
    juce::TextButton openEditorButton { "Open Editor" };
    juce::ComboBox midiInputSelector;
    juce::TextButton midiRefreshButton { "Refresh" };
    juce::TextButton audioSettingsButton { "Audio Settings" };
    juce::TextButton fullscreenButton { "VIS" };
    juce::ComboBox visSelector;
    bool visualizerFullScreen = false;
    int currentVisMode = 0;  // 0=Spectrum+Lissajous, 1=G-Force, 2=MilkDrop
    juce::TextButton nextPresetButton { ">>|" };
    juce::TextButton visExitButton { "EXIT" };
    juce::TextButton testNoteButton { "Test Note" };

    // ── Workflow Guide ──
    juce::TextButton workflowGuideButton { "GUIDE" };
    std::unique_ptr<WorkflowGuideWindow> workflowGuideWindow;
    void executeWorkflowAction(const juce::String& actionId,
                               const juce::StringPairArray& params);
    void loadWorkflowStage(int stageIndex);
    juce::String loadSkillFile(int stageIndex);
    void saveWorkflowGuideState();
    void restoreWorkflowGuideState();

    // MidiInputCallback — intercept SysEx for CI before it goes to collector
    void handleIncomingMidiMessage(juce::MidiInput* source, const juce::MidiMessage& msg) override;

    // ── FX Inserts ──
    static constexpr int NUM_FX_SLOTS = 2;
    juce::OwnedArray<juce::ComboBox> fxSelectors;
    juce::OwnedArray<juce::TextButton> fxEditorButtons;
    void updateFxDisplay();
    void loadFxPlugin(int slotIndex);
    void openFxEditor(int slotIndex);

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


    // ── MIDI 2.0 CI ──
    Midi2Handler midi2Handler;
    juce::TextButton midi2Button { "M2" };
    bool midi2Enabled = false;
    juce::String midiOutputId;
    std::unique_ptr<juce::MidiOutput> midiOutput;  // kept open for CI responses

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
    juce::Array<juce::PluginDescription> pluginDescriptions;  // instruments
    juce::Array<juce::PluginDescription> fxDescriptions;      // effects
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
    void applyThemeToControls();

    // ── MIDI Learn ──
    enum class MidiTarget {
        None, Volume, Pan, Bpm,
        Play, Stop, Record, Metronome, Loop,
        Param0, Param1, Param2, Param3, Param4, Param5,
        TrackNext, TrackPrev
    };

    struct MidiMapping {
        int channel = -1;
        int ccNumber = -1;
        MidiTarget target = MidiTarget::None;
    };

    bool midiLearnActive = false;
    MidiTarget midiLearnTarget = MidiTarget::None;
    juce::Array<MidiMapping> midiMappings;
    void startMidiLearn(MidiTarget target);
    void processMidiLearnCC(int channel, int cc, int value);
    void applyMidiCC(const MidiMapping& mapping, int value);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
