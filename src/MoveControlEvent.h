#pragma once

#include <JuceHeader.h>

// ─────────────────────────────────────────────────────────────────────────────
// MoveControlEvent — the normalized control vocabulary for the on-screen
// Ableton-Move surface. Every input method (touch, gamepad, keyboard) is
// translated into these events by an adapter, and every Move-mode consumes them.
// This is the abstraction that lets one manual describe three input methods:
// modes only ever see named Move controls, never raw touch/gamepad input.
//
// Spec: docs/superpowers/specs/2026-07-05-control-surface-design.md
// ─────────────────────────────────────────────────────────────────────────────

enum class MoveControl
{
    Pad,               // index 0..31 (4 rows x 8 cols), velocity + aftertouch
    Step,              // index 0..15
    Encoder,           // index 0..8; the Volume encoder is index 9
    Wheel,             // relative Turn deltas; Down/Up = wheel click
    ArrowLeft,
    ArrowRight,
    Plus,
    Minus,
    Shift,             // modifier — handled by the bus, not forwarded as a press
    Play,
    Record,
    Capture,
    Undo,
    Copy,
    Delete,
    Mute,
    Loop,
    Sampling,
    Track,             // index 0..3
    NoteSessionToggle,
    BackSettings
};

// Index of the dedicated Volume encoder within the Encoder control.
static constexpr int kMoveVolumeEncoderIndex = 9;

enum class MovePhase
{
    Down,              // button/pad pressed, wheel/encoder clicked
    Up,                // released
    Hold,              // held past the long-press threshold
    Turn               // relative motion (encoders, wheel)
};

struct MoveControlEvent
{
    MoveControl control;
    MovePhase   phase     = MovePhase::Down;
    int         index     = 0;      // pad / step / encoder / track index
    float       velocity  = 1.0f;   // pads: 0..1 (Full Velocity default = 1.0)
    float       pressure  = 0.0f;   // pads: polyphonic aftertouch 0..1
    float       delta      = 0.0f;  // encoder / wheel relative turn
    bool        shiftHeld = false;  // stamped by MoveControlBus at post() time

    static MoveControlEvent pad(int i, MovePhase p, float vel = 1.0f, float aft = 0.0f)
    {
        MoveControlEvent e; e.control = MoveControl::Pad; e.phase = p;
        e.index = i; e.velocity = vel; e.pressure = aft; return e;
    }

    static MoveControlEvent step(int i, MovePhase p)
    {
        MoveControlEvent e; e.control = MoveControl::Step; e.phase = p; e.index = i; return e;
    }

    static MoveControlEvent encoder(int i, float d)
    {
        MoveControlEvent e; e.control = MoveControl::Encoder; e.phase = MovePhase::Turn;
        e.index = i; e.delta = d; return e;
    }

    static MoveControlEvent button(MoveControl c, MovePhase p = MovePhase::Down)
    {
        MoveControlEvent e; e.control = c; e.phase = p; return e;
    }
};
