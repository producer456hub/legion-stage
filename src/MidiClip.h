#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

struct MidiClip
{
    juce::MidiMessageSequence events;  // timestamps in beats (relative to clip start)
    double lengthInBeats = 4.0;        // default 1 bar at 4/4
    double timelinePosition = 0.0;     // where this clip sits on the arrangement timeline (in beats)
};

struct ClipSlot
{
    enum State { Empty, Stopped, Playing, Recording };

    std::unique_ptr<MidiClip> clip;
    std::atomic<State> state { Empty };

    bool hasContent() const { return clip != nullptr && clip->events.getNumEvents() > 0; }
};
