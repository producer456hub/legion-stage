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

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override;
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
    double pixelsPerBeat = 40.0;
    int trackHeight = 56;      // taller for touch-friendly buttons
    int headerHeight = 24;
    int trackLabelWidth = 160;  // wide enough for big buttons

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
    void drawPlayhead(juce::Graphics& g);
    void drawMiniNotes(juce::Graphics& g, const MidiClip& clip, juce::Rectangle<float> area);

    // Track control click areas
    juce::Rectangle<int> getArmButtonRect(int trackIndex) const;
    juce::Rectangle<int> getSelectButtonRect(int trackIndex) const;
    void handleTrackControlClick(int trackIndex, float x, float y);

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
