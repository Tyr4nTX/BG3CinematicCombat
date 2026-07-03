#include "pch.h"
#include "Settings.h"

CinematicSettings& CinematicSettings::Get()
{
    static CinematicSettings instance;
    return instance;
}

void CinematicSettings::Load(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) {
        spdlog::warn("[Settings] Config not found at {}, using defaults", path.string());
        return;
    }

    try {
        auto tbl = toml::parse_file(path.string());

        // General
        enabled           = tbl["General"]["Enabled"].value_or(enabled);
        enableInDialogue  = tbl["General"]["EnableInDialogue"].value_or(enableInDialogue);
        globalIntensity   = tbl["General"]["GlobalIntensity"].value_or(globalIntensity);
        slowMotionEnabled = tbl["General"]["SlowMotion"].value_or(slowMotionEnabled);
        panToTarget       = tbl["General"]["PanToTarget"].value_or(panToTarget);
        hideHud           = tbl["General"]["HideHud"].value_or(hideHud);

        // Kill Cam
        killCamEnabled       = tbl["KillCam"]["Enabled"].value_or(killCamEnabled);
        killCamZoom          = tbl["KillCam"]["Zoom"].value_or(killCamZoom);
        killCamPitch         = tbl["KillCam"]["Pitch"].value_or(killCamPitch);
        killCamFov           = tbl["KillCam"]["FOV"].value_or(killCamFov);
        killCamHoldTime      = tbl["KillCam"]["HoldTime"].value_or(killCamHoldTime);
        killCamTransitionIn  = tbl["KillCam"]["TransitionIn"].value_or(killCamTransitionIn);
        killCamTransitionOut = tbl["KillCam"]["TransitionOut"].value_or(killCamTransitionOut);
        killCamTimeScale     = tbl["KillCam"]["TimeScale"].value_or(killCamTimeScale);

        // Crit Cam
        critCamEnabled       = tbl["CritCam"]["Enabled"].value_or(critCamEnabled);
        critCamZoom          = tbl["CritCam"]["Zoom"].value_or(critCamZoom);
        critCamPitch         = tbl["CritCam"]["Pitch"].value_or(critCamPitch);
        critCamFov           = tbl["CritCam"]["FOV"].value_or(critCamFov);
        critCamHoldTime      = tbl["CritCam"]["HoldTime"].value_or(critCamHoldTime);
        critCamTransitionIn  = tbl["CritCam"]["TransitionIn"].value_or(critCamTransitionIn);
        critCamTransitionOut = tbl["CritCam"]["TransitionOut"].value_or(critCamTransitionOut);
        critCamTimeScale     = tbl["CritCam"]["TimeScale"].value_or(critCamTimeScale);

        // Hit Cam
        hitCamEnabled       = tbl["HitCam"]["Enabled"].value_or(hitCamEnabled);
        hitCamZoom          = tbl["HitCam"]["Zoom"].value_or(hitCamZoom);
        hitCamPitch         = tbl["HitCam"]["Pitch"].value_or(hitCamPitch);
        hitCamFov           = tbl["HitCam"]["FOV"].value_or(hitCamFov);
        hitCamHoldTime      = tbl["HitCam"]["HoldTime"].value_or(hitCamHoldTime);
        hitCamTransitionIn  = tbl["HitCam"]["TransitionIn"].value_or(hitCamTransitionIn);
        hitCamTransitionOut = tbl["HitCam"]["TransitionOut"].value_or(hitCamTransitionOut);
        hitCamTimeScale     = tbl["HitCam"]["TimeScale"].value_or(hitCamTimeScale);

        // Action Cam
        actionCamEnabled       = tbl["ActionCam"]["Enabled"].value_or(actionCamEnabled);
        actionCamZoom          = tbl["ActionCam"]["Zoom"].value_or(actionCamZoom);
        actionCamPitch         = tbl["ActionCam"]["Pitch"].value_or(actionCamPitch);
        actionCamFov           = tbl["ActionCam"]["FOV"].value_or(actionCamFov);
        actionCamTransitionIn  = tbl["ActionCam"]["TransitionIn"].value_or(actionCamTransitionIn);
        actionCamTransitionOut = tbl["ActionCam"]["TransitionOut"].value_or(actionCamTransitionOut);
        actionCamMaxDuration   = tbl["ActionCam"]["MaxDuration"].value_or(actionCamMaxDuration);

        // Follow Cam
        followCamEnabled           = tbl["FollowCam"]["Enabled"].value_or(followCamEnabled);
        followCamZoom              = tbl["FollowCam"]["Zoom"].value_or(followCamZoom);
        followCamPitch             = tbl["FollowCam"]["Pitch"].value_or(followCamPitch);
        followCamFov               = tbl["FollowCam"]["FOV"].value_or(followCamFov);
        followCamTransitionIn      = tbl["FollowCam"]["TransitionIn"].value_or(followCamTransitionIn);
        followCamTransitionOut     = tbl["FollowCam"]["TransitionOut"].value_or(followCamTransitionOut);
        followCamMovementThreshold = tbl["FollowCam"]["MovementThreshold"].value_or(followCamMovementThreshold);
        followCamMinFrames         = tbl["FollowCam"]["MinFrames"].value_or(followCamMinFrames);

        // Spell Cam
        spellCamEnabled       = tbl["SpellCam"]["Enabled"].value_or(spellCamEnabled);
        spellCamZoom          = tbl["SpellCam"]["Zoom"].value_or(spellCamZoom);
        spellCamPitch         = tbl["SpellCam"]["Pitch"].value_or(spellCamPitch);
        spellCamFov           = tbl["SpellCam"]["FOV"].value_or(spellCamFov);
        spellCamHoldTime      = tbl["SpellCam"]["HoldTime"].value_or(spellCamHoldTime);
        spellCamTransitionIn  = tbl["SpellCam"]["TransitionIn"].value_or(spellCamTransitionIn);
        spellCamTransitionOut = tbl["SpellCam"]["TransitionOut"].value_or(spellCamTransitionOut);
        spellCamTimeScale     = tbl["SpellCam"]["TimeScale"].value_or(spellCamTimeScale);

        // IPC
        ipcPollIntervalFrames      = tbl["IPC"]["PollIntervalFrames"].value_or(ipcPollIntervalFrames);
        settingsPollIntervalFrames = tbl["IPC"]["SettingsPollIntervalFrames"].value_or(settingsPollIntervalFrames);

        // Debug
        debugOverlay = tbl["Debug"]["Overlay"].value_or(debugOverlay);
        logLevel     = tbl["Debug"]["LogLevel"].value_or(logLevel);
        traceCamera  = tbl["Debug"]["TraceCamera"].value_or(traceCamera);

        spdlog::info("[Settings] Loaded configuration from {}", path.string());

    } catch (const toml::parse_error& e) {
        spdlog::error("[Settings] TOML parse error: {}", e.what());
    }
}

void CinematicSettings::ApplyJsonOverrides(const nlohmann::json& j)
{
    auto b = [&](const char* key, bool& field) {
        if (j.contains(key) && j[key].is_boolean()) field = j[key].get<bool>();
    };
    auto f = [&](const char* key, float& field) {
        if (j.contains(key) && j[key].is_number()) field = j[key].get<float>();
    };
    auto i = [&](const char* key, int& field) {
        if (j.contains(key) && j[key].is_number()) field = j[key].get<int>();
    };

    // Keys match the MCM blueprint setting IDs (see MCM_blueprint.json)
    b("enabled", enabled);
    f("global_intensity", globalIntensity);
    b("slow_motion_enabled", slowMotionEnabled);
    b("pan_to_target", panToTarget);
    b("hide_hud", hideHud);

    b("kill_enabled", killCamEnabled);
    f("kill_zoom", killCamZoom);
    f("kill_pitch", killCamPitch);
    f("kill_fov", killCamFov);
    f("kill_hold", killCamHoldTime);
    f("kill_in", killCamTransitionIn);
    f("kill_out", killCamTransitionOut);
    f("kill_timescale", killCamTimeScale);

    b("crit_enabled", critCamEnabled);
    f("crit_zoom", critCamZoom);
    f("crit_pitch", critCamPitch);
    f("crit_fov", critCamFov);
    f("crit_hold", critCamHoldTime);
    f("crit_in", critCamTransitionIn);
    f("crit_out", critCamTransitionOut);
    f("crit_timescale", critCamTimeScale);

    b("hit_enabled", hitCamEnabled);
    f("hit_zoom", hitCamZoom);
    f("hit_pitch", hitCamPitch);
    f("hit_fov", hitCamFov);
    f("hit_hold", hitCamHoldTime);
    f("hit_in", hitCamTransitionIn);
    f("hit_out", hitCamTransitionOut);
    f("hit_timescale", hitCamTimeScale);

    b("action_enabled", actionCamEnabled);
    f("action_zoom", actionCamZoom);
    f("action_pitch", actionCamPitch);
    f("action_fov", actionCamFov);
    f("action_in", actionCamTransitionIn);
    f("action_out", actionCamTransitionOut);
    f("action_max_duration", actionCamMaxDuration);

    b("follow_enabled", followCamEnabled);
    f("follow_zoom", followCamZoom);
    f("follow_pitch", followCamPitch);
    f("follow_fov", followCamFov);
    f("follow_threshold", followCamMovementThreshold);
    i("follow_min_frames", followCamMinFrames);

    b("spell_enabled", spellCamEnabled);
    f("spell_zoom", spellCamZoom);
    f("spell_pitch", spellCamPitch);
    f("spell_fov", spellCamFov);
    f("spell_hold", spellCamHoldTime);
    f("spell_timescale", spellCamTimeScale);

    spdlog::info("[Settings] Applied MCM overrides ({} keys)", j.size());
}
