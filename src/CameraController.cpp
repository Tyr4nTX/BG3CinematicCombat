#include "pch.h"
#include "CameraController.h"
#include "TimeManager.h"

CameraController& CameraController::Get()
{
    static CameraController instance;
    return instance;
}

float CameraController::GetDeltaTime()
{
    double now = TimeManager::RealSeconds();
    if (!m_hasLastFrame) {
        m_hasLastFrame = true;
        m_lastFrameTime = now;
        return 0.016f;
    }
    float dt = static_cast<float>(now - m_lastFrameTime);
    m_lastFrameTime = now;
    return std::clamp(dt, 0.0001f, 0.1f);
}

float CameraController::Lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

float CameraController::SmoothStep(float t)
{
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

void CameraController::SetTarget(float zoom, float pitch, float fov, float transitionIn,
                                 bool panToTarget, const RE::Vector3& targetPos, bool flipYaw)
{
    // Keep the current weight when already active so a new cinematic
    // (e.g. kill cam overriding crit cam) blends over smoothly.
    if (!m_active) {
        m_weight = 0.f;
        m_capturedOriginals = false;
    }
    m_active = true;
    m_returning = false;
    m_targetZoom = zoom;
    m_targetPitch = pitch;
    m_targetFov = fov;
    m_panToTarget = panToTarget;
    m_flipYaw = flipYaw;
    m_targetPos = targetPos;
    m_transitionIn = std::max(transitionIn, 0.01f);
}

void CameraController::BeginReturn(float transitionOut)
{
    if (!m_active) return;
    m_returning = true;
    m_transitionOut = std::max(transitionOut, 0.01f);
}

void CameraController::RestoreFovDef()
{
    if (m_fovDef) {
        m_fovDef->fovClose_84 = m_defOrigFovClose;
        m_fovDef->fovFar_88 = m_defOrigFovFar;
        m_fovDef = nullptr;
    }
}

void CameraController::Reset()
{
    RestoreFovDef();
    m_active = false;
    m_returning = false;
    m_capturedOriginals = false;
    m_weight = 0.f;
    m_easedWeight = 0.f;
}

// Shortest-arc difference between two angles in degrees, result in [-180, 180]
static float AngleDiffDeg(float from, float to)
{
    float d = fmodf(to - from + 540.f, 360.f) - 180.f;
    return d;
}

void CameraController::PreApply(RE::CameraObject* cam)
{
    if (!m_active || !cam || !m_panToTarget || !m_capturedOriginals) return;
    if (m_returning || m_easedWeight <= 0.f) return;

    // Position control is a dead end (the game re-derives the camera root from
    // the followed character every frame; both direct writes and pan-input
    // injection were proven ineffective). Rotation however is STATE: the yaw
    // angle persists once set, so turning the camera toward the target frames
    // the enemy center-screen - the shoulder-cam look.
    float dx = m_targetPos.x - cam->cameraRootPos.x;
    float dz = m_targetPos.z - cam->cameraRootPos.z;
    float dist = sqrtf(dx * dx + dz * dz);
    if (dist < 0.8f) return;

    constexpr float RAD2DEG = 180.f / 3.14159265f;
    // Yaw convention: atan2(dx, dz) in degrees - confirmed correct in testing.
    float desiredYaw = atan2f(dx, dz) * RAD2DEG;
    if (m_flipYaw) {
        // Frontal view of the caster: look back along the aim direction
        desiredYaw += 180.f;
    }

    // TIME-based smooth turn. The old version applied a fraction of the
    // remaining arc PER FRAME, which snapped near-instantly on high FPS.
    // dt is additionally scaled by the current time scale so that during
    // slow motion the turn pans with the slowed world - a deliberate
    // cinematic sweep instead of a real-time snap.
    double now = TimeManager::RealSeconds();
    float dt = 0.016f;
    if (m_hasLastPre) {
        dt = std::clamp(static_cast<float>(now - m_lastPreTime), 0.0001f, 0.1f);
    }
    m_hasLastPre = true;
    m_lastPreTime = now;
    float dtEff = dt * TimeManager::GetTimeScale();

    float diff = AngleDiffDeg(cam->angle, desiredYaw);
    // Exponential ease (~63% of the arc in 0.25s, ~95% in 0.75s)...
    float k = 1.f - expf(-dtEff * 4.0f);
    float step = diff * k * m_easedWeight;
    // ...with a hard cap so even 180-degree turns never whip
    float maxStep = 240.f * dtEff;
    step = std::clamp(step, -maxStep, maxStep);
    cam->angle += step;

    m_lastDesiredYaw = desiredYaw;
}

void CameraController::Apply(RE::CameraObject* cam, RE::CameraDefinition* def)
{
    if (!m_active || !cam) return;

    float dt = GetDeltaTime();
    float intensity = std::clamp(CinematicSettings::Get().globalIntensity, 0.f, 1.f);

    // Capture originals on the first frame of the cinematic
    if (!m_capturedOriginals) {
        m_originalZoom = cam->desiredZoom;
        m_originalRootPos = cam->desiredCameraRootPos;
        m_capturedOriginals = true;
        spdlog::info("[PanDiag] originals: root=({:.1f},{:.1f},{:.1f}) posA=({:.1f},{:.1f},{:.1f}) posB=({:.1f},{:.1f},{:.1f})",
            cam->cameraRootPos.x, cam->cameraRootPos.y, cam->cameraRootPos.z,
            cam->unkCameraRootPosA.x, cam->unkCameraRootPosA.y, cam->unkCameraRootPosA.z,
            cam->unkCameraRootPosB.x, cam->unkCameraRootPosB.y, cam->unkCameraRootPosB.z);
    }

    // Advance blend weight
    if (m_returning) {
        m_weight -= dt / m_transitionOut;
        if (m_weight <= 0.f) {
            // Fully returned: leave the camera to the game
            cam->desiredZoom = m_originalZoom;
            Reset();
            return;
        }
    } else {
        m_weight = std::min(m_weight + dt / m_transitionIn, 1.f);
    }

    m_easedWeight = SmoothStep(m_weight) * intensity;

    // Pan diagnostics: log what the game's update left in the camera BEFORE
    // our post-writes - shows whether the pre-applied pan position survived.
    if (m_panToTarget && CinematicSettings::Get().traceCamera && (++m_diagCounter % 20 == 0)) {
        spdlog::info("[PanDiag] w={:.2f} target=({:.1f},{:.1f},{:.1f}) root=({:.1f},{:.1f},{:.1f}) yaw={:.1f} desiredYaw={:.1f}",
            m_easedWeight,
            m_targetPos.x, m_targetPos.y, m_targetPos.z,
            cam->cameraRootPos.x, cam->cameraRootPos.y, cam->cameraRootPos.z,
            cam->angle, m_lastDesiredYaw);
    }

    // --- Zoom ---
    cam->desiredZoom = Lerp(m_originalZoom, m_targetZoom, m_easedWeight);

    // Pan happens via input injection in PreApply - no position writes here.

    // --- FOV via camera definition (shared data, save/restore carefully) ---
    if (def) {
        if (def != m_fovDef) {
            // Definition switched (camera mode change) - restore the old one
            RestoreFovDef();
            m_fovDef = def;
            m_defOrigFovClose = def->fovClose_84;
            m_defOrigFovFar = def->fovFar_88;
        }
        def->fovClose_84 = Lerp(m_defOrigFovClose, m_targetFov, m_easedWeight);
        def->fovFar_88 = Lerp(m_defOrigFovFar, m_targetFov, m_easedWeight);
    } else if (m_fovDef) {
        // Definition no longer available - restore
        RestoreFovDef();
    }
}

float CameraController::FilterPitch(float gamePitch)
{
    if (!m_active || m_easedWeight <= 0.f) return gamePitch;
    return Lerp(gamePitch, m_targetPitch, m_easedWeight);
}
