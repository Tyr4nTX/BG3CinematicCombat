#pragma once
#include "pch.h"
#include "RE/Camera.h"
#include "Pattern.h"

namespace Hooks
{
    // ---- Function typedefs ----

    // UpdateCamera: called every frame for camera processing.
    // a4 is the camera UnkObject (per ersh1/BG3_NativeCameraTweaks RE).
    using UpdateCamera_t = void (*)(void*, void*, void*, void*);

    // GetCurrentCameraDefinition: returns CameraDefinition* for the active camera mode.
    // The AOB pattern IS the function start (per ersh1) - no prologue search needed.
    using GetCurrentCameraDefinition_t = RE::CameraDefinition* (*)(RE::CameraObject*);

    // CalculateCameraPitch: the game recomputes the camera pitch (degrees) from
    // zoom every frame. Overriding the return value is the only reliable way to
    // change pitch (writing CameraObject::currentPitch_164 gets overwritten).
    using CalculateCameraPitch_t = float (*)(RE::CameraObject*, uint8_t, uint8_t);

    // ---- Globals ----
    extern UpdateCamera_t               OriginalUpdateCamera;
    extern CalculateCameraPitch_t      OriginalCalculateCameraPitch;
    extern GetCurrentCameraDefinition_t GetCurrentCameraDefinition;

    // Camera singleton pointer (from the game's global)
    extern RE::UnkObject** g_unkCameraSingletonPtr;

    // ---- Init / Shutdown ----
    bool Install();
    void Uninstall();
}
