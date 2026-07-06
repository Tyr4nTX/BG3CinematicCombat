#pragma once
#include "RE/Camera.h"
#include "IPC.h"

class CinematicManager
{
public:
    enum class State
    {
        Idle,
        ActionCam,
        KillCam,
        CritCam,
        HitCam,
        FollowCam,
        SpellCam,
        Returning
    };

    static CinematicManager& Get();

    // Called every frame from the UpdateCamera hook
    void Tick(RE::CameraObject* cam, RE::CameraDefinition* def);

    // Get the current cinematic state
    State GetState() const { return m_state; }
    const char* GetStateName() const;

    // Force return to idle
    void ForceIdle();

private:
    CinematicManager() = default;

    State m_state = State::Idle;
    float m_stateTimer = 0.f;      // how long we've been in this state (real seconds)
    float m_holdDuration = 0.f;    // how long to hold the cinematic (real seconds)

    // Follow cam movement detection
    RE::Vector3 m_lastCamRootPos{};
    bool        m_hasLastPos = false;
    int         m_consecutiveMovementFrames = 0;

    // Anchor-teleport detection during active cinematics
    RE::Vector3 m_jumpRefPos{};
    bool        m_hasJumpRef = false;

    // IPC polling
    int m_framesSinceEventPoll = 0;
    int m_framesSinceSettingsPoll = 0;

    // Frame timing (real, unscaled)
    double m_lastTickTime = 0.0;
    bool   m_hasLastTick = false;

    float GetDeltaTime();

    void ProcessEvents(const std::vector<IPC::CombatEvent>& events, RE::CameraObject* cam);
    void DetectMovement(RE::CameraObject* cam);

    void TransitionTo(State newState, const IPC::CombatEvent* evt, RE::CameraObject* cam);
    void BeginReturn();
};
