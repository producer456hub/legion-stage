#include "TimelineComponent.h"

TimelineComponent::TimelineComponent(PluginHost& host)
    : pluginHost(host)
{
    setWantsKeyboardFocus(true);
    startTimerHz(30);
}

// ── Coordinate conversion ────────────────────────────────────────────────────

float TimelineComponent::beatToX(double beat) const
{
    return trackLabelWidth + static_cast<float>((beat - scrollX) * pixelsPerBeat);
}

double TimelineComponent::xToBeat(float x) const
{
    return scrollX + (x - trackLabelWidth) / pixelsPerBeat;
}

int TimelineComponent::yToTrack(float y) const
{
    return static_cast<int>((y - headerHeight) / trackHeight);
}

// ── Hit testing ──────────────────────────────────────────────────────────────

juce::Rectangle<float> TimelineComponent::getClipRect(int trackIndex, int slotIndex) const
{
    auto* cp = pluginHost.getTrack(trackIndex).clipPlayer;
    if (cp == nullptr) return {};

    auto& slot = cp->getSlot(slotIndex);
    if (slot.clip == nullptr) return {};

    float x1 = beatToX(slot.clip->timelinePosition);
    float x2 = beatToX(slot.clip->timelinePosition + slot.clip->lengthInBeats);
    float y = static_cast<float>(headerHeight + trackIndex * trackHeight + 2);
    float h = static_cast<float>(trackHeight - 4);

    return { x1, y, x2 - x1, h };
}

TimelineComponent::ClipRef TimelineComponent::hitTestClip(float x, float y) const
{
    int trackIdx = yToTrack(y);
    if (trackIdx < 0 || trackIdx >= PluginHost::NUM_TRACKS) return {};

    auto* cp = pluginHost.getTrack(trackIdx).clipPlayer;
    if (cp == nullptr) return {};

    for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
    {
        auto rect = getClipRect(trackIdx, s);
        if (!rect.isEmpty() && rect.contains(x, y))
            return { trackIdx, s };
    }
    return {};
}

bool TimelineComponent::isOnClipLeftEdge(float x, const juce::Rectangle<float>& rect) const
{
    return x < rect.getX() + 6.0f;
}

bool TimelineComponent::isOnClipRightEdge(float x, const juce::Rectangle<float>& rect) const
{
    return x > rect.getRight() - 6.0f;
}

ClipSlot* TimelineComponent::getSlot(const ClipRef& ref) const
{
    if (!ref.isValid()) return nullptr;
    auto* cp = pluginHost.getTrack(ref.trackIndex).clipPlayer;
    if (cp == nullptr) return nullptr;
    return &cp->getSlot(ref.slotIndex);
}

MidiClip* TimelineComponent::getClip(const ClipRef& ref) const
{
    auto* slot = getSlot(ref);
    if (slot == nullptr) return nullptr;
    return slot->clip.get();
}

// ── Timer ────────────────────────────────────────────────────────────────────

void TimelineComponent::timerCallback()
{
    auto& engine = pluginHost.getEngine();
    double pos = engine.getPositionInBeats();
    float playheadX = beatToX(pos);
    float viewWidth = static_cast<float>(getWidth());

    if (engine.isPlaying())
    {
        // Auto-scroll to follow playhead during playback
        if (playheadX > viewWidth * 0.8f)
            scrollX = pos - (viewWidth * 0.2 - trackLabelWidth) / pixelsPerBeat;
        else if (playheadX < static_cast<float>(trackLabelWidth))
        {
            scrollX = pos - 1.0;
            if (scrollX < 0.0) scrollX = 0.0;
        }

        repaint();
    }
    else
    {
        // When stopped, check if playhead moved (e.g. reset to 0) and follow it
        if (playheadX < static_cast<float>(trackLabelWidth) || playheadX > viewWidth)
        {
            scrollX = pos;
            if (scrollX < 0.0) scrollX = 0.0;
            repaint();
        }
    }
}

// ── Mouse handling ───────────────────────────────────────────────────────────

void TimelineComponent::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);

    // Click on header = jump playhead to that position
    if (e.y < headerHeight && e.x >= trackLabelWidth)
    {
        double beat = snapToGrid(xToBeat(mx));
        if (beat < 0.0) beat = 0.0;
        pluginHost.getEngine().setPosition(beat);
        repaint();
        return;
    }

    // Handle clicks in the track control area
    if (e.x < trackLabelWidth)
    {
        int trackIdx = yToTrack(my);
        if (trackIdx >= 0 && trackIdx < PluginHost::NUM_TRACKS)
        {
            // Check if pressing the track select button — use long press for lock-arm
            auto selRect = getSelectButtonRect(trackIdx);
            if (selRect.toFloat().contains(mx, my))
            {
                longPressTrack = trackIdx;
                longPressPos = { mx, my };
                mouseDownTime = juce::Time::currentTimeMillis();
                longPressTriggered = false;
            }
            else
            {
                handleTrackControlClick(trackIdx, mx, my);
            }
        }
        return;
    }
    auto hit = hitTestClip(mx, my);

    if (e.mods.isRightButtonDown() && hit.isValid())
    {
        // Right-click → open piano roll
        auto* clip = getClip(hit);
        if (clip != nullptr)
        {
            new PianoRollWindow("Piano Roll - Track " + juce::String(hit.trackIndex + 1)
                + " Slot " + juce::String(hit.slotIndex + 1), *clip,
                pluginHost.getEngine());
        }
        return;
    }

    if (hit.isValid())
    {
        selectedClip = hit;
        dragClip = hit;
        auto* clip = getClip(hit);
        if (clip == nullptr) return;

        clipOrigPosition = clip->timelinePosition;
        clipOrigLength = clip->lengthInBeats;
        dragStartBeat = xToBeat(mx);
        dragStartTrack = yToTrack(my);

        auto rect = getClipRect(hit.trackIndex, hit.slotIndex);
        if (isOnClipRightEdge(mx, rect))
            dragMode = ResizeClipRight;
        else if (isOnClipLeftEdge(mx, rect))
            dragMode = ResizeClipLeft;
        else
            dragMode = MoveClip;
    }
    else
    {
        selectedClip = {};
    }

    repaint();
}

void TimelineComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (dragMode == NoDrag || !dragClip.isValid()) return;

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    auto* clip = getClip(dragClip);
    if (clip == nullptr) return;

    double currentBeat = xToBeat(mx);

    if (dragMode == MoveClip)
    {
        double beatDelta = currentBeat - dragStartBeat;
        double newPos = clipOrigPosition + beatDelta;
        // Snap to quarter beat
        newPos = std::floor(newPos * 4.0 + 0.5) / 4.0;
        if (newPos < 0.0) newPos = 0.0;
        clip->timelinePosition = newPos;

        // Check if dragged to a different track
        int newTrack = yToTrack(my);
        if (newTrack >= 0 && newTrack < PluginHost::NUM_TRACKS && newTrack != dragClip.trackIndex)
        {
            // Move clip to different track — find an empty slot
            auto* srcCp = pluginHost.getTrack(dragClip.trackIndex).clipPlayer;
            auto* dstCp = pluginHost.getTrack(newTrack).clipPlayer;

            if (srcCp != nullptr && dstCp != nullptr)
            {
                int emptySlot = -1;
                for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
                {
                    if (!dstCp->getSlot(s).hasContent() && dstCp->getSlot(s).clip == nullptr)
                    {
                        emptySlot = s;
                        break;
                    }
                }

                if (emptySlot >= 0)
                {
                    // Move the clip
                    auto& srcSlot = srcCp->getSlot(dragClip.slotIndex);
                    auto& dstSlot = dstCp->getSlot(emptySlot);

                    dstSlot.clip = std::move(srcSlot.clip);
                    dstSlot.state.store(srcSlot.state.load());
                    srcSlot.state.store(ClipSlot::Empty);

                    dragClip.trackIndex = newTrack;
                    dragClip.slotIndex = emptySlot;
                    selectedClip = dragClip;
                }
            }
        }
    }
    else if (dragMode == ResizeClipRight)
    {
        double newEnd = currentBeat;
        newEnd = std::floor(newEnd * 4.0 + 0.5) / 4.0;
        double newLength = newEnd - clip->timelinePosition;
        if (newLength < 0.25) newLength = 0.25;
        clip->lengthInBeats = newLength;
    }
    else if (dragMode == ResizeClipLeft)
    {
        double newStart = currentBeat;
        newStart = std::floor(newStart * 4.0 + 0.5) / 4.0;
        if (newStart < 0.0) newStart = 0.0;

        double origEnd = clipOrigPosition + clipOrigLength;
        double newLength = origEnd - newStart;
        if (newLength < 0.25) newLength = 0.25;

        // Shift MIDI events to compensate for the start position change
        double shift = clip->timelinePosition - newStart;
        if (std::abs(shift) > 0.001)
        {
            for (int i = 0; i < clip->events.getNumEvents(); ++i)
            {
                auto* event = clip->events.getEventPointer(i);
                event->message.setTimeStamp(event->message.getTimeStamp() + shift);
            }
        }

        clip->timelinePosition = newStart;
        clip->lengthInBeats = newLength;
    }

    repaint();
}

void TimelineComponent::mouseUp(const juce::MouseEvent& /*e*/)
{
    // Handle ARM button release — distinguish tap vs long press
    if (longPressTrack >= 0)
    {
        auto& track = pluginHost.getTrack(longPressTrack);
        juce::int64 holdTime = juce::Time::currentTimeMillis() - mouseDownTime;

        if (holdTime >= longPressMs)
        {
            // Long press = toggle lock-arm
            if (track.clipPlayer != nullptr)
            {
                bool wasLocked = track.clipPlayer->armLocked.load();
                track.clipPlayer->armLocked.store(!wasLocked);
                track.clipPlayer->armed.store(!wasLocked);
            }
        }
        else
        {
            // Short tap = select track (auto-arms)
            pluginHost.setSelectedTrack(longPressTrack);
        }

        longPressTrack = -1;
        repaint();
    }

    dragMode = NoDrag;
    dragClip = {};
}

void TimelineComponent::mouseMove(const juce::MouseEvent& e)
{
    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    auto hit = hitTestClip(mx, my);

    if (hit.isValid())
    {
        auto rect = getClipRect(hit.trackIndex, hit.slotIndex);
        if (isOnClipRightEdge(mx, rect) || isOnClipLeftEdge(mx, rect))
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        else
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }
    else
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void TimelineComponent::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.x < trackLabelWidth) return;

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    auto hit = hitTestClip(mx, my);

    if (!hit.isValid())
    {
        // Double-click empty space → create new empty clip
        int trackIdx = yToTrack(my);
        double beatPos = xToBeat(mx);
        beatPos = std::floor(beatPos); // snap to beat
        if (beatPos < 0.0) beatPos = 0.0;

        if (trackIdx >= 0 && trackIdx < PluginHost::NUM_TRACKS)
            createEmptyClip(trackIdx, beatPos);
    }
    else
    {
        // Double-click clip → open piano roll
        auto* clip = getClip(hit);
        if (clip != nullptr)
        {
            new PianoRollWindow("Piano Roll - Track " + juce::String(hit.trackIndex + 1),
                *clip, pluginHost.getEngine());
        }
    }
}

void TimelineComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown())
    {
        double zoomFactor = 1.0 + w.deltaY * 0.3;
        pixelsPerBeat = juce::jlimit(10.0, 200.0, pixelsPerBeat * zoomFactor);
    }
    else
    {
        scrollX -= w.deltaY * 4.0;
        if (scrollX < 0.0) scrollX = 0.0;
    }
    repaint();
}

bool TimelineComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
    {
        deleteSelectedClip();
        return true;
    }

    if (key.getModifiers().isCtrlDown() && key.getKeyCode() == 'D')
    {
        duplicateSelectedClip();
        return true;
    }

    if (key.getModifiers().isCtrlDown() && key.getKeyCode() == 'B')
    {
        // Split at playhead
        if (selectedClip.isValid())
        {
            double playheadBeat = pluginHost.getEngine().getPositionInBeats();
            auto* clip = getClip(selectedClip);
            if (clip != nullptr)
            {
                double clipStart = clip->timelinePosition;
                double clipEnd = clipStart + clip->lengthInBeats;
                if (playheadBeat > clipStart && playheadBeat < clipEnd)
                    splitClipAtBeat(selectedClip, playheadBeat);
            }
        }
        return true;
    }

    return false;
}

// ── Editing operations ───────────────────────────────────────────────────────

void TimelineComponent::deleteSelectedClip()
{
    if (!selectedClip.isValid()) return;

    auto* slot = getSlot(selectedClip);
    if (slot == nullptr) return;

    slot->clip = nullptr;
    slot->state.store(ClipSlot::Empty);
    selectedClip = {};
    repaint();
}

void TimelineComponent::duplicateSelectedClip()
{
    if (!selectedClip.isValid()) return;

    auto* srcClip = getClip(selectedClip);
    if (srcClip == nullptr) return;

    auto* cp = pluginHost.getTrack(selectedClip.trackIndex).clipPlayer;
    if (cp == nullptr) return;

    // Find empty slot on same track
    int emptySlot = -1;
    for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
    {
        if (!cp->getSlot(s).hasContent() && cp->getSlot(s).clip == nullptr)
        {
            emptySlot = s;
            break;
        }
    }

    if (emptySlot < 0) return; // no empty slots

    auto newClip = std::make_unique<MidiClip>();
    newClip->lengthInBeats = srcClip->lengthInBeats;
    newClip->timelinePosition = srcClip->timelinePosition + srcClip->lengthInBeats; // place after original

    // Copy MIDI events
    for (int i = 0; i < srcClip->events.getNumEvents(); ++i)
    {
        auto* event = srcClip->events.getEventPointer(i);
        newClip->events.addEvent(event->message);
    }
    newClip->events.updateMatchedPairs();

    cp->getSlot(emptySlot).clip = std::move(newClip);
    cp->getSlot(emptySlot).state.store(ClipSlot::Stopped);

    selectedClip = { selectedClip.trackIndex, emptySlot };
    repaint();
}

void TimelineComponent::splitClipAtBeat(const ClipRef& ref, double beat)
{
    auto* srcClip = getClip(ref);
    if (srcClip == nullptr) return;

    double clipStart = srcClip->timelinePosition;
    double splitPoint = beat - clipStart; // relative to clip start

    if (splitPoint <= 0.0 || splitPoint >= srcClip->lengthInBeats) return;

    auto* cp = pluginHost.getTrack(ref.trackIndex).clipPlayer;
    if (cp == nullptr) return;

    // Find empty slot for the second half
    int emptySlot = -1;
    for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
    {
        if (!cp->getSlot(s).hasContent() && cp->getSlot(s).clip == nullptr)
        {
            emptySlot = s;
            break;
        }
    }
    if (emptySlot < 0) return;

    // Create second half clip
    auto newClip = std::make_unique<MidiClip>();
    newClip->timelinePosition = beat;
    newClip->lengthInBeats = srcClip->lengthInBeats - splitPoint;

    // Split MIDI events
    juce::MidiMessageSequence firstHalf, secondHalf;

    for (int i = 0; i < srcClip->events.getNumEvents(); ++i)
    {
        auto* event = srcClip->events.getEventPointer(i);
        double t = event->message.getTimeStamp();

        if (t < splitPoint)
        {
            firstHalf.addEvent(event->message);
        }
        else
        {
            auto msg = event->message;
            msg.setTimeStamp(t - splitPoint);
            secondHalf.addEvent(msg);
        }
    }

    firstHalf.updateMatchedPairs();
    secondHalf.updateMatchedPairs();

    // Update original clip (first half)
    srcClip->events = firstHalf;
    srcClip->lengthInBeats = splitPoint;

    // Set up second half
    newClip->events = secondHalf;
    cp->getSlot(emptySlot).clip = std::move(newClip);
    cp->getSlot(emptySlot).state.store(ClipSlot::Stopped);

    repaint();
}

double TimelineComponent::snapToGrid(double beat) const
{
    return std::round(beat / gridResolution) * gridResolution;
}

void TimelineComponent::quantizeSelectedClip()
{
    auto* clip = getClip(selectedClip);
    if (clip == nullptr) return;

    juce::MidiMessageSequence quantized;

    for (int i = 0; i < clip->events.getNumEvents(); ++i)
    {
        auto* event = clip->events.getEventPointer(i);
        auto msg = event->message;

        // Snap timestamp to grid
        double t = msg.getTimeStamp();
        t = std::round(t / gridResolution) * gridResolution;
        if (t < 0.0) t = 0.0;
        msg.setTimeStamp(t);

        quantized.addEvent(msg);
    }

    quantized.updateMatchedPairs();
    clip->events = quantized;
    repaint();
}

void TimelineComponent::createClipAtPlayhead()
{
    int trackIdx = pluginHost.getSelectedTrack();
    double beatPos = pluginHost.getEngine().getPositionInBeats();
    beatPos = std::floor(beatPos); // snap to beat
    if (beatPos < 0.0) beatPos = 0.0;
    createEmptyClip(trackIdx, beatPos);
}

void TimelineComponent::deleteSelected()
{
    deleteSelectedClip();
}

void TimelineComponent::duplicateSelected()
{
    duplicateSelectedClip();
}

void TimelineComponent::splitSelected()
{
    if (!selectedClip.isValid()) return;
    auto* clip = getClip(selectedClip);
    if (clip == nullptr) return;

    double playheadBeat = pluginHost.getEngine().getPositionInBeats();
    double clipStart = clip->timelinePosition;
    double clipEnd = clipStart + clip->lengthInBeats;

    if (playheadBeat > clipStart && playheadBeat < clipEnd)
        splitClipAtBeat(selectedClip, playheadBeat);
}

MidiClip* TimelineComponent::getSelectedClip()
{
    return getClip(selectedClip);
}

void TimelineComponent::createEmptyClip(int trackIndex, double beatPos)
{
    auto* cp = pluginHost.getTrack(trackIndex).clipPlayer;
    if (cp == nullptr) return;

    int emptySlot = -1;
    for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
    {
        if (!cp->getSlot(s).hasContent() && cp->getSlot(s).clip == nullptr)
        {
            emptySlot = s;
            break;
        }
    }
    if (emptySlot < 0) return;

    auto newClip = std::make_unique<MidiClip>();
    newClip->timelinePosition = beatPos;
    newClip->lengthInBeats = 4.0; // 1 bar

    cp->getSlot(emptySlot).clip = std::move(newClip);
    cp->getSlot(emptySlot).state.store(ClipSlot::Stopped);

    selectedClip = { trackIndex, emptySlot };
    repaint();
}

// ── Drawing ──────────────────────────────────────────────────────────────────

void TimelineComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    drawHeader(g);
    drawTrackLanes(g);
    drawClips(g);
    drawPlayhead(g);
}

void TimelineComponent::resized() {}

void TimelineComponent::drawHeader(juce::Graphics& g)
{
    g.setColour(juce::Colour(0xff2a2a2a));
    g.fillRect(0, 0, getWidth(), headerHeight);

    double firstBeat = std::floor(scrollX / gridResolution) * gridResolution;
    double lastBeat = scrollX + (getWidth() - trackLabelWidth) / pixelsPerBeat;

    // Draw grid lines at the selected resolution
    for (double beat = firstBeat; beat <= lastBeat; beat += gridResolution)
    {
        float x = beatToX(beat);
        if (x < trackLabelWidth) continue;

        bool isBar = std::abs(std::fmod(beat, 4.0)) < 0.001;
        bool isBeat = std::abs(std::fmod(beat, 1.0)) < 0.001;

        if (isBar)
        {
            int barNum = static_cast<int>(beat / 4.0) + 1;
            g.setColour(juce::Colour(0xffcccccc));
            g.setFont(11.0f);
            g.drawText(juce::String(barNum), static_cast<int>(x) + 2, 0, 40, headerHeight,
                       juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff666666));
        }
        else if (isBeat)
        {
            g.setColour(juce::Colour(0xff444444));
        }
        else
        {
            g.setColour(juce::Colour(0xff2d2d2d));
        }

        g.drawVerticalLine(static_cast<int>(x), static_cast<float>(headerHeight),
                           static_cast<float>(getHeight()));
    }

    g.setColour(juce::Colour(0xff444444));
    g.drawHorizontalLine(headerHeight - 1, 0, static_cast<float>(getWidth()));
}

void TimelineComponent::drawTrackLanes(juce::Graphics& g)
{
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        int y = headerHeight + t * trackHeight;

        // Selected track highlight
        bool isSelected = (t == pluginHost.getSelectedTrack());
        if (isSelected)
            g.setColour(juce::Colour(0xff2a3a4a));
        else
            g.setColour(t % 2 == 0 ? juce::Colour(0xff1e1e1e) : juce::Colour(0xff222222));
        g.fillRect(0, y, getWidth(), trackHeight);

        g.setColour(juce::Colour(0xff333333));
        g.drawHorizontalLine(y + trackHeight - 1, 0, static_cast<float>(getWidth()));
    }

    drawTrackControls(g);
}

juce::Rectangle<int> TimelineComponent::getSelectButtonRect(int trackIndex) const
{
    int y = headerHeight + trackIndex * trackHeight;
    return { 2, y + 2, trackLabelWidth - 40, trackHeight - 4 };
}

juce::Rectangle<int> TimelineComponent::getMuteButtonRect(int trackIndex) const
{
    int y = headerHeight + trackIndex * trackHeight;
    return { trackLabelWidth - 36, y + 3, 34, (trackHeight - 8) / 2 };
}

juce::Rectangle<int> TimelineComponent::getSoloButtonRect(int trackIndex) const
{
    int y = headerHeight + trackIndex * trackHeight;
    int halfH = (trackHeight - 8) / 2;
    return { trackLabelWidth - 36, y + 3 + halfH + 2, 34, halfH };
}

void TimelineComponent::drawTrackControls(juce::Graphics& g)
{
    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto& track = pluginHost.getTrack(t);
        bool isSelected = (t == pluginHost.getSelectedTrack());
        bool isArmed = track.clipPlayer != nullptr && track.clipPlayer->armed.load();
        bool isLocked = track.clipPlayer != nullptr && track.clipPlayer->armLocked.load();

        // Track select button — color shows selection + arm state
        auto selRect = getSelectButtonRect(t);

        if (isLocked)
            g.setColour(juce::Colour(0xff882222));      // locked arm = deep red
        else if (isSelected)
            g.setColour(juce::Colour(0xff3a5a8a));      // selected = blue
        else
            g.setColour(juce::Colour(0xff333333));       // normal = gray

        g.fillRoundedRectangle(selRect.toFloat(), 3.0f);

        // Arm indicator dot on the left side
        if (isArmed || isLocked)
        {
            g.setColour(isLocked ? juce::Colours::red : juce::Colours::red.darker());
            g.fillEllipse(static_cast<float>(selRect.getX() + 4),
                         static_cast<float>(selRect.getCentreY() - 4), 8.0f, 8.0f);
        }

        // Track label
        g.setColour(juce::Colours::white);
        g.setFont(13.0f);
        juce::String label = juce::String(t + 1);
        if (track.plugin != nullptr)
            label += " " + track.plugin->getName().substring(0, 7);
        g.drawText(label, selRect.reduced(14, 0), juce::Justification::centredLeft);

        // Mute button
        auto muteRect = getMuteButtonRect(t);
        bool isMuted = track.gainProcessor != nullptr && track.gainProcessor->muted.load();
        g.setColour(isMuted ? juce::Colours::red : juce::Colour(0xff444444));
        g.fillRoundedRectangle(muteRect.toFloat(), 3.0f);
        g.setColour(juce::Colours::white);
        g.setFont(13.0f);
        g.drawText("M", muteRect, juce::Justification::centred);

        // Solo button
        auto soloRect = getSoloButtonRect(t);
        bool isSoloed = track.gainProcessor != nullptr && track.gainProcessor->soloed.load();
        g.setColour(isSoloed ? juce::Colours::yellow : juce::Colour(0xff444444));
        g.fillRoundedRectangle(soloRect.toFloat(), 3.0f);
        g.setColour(isSoloed ? juce::Colours::black : juce::Colours::white);
        g.setFont(13.0f);
        g.drawText("S", soloRect, juce::Justification::centred);

        // Divider
        g.setColour(juce::Colour(0xff444444));
        g.drawVerticalLine(trackLabelWidth - 1, static_cast<float>(headerHeight),
                           static_cast<float>(getHeight()));
    }
}

void TimelineComponent::handleTrackControlClick(int trackIndex, float x, float y)
{
    auto& track = pluginHost.getTrack(trackIndex);

    // Check Mute button
    auto muteRect = getMuteButtonRect(trackIndex);
    if (muteRect.toFloat().contains(x, y))
    {
        if (track.gainProcessor != nullptr)
            track.gainProcessor->muted.store(!track.gainProcessor->muted.load());
        repaint();
        return;
    }

    // Check Solo button
    auto soloRect = getSoloButtonRect(trackIndex);
    if (soloRect.toFloat().contains(x, y))
    {
        if (track.gainProcessor != nullptr)
        {
            bool was = track.gainProcessor->soloed.load();
            bool now = !was;
            track.gainProcessor->soloed.store(now);
            if (now && !was) pluginHost.soloCount.fetch_add(1);
            else if (!now && was) pluginHost.soloCount.fetch_sub(1);
        }
        repaint();
        return;
    }

    // Click on track name = select track
    auto selRect = getSelectButtonRect(trackIndex);
    if (selRect.toFloat().contains(x, y))
    {
        pluginHost.setSelectedTrack(trackIndex);
        repaint();
        return;
    }
}

void TimelineComponent::drawClips(juce::Graphics& g)
{
    // Clip drawing to the timeline area only — don't draw over track controls
    g.saveState();
    g.reduceClipRegion(trackLabelWidth, headerHeight,
                       getWidth() - trackLabelWidth, getHeight() - headerHeight);

    for (int t = 0; t < PluginHost::NUM_TRACKS; ++t)
    {
        auto* cp = pluginHost.getTrack(t).clipPlayer;
        if (cp == nullptr) continue;

        for (int s = 0; s < ClipPlayerNode::NUM_SLOTS; ++s)
        {
            auto& slot = cp->getSlot(s);
            if (slot.clip == nullptr) continue;

            auto clipRect = getClipRect(t, s);
            if (clipRect.isEmpty()) continue;
            if (clipRect.getRight() < trackLabelWidth || clipRect.getX() > getWidth()) continue;

            // Color based on state
            auto state = slot.state.load();
            juce::Colour clipColor;
            if (state == ClipSlot::Playing)
                clipColor = juce::Colour(0xff338844);
            else if (state == ClipSlot::Recording)
                clipColor = juce::Colour(0xff883333);
            else if (state == ClipSlot::Armed)
                clipColor = juce::Colour(0xff884400);
            else
                clipColor = juce::Colour(0xff445566);

            // Selected highlight
            bool isSelected = selectedClip.trackIndex == t && selectedClip.slotIndex == s;
            if (isSelected)
                clipColor = clipColor.brighter(0.3f);

            g.setColour(clipColor);
            g.fillRoundedRectangle(clipRect, 3.0f);

            g.setColour(isSelected ? juce::Colours::white : clipColor.brighter(0.3f));
            g.drawRoundedRectangle(clipRect, 3.0f, isSelected ? 2.0f : 1.0f);

            // Resize handles
            if (isSelected)
            {
                g.setColour(juce::Colours::white.withAlpha(0.4f));
                g.fillRect(clipRect.getX(), clipRect.getY() + 4, 3.0f, clipRect.getHeight() - 8);
                g.fillRect(clipRect.getRight() - 3, clipRect.getY() + 4, 3.0f, clipRect.getHeight() - 8);
            }

            // Mini note preview
            if (slot.hasContent())
            {
                g.saveState();
                g.reduceClipRegion(clipRect.toNearestInt());
                drawMiniNotes(g, *slot.clip, clipRect);
                g.restoreState();
            }
        }
    }

    g.restoreState();
}

void TimelineComponent::drawMiniNotes(juce::Graphics& g, const MidiClip& clip, juce::Rectangle<float> area)
{
    if (clip.events.getNumEvents() == 0 || clip.lengthInBeats <= 0.0) return;

    int minNote = 127, maxNote = 0;
    for (int i = 0; i < clip.events.getNumEvents(); ++i)
    {
        auto& msg = clip.events.getEventPointer(i)->message;
        if (msg.isNoteOn())
        {
            minNote = juce::jmin(minNote, msg.getNoteNumber());
            maxNote = juce::jmax(maxNote, msg.getNoteNumber());
        }
    }

    if (minNote > maxNote) return;
    int noteRange = juce::jmax(1, maxNote - minNote + 1);

    float noteH = juce::jmax(1.0f, (area.getHeight() - 6.0f) / static_cast<float>(noteRange));
    float beatsToPixels = area.getWidth() / static_cast<float>(clip.lengthInBeats);

    g.setColour(juce::Colours::white.withAlpha(0.5f));

    for (int i = 0; i < clip.events.getNumEvents(); ++i)
    {
        auto* event = clip.events.getEventPointer(i);
        if (!event->message.isNoteOn()) continue;

        float nx = area.getX() + static_cast<float>(event->message.getTimeStamp()) * beatsToPixels;
        float noteLen = 0.25f;
        if (event->noteOffObject != nullptr)
        {
            noteLen = static_cast<float>(event->noteOffObject->message.getTimeStamp()
                                         - event->message.getTimeStamp());
            if (noteLen < 0.05f) noteLen = 0.25f;
        }
        float nw = noteLen * beatsToPixels;

        int noteRow = maxNote - event->message.getNoteNumber();
        float ny = area.getY() + 3.0f + noteRow * noteH;

        g.fillRect(nx, ny, juce::jmax(1.0f, nw), juce::jmax(1.0f, noteH - 1.0f));
    }
}

void TimelineComponent::drawPlayhead(juce::Graphics& g)
{
    auto& engine = pluginHost.getEngine();
    double pos = engine.getPositionInBeats();
    float x = beatToX(pos);

    if (x < static_cast<float>(trackLabelWidth) || x > static_cast<float>(getWidth())) return;

    g.setColour(juce::Colour(0xddffcc00));
    g.drawVerticalLine(static_cast<int>(x), static_cast<float>(headerHeight),
                       static_cast<float>(getHeight()));

    g.setColour(juce::Colour(0x33ffcc00));
    g.fillRect(x - 1.0f, static_cast<float>(headerHeight), 3.0f,
               static_cast<float>(getHeight() - headerHeight));

    g.setColour(juce::Colour(0xffee9900));
    juce::Path triangle;
    triangle.addTriangle(x - 5, static_cast<float>(headerHeight),
                         x + 5, static_cast<float>(headerHeight),
                         x, static_cast<float>(headerHeight + 8));
    g.fillPath(triangle);
}
