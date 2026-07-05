#pragma once

#include <JuceHeader.h>
#include "MoveControlEvent.h"
#include "MoveMode.h"

// ─────────────────────────────────────────────────────────────────────────────
// MoveControlBus — fans in normalized events from the three input adapters
// (touch, gamepad, keyboard), owns the Shift modifier state, and dispatches to
// the currently active MoveMode.
//
// The Shift control is NOT forwarded as an ordinary press; instead the bus tracks
// it and stamps `shiftHeld` onto every other event. This gives every mode Move's
// "hold Shift for the secondary function" behavior for free — including the
// multifunctional step buttons (a Step event with shiftHeld == true is a
// secondary-function shortcut rather than a sequencer step).
//
// Spec: docs/superpowers/specs/2026-07-05-control-surface-design.md
// ─────────────────────────────────────────────────────────────────────────────

class MoveControlBus
{
public:
    MoveControlBus() = default;

    // The active mode receives dispatched events. Not owned by the bus.
    void setActiveMode(MoveMode* mode) { activeMode = mode; }
    MoveMode* getActiveMode() const { return activeMode; }

    bool isShiftHeld() const { return shiftHeld; }

    // Entry point for all three input adapters. Handles Shift internally;
    // stamps shiftHeld and forwards everything else to the active mode.
    void post(MoveControlEvent e);

private:
    MoveMode* activeMode = nullptr;
    bool      shiftHeld  = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MoveControlBus)
};
