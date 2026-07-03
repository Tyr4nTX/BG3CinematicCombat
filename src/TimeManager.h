#pragma once
#include <cstdint>

// Slow-motion via QueryPerformanceCounter hook.
// The game derives its frame delta from QPC; scaling the virtual clock slows
// down everything without touching game code.
//
// NOTE: deliberately QPC-only. An experiment that additionally hooked
// GetTickCount/GetTickCount64/timeGetTime crashed the game during loading -
// those functions are hammered by dozens of loader/driver threads and
// patching them mid-load is a known instant-crash race. Do not re-add them.
namespace TimeManager
{
    // Create the QPC hook (MinHook must be initialized). Does NOT enable it -
    // the caller enables all hooks in one batch via MH_EnableHook(MH_ALL_HOOKS)
    // to keep the number of thread-suspension windows minimal.
    bool Install();

    // Call after the batched enable succeeded.
    void OnHooksEnabled();

    // 1.0 = normal speed, 0.35 = dramatic slow motion. Clamped to [0.05, 1.0].
    void SetTimeScale(float scale);
    float GetTimeScale();

    // Unscaled wall-clock seconds (uses the original, unhooked QPC).
    // All internal mod timing must use this, since std::chrono goes through
    // the hooked QPC and would be scaled too.
    double RealSeconds();

    bool IsInstalled();

    // Diagnostics: how often the game queried QPC since the last call.
    uint64_t GetAndResetQpcCount();
}
