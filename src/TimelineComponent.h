#pragma once

#include <JuceHeader.h>
#include "PluginHost.h"
#include "SequencerEngine.h"
#include "PianoRollComponent.h"

class TimelineComponent : public juce::Component, public juce::Timer
{
public:
    TimelineComponent(PluginHost& host);

    // Public editing actions (called from toolbar)
    void createClipAtPlayhead();
    void deleteSelected();
    void duplicateSelected();
    void splitSelected();
    bool hasSelection() const { return selectedClip.isValid(); }
    MidiClip* getSelectedClip();

    // Callback for undo snapshots before drag edits
    std::function<void()> onBeforeEdit;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
    void mouseMagnify(const juce::MouseEvent& e, float scaleFactor) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    bool keyPressed(const juce::KeyPress& key) override;

private:
    PluginHost& pluginHost;

    // View state
    double scrollX = 0.0;
    double lastPaintedScrollX = -1.0;
    int scrollY = 0;           // vertical scroll offset in pixels
    double pixelsPerBeat = 40.0;
    int trackHeight = 72;      // computed dynamically to fit 7 tracks
    static constexpr int visibleTracks = 7;
    int headerHeight = 28;
    int trackLabelWidth = 140;  // room for track name + M/S buttons
    double gridResolution = 0.25; // beats per grid line (0.25 = 1/16, 0.5 = 1/8, 1.0 = 1/4)
    void recalcTrackHeight();

public:
    void setGridResolution(double beatsPerGrid) { gridResolution = beatsPerGrid; repaint(); }
    double getGridResolution() const { return gridResolution; }
    double snapToGrid(double beat) const;
    void quantizeSelectedClip();

    // Navigation
    void suppressAutoFollow() { userScrollActive = true; userScrollExpireTime = juce::Time::getMillisecondCounterHiRes() * 0.001 + 3.0; }
    void zoomIn() { pixelsPerBeat = juce::jmin(200.0, pixelsPerBeat * 1.3); suppressAutoFollow(); repaint(); }
    void zoomOut() { pixelsPerBeat = juce::jmax(10.0, pixelsPerBeat / 1.3); suppressAutoFollow(); repaint(); }
    void scrollLeft() { scrollX = juce::jmax(0.0, scrollX - 4.0); suppressAutoFollow(); repaint(); }
    void scrollRight() { scrollX += 4.0; suppressAutoFollow(); repaint(); }
private:

    // Selection / interaction
    struct ClipRef {
        int trackIndex = -1;
        int slotIndex = -1;
        bool isValid() const { return trackIndex >= 0 && slotIndex >= 0; }
    };

    enum DragMode { NoDrag, MoveClip, ResizeClipLeft, ResizeClipRight };
    DragMode dragMode = NoDrag;

    ClipRef selectedClip;
    ClipRef dragClip;
    double dragStartBeat = 0.0;
    int dragStartTrack = 0;
    double clipOrigPosition = 0.0;
    double clipOrigLength = 0.0;

    // Hit testing
    ClipRef hitTestClip(float x, float y) const;
    juce::Rectangle<float> getClipRect(int trackIndex, int slotIndex) const;
    bool isOnClipLeftEdge(float x, const juce::Rectangle<float>& rect) const;
    bool isOnClipRightEdge(float x, const juce::Rectangle<float>& rect) const;
    ClipSlot* getSlot(const ClipRef& ref) const;
    MidiClip* getClip(const ClipRef& ref) const;

    // Drawing
    void drawHeader(juce::Graphics& g);
    void drawTrackLanes(juce::Graphics& g);
    void drawTrackControls(juce::Graphics& g);
    void drawClips(juce::Graphics& g);
    void drawAutomation(juce::Graphics& g);
    void drawPlayhead(juce::Graphics& g);
    void drawMiniNotes(juce::Graphics& g, const MidiClip& clip, juce::Rectangle<float> area);

    // Track control click areas
    juce::Rectangle<int> getSelectButtonRect(int trackIndex) const;
    juce::Rectangle<int> getMuteButtonRect(int trackIndex) const;
    juce::Rectangle<int> getSoloButtonRect(int trackIndex) const;
    void handleTrackControlClick(int trackIndex, float x, float y);

    // Touch scroll + pinch zoom
    bool touchScrolling = false;
    juce::Point<float> touchScrollStart;
    juce::Point<float> firstFingerPos;     // track first finger for pinch distance
    double touchScrollStartX = 0.0;
    int touchScrollStartY = 0;
    double pinchStartPixelsPerBeat = 0.0;
    float pinchStartDistance = 0.0f;

    // User scroll override — suppress auto-follow while user is scrolling
    bool userScrollActive = false;
    double userScrollExpireTime = 0.0;  // time when auto-follow resumes

    // Loop region dragging
    bool draggingLoop = false;
    double loopDragStartBeat = 0.0;
    void drawLoopRegion(juce::Graphics& g);

    // Long press detection for arm lock
    int longPressTrack = -1;
    juce::Point<float> longPressPos;
    juce::int64 mouseDownTime = 0;
    bool longPressTriggered = false;
    static constexpr int longPressMs = 500;

    // Coordinate conversion
    float beatToX(double beat) const;
    double xToBeat(float x) const;
    int yToTrack(float y) const;

    // Editing operations
    void deleteSelectedClip();
    void duplicateSelectedClip();
    void splitClipAtBeat(const ClipRef& ref, double beat);
    void createEmptyClip(int trackIndex, double beatPos);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineComponent)
};
