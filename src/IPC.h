#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <filesystem>

namespace IPC
{
    enum class EventType
    {
        Kill,
        CriticalHit,
        Hit,
        DashStart,
        DashEnd,
        SpellCast,
        ActionStart,
        ActionStartRanged,
        ActionEnd,
        Unknown
    };

    struct CombatEvent
    {
        EventType type = EventType::Unknown;
        float targetX = 0.f;
        float targetY = 0.f;
        float targetZ = 0.f;
        bool  hasPosition = false;
        double timestamp = 0.0;
    };

    // Initialize the IPC reader (sets up the file paths)
    void Init();

    // Poll for new events from the Script Extender Lua mod.
    // Returns any events not yet processed. Handles session restarts
    // (Lua counter reset) via the session id in the file.
    std::vector<CombatEvent> Poll();

    // Poll the MCM settings file. Returns true and applies overrides to
    // CinematicSettings when the file changed since the last call.
    bool PollSettings();

    // Get the event file path
    const std::filesystem::path& GetEventFilePath();
}
