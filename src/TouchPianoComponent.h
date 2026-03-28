#pragma once

#include <JuceHeader.h>
#include <set>

// On-screen touch/click piano keyboard.
// Sends note on/off via a callback. Supports multi-touch,
// dragging across keys, and octave shifting.
class TouchPianoComponent : public juce::Component
{
public:
    // Callback: (noteNumber, isNoteOn)
    std::function<void(int, bool)> onNote;

    TouchPianoComponent()
    {
        setWantsKeyboardFocus(false);
        setInterceptsMouseClicks(true, false);
    }

    ~TouchPianoComponent() override
    {
        // Release any held notes
        for (int n : activeNotes)
            if (onNote) onNote(n, false);
    }

    void setOctave(int oct) { baseOctave = juce::jlimit(0, 7, oct); repaint(); }
    int getOctave() const { return baseOctave; }
    void octaveUp()   { setOctave(baseOctave + 1); }
    void octaveDown() { setOctave(baseOctave - 1); }

    void setNumOctaves(int n) { numOctaves = juce::jlimit(1, 4, n); repaint(); }
    int getNumOctaves() const { return numOctaves; }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        int totalWhite = numOctaves * 7;
        float keyW = bounds.getWidth() / static_cast<float>(totalWhite);
        float keyH = bounds.getHeight();
        float blackH = keyH * 0.6f;
        float blackW = keyW * 0.65f;

        // Draw white keys
        for (int i = 0; i < totalWhite; ++i)
        {
            float x = bounds.getX() + i * keyW;
            int note = whiteKeyToNote(i);
            bool pressed = activeNotes.count(note) > 0;

            g.setColour(pressed ? juce::Colour(0xFFB0C4FF) : juce::Colours::white);
            g.fillRect(x + 0.5f, 0.0f, keyW - 1.0f, keyH);
            g.setColour(juce::Colour(0xFF333333));
            g.drawRect(x, 0.0f, keyW, keyH, 0.5f);

            // Label C notes
            if (note % 12 == 0)
            {
                g.setColour(juce::Colour(0xFF666666));
                g.setFont(10.0f);
                g.drawText("C" + juce::String(note / 12 - 1),
                           static_cast<int>(x), static_cast<int>(keyH - 16),
                           static_cast<int>(keyW), 14, juce::Justification::centred);
            }
        }

        // Draw black keys on top
        for (int i = 0; i < totalWhite - 1; ++i)
        {
            int noteInOctave = whiteKeyToNote(i) % 12;
            // Black key exists after C, D, F, G, A (positions 0,2,5,7,9 in chromatic)
            if (noteInOctave == 0 || noteInOctave == 2 || noteInOctave == 5 ||
                noteInOctave == 7 || noteInOctave == 9)
            {
                float x = bounds.getX() + (i + 1) * keyW - blackW * 0.5f;
                int note = whiteKeyToNote(i) + 1; // the sharp
                bool pressed = activeNotes.count(note) > 0;

                g.setColour(pressed ? juce::Colour(0xFF4466AA) : juce::Colour(0xFF1a1a1a));
                g.fillRect(x, 0.0f, blackW, blackH);
                g.setColour(juce::Colour(0xFF000000));
                g.drawRect(x, 0.0f, blackW, blackH, 0.5f);
            }
        }
    }

    void mouseDown(const juce::MouseEvent& e) override { handleTouch(e.position, true); }
    void mouseDrag(const juce::MouseEvent& e) override { handleTouch(e.position, true); }
    void mouseUp(const juce::MouseEvent&) override { releaseAll(); }

private:
    int baseOctave = 3;
    int numOctaves = 2;
    std::set<int> activeNotes;
    int lastDragNote = -1;

    // Map white key index to MIDI note
    int whiteKeyToNote(int whiteIndex) const
    {
        // White keys within an octave: C D E F G A B = offsets 0,2,4,5,7,9,11
        static const int offsets[] = { 0, 2, 4, 5, 7, 9, 11 };
        int octave = whiteIndex / 7;
        int keyInOctave = whiteIndex % 7;
        return (baseOctave + octave + 1) * 12 + offsets[keyInOctave]; // +1 for MIDI C convention
    }

    int hitTest(juce::Point<float> pos) const
    {
        auto bounds = getLocalBounds().toFloat();
        int totalWhite = numOctaves * 7;
        float keyW = bounds.getWidth() / static_cast<float>(totalWhite);
        float blackH = bounds.getHeight() * 0.6f;
        float blackW = keyW * 0.65f;

        // Check black keys first (they're on top)
        if (pos.y < blackH)
        {
            for (int i = 0; i < totalWhite - 1; ++i)
            {
                int noteInOctave = whiteKeyToNote(i) % 12;
                if (noteInOctave == 0 || noteInOctave == 2 || noteInOctave == 5 ||
                    noteInOctave == 7 || noteInOctave == 9)
                {
                    float x = (i + 1) * keyW - blackW * 0.5f;
                    if (pos.x >= x && pos.x < x + blackW)
                        return whiteKeyToNote(i) + 1;
                }
            }
        }

        // White key
        int whiteIndex = static_cast<int>(pos.x / keyW);
        whiteIndex = juce::jlimit(0, totalWhite - 1, whiteIndex);
        return whiteKeyToNote(whiteIndex);
    }

    void handleTouch(juce::Point<float> pos, bool down)
    {
        int note = hitTest(pos);
        if (note < 0 || note > 127) return;

        if (down && note != lastDragNote)
        {
            // Release old drag note
            if (lastDragNote >= 0 && activeNotes.count(lastDragNote))
            {
                activeNotes.erase(lastDragNote);
                if (onNote) onNote(lastDragNote, false);
            }

            if (!activeNotes.count(note))
            {
                activeNotes.insert(note);
                if (onNote) onNote(note, true);
            }
            lastDragNote = note;
            repaint();
        }
    }

    void releaseAll()
    {
        for (int n : activeNotes)
            if (onNote) onNote(n, false);
        activeNotes.clear();
        lastDragNote = -1;
        repaint();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TouchPianoComponent)
};
