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

    // Send all-notes-off if flagged (prevents stuck notes)
    if (sendAllNotesOff.exchange(false))
    {
        for (int ch = 1; ch <= 16; ++ch)
        {
            midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
            midi.addEvent(juce::MidiMessage::allSoundOff(ch), 0);
        }
    }

    // Handle recording — capture incoming MIDI before we add clip playback
    if (recordingSlot >= 0 && engine.isPlaying())
    {
        processRecording(midi, numSamples);
    }

    // Handle playback for all playing clips
    if (engine.isPlaying())
    {
        for (int i = 0; i < NUM_SLOTS; ++i)
        {
            if (slots[static_cast<size_t>(i)].state.load() == ClipSlot::Playing)
            {
                processClipPlayback(i, midi, numSamples);
            }
        }

        lastPositionInBeats = engine.getPositionInBeats();
    }

    // Audio passes through unchanged (this node only handles MIDI)
}

void ClipPlayerNode::processClipPlayback(int slotIndex, juce::MidiBuffer& midi, int numSamples)
{
    auto& slot = slots[static_cast<size_t>(slotIndex)];
    if (slot.clip == nullptr) return;

    auto& clip = *slot.clip;
    double pos = engine.getPositionInBeats();
    double bpm = engine.getBpm();
    double beatsPerSample = (bpm / 60.0) / currentSampleRate;

    double clipLen = clip.lengthInBeats;
    if (clipLen <= 0.0) return;

    double clipTimelineStart = clip.timelinePosition;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        double beatPos = pos + (sample * beatsPerSample);

        // Calculate clip-local position, looping within the clip
        double relPos = beatPos - clipTimelineStart;
        double clipPos = std::fmod(relPos, clipLen);
        if (clipPos < 0.0) clipPos += clipLen;

        // Check previous sample position for event detection
        double prevRelPos = relPos - beatsPerSample;
        double prevClipPos = std::fmod(prevRelPos, clipLen);
        if (prevClipPos < 0.0) prevClipPos += clipLen;

        // Handle wrap-around
        bool wrapped = prevClipPos > clipPos;

        for (int e = 0; e < clip.events.getNumEvents(); ++e)
        {
            auto* event = clip.events.getEventPointer(e);
            double eventBeat = event->message.getTimeStamp();

            bool shouldTrigger = false;

            if (wrapped)
            {
                // Event is between prevClipPos..end OR 0..clipPos
                if (eventBeat > prevClipPos || eventBeat <= clipPos)
                    shouldTrigger = true;
            }
            else
            {
                if (eventBeat > prevClipPos && eventBeat <= clipPos)
                    shouldTrigger = true;
            }

            if (shouldTrigger)
            {
                midi.addEvent(event->message, sample);
            }
        }
    }
}

void ClipPlayerNode::processRecording(const juce::MidiBuffer& incomingMidi, int numSamples)
{
    if (recordingSlot < 0 || recordingSlot >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(recordingSlot)];
    if (slot.clip == nullptr) return;

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

    if (slot.state.load() == ClipSlot::Empty && armed.load() && engine.isRecording())
    {
        // Start recording
        slot.clip = std::make_unique<MidiClip>();
        slot.state.store(ClipSlot::Recording);
        recordingSlot = slotIndex;
        recordStartBeat = engine.getPositionInBeats();
    }
    else if (slot.hasContent() && slot.state.load() != ClipSlot::Playing)
    {
        // Start playback
        stopAllSlots();
        slot.state.store(ClipSlot::Playing);
    }
    else if (slot.state.load() == ClipSlot::Playing)
    {
        // Stop playback
        slot.state.store(ClipSlot::Stopped);
        sendAllNotesOff.store(true);
    }
    else if (slot.state.load() == ClipSlot::Recording)
    {
        // Stop recording
        slot.state.store(ClipSlot::Stopped);
        recordingSlot = -1;
        if (slot.clip != nullptr)
            slot.clip->events.sort();
    }
}

void ClipPlayerNode::stopSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= NUM_SLOTS) return;

    auto& slot = slots[static_cast<size_t>(slotIndex)];

    if (slot.state.load() == ClipSlot::Recording)
    {
        recordingSlot = -1;
        if (slot.clip != nullptr)
            slot.clip->events.sort();
    }

    if (slot.state.load() != ClipSlot::Empty)
    {
        slot.state.store(slot.hasContent() ? ClipSlot::Stopped : ClipSlot::Empty);
        sendAllNotesOff.store(true);
    }
}

void ClipPlayerNode::stopAllSlots()
{
    for (int i = 0; i < NUM_SLOTS; ++i)
        stopSlot(i);
}
