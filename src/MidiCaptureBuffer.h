#pragma once

#include <JuceHeader.h>
#include <mutex>
#include <vector>

// Ableton-style Capture MIDI — always listening on armed tracks.
// Stores up to 16384 MIDI events; when full, discards the oldest 1024.
// Pressing the Capture button retrieves the data as clips.
// If transport was stopped and no clips exist, tempo is estimated from note timing.
class MidiCaptureBuffer
{
public:
    static constexpr int MAX_EVENTS = 16384;
    static constexpr int DISCARD_COUNT = 1024;

    MidiCaptureBuffer() { events.reserve(MAX_EVENTS); }

    // Feed a note message into the capture buffer.
    // Called from any thread — the caller is responsible for only feeding
    // notes from armed/input-monitored tracks.
    void addMessage(const juce::MidiMessage& msg, int trackIndex)
    {
        if (!msg.isNoteOnOrOff()) return;

        std::lock_guard<std::mutex> lock(mutex);

        double now = juce::Time::getMillisecondCounterHiRes() * 0.001;

        CapturedEvent ev;
        ev.msg = msg;
        ev.trackIndex = trackIndex;
        ev.wallClockTime = now;
        events.push_back(ev);

        // Trim when capacity exceeded
        if (static_cast<int>(events.size()) > MAX_EVENTS)
            events.erase(events.begin(), events.begin() + DISCARD_COUNT);
    }

    // Returns true if there's captured MIDI data ready to retrieve.
    bool hasContent() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        // Need at least one note-on
        for (auto& ev : events)
            if (ev.msg.isNoteOn()) return true;
        return false;
    }

    // Estimate tempo from note-on timing (average inter-onset interval).
    // Returns 0.0 if not enough data to estimate.
    double estimateTempo() const
    {
        std::lock_guard<std::mutex> lock(mutex);

        // Collect note-on timestamps
        std::vector<double> onsets;
        for (auto& ev : events)
            if (ev.msg.isNoteOn())
                onsets.push_back(ev.wallClockTime);

        if (onsets.size() < 4) return 0.0;

        // Compute inter-onset intervals
        std::vector<double> iois;
        for (size_t i = 1; i < onsets.size(); ++i)
        {
            double dt = onsets[i] - onsets[i - 1];
            if (dt > 0.05 && dt < 2.0)  // filter outliers (50ms to 2s)
                iois.push_back(dt);
        }

        if (iois.size() < 3) return 0.0;

        // Median IOI (more robust than mean)
        std::sort(iois.begin(), iois.end());
        double medianIOI = iois[iois.size() / 2];

        // Assume median IOI corresponds to a beat subdivision.
        // Try common subdivisions: 1 beat, 1/2 beat, 1/4 beat
        // Pick the one that gives a tempo in [60, 200] BPM
        for (double div : { 1.0, 0.5, 0.25, 2.0 })
        {
            double beatDuration = medianIOI / div;
            double bpm = 60.0 / beatDuration;
            if (bpm >= 60.0 && bpm <= 200.0)
                return std::round(bpm);
        }

        // Fallback: assume 1 beat per IOI, clamp
        double bpm = 60.0 / medianIOI;
        return std::round(juce::jlimit(60.0, 200.0, bpm));
    }

    // Retrieve captured MIDI for all tracks.
    // Returns a map of trackIndex -> MidiMessageSequence (timestamps in beats).
    // bpm is used to convert wall-clock time to beats.
    // Clears the buffer after retrieval.
    struct TrackCapture {
        int trackIndex;
        juce::MidiMessageSequence sequence;
        double lengthInBeats;
    };

    juce::Array<TrackCapture> retrieve(double bpm)
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (events.empty()) return {};

        // Find the reference time (first event)
        double refTime = events.front().wallClockTime;
        double beatsPerSecond = bpm / 60.0;

        // Group events by track
        std::map<int, juce::MidiMessageSequence> trackSeqs;
        for (auto& ev : events)
        {
            double beatTime = (ev.wallClockTime - refTime) * beatsPerSecond;
            auto stamped = ev.msg;
            stamped.setTimeStamp(beatTime);
            trackSeqs[ev.trackIndex].addEvent(stamped);
        }

        juce::Array<TrackCapture> result;
        for (auto& [trackIdx, seq] : trackSeqs)
        {
            seq.updateMatchedPairs();

            // Find last event time for clip length
            double lastBeat = 0.0;
            for (int i = 0; i < seq.getNumEvents(); ++i)
                lastBeat = juce::jmax(lastBeat, seq.getEventPointer(i)->message.getTimeStamp());

            double clipLength = std::ceil(lastBeat / 4.0) * 4.0;
            if (clipLength < 4.0) clipLength = 4.0;

            TrackCapture tc;
            tc.trackIndex = trackIdx;
            tc.sequence = seq;
            tc.lengthInBeats = clipLength;
            result.add(tc);
        }

        events.clear();
        return result;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        events.clear();
    }

private:
    struct CapturedEvent {
        juce::MidiMessage msg;
        int trackIndex = 0;
        double wallClockTime = 0.0;
    };

    mutable std::mutex mutex;
    std::vector<CapturedEvent> events;
};
