#pragma once
#include "RE/Camera.h"
#include "Settings.h"

// Cinematic camera controller - blends camera parameters toward a cinematic
// target with a single weight (0 = game camera, 1 = full cinematic).
//
// Application points:
//  - zoom:  cam->desiredZoom, written after the game's UpdateCamera
//  - pos:   cam->desiredCameraRootPos, written after UpdateCamera (pan to target)
//  - pitch: returned from the CalculateCameraPitch detour (the game derives
//           pitch from zoom every frame, so writing the field directly does nothing)
//  - fov:   CameraDefinition fovClose/fovFar (shared game data, always restored)
class CameraController
{
public:
    static CameraController& Get();

    // Set cinematic target. Keeps current blend weight if already active,
    // so cinematics can hand off smoothly (e.g. crit cam -> kill cam).
    // flipYaw: face 180 degrees AWAY from the target - frontal view of the
    // caster while aiming a ranged attack (the target sits behind the camera).
    void SetTarget(float zoom, float pitch, float fov, float transitionIn,
                   bool panToTarget, const RE::Vector3& targetPos, bool flipYaw = false);

    // Begin blending back to the original camera
    void BeginReturn(float transitionOut);

    // Immediately stop, restoring FOV definition values
    void Reset();

    bool IsActive() const { return m_active; }

    // Called BEFORE the game's UpdateCamera runs: writes the pan position so
    // the game's own update integrates from our values (post-update writes to
    // the root position get recomputed away by the combat follow logic).
    void PreApply(RE::CameraObject* cam);

    // Per-frame application; called from the UpdateCamera detour after the original
    void Apply(RE::CameraObject* cam, RE::CameraDefinition* def);

    // Called from the CalculateCameraPitch detour with the game's computed pitch
    // (degrees). Returns the pitch the game should actually use.
    float FilterPitch(float gamePitch);

private:
    CameraController() = default;

    bool  m_active = false;
    bool  m_returning = false;

    // Blend weight and eased weight
    float m_weight = 0.f;        // linear 0..1
    float m_easedWeight = 0.f;   // smoothstepped + intensity, used for application

    // Target values
    float m_targetZoom  = 0.f;
    float m_targetPitch = 0.f;
    float m_targetFov   = 0.f;
    bool  m_panToTarget = false;
    bool  m_flipYaw = false;
    RE::Vector3 m_targetPos{};

    // Originals captured at cinematic start (for blending back)
    bool  m_capturedOriginals = false;
    float m_originalZoom = 0.f;
    RE::Vector3 m_originalRootPos{};

    // FOV definition bookkeeping (shared game data -> must always restore)
    RE::CameraDefinition* m_fovDef = nullptr;
    float m_defOrigFovClose = 0.f;
    float m_defOrigFovFar = 0.f;

    // Transition timing (real seconds)
    float m_transitionIn = 0.3f;
    float m_transitionOut = 0.5f;

    double m_lastFrameTime = 0.0;
    bool   m_hasLastFrame = false;
    int    m_diagCounter = 0;
    float  m_lastDesiredYaw = 0.f;

    float GetDeltaTime();
    void  RestoreFovDef();
    static float Lerp(float a, float b, float t);
    static float SmoothStep(float t);
};
