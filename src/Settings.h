#pragma once
#include <string>
#include <filesystem>
#include <nlohmann/json.hpp>

struct CinematicSettings
{
    // ---- General ----
    bool  enabled = true;
    bool  enableInDialogue = false;
    float globalIntensity = 1.0f;        // multiplier for all effects
    bool  slowMotionEnabled = true;      // master toggle for time-scale effects
    bool  panToTarget = true;            // pan camera root toward event position
    bool  hideHud = true;                // hide HUD (simulated F10) during kill/crit cams

    // ---- Kill Cam ----
    bool  killCamEnabled = true;
    float killCamZoom = 1.2f;            // close zoom
    float killCamPitch = 15.0f;          // low angle (degrees)
    float killCamFov = 40.0f;            // narrow FOV
    float killCamHoldTime = 2.0f;        // seconds to hold (real time)
    float killCamTransitionIn = 0.4f;    // seconds to lerp in
    float killCamTransitionOut = 0.8f;   // seconds to lerp out
    float killCamTimeScale = 0.35f;      // slow motion factor during kill cam

    // ---- Crit Cam ----
    bool  critCamEnabled = true;
    float critCamZoom = 2.0f;
    float critCamPitch = 20.0f;
    float critCamFov = 45.0f;
    float critCamHoldTime = 1.0f;
    float critCamTransitionIn = 0.2f;
    float critCamTransitionOut = 0.5f;
    float critCamTimeScale = 0.6f;

    // ---- Hit Cam (normal hits) ----
    bool  hitCamEnabled = false;
    float hitCamZoom = 2.5f;
    float hitCamPitch = 20.0f;
    float hitCamFov = 50.0f;
    float hitCamHoldTime = 0.6f;
    float hitCamTransitionIn = 0.15f;
    float hitCamTransitionOut = 0.4f;
    float hitCamTimeScale = 0.8f;

    // ---- Action Cam (shoulder cam while an action executes) ----
    bool  actionCamEnabled = false;   // opt-in: always-on cams proved exhausting
    float actionCamZoom = 2.0f;
    float actionCamPitch = 20.0f;
    float actionCamFov = 55.0f;
    float actionCamTransitionIn = 0.35f;
    float actionCamTransitionOut = 0.6f;
    float actionCamMaxDuration = 8.0f;   // safety timeout if no end event arrives

    // ---- Follow Cam (Dash/Sprint) ----
    bool  followCamEnabled = false;   // opt-in: dash chase cam
    float followCamZoom = 2.0f;          // medium-close
    float followCamPitch = 10.0f;        // very low angle
    float followCamFov = 65.0f;          // wider
    float followCamTransitionIn = 0.3f;
    float followCamTransitionOut = 0.6f;
    float followCamMovementThreshold = 0.5f;   // min delta per frame to trigger
    int   followCamMinFrames = 5;              // consecutive frames above threshold

    // ---- Spell Cam ----
    bool  spellCamEnabled = false;       // disabled by default
    float spellCamZoom = 6.0f;
    float spellCamPitch = 40.0f;
    float spellCamFov = 70.0f;
    float spellCamHoldTime = 1.5f;
    float spellCamTransitionIn = 0.3f;
    float spellCamTransitionOut = 0.5f;
    float spellCamTimeScale = 1.0f;

    // ---- IPC ----
    int   ipcPollIntervalFrames = 1;     // check for events every N frames
    int   settingsPollIntervalFrames = 60; // check for MCM settings changes every N frames

    // ---- Debug ----
    bool  debugOverlay = false;
    int   logLevel = 2;                  // 0=trace, 1=debug, 2=info, 3=warn, 4=err
    bool  traceCamera = false;           // log which CameraObject fields change (analysis mode)

    // Load from TOML file (defaults when MCM is not installed)
    void Load(const std::filesystem::path& path);

    // Apply overrides pushed by the Lua/MCM side (flat key -> value map)
    void ApplyJsonOverrides(const nlohmann::json& j);

    static CinematicSettings& Get();
};
