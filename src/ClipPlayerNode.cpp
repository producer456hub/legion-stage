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
    if (sendAllNotesOff.exchange(false))
    {
        killActiveNotes(midi, 0, true);
        lastPositionInBeats = -1.0;
        wasInsideClip.fill(false);
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
            double beatsPerSample = (engine.getBpm() / 60.0) / currentSampleRate;
            double blockStartPos = engine.getPositionInBeats() - (beatsPerSample * numSamples);
            slot.clip->timelinePosition = blockStartPos;
            slot.state.store(ClipSlot::Recording);
            recordingSlot = targetSlot;
            recordStartBeat = blockStartPos;
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

        // Detect loop wrap-around or play start (position jumped) — kill all sounding notes
        bool positionJumped = (currentPos < lastPositionInBeats - 0.001);
        if (positionJumped)
        {
            killActiveNotes(midi, 0);
            wasInsideClip.fill(false);
        }

        // Always run clip playback — note-offs were added first in the buffer
        // so synths process them before any new note-ons
        for (int i = 0; i < NUM_SLOTS; ++i)
        {
            if (slots[static_cast<size_t>(i)].state.load() == ClipSlot::Playing)
            {
                processClipPlayback(i, midi, numSamples);
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
    double blockStart = engine.getPositionInBeats() - beatsThisBlock;
    if (blockStart < 0.0) blockStart = 0.0;
    double blockEnd = blockStart + beatsThisBlock;

    double clipStart = clip.timelinePosition;
    double clipEnd = clipStart + clip.lengthInBeats;
    if (clip.lengthInBeats <= 0.0) return;

    // Detect clip exit → kill active notes
    bool isInside = (blockEnd > clipStart && blockStart < clipEnd);
    if (wasInsideClip[static_cast<size_t>(slotIndex)] && !isInside)
        killActiveNotes(midi, 0);
    wasInsideClip[static_cast<size_t>(slotIndex)] = isInside;
    if (!isInside) return;

    // Clip-relative time range for this block
    double relStart = juce::jmax(0.0, blockStart - clipStart);
    double relEnd = blockEnd - clipStart;

    // Use JUCE's getNextIndexAtTime to find first event in range
    int startIdx = clip.events.getNextIndexAtTime(relStart);

    for (int e = startIdx; e < clip.events.getNumEvents(); ++e)
    {
        auto* holder = clip.events.getEventPointer(e);
        double eventBeat = holder->message.getTimeStamp();

        if (eventBeat >= relEnd)
            break;

        // Calculate sample offset within block
        double eventAbsBeat = clipStart + eventBeat;
        int samplePos = static_cast<int>((eventAbsBeat - blockStart) / beatsPerSample);
        samplePos = juce::jlimit(0, numSamples - 1, samplePos);

        midi.addEvent(holder->message, samplePos);

        if (holder->message.isNoteOn())
            activePlaybackNotes.insert((holder->message.getChannel() << 8) | holder->message.getNoteNumber());
        else if (holder->message.isNoteOff())
            activePlaybackNotes.erase((holder->message.getChannel() << 8) | holder->message.getNoteNumber());
    }
}

void ClipPlayerNode::processRecording(const juce::MidiBuffer& incomingMidi, int numSamples)
{
    if (recordingSlot < 0 || recordingSlot >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(recordingSlot)];
    if (slot.clip == nullptr) { recordingSlot = -1; return; }

    double bpm = engine.getBpm();
    double beatsPerSample = (bpm / 60.0) / currentSampleRate;
    double beatsThisBlock = beatsPerSample * numSamples;
    // Use start-of-block position (engine already advanced past this block)
    double pos = engine.getPositionInBeats() - beatsThisBlock;

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
        {
            slot.clip->events.sort();
            closeOpenNotes(*slot.clip);
            slot.clip->events.updateMatchedPairs();
        }
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
        {
            slot.clip->events.sort();
            closeOpenNotes(*slot.clip);
            slot.clip->events.updateMatchedPairs();
        }
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

void ClipPlayerNode::closeOpenNotes(MidiClip& clip)
{
    // Find note-ons without matching note-offs and add note-offs at clip end
    std::set<int> openNotes; // (channel << 8) | noteNumber
    for (int e = 0; e < clip.events.getNumEvents(); ++e)
    {
        auto& msg = clip.events.getEventPointer(e)->message;
        int key = (msg.getChannel() << 8) | msg.getNoteNumber();
        if (msg.isNoteOn())
            openNotes.insert(key);
        else if (msg.isNoteOff())
            openNotes.erase(key);
    }
    for (int key : openNotes)
    {
        int ch = (key >> 8) & 0xF;
        int note = key & 0x7F;
        auto noteOff = juce::MidiMessage::noteOff(ch, note);
        noteOff.setTimeStamp(clip.lengthInBeats - 0.001);
        clip.events.addEvent(noteOff);
    }
    if (!openNotes.empty())
        clip.events.sort();
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
