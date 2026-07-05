#pragma once

#include <JuceHeader.h>
#include <array>
#include "MoveMode.h"

// ─────────────────────────────────────────────────────────────────────────────
// StubNoteMode — the minimal end-to-end proof of the control-surface pipeline
// (plan Task 4). It does NOT implement real step sequencing (that is the next
// spec). It proves: input adapter -> MoveControlBus -> mode -> engine -> sound.
//
//   Pad Down     -> play a note on the loaded instrument (+ mark pad lit)
//   Pad Up       -> stop that note
//   Play  Down   -> transport play
//   Record Down  -> transport record toggle
//
// Kept decoupled from PluginHost/MainComponent via std::function hooks so it can
// be built and unit-exercised before the surface component exists. Wire the hooks
// in MainComponent to pluginHost.getMidiCollector()/getEngine().
//
// Spec: docs/superpowers/specs/2026-07-05-control-surface-design.md
// ─────────────────────────────────────────────────────────────────────────────

class StubNoteMode : public MoveMode
{
public:
    // Injected by the host (MainComponent).
    std::function<void(int midiNote, float velocity)> playNote;
    std::function<void(int midiNote)>                 stopNote;
    std::function<void()>                             requestPlay;
    std::function<void()>                             requestRecordToggle;
    std::function<juce::String()>                     transportText; // e.g. "PLAY 120 BPM"

    // Lowest pad maps to this MIDI note; pads ascend chromatically.
    int baseNote = 36; // C1, classic groovebox base

    void onControlEvent(const MoveControlEvent& e) override
    {
        switch (e.control)
        {
            case MoveControl::Pad:
            {
                const int note = juce::jlimit(0, 127, baseNote + e.index);
                if (e.phase == MovePhase::Down)
                {
                    padLit[static_cast<size_t>(e.index) & 31u] = true;
                    if (playNote) playNote(note, e.velocity);
                }
                else if (e.phase == MovePhase::Up)
                {
                    padLit[static_cast<size_t>(e.index) & 31u] = false;
                    if (stopNote) stopNote(note);
                }
                break;
            }

            case MoveControl::Play:
                if (e.phase == MovePhase::Down && requestPlay) requestPlay();
                break;

            case MoveControl::Record:
                if (e.phase == MovePhase::Down && requestRecordToggle) requestRecordToggle();
                break;

            default:
                break; // other controls: no-op in the stub
        }
    }

    // No-op until the surface exists (Task 3). Pad state is tracked in padLit and
    // will drive surface.setPadLit(i, ...) once ControlSurfaceComponent is built.
    void refreshLeds(ControlSurfaceComponent& /*surface*/) override {}

    juce::String oledText() const override
    {
        juce::String t = transportText ? transportText() : juce::String("STOP");
        return "Note  " + t;
    }

    juce::String name() const override { return "Note"; }

    bool isPadLit(int i) const { return padLit[static_cast<size_t>(i) & 31u]; }

private:
    std::array<bool, 32> padLit { {} };
};
