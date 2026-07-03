#include "pch.h"
#include "CinematicManager.h"
#include "CameraController.h"
#include "Settings.h"
#include "TimeManager.h"
#include "HudToggle.h"

CinematicManager& CinematicManager::Get()
{
    static CinematicManager instance;
    return instance;
}

const char* CinematicManager::GetStateName() const
{
    switch (m_state) {
        case State::Idle:      return "Idle";
        case State::ActionCam: return "ActionCam";
        case State::KillCam:   return "KillCam";
        case State::CritCam:   return "CritCam";
        case State::HitCam:    return "HitCam";
        case State::FollowCam: return "FollowCam";
        case State::SpellCam:  return "SpellCam";
        case State::Returning: return "Returning";
        default:               return "Unknown";
    }
}

float CinematicManager::GetDeltaTime()
{
    double now = TimeManager::RealSeconds();
    if (!m_hasLastTick) {
        m_hasLastTick = true;
        m_lastTickTime = now;
        return 0.016f;
    }
    float dt = static_cast<float>(now - m_lastTickTime);
    m_lastTickTime = now;
    return std::clamp(dt, 0.0001f, 0.1f);
}

void CinematicManager::Tick(RE::CameraObject* cam, RE::CameraDefinition* def)
{
    auto& settings = CinematicSettings::Get();
    if (!cam) return;

    float dt = GetDeltaTime();

    // --- Poll MCM settings changes periodically ---
    m_framesSinceSettingsPoll++;
    if (m_framesSinceSettingsPoll >= settings.settingsPollIntervalFrames) {
        m_framesSinceSettingsPoll = 0;
        IPC::PollSettings();
    }

    if (!settings.enabled) {
        if (m_state != State::Idle) ForceIdle();
        return;
    }

    // --- Poll IPC events periodically ---
    m_framesSinceEventPoll++;
    if (m_framesSinceEventPoll >= settings.ipcPollIntervalFrames) {
        m_framesSinceEventPoll = 0;
        auto events = IPC::Poll();
        if (!events.empty()) {
            ProcessEvents(events, cam);
        }
    }

    // NOTE: the old camera-movement heuristic for the follow cam is gone -
    // it could not tell character dashes from the player manually panning the
    // camera and misfired constantly. The follow cam now triggers only on
    // dash events sent by the Lua side.

    // --- State timer (real seconds, so slow motion doesn't stretch the hold) ---
    if (m_state != State::Idle && m_state != State::Returning) {
        m_stateTimer += dt;

        if (m_holdDuration > 0.f && m_stateTimer >= m_holdDuration) {
            BeginReturn();
        }
    }

    // When the controller finished returning on its own, go idle
    if (m_state == State::Returning && !CameraController::Get().IsActive()) {
        m_state = State::Idle;
        m_stateTimer = 0.f;
    }
}

void CinematicManager::ProcessEvents(const std::vector<IPC::CombatEvent>& events, RE::CameraObject* cam)
{
    auto& settings = CinematicSettings::Get();

    for (auto& evt : events) {
        // Priority: Kill > Crit > Hit > Action > Dash/Spell
        switch (evt.type) {
            case IPC::EventType::ActionStart:
            case IPC::EventType::ActionStartRanged:
                // Shoulder cam while the action plays out; never interrupts
                // a kill/crit/hit cinematic. Faces the action's target
                // (ranged: frontal view of the shooter instead).
                if (settings.actionCamEnabled &&
                    (m_state == State::Idle || m_state == State::Returning ||
                     m_state == State::FollowCam || m_state == State::ActionCam)) {
                    TransitionTo(State::ActionCam, &evt, cam);
                    spdlog::info("[Cinematic] Action started ({})",
                        evt.type == IPC::EventType::ActionStartRanged ? "ranged" : "melee");
                }
                break;

            case IPC::EventType::ActionEnd:
                // Don't zoom out immediately: for ranged attacks the
                // projectile is still flying - linger a moment so an incoming
                // kill/crit/hit cam takes over seamlessly instead of the
                // camera pumping out and back in.
                if (m_state == State::ActionCam) {
                    m_holdDuration = m_stateTimer + 1.0f;
                    spdlog::info("[Cinematic] Action ended - lingering 1s for impact");
                }
                break;

            case IPC::EventType::Kill:
                if (settings.killCamEnabled) {
                    TransitionTo(State::KillCam, &evt, cam);
                    spdlog::info("[Cinematic] Kill detected at ({:.1f}, {:.1f}, {:.1f})",
                        evt.targetX, evt.targetY, evt.targetZ);
                }
                break;

            case IPC::EventType::CriticalHit:
                if (settings.critCamEnabled && m_state != State::KillCam) {
                    TransitionTo(State::CritCam, &evt, cam);
                    spdlog::info("[Cinematic] Critical hit detected");
                }
                break;

            case IPC::EventType::Hit:
                // Normal hits only trigger from idle - never interrupt a cinematic
                if (settings.hitCamEnabled &&
                    (m_state == State::Idle || m_state == State::Returning || m_state == State::FollowCam)) {
                    TransitionTo(State::HitCam, &evt, cam);
                    spdlog::info("[Cinematic] Hit detected");
                }
                break;

            case IPC::EventType::DashStart:
                if (settings.followCamEnabled &&
                    (m_state == State::Idle || m_state == State::Returning)) {
                    TransitionTo(State::FollowCam, nullptr, cam);
                    spdlog::info("[Cinematic] Dash detected");
                }
                break;

            case IPC::EventType::SpellCast:
                if (settings.spellCamEnabled &&
                    m_state != State::KillCam && m_state != State::CritCam) {
                    TransitionTo(State::SpellCam, &evt, cam);
                    spdlog::info("[Cinematic] Spell cast detected");
                }
                break;

            default:
                break;
        }
    }
}

void CinematicManager::DetectMovement(RE::CameraObject* cam)
{
    auto& settings = CinematicSettings::Get();

    if (!m_hasLastPos) {
        m_lastCamRootPos = cam->desiredCameraRootPos;
        m_hasLastPos = true;
        return;
    }

    RE::Vector3 delta = cam->desiredCameraRootPos - m_lastCamRootPos;
    float speed = delta.LengthXZ();
    m_lastCamRootPos = cam->desiredCameraRootPos;

    static int dbgCounter = 0;
    dbgCounter++;
    if (dbgCounter % 120 == 0) {
        bool inCombat = (cam->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0;
        spdlog::debug("[Move] speed={:.4f} thresh={:.4f} consec={} inCombat={} flags={:X}",
            speed, settings.followCamMovementThreshold,
            m_consecutiveMovementFrames, inCombat, (uint32_t)cam->cameraModeFlags);
    }

    bool inCombat = (cam->cameraModeFlags & RE::CameraModeFlags::kCombat) != 0;
    bool inTactical = (cam->cameraModeFlags & RE::CameraModeFlags::kTactical) != 0;
    if (!inCombat && !inTactical) {
        m_consecutiveMovementFrames = 0;
        return;
    }

    if (speed >= settings.followCamMovementThreshold) {
        m_consecutiveMovementFrames++;
        if (m_consecutiveMovementFrames >= settings.followCamMinFrames) {
            spdlog::info("[Cinematic] FollowCam triggered! speed={:.4f} consec={}", speed, m_consecutiveMovementFrames);
            TransitionTo(State::FollowCam, nullptr, cam);
        }
    } else {
        m_consecutiveMovementFrames = 0;
    }
}

void CinematicManager::TransitionTo(State newState, const IPC::CombatEvent* evt, RE::CameraObject* cam)
{
    auto& settings = CinematicSettings::Get();
    auto& camCtrl = CameraController::Get();

    m_state = newState;
    m_stateTimer = 0.f;

    RE::Vector3 targetPos{};
    bool pan = false;
    if (evt && evt->hasPosition && settings.panToTarget) {
        targetPos = { evt->targetX, evt->targetY, evt->targetZ };
        pan = true;
    }

    // Distance from the camera anchor to the target. The anchor sticks to the
    // acting character (position is not controllable), so for FAR targets
    // (ranged/spell kills) a tight zoom would bury the view in the shooter's
    // back and hide the victim entirely. Widen the zoom with distance so the
    // faced enemy stays in frame.
    float targetDist = 0.f;
    if (pan && cam) {
        float ddx = targetPos.x - cam->cameraRootPos.x;
        float ddz = targetPos.z - cam->cameraRootPos.z;
        targetDist = sqrtf(ddx * ddx + ddz * ddz);
    }
    auto adaptZoom = [&](float zoom) {
        if (targetDist > 5.f) {
            return std::max(zoom, std::min(targetDist * 0.6f, 7.f));
        }
        return zoom;
    };

    float timeScale = 1.0f;

    switch (newState) {
        case State::ActionCam: {
            m_holdDuration = settings.actionCamMaxDuration;
            // Ranged: ONE calm turn toward the target at aim start (the
            // kill/hit cam keeps that yaw, so there is no second turn) plus
            // a distance-widened zoom - enemy centered ahead, shooter's back
            // in the lower frame: the over-the-shoulder shot.
            bool ranged = (evt && evt->type == IPC::EventType::ActionStartRanged);
            float zoom = settings.actionCamZoom;
            if (ranged && targetDist > 5.f) {
                zoom = std::max(zoom, std::min(targetDist * 0.5f, 5.5f));
            }
            camCtrl.SetTarget(zoom, settings.actionCamPitch,
                settings.actionCamFov, settings.actionCamTransitionIn, pan, targetPos, false);
            break;
        }

        case State::KillCam:
            m_holdDuration = settings.killCamHoldTime;
            timeScale = settings.killCamTimeScale;
            camCtrl.SetTarget(adaptZoom(settings.killCamZoom), settings.killCamPitch,
                settings.killCamFov, settings.killCamTransitionIn, pan, targetPos);
            // HUD hiding via simulated F10 is DISABLED: F10 is the Windows
            // system-menu key - injecting it froze input until the next real
            // key press and desynced the HUD toggle state (user-reported).
            // Needs a proper UI-visibility API before it comes back.
            break;

        case State::CritCam:
            m_holdDuration = settings.critCamHoldTime;
            timeScale = settings.critCamTimeScale;
            camCtrl.SetTarget(adaptZoom(settings.critCamZoom), settings.critCamPitch,
                settings.critCamFov, settings.critCamTransitionIn, pan, targetPos);
            break;

        case State::HitCam:
            m_holdDuration = settings.hitCamHoldTime;
            timeScale = settings.hitCamTimeScale;
            camCtrl.SetTarget(adaptZoom(settings.hitCamZoom), settings.hitCamPitch,
                settings.hitCamFov, settings.hitCamTransitionIn, pan, targetPos);
            break;

        case State::FollowCam:
            m_holdDuration = 2.5f; // dash cam runs for a fixed time
            camCtrl.SetTarget(settings.followCamZoom, settings.followCamPitch,
                settings.followCamFov, settings.followCamTransitionIn, false, {});
            break;

        case State::SpellCam:
            m_holdDuration = settings.spellCamHoldTime;
            timeScale = settings.spellCamTimeScale;
            camCtrl.SetTarget(settings.spellCamZoom, settings.spellCamPitch,
                settings.spellCamFov, settings.spellCamTransitionIn, pan, targetPos);
            break;

        default:
            break;
    }

    if (settings.slowMotionEnabled && TimeManager::IsInstalled()) {
        TimeManager::SetTimeScale(timeScale);
    }

    spdlog::info("[Cinematic] -> {} (timescale {:.2f}, pan {}, targetDist {:.1f})",
        GetStateName(), timeScale, pan, targetDist);
}

void CinematicManager::BeginReturn()
{
    auto& settings = CinematicSettings::Get();
    float returnTime = 0.5f;

    switch (m_state) {
        case State::ActionCam: returnTime = settings.actionCamTransitionOut; break;
        case State::KillCam:   returnTime = settings.killCamTransitionOut; break;
        case State::CritCam:   returnTime = settings.critCamTransitionOut; break;
        case State::HitCam:    returnTime = settings.hitCamTransitionOut; break;
        case State::FollowCam: returnTime = settings.followCamTransitionOut; break;
        case State::SpellCam:  returnTime = settings.spellCamTransitionOut; break;
        default: break;
    }

    m_state = State::Returning;
    m_consecutiveMovementFrames = 0;
    CameraController::Get().BeginReturn(returnTime);

    if (TimeManager::IsInstalled()) {
        TimeManager::SetTimeScale(1.0f);
    }

    spdlog::info("[Cinematic] -> Returning");
}

void CinematicManager::ForceIdle()
{
    m_state = State::Idle;
    m_stateTimer = 0.f;
    m_consecutiveMovementFrames = 0;
    CameraController::Get().Reset();
    if (TimeManager::IsInstalled()) {
        TimeManager::SetTimeScale(1.0f);
    }
    spdlog::info("[Cinematic] Forced idle");
}
