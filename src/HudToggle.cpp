#include "pch.h"
#include "HudToggle.h"

namespace HudToggle
{
    static bool s_hidden = false;

    static void SendHudToggleKey()
    {
        // Simulate a full F10 press (down + up). The game window has focus
        // during gameplay, so SendInput reaches it.
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = VK_F10;
        inputs[0].ki.wScan = static_cast<WORD>(MapVirtualKeyA(VK_F10, MAPVK_VK_TO_VSC));
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wVk = VK_F10;
        inputs[1].ki.wScan = inputs[0].ki.wScan;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

        UINT sent = SendInput(2, inputs, sizeof(INPUT));
        if (sent != 2) {
            spdlog::warn("[HUD] SendInput sent {} of 2 events", sent);
        }
    }

    void SetHidden(bool hidden)
    {
        if (hidden == s_hidden) return;
        s_hidden = hidden;
        SendHudToggleKey();
        spdlog::debug("[HUD] {} (simulated F10)", hidden ? "hidden" : "restored");
    }

    bool IsHidden()
    {
        return s_hidden;
    }
}
