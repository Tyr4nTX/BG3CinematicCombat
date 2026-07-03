#include "pch.h"
#include "IPC.h"
#include "Settings.h"

namespace IPC
{
    static std::filesystem::path s_eventFilePath;
    static std::filesystem::path s_settingsFilePath;
    static std::mutex            s_mutex;

    // Event dedup state. The Lua side writes a rolling buffer of recent
    // events tagged with a per-session id and a monotonic counter.
    static double s_lastSession   = -1.0;
    static double s_lastTimestamp = 0.0;

    static std::filesystem::file_time_type s_lastEventWrite{};
    static std::filesystem::file_time_type s_lastSettingsWrite{};
    static bool s_hasEventWrite = false;
    static bool s_hasSettingsWrite = false;

    void Init()
    {
        // SE Lua writes into the "Script Extender" folder under the game's %LOCALAPPDATA% dir
        char* localAppData = nullptr;
        size_t len = 0;
        if (_dupenv_s(&localAppData, &len, "LOCALAPPDATA") == 0 && localAppData) {
            auto seDir = std::filesystem::path(localAppData)
                / "Larian Studios" / "Baldur's Gate 3" / "Script Extender";
            s_eventFilePath    = seDir / "CinematicCombat_events.json";
            s_settingsFilePath = seDir / "CinematicCombat_settings.json";
            free(localAppData);
        }
        spdlog::info("[IPC] Event file:    {}", s_eventFilePath.string());
        spdlog::info("[IPC] Settings file: {}", s_settingsFilePath.string());
    }

    const std::filesystem::path& GetEventFilePath()
    {
        return s_eventFilePath;
    }

    std::vector<CombatEvent> Poll()
    {
        std::lock_guard lock(s_mutex);
        std::vector<CombatEvent> events;

        if (s_eventFilePath.empty()) return events;

        std::error_code ec;
        if (!std::filesystem::exists(s_eventFilePath, ec)) return events;

        // Skip parsing when the file has not changed since the last poll
        auto writeTime = std::filesystem::last_write_time(s_eventFilePath, ec);
        if (!ec) {
            if (s_hasEventWrite && writeTime == s_lastEventWrite) return events;
            s_lastEventWrite = writeTime;
            s_hasEventWrite = true;
        }

        try {
            std::ifstream file(s_eventFilePath);
            if (!file.is_open()) return events;

            nlohmann::json j;
            file >> j;
            file.close();

            double session = j.value("session", 0.0);

            // First read after DLL start: whatever sits in the file was
            // written by a PREVIOUS game run. Consume it silently - replaying
            // it caused kill cams (with HUD toggle + slow motion) in the main
            // menu and during loading screens.
            if (s_lastSession < 0.0) {
                s_lastSession = session;
                s_lastTimestamp = 0.0;
                size_t discarded = 0;
                if (j.contains("events") && j["events"].is_array()) {
                    for (auto& evt : j["events"]) {
                        s_lastTimestamp = std::max(s_lastTimestamp, evt.value("time", 0.0));
                        discarded++;
                    }
                }
                spdlog::info("[IPC] Discarded {} stale events from a previous run (session {})",
                    discarded, session);
                return events;
            }

            // Session change (save reload / new game in the same process):
            // the Lua counter restarts, so reset our dedup watermark.
            if (session != s_lastSession) {
                spdlog::info("[IPC] New Lua session detected ({} -> {}), resetting event watermark",
                    s_lastSession, session);
                s_lastSession = session;
                s_lastTimestamp = 0.0;
            }

            if (!j.contains("events") || !j["events"].is_array()) return events;

            double maxSeen = s_lastTimestamp;
            for (auto& evt : j["events"]) {
                CombatEvent ce;
                ce.timestamp = evt.value("time", 0.0);

                // Skip already-processed events
                if (ce.timestamp <= s_lastTimestamp) continue;

                std::string typeStr = evt.value("type", "");
                if (typeStr == "kill")           ce.type = EventType::Kill;
                else if (typeStr == "crit")      ce.type = EventType::CriticalHit;
                else if (typeStr == "hit")       ce.type = EventType::Hit;
                else if (typeStr == "dash")      ce.type = EventType::DashStart;
                else if (typeStr == "spell")     ce.type = EventType::SpellCast;
                else if (typeStr == "action")    ce.type = EventType::ActionStart;
                else if (typeStr == "actionr")   ce.type = EventType::ActionStartRanged;
                else if (typeStr == "actionend") ce.type = EventType::ActionEnd;
                else                             ce.type = EventType::Unknown;

                ce.targetX = evt.value("x", 0.f);
                ce.targetY = evt.value("y", 0.f);
                ce.targetZ = evt.value("z", 0.f);
                ce.hasPosition = (ce.targetX != 0.f || ce.targetY != 0.f || ce.targetZ != 0.f);

                maxSeen = std::max(maxSeen, ce.timestamp);
                events.push_back(ce);
            }

            if (!events.empty()) {
                s_lastTimestamp = maxSeen;
                spdlog::debug("[IPC] Read {} new events (watermark {})", events.size(), s_lastTimestamp);
            }

        } catch (const std::exception& e) {
            spdlog::warn("[IPC] Error reading events: {}", e.what());
        }

        return events;
    }

    bool PollSettings()
    {
        std::lock_guard lock(s_mutex);

        if (s_settingsFilePath.empty()) return false;

        std::error_code ec;
        if (!std::filesystem::exists(s_settingsFilePath, ec)) return false;

        auto writeTime = std::filesystem::last_write_time(s_settingsFilePath, ec);
        if (ec) return false;
        if (s_hasSettingsWrite && writeTime == s_lastSettingsWrite) return false;

        try {
            std::ifstream file(s_settingsFilePath);
            if (!file.is_open()) return false;

            nlohmann::json j;
            file >> j;
            file.close();

            // Only mark as consumed after a successful parse, so a partially
            // written file is retried on the next poll.
            s_lastSettingsWrite = writeTime;
            s_hasSettingsWrite = true;

            if (j.is_object()) {
                CinematicSettings::Get().ApplyJsonOverrides(j);
                return true;
            }
        } catch (const std::exception& e) {
            spdlog::debug("[IPC] Settings file not ready yet: {}", e.what());
        }
        return false;
    }
}
