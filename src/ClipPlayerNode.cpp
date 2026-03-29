#include "ClipPlayerNode.h"

ClipPlayerNode::ClipPlayerNode(SequencerEngine& eng)
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      engine(eng)
{
}

void ClipPlayerNode::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate = sampleRate;
}

void ClipPlayerNode::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    int numSamples = buffer.getNumSamples();

    // Send all-notes-off if flagged (stop/panic — hard kill)
    bool skipPlayback = false;
    if (sendAllNotesOff.exchange(false))
    {
        killActiveNotes(midi, 0, true);
        // Set high so next play detects a "wrap" and does a clean-start skip
        lastPositionInBeats = 999999.0;
        wasInsideClip.fill(false);
        skipPlayback = true;
    }

    // Check if we should start recording (not during count-in)
    if (engine.isPlaying() && engine.isRecording() && !engine.isInCountIn() && armed.load() && recordingSlot < 0)
    {
        // First check for explicitly armed slots
        int targetSlot = -1;
        for (int i = 0; i < NUM_SLOTS; ++i)
        {
            if (slots[static_cast<size_t>(i)].state.load() == ClipSlot::Armed)
            {
                targetSlot = i;
                break;
            }
        }

        // If no slot is armed, auto-find an empty/available slot
        if (targetSlot < 0)
        {
            for (int i = 0; i < NUM_SLOTS; ++i)
            {
                auto& s = slots[static_cast<size_t>(i)];
                auto state = s.state.load();
                if (state == ClipSlot::Empty ||
                    (state == ClipSlot::Stopped && !s.hasContent()) ||
                    (state == ClipSlot::Stopped && s.clip == nullptr))
                {
                    targetSlot = i;
                    break;
                }
            }
        }

        if (targetSlot >= 0)
        {
            auto& slot = slots[static_cast<size_t>(targetSlot)];
            if (slot.clip == nullptr)
                slot.clip = std::make_unique<MidiClip>();
            slot.clip->timelinePosition = engine.getPositionInBeats();
            slot.state.store(ClipSlot::Recording);
            recordingSlot = targetSlot;
            recordStartBeat = engine.getPositionInBeats();
        }
    }

    // Handle recording — capture incoming MIDI before we add clip playback
    if (recordingSlot >= 0 && engine.isPlaying())
    {
        processRecording(midi, numSamples);
    }

    // Handle playback for all playing clips (skip during count-in)
    if (engine.isPlaying() && !engine.isInCountIn())
    {
        double currentPos = engine.getPositionInBeats();

        // Detect loop wrap-around (position jumped backward) — kill all sounding notes
        bool loopJustWrapped = (currentPos < lastPositionInBeats - 0.001);
        if (loopJustWrapped)
        {
            killActiveNotes(midi, 0);
            lastPositionInBeats = currentPos - 0.001;
            skipPlayback = true;
        }

        if (!skipPlayback)
        {
            for (int i = 0; i < NUM_SLOTS; ++i)
            {
                if (slots[static_cast<size_t>(i)].state.load() == ClipSlot::Playing)
                {
                    processClipPlayback(i, midi, numSamples);
                }
            }
        }

        lastPositionInBeats = currentPos;
    }

    // Audio passes through unchanged (this node only handles MIDI)
}

void ClipPlayerNode::processClipPlayback(int slotIndex, juce::MidiBuffer& midi, int numSamples)
{
    auto& slot = slots[static_cast<size_t>(slotIndex)];
    if (slot.clip == nullptr) return;

    auto& clip = *slot.clip;
    double bpm = engine.getBpm();
    double beatsPerSample = (bpm / 60.0) / currentSampleRate;
    double beatsThisBlock = beatsPerSample * numSamples;
    // Position was already advanced, so subtract back to get block start
    double pos = engine.getPositionInBeats() - beatsThisBlock;
    if (pos < 0.0) pos = 0.0;

    double clipLen = clip.lengthInBeats;
    if (clipLen <= 0.0) return;

    double clipTimelineStart = clip.timelinePosition;
    double clipTimelineEnd = clipTimelineStart + clipLen;

    // Check if playhead just exited this clip — send all-notes-off
    double blockStartBeat = pos;
    double blockEndBeat = pos + (numSamples * beatsPerSample);
    bool isInsideNow = (blockEndBeat > clipTimelineStart && blockStartBeat < clipTimelineEnd);

    if (wasInsideClip[static_cast<size_t>(slotIndex)] && !isInsideNow)
    {
        killActiveNotes(midi, 0);
    }
    wasInsideClip[static_cast<size_t>(slotIndex)] = isInsideNow;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        double beatPos = pos + (sample * beatsPerSample);

        // Calculate clip-local position — NO looping in arranger mode
        double relPos = beatPos - clipTimelineStart;

        // Skip if playhead is before or past the clip
        // Use small epsilon past clipLen to catch note-offs at the boundary
        if (relPos < 0.0 || relPos >= clipLen + 0.01)
            continue;

        double clipPos = relPos;
        double prevClipPos = relPos - beatsPerSample;

        for (int e = 0; e < clip.events.getNumEvents(); ++e)
        {
            auto* event = clip.events.getEventPointer(e);
            double eventBeat = event->message.getTimeStamp();

            bool shouldTrigger = false;

            if (sample == 0 && relPos < beatsPerSample)
            {
                // First sample inside the clip — trigger events at beat 0 only once
                if (eventBeat >= 0.0 && eventBeat <= clipPos + 0.0001)
                    shouldTrigger = true;
            }
            else if (prevClipPos >= 0.0)
            {
                // Normal case: half-open interval (prevClipPos, clipPos]
                if (eventBeat > prevClipPos && eventBeat <= clipPos + 0.0001)
                    shouldTrigger = true;
            }

            if (shouldTrigger)
            {
                midi.addEvent(event->message, sample);

                // Track active notes for clean loop/stop handling
                if (event->message.isNoteOn())
                    activePlaybackNotes.insert((event->message.getChannel() << 8) | event->message.getNoteNumber());
                else if (event->message.isNoteOff())
                    activePlaybackNotes.erase((event->message.getChannel() << 8) | event->message.getNoteNumber());
            }
        }
    }
}

void ClipPlayerNode::processRecording(const juce::MidiBuffer& incomingMidi, int numSamples)
{
    if (recordingSlot < 0 || recordingSlot >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(recordingSlot)];
    if (slot.clip == nullptr) { recordingSlot = -1; return; }

    double bpm = engine.getBpm();
    double beatsPerSample = (bpm / 60.0) / currentSampleRate;
    double pos = engine.getPositionInBeats();

    for (const auto metadata : incomingMidi)
    {
        auto msg = metadata.getMessage();
        if (msg.isNoteOnOrOff())
        {
            double beatTimestamp = (pos - recordStartBeat) + (metadata.samplePosition * beatsPerSample);
            if (beatTimestamp < 0.0) beatTimestamp = 0.0;

            msg.setTimeStamp(beatTimestamp);
            slot.clip->events.addEvent(msg);

            // Extend clip length to fit the recorded content
            if (beatTimestamp > slot.clip->lengthInBeats - 0.1)
            {
                // Round up to next bar
                slot.clip->lengthInBeats = std::ceil(beatTimestamp / 4.0) * 4.0;
                if (slot.clip->lengthInBeats < 4.0) slot.clip->lengthInBeats = 4.0;
            }
        }
    }
}

void ClipPlayerNode::triggerSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(slotIndex)];
    auto currentState = slot.state.load();

    bool slotIsEmpty = currentState == ClipSlot::Empty ||
                       (slot.clip != nullptr && !slot.hasContent() && currentState == ClipSlot::Stopped);

    if (currentState == ClipSlot::Armed)
    {
        // Click armed slot → disarm it
        slot.state.store(slot.clip != nullptr ? ClipSlot::Stopped : ClipSlot::Empty);
        return;
    }

    if (currentState == ClipSlot::Recording)
    {
        // Click recording slot → stop recording, auto-set to Playing
        recordingSlot = -1;
        if (slot.clip != nullptr)
            slot.clip->events.sort();
        slot.state.store(slot.hasContent() ? ClipSlot::Playing : ClipSlot::Empty);
        return;
    }

    if (currentState == ClipSlot::Playing)
    {
        // Click playing slot → stop playback
        slot.state.store(ClipSlot::Stopped);
        sendAllNotesOff.store(true);
        return;
    }

    if (slotIsEmpty && armed.load())
    {
        if (engine.isRecording() && engine.isPlaying())
        {
            // Transport already running with REC → start recording immediately
            if (slot.clip == nullptr)
            {
                slot.clip = std::make_unique<MidiClip>();
                slot.clip->timelinePosition = engine.getPositionInBeats();
            }
            slot.state.store(ClipSlot::Recording);
            recordingSlot = slotIndex;
            recordStartBeat = engine.getPositionInBeats();
        }
        else
        {
            // Transport not running → arm the slot, recording starts when transport plays
            if (slot.clip == nullptr)
                slot.clip = std::make_unique<MidiClip>();
            slot.state.store(ClipSlot::Armed);
        }
        return;
    }

    if (slot.hasContent())
    {
        // Click a clip with content → start playback
        stopAllSlots();
        slot.state.store(ClipSlot::Playing);
    }
}

void ClipPlayerNode::stopSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(slotIndex)];
    auto state = slot.state.load();

    // Playing and Armed clips stay in their state — they resume when transport plays again
    if (state == ClipSlot::Armed || state == ClipSlot::Empty || state == ClipSlot::Stopped)
        return;

    if (state == ClipSlot::Playing)
    {
        sendAllNotesOff.store(true);
        // If clip has no content, clean it up instead of keeping it in Playing
        if (!slot.hasContent())
        {
            slot.clip = nullptr;
            slot.state.store(ClipSlot::Empty);
        }
        return;
    }

    if (state == ClipSlot::Recording)
    {
        recordingSlot = -1;
        if (slot.clip != nullptr)
            slot.clip->events.sort();
        // After recording, go to Playing only if we captured notes — otherwise clean up
        if (slot.hasContent())
        {
            slot.state.store(ClipSlot::Playing);
        }
        else
        {
            slot.clip = nullptr;
            slot.state.store(ClipSlot::Empty);
        }
        sendAllNotesOff.store(true);
    }
}

void ClipPlayerNode::stopAllSlots()
{
    for (int i = 0; i < NUM_SLOTS; ++i)
        stopSlot(i);
}

void ClipPlayerNode::killActiveNotes(juce::MidiBuffer& midi, int sampleOffset, bool hard)
{
    // Send explicit note-offs for every tracked note
    for (int key : activePlaybackNotes)
    {
        int ch = (key >> 8) & 0xF;
        int note = key & 0x7F;
        midi.addEvent(juce::MidiMessage::noteOff(ch, note), sampleOffset);
    }
    activePlaybackNotes.clear();

    // Hard kill: send note-off for ALL possible notes on channel 1
    // Some plugins ignore CC 120/123, so brute-force every note
    if (hard)
    {
        for (int note = 0; note < 128; ++note)
            midi.addEvent(juce::MidiMessage::noteOff(1, note), sampleOffset);
        for (int ch = 1; ch <= 16; ++ch)
        {
            midi.addEvent(juce::MidiMessage::allNotesOff(ch), sampleOffset);
            midi.addEvent(juce::MidiMessage::allSoundOff(ch), sampleOffset);
        }
    }
}
