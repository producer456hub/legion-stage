#pragma once

#include <JuceHeader.h>
#include <set>
#include "PluginHost.h"
#include "PianoRollComponent.h"
#include "TimelineComponent.h"
#include "Midi2Handler.h"
#include "QuickKeysHandler.h"
#include "ThemeManager.h"
#include "SpectrumComponent.h"
#include "LissajousComponent.h"
#include "GForceComponent.h"
#include "GeissComponent.h"
#include "ProjectMComponent.h"
#include "TouchPianoComponent.h"
#include "MixerComponent.h"
#include "UpdateDialog.h"

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
        setResizable(true, false);
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
    GeissComponent geissDisplay;
    ProjectMComponent projectMDisplay;
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
    juce::TextButton tapTempoButton { "TAP" };
    juce::Array<double> tapTimes;
    static constexpr int maxTaps = 8;

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
    double panicAnimEndTime = 0.0;
    juce::TextButton captureButton { "CAP" };

    // ── MIDI Capture ──
    struct CapturedMidiEvent {
        juce::MidiMessage message;
        juce::int64 timestampMs;
    };
    juce::Array<CapturedMidiEvent> captureBuffer;
    juce::CriticalSection captureLock;
    static constexpr int CAPTURE_WINDOW_MS = 30000;
    void performMidiCapture();

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
    juce::TextButton settingsButton { "Settings" };
    juce::TextButton fullscreenButton { "VIS" };
    juce::ComboBox visSelector;
    bool visualizerFullScreen = false;
    int currentVisMode = 0;  // 0=Spectrum, 1=Lissajous, 2=G-Force, 3=Geiss
    juce::TextButton visExitButton { "EXIT" };
    juce::TextButton projectorButton { "PROJ" };
    bool projectorMode = false;
    juce::TextButton testNoteButton { "Test Note" };

    // ── Mixer ──
    std::unique_ptr<MixerComponent> mixerComponent;
    juce::TextButton mixerButton { "MIX" };
    bool mixerVisible = false;

    // ── Touch Piano ──
    TouchPianoComponent touchPiano;
    juce::TextButton pianoToggleButton { "KEYS" };
    juce::TextButton pianoOctUpButton { "Oct+" };
    juce::TextButton pianoOctDownButton { "Oct-" };
    bool touchPianoVisible = false;

    // ── Visualizer Controls ──
    // Geiss
    juce::TextButton geissWaveBtn { "Wave" };
    juce::TextButton geissPaletteBtn { "Color" };
    juce::TextButton geissSceneBtn { "Scene" };
    juce::TextButton geissWaveUpBtn { "W+" };
    juce::TextButton geissWaveDownBtn { "W-" };
    juce::TextButton geissWarpLockBtn { "Warp" };
    juce::TextButton geissPalLockBtn { "PLock" };
    juce::ComboBox geissSpeedSelector;
    juce::TextButton geissAutoPilotBtn { "Auto" };
    juce::TextButton geissBgBtn { "BG" };
    // ProjectM
    juce::TextButton pmNextBtn { "Next" };
    juce::TextButton pmPrevBtn { "Prev" };
    juce::TextButton pmRandBtn { "Rand" };
    juce::TextButton pmLockBtn { "Lock" };
    juce::TextButton pmBgBtn { "BG" };
    // G-Force
    juce::TextButton gfRibbonUpBtn { "R+" };
    juce::TextButton gfRibbonDownBtn { "R-" };
    juce::TextButton gfTrailBtn { "Trail" };
    juce::ComboBox gfSpeedSelector;
    // Spectrum
    juce::TextButton specDecayBtn { "Decay" };
    juce::TextButton specSensUpBtn { "S+" };
    juce::TextButton specSensDownBtn { "S-" };
    // Lissajous
    juce::TextButton lissZoomInBtn { "Z+" };
    juce::TextButton lissZoomOutBtn { "Z-" };
    juce::TextButton lissDotsBtn { "Dots" };

    void setVisControlsVisible();

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

    // ── Xencelabs Quick Keys ──
    QuickKeysHandler quickKeysHandler;
    juce::TextButton quickKeysButton { "QK" };
    bool quickKeysEnabled = false;
    void setupQuickKeysCallbacks();
    void syncQuickKeysParams();
    void flushMidi2Outgoing();

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
    void showSettingsMenu();
    void updateStatusLabel();
    void applyThemeToControls();

    // ── MIDI Learn ──
    enum class MidiTarget {
        None, Volume, Pan, Bpm,
        Play, Stop, Record, Metronome, Loop,
        Param0, Param1, Param2, Param3, Param4, Param5,
        TrackNext, TrackPrev,
        GeissWaveform, GeissPalette, GeissScene,
        GeissWaveScale, GeissWarpLock, GeissPaletteLock, GeissSpeed,
        GForceRibbons, GForceTrail, GForceSpeed,
        SpecDecay, SpecSensitivity,
        LissZoom, LissDots
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
