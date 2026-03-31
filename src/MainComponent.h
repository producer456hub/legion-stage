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
#include "GeissComponent.h"
#include "ProjectMComponent.h"
#include "TouchPianoComponent.h"
#include "MixerComponent.h"
#include "UpdateDialog.h"
#include "MidiCaptureBuffer.h"
#include "GamepadHandler.h"
#include "GamepadOverlayComponent.h"

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

// Multi-line OLED info display for beat/status
class OledInfoPanel : public juce::Component
{
public:
    void setLines(const juce::String& line1, const juce::String& line2, const juce::String& line3 = {})
    {
        lines[0] = line1; lines[1] = line2; lines[2] = line3;
        repaint();
    }
    void setColors(juce::Colour text, juce::Colour bg) { textCol = text; bgCol = bg; repaint(); }
    void setFontName(const juce::String& name) { fontName = name; repaint(); }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(bgCol);
        g.fillRoundedRectangle(bounds, 4.0f);

        int numLines = lines[2].isNotEmpty() ? 3 : (lines[1].isNotEmpty() ? 2 : 1);
        float lineH = bounds.getHeight() / static_cast<float>(numLines);
        float fontSize = juce::jmin(lineH * 0.75f, 13.0f);

        g.setFont(juce::Font(fontName, fontSize, juce::Font::bold));
        for (int i = 0; i < numLines; ++i)
        {
            auto lineRect = bounds.removeFromTop(lineH).reduced(4.0f, 0.0f);
            // First line (beat) brighter, others dimmer
            g.setColour(i == 0 ? textCol : textCol.withAlpha(0.65f));
            g.drawText(lines[i], lineRect, juce::Justification::centredLeft);
        }
    }

private:
    juce::String lines[3];
    juce::Colour textCol { 0xffb8d8f0 };
    juce::Colour bgCol { 0xff000000 };
    juce::String fontName { "Consolas" };
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
    juce::TextButton bpmDownButton { "-" };  // kept for logic, hidden
    juce::Label bpmLabel;
    juce::TextButton bpmUpButton { "+" };   // kept for logic, hidden
    // Combined up/down arrow button for BPM
    class BpmArrowButton : public juce::Component
    {
    public:
        std::function<void()> onUp, onDown;
        void paint(juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            g.setColour(juce::Colour(0xff080808));
            g.fillRoundedRectangle(bounds.reduced(1.0f), 4.0f);
            g.setColour(juce::Colour(0xff3a3530));
            g.drawRoundedRectangle(bounds.reduced(1.0f), 4.0f, 1.0f);
            float midY = bounds.getCentreY();
            float cx = bounds.getCentreX();
            // Up arrow
            juce::Path up;
            up.addTriangle(cx - 6, midY - 2, cx + 6, midY - 2, cx, midY - 10);
            g.setColour(juce::Colour(0xffb8d8f0));
            g.fillPath(up);
            // Down arrow
            juce::Path down;
            down.addTriangle(cx - 6, midY + 2, cx + 6, midY + 2, cx, midY + 10);
            g.fillPath(down);
        }
        void mouseDown(const juce::MouseEvent& e) override
        {
            if (e.y < getHeight() / 2) { if (onUp) onUp(); }
            else { if (onDown) onDown(); }
        }
    } bpmArrowButton;
    OledInfoPanel beatPanel;
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

    // ── MIDI Capture (Ableton-style — always listening on armed tracks) ──
    MidiCaptureBuffer captureBuffer;
    juce::TextButton captureButton { "CAPT" };
    void retrieveCapture();
    bool hasExistingClips() const;

    // ── Gamepad (Legion Go) ──
    GamepadHandler gamepadHandler;
    GamepadOverlayComponent gamepadOverlay;
    juce::TextButton goButton { "GO" };

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
    juce::String cachedStatusText;
    juce::String cachedStatusLine1;  // MIDI input info
    juce::String cachedStatusLine2;  // Audio device info

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
