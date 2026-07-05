#include "MoveControlBus.h"

void MoveControlBus::post(MoveControlEvent e)
{
    // Shift is a modifier: update state, do not forward as a press.
    if (e.control == MoveControl::Shift)
    {
        if (e.phase == MovePhase::Down) shiftHeld = true;
        else if (e.phase == MovePhase::Up) shiftHeld = false;
        return;
    }

    e.shiftHeld = shiftHeld;

    if (activeMode != nullptr)
        activeMode->onControlEvent(e);
}
