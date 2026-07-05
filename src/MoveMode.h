#pragma once

#include <JuceHeader.h>
#include "MoveControlEvent.h"

// Forward-declared so modes can be built without the (Task 3) surface component.
// Modes only touch the surface through its LED/OLED API, passed by reference.
class ControlSurfaceComponent;

// ─────────────────────────────────────────────────────────────────────────────
// MoveMode — one operating mode of the Move surface (Note / Session / Sampling /
// Mixing, per manual §5). The active mode receives normalized control events,
// paints the surface's LEDs/highlights, and supplies the OLED text.
//
// Spec: docs/superpowers/specs/2026-07-05-control-surface-design.md
// ─────────────────────────────────────────────────────────────────────────────

class MoveMode
{
public:
    virtual ~MoveMode() = default;

    // A normalized control event from the bus (already stamped with shiftHeld).
    virtual void onControlEvent(const MoveControlEvent& e) = 0;

    // Repaint the surface's pad/step LEDs to reflect this mode's state.
    virtual void refreshLeds(ControlSurfaceComponent& surface) = 0;

    // The contextual text this mode wants on the 128x64 OLED strip.
    virtual juce::String oledText() const = 0;

    // Human-readable mode name (for the OLED / debugging).
    virtual juce::String name() const = 0;
};
