#pragma once

// Hides the game HUD during dramatic cinematics by simulating the game's
// HUD toggle key (F10 by default). Toggle-based, so the state is tracked to
// send exactly one press on hide and one on restore.
namespace HudToggle
{
    // Hide/show the HUD. No-op if already in the requested state.
    void SetHidden(bool hidden);

    // True while we believe the HUD is hidden by us.
    bool IsHidden();
}
