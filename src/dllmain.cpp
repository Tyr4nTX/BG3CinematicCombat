#include "pch.h"
#include "Hooks.h"
#include "Settings.h"
#include "IPC.h"
#include "CinematicManager.h"
#include "CameraController.h"
#include "TimeManager.h"

static std::filesystem::path GetThisDllDirectory()
{
    char path[MAX_PATH];
    HMODULE hModule = nullptr;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetThisDllDirectory),
        &hModule);
    GetModuleFileNameA(hModule, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

static void InitializeLog(const std::filesystem::path& dllDir)
{
    auto logPath = dllDir / "BG3CinematicCombat.log";
    try {
        auto logger = spdlog::basic_logger_mt("main", logPath.string(), true);
        spdlog::set_default_logger(logger);
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
        spdlog::info("=== BG3 Cinematic Combat v1.0.0 ===");
        spdlog::info("Log initialized at {}", logPath.string());
    } catch (const spdlog::spdlog_ex& e) {
        // Can't log, but don't crash
        OutputDebugStringA(e.what());
    }
}

static DWORD WINAPI InitThread(LPVOID)
{
    auto dllDir = GetThisDllDirectory();

    // 1. Set up logging
    InitializeLog(dllDir);

    // 2. Load configuration
    auto configPath = dllDir / "BG3CinematicCombat.toml";
    CinematicSettings::Get().Load(configPath);

    // Apply log level from config
    auto& settings = CinematicSettings::Get();
    spdlog::set_level(static_cast<spdlog::level::level_enum>(settings.logLevel));

    // Kill switch: with Enabled=false in the TOML, install NOTHING.
    // Lets us verify whether startup crashes come from this mod at all.
    if (!settings.enabled) {
        spdlog::warn("[DLL] Enabled=false in TOML - no hooks installed, mod inactive");
        return 0;
    }

    // 3. Initialize IPC
    IPC::Init();

    // 4. Initialize MinHook
    MH_STATUS mhStatus = MH_Initialize();
    if (mhStatus != MH_OK) {
        spdlog::critical("[DLL] MH_Initialize failed: {}", MH_StatusToString(mhStatus));
        return 1;
    }
    spdlog::info("[DLL] MinHook initialized");

    // 5. Create game hooks (not enabled yet)
    if (!Hooks::Install()) {
        spdlog::critical("[DLL] Failed to install hooks - cinematic features disabled");
        MH_Uninitialize();
        return 1;
    }

    // 6. Create time hook for slow motion (non-fatal if it fails)
    bool timeHookCreated = TimeManager::Install();
    if (!timeHookCreated) {
        spdlog::warn("[DLL] Time hook failed - slow motion disabled");
    }

    // 7. Wait out the startup storm before patching: other native mods (WASD)
    // install their hooks in the first seconds too, and the loader spawns
    // threads constantly - patching hot functions in that window is a
    // crash lottery. 10s later the process is far calmer.
    spdlog::info("[DLL] Waiting 10s before enabling hooks (startup safety window)...");
    Sleep(10000);

    // 8. Enable everything in ONE batch = a single thread-suspension window.
    MH_STATUS enableStatus = MH_EnableHook(MH_ALL_HOOKS);
    if (enableStatus != MH_OK) {
        spdlog::critical("[DLL] MH_EnableHook(ALL) failed: {}", MH_StatusToString(enableStatus));
        MH_Uninitialize();
        return 1;
    }
    if (timeHookCreated) {
        TimeManager::OnHooksEnabled();
    }

    spdlog::info("[DLL] BG3 Cinematic Combat fully initialized!");
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);
            // Initialize on a separate thread to avoid loader lock issues
            CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);
            break;

        case DLL_PROCESS_DETACH:
            Hooks::Uninstall();
            MH_Uninitialize();
            spdlog::info("[DLL] BG3 Cinematic Combat unloaded");
            spdlog::shutdown();
            break;
    }
    return TRUE;
}
