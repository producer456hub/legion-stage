#include "PianoRollComponent.h"

PianoRollComponent::PianoRollComponent(MidiClip& c) : clip(c)
{
    rebuildNoteList();
    scrollY = 48; // start around C3-C5 range
}

// ── Note list management ─────────────────────────────────────────────────────

void PianoRollComponent::rebuildNoteList()
{
    noteEvents.clear();

    for (int i = 0; i < clip.events.getNumEvents(); ++i)
    {
        auto* event = clip.events.getEventPointer(i);
        if (event->message.isNoteOn())
        {
            NoteEvent n;
            n.noteNumber = event->message.getNoteNumber();
            n.startBeat = event->message.getTimeStamp();

            // Find matching note-off
            n.lengthBeats = 0.25; // default
            if (event->noteOffObject != nullptr)
            {
                n.lengthBeats = event->noteOffObject->message.getTimeStamp() - n.startBeat;
                if (n.lengthBeats < 0.05) n.lengthBeats = 0.25;
            }

            noteEvents.add(n);
        }
    }
}

void PianoRollComponent::applyNoteListToClip()
{
    clip.events.clear();

    for (auto& n : noteEvents)
    {
        auto noteOn = juce::MidiMessage::noteOn(1, n.noteNumber, (juce::uint8) 100);
        noteOn.setTimeStamp(n.startBeat);

        auto noteOff = juce::MidiMessage::noteOff(1, n.noteNumber);
        noteOff.setTimeStamp(n.startBeat + n.lengthBeats);

        clip.events.addEvent(noteOn);
        clip.events.addEvent(noteOff);
    }

    clip.events.sort();
    clip.events.updateMatchedPairs();
}

// ── Coordinate conversion ────────────────────────────────────────────────────

double PianoRollComponent::xToBeat(float x) const
{
    return scrollX + (x - pianoKeyWidth) / pixelsPerBeat;
}

float PianoRollComponent::beatToX(double beat) const
{
    return pianoKeyWidth + static_cast<float>((beat - scrollX) * pixelsPerBeat);
}

int PianoRollComponent::yToNote(float y) const
{
    int visNote = static_cast<int>(y / noteHeight);
    return (scrollY + visibleNotes() - 1) - visNote;
}

float PianoRollComponent::noteToY(int note) const
{
    int row = (scrollY + visibleNotes() - 1) - note;
    return static_cast<float>(row * noteHeight);
}

juce::Rectangle<float> PianoRollComponent::getNoteRect(const NoteEvent& n) const
{
    float x = beatToX(n.startBeat);
    float y = noteToY(n.noteNumber);
    float w = static_cast<float>(n.lengthBeats * pixelsPerBeat);
    return { x, y, juce::jmax(4.0f, w), static_cast<float>(noteHeight - 1) };
}

// ── Hit testing ──────────────────────────────────────────────────────────────

int PianoRollComponent::hitTestNote(float x, float y) const
{
    for (int i = noteEvents.size() - 1; i >= 0; --i)
    {
        if (getNoteRect(noteEvents[i]).contains(x, y))
            return i;
    }
    return -1;
}

bool PianoRollComponent::isOnNoteRightEdge(float x, const NoteEvent& n) const
{
    auto rect = getNoteRect(n);
    return x > rect.getRight() - 6.0f;
}

// ── Mouse handling ───────────────────────────────────────────────────────────

void PianoRollComponent::mouseDown(const juce::MouseEvent& e)
{
    if (e.x < pianoKeyWidth) return;

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);

    int hitIndex = hitTestNote(mx, my);

    if (e.mods.isRightButtonDown())
    {
        // Right click = delete note
        if (hitIndex >= 0)
        {
            noteEvents.remove(hitIndex);
            selectedNoteIndex = -1;
            applyNoteListToClip();
            repaint();
        }
        return;
    }

    if (hitIndex >= 0)
    {
        selectedNoteIndex = hitIndex;
        auto& n = noteEvents.getReference(hitIndex);
        noteOrigStart = n.startBeat;
        noteOrigNote = n.noteNumber;
        noteOrigLength = n.lengthBeats;
        dragStartBeat = xToBeat(mx);
        dragStartNote = yToNote(my);

        if (isOnNoteRightEdge(mx, n))
            dragMode = ResizeNote;
        else
            dragMode = MoveNote;
    }
    else
    {
        // Click empty space = create new note
        double beat = xToBeat(mx);
        int note = yToNote(my);

        // Snap to quarter beat
        beat = std::floor(beat * 4.0) / 4.0;
        if (beat < 0.0) beat = 0.0;

        NoteEvent newNote;
        newNote.noteNumber = juce::jlimit(0, 127, note);
        newNote.startBeat = beat;
        newNote.lengthBeats = 0.25;

        noteEvents.add(newNote);
        selectedNoteIndex = noteEvents.size() - 1;
        dragMode = ResizeNote;
        noteOrigLength = 0.25;
        dragStartBeat = beat;

        applyNoteListToClip();
    }

    repaint();
}

void PianoRollComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (selectedNoteIndex < 0 || selectedNoteIndex >= noteEvents.size()) return;

    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);
    auto& n = noteEvents.getReference(selectedNoteIndex);

    if (dragMode == MoveNote)
    {
        double beatDelta = xToBeat(mx) - dragStartBeat;
        int noteDelta = yToNote(my) - dragStartNote;

        double newStart = noteOrigStart + beatDelta;
        // Snap to 1/4 beat
        newStart = std::floor(newStart * 4.0 + 0.5) / 4.0;
        if (newStart < 0.0) newStart = 0.0;

        n.startBeat = newStart;
        n.noteNumber = juce::jlimit(0, 127, noteOrigNote + noteDelta);
    }
    else if (dragMode == ResizeNote)
    {
        double endBeat = xToBeat(mx);
        // Snap to 1/4 beat
        endBeat = std::floor(endBeat * 4.0 + 0.5) / 4.0;
        double newLength = endBeat - n.startBeat;
        if (newLength < 0.0625) newLength = 0.0625; // minimum 1/16 beat
        n.lengthBeats = newLength;
    }

    repaint();
}

void PianoRollComponent::mouseUp(const juce::MouseEvent& /*e*/)
{
    if (dragMode != None)
    {
        applyNoteListToClip();
        dragMode = None;
    }
}

void PianoRollComponent::mouseMove(const juce::MouseEvent& e)
{
    float mx = static_cast<float>(e.x);
    float my = static_cast<float>(e.y);

    int hit = hitTestNote(mx, my);
    if (hit >= 0 && isOnNoteRightEdge(mx, noteEvents[hit]))
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    else if (hit >= 0)
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    else
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void PianoRollComponent::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    if (e.mods.isCtrlDown())
    {
        // Zoom horizontal
        double zoomFactor = 1.0 + w.deltaY * 0.3;
        pixelsPerBeat = juce::jlimit(20.0, 400.0, pixelsPerBeat * zoomFactor);
    }
    else if (e.mods.isShiftDown())
    {
        // Scroll horizontal
        scrollX -= w.deltaY * 2.0;
        if (scrollX < 0.0) scrollX = 0.0;
    }
    else
    {
        // Scroll vertical (notes)
        scrollY -= static_cast<int>(w.deltaY * 5.0f);
        scrollY = juce::jlimit(MIN_NOTE, MAX_NOTE - 10, scrollY);
    }

    repaint();
}

// ── Drawing ──────────────────────────────────────────────────────────────────

void PianoRollComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff1a1a1a));
    drawPianoKeys(g);
    drawGrid(g);
    drawNotes(g);
}

void PianoRollComponent::resized() {}

bool PianoRollComponent::isBlackKey(int note)
{
    int n = note % 12;
    return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
}

juce::String PianoRollComponent::noteName(int note)
{
    static const char* names[] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}

void PianoRollComponent::drawPianoKeys(juce::Graphics& g)
{
    int topNote = scrollY + visibleNotes() - 1;

    for (int note = scrollY; note <= topNote && note <= MAX_NOTE; ++note)
    {
        float y = noteToY(note);
        bool black = isBlackKey(note);

        g.setColour(black ? juce::Colour(0xff333333) : juce::Colour(0xffcccccc));
        g.fillRect(0.0f, y, static_cast<float>(pianoKeyWidth), static_cast<float>(noteHeight - 1));

        g.setColour(juce::Colour(0xff555555));
        g.drawLine(0, y + noteHeight - 1, static_cast<float>(pianoKeyWidth), y + noteHeight - 1);

        // Label C notes
        if (note % 12 == 0)
        {
            g.setColour(black ? juce::Colours::white : juce::Colours::black);
            g.setFont(10.0f);
            g.drawText(noteName(note), 2, static_cast<int>(y), pianoKeyWidth - 4, noteHeight, juce::Justification::centredLeft);
        }
    }
}

void PianoRollComponent::drawGrid(juce::Graphics& g)
{
    int w = getWidth();
    int h = getHeight();
    int topNote = scrollY + visibleNotes() - 1;

    // Horizontal lines (note lanes)
    for (int note = scrollY; note <= topNote && note <= MAX_NOTE; ++note)
    {
        float y = noteToY(note);
        bool black = isBlackKey(note);

        // Alternating lane colors
        g.setColour(black ? juce::Colour(0xff1e1e1e) : juce::Colour(0xff242424));
        g.fillRect(static_cast<float>(pianoKeyWidth), y, static_cast<float>(w - pianoKeyWidth), static_cast<float>(noteHeight));

        g.setColour(juce::Colour(0xff2a2a2a));
        g.drawHorizontalLine(static_cast<int>(y + noteHeight - 1), static_cast<float>(pianoKeyWidth), static_cast<float>(w));
    }

    // Vertical lines (beats)
    double firstBeat = std::floor(scrollX);
    double lastBeat = scrollX + (w - pianoKeyWidth) / pixelsPerBeat;

    for (double beat = firstBeat; beat <= lastBeat; beat += 0.25)
    {
        float x = beatToX(beat);
        if (x < pianoKeyWidth) continue;

        double wholeBeat = std::fmod(beat, 1.0);
        bool isBeat = std::abs(wholeBeat) < 0.001 || std::abs(wholeBeat - 1.0) < 0.001;
        bool isBar = std::abs(std::fmod(beat, 4.0)) < 0.001;

        if (isBar)
            g.setColour(juce::Colour(0xff555555));
        else if (isBeat)
            g.setColour(juce::Colour(0xff3a3a3a));
        else
            g.setColour(juce::Colour(0xff2d2d2d));

        g.drawVerticalLine(static_cast<int>(x), 0.0f, static_cast<float>(h));

        // Beat numbers at top
        if (isBeat)
        {
            g.setColour(juce::Colour(0xff888888));
            g.setFont(10.0f);
            g.drawText(juce::String(static_cast<int>(beat) + 1), static_cast<int>(x) + 2, 0, 30, 12, juce::Justification::left);
        }
    }
}

void PianoRollComponent::drawNotes(juce::Graphics& g)
{
    for (int i = 0; i < noteEvents.size(); ++i)
    {
        auto rect = getNoteRect(noteEvents[i]);

        // Skip if out of view
        if (rect.getRight() < pianoKeyWidth || rect.getX() > getWidth()) continue;
        if (rect.getBottom() < 0 || rect.getY() > getHeight()) continue;

        // Note color
        if (i == selectedNoteIndex)
            g.setColour(juce::Colour(0xffff9944)); // orange for selected
        else
            g.setColour(juce::Colour(0xff5588cc)); // blue

        g.fillRoundedRectangle(rect, 2.0f);

        // Border
        g.setColour(juce::Colour(0xff88aadd));
        g.drawRoundedRectangle(rect, 2.0f, 1.0f);

        // Resize handle hint (right edge)
        g.setColour(juce::Colour(0x44ffffff));
        g.fillRect(rect.getRight() - 4, rect.getY() + 2, 2.0f, rect.getHeight() - 4);
    }
}
