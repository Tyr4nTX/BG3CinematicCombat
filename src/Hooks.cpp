#include "pch.h"
#include "Hooks.h"
#include "CinematicManager.h"
#include "CameraController.h"
#include "TimeManager.h"

namespace Hooks
{
    // ---- Globals ----
    UpdateCamera_t               OriginalUpdateCamera = nullptr;
    CalculateCameraPitch_t       OriginalCalculateCameraPitch = nullptr;
    GetCurrentCameraDefinition_t GetCurrentCameraDefinition = nullptr;
    RE::UnkObject**              g_unkCameraSingletonPtr = nullptr;

    // ---- AOB Patterns (verified against game exe 2026-07-01, both Vulkan and DX11) ----
    // From ersh1/BG3_NativeCameraTweaks (GPL-3.0) analysis

    // GetCurrentCameraDefinition function start:
    // MOV RAX, [singleton]; CMP BYTE PTR [RAX+0x1332], 0; JZ +7
    static constexpr const char* PAT_GetCurrentCameraDef =
        "48 8B 05 ?? ?? ?? ?? 80 B8 32 13 00 00 00 74 07";

    // UpdateCamera call site: CALL UpdateCamera; LEA RCX, [rbp+4F8]; CALL ...; JMP
    static constexpr const char* PAT_UpdateCamera =
        "E8 ?? ?? ?? ?? 48 8D 8D F8 04 00 00 E8 ?? ?? ?? ?? E9 AF FD FF FF";

    // CalculateCameraPitch call site: CALL CalculateCameraPitch; CMP BYTE PTR [RDI+0x14C], 0
    static constexpr const char* PAT_CalculateCameraPitch =
        "E8 ?? ?? ?? ?? 80 BF 4C 01 00 00 00";

    // Diagnostics
    static uint64_t s_frameCount = 0;
    static uint64_t s_tickExceptions = 0;
    static bool s_loggedFirstUpdate = false;
    static bool s_loggedFirstPitch = false;
    static bool s_loggedNullCamera = false;

    // ---- SEH-guarded helpers ----
    // catch(...) does NOT catch access violations with /EHsc; these __try wrappers do.
    // Functions containing __try must not have locals with destructors.

    static RE::CameraObject* FetchCameraSafe(RE::UnkObject* obj)
    {
        __try {
            return obj ? obj->currentCameraObject : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    static RE::CameraDefinition* GetDefinitionSafe(RE::CameraObject* cam)
    {
        __try {
            return (GetCurrentCameraDefinition && cam) ? GetCurrentCameraDefinition(cam) : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    // Analysis mode: every ~0.5s, log which 4-byte slots of the CameraObject
    // changed. Move/zoom/rotate the camera ingame and the changing offsets
    // reveal the real field layout (e.g. where the position anchor lives).
    static void TraceCameraDiff(RE::CameraObject* cam)
    {
        static uint8_t s_snap[0x260];
        static bool s_snapValid = false;
        static int s_counter = 0;
        static RE::CameraObject* s_lastCam = nullptr;

        if (++s_counter % 30 != 0) return;

        const uint8_t* p = reinterpret_cast<const uint8_t*>(cam);
        if (!s_snapValid || s_lastCam != cam) {
            memcpy(s_snap, p, sizeof(s_snap));
            s_snapValid = true;
            s_lastCam = cam;
            spdlog::info("[CamDiff] tracing camera object {:X}", reinterpret_cast<uintptr_t>(cam));
            return;
        }

        std::string diffs;
        int count = 0;
        for (int off = 0; off < 0x260 && count < 24; off += 4) {
            float oldF, newF;
            memcpy(&oldF, s_snap + off, 4);
            memcpy(&newF, p + off, 4);
            if (oldF != newF) {
                char buf[80];
                snprintf(buf, sizeof(buf), " %X:%.2f->%.2f", off, oldF, newF);
                diffs += buf;
                count++;
            }
        }
        memcpy(s_snap, p, sizeof(s_snap));
        if (!diffs.empty()) {
            spdlog::info("[CamDiff]{}", diffs);
        }
    }

    static void DoTick(RE::CameraObject* cam, RE::CameraDefinition* def)
    {
        CinematicManager::Get().Tick(cam, def);
        CameraController::Get().Apply(cam, def);

        if (CinematicSettings::Get().traceCamera) {
            TraceCameraDiff(cam);
        }
    }

    static void TickSafe(RE::CameraObject* cam, RE::CameraDefinition* def)
    {
        __try {
            DoTick(cam, def);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_tickExceptions++;
            if (s_tickExceptions == 1 || s_tickExceptions % 600 == 0) {
                spdlog::error("[Hook] Exception in cinematic tick (total: {})", s_tickExceptions);
            }
        }
    }

    // ---- Detours ----

    static void PreApplySafe(RE::CameraObject* cam)
    {
        __try {
            CameraController::Get().PreApply(cam);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    static void Hook_UpdateCamera(void* a1, void* a2, void* a3, void* a4)
    {
        // Feed the pan position in BEFORE the game's camera update so the
        // follow logic integrates from our values instead of overwriting them
        RE::CameraObject* preCam = FetchCameraSafe(reinterpret_cast<RE::UnkObject*>(a4));
        if (preCam) {
            PreApplySafe(preCam);
        }

        // Let the game compute normal camera state
        OriginalUpdateCamera(a1, a2, a3, a4);

        s_frameCount++;

        // Unconditional proof-of-life logging for the first calls
        if (!s_loggedFirstUpdate) {
            s_loggedFirstUpdate = true;
            spdlog::info("[Hook] UpdateCamera detour is firing! a4={:X} singleton={:X}",
                reinterpret_cast<uintptr_t>(a4),
                g_unkCameraSingletonPtr ? reinterpret_cast<uintptr_t>(*g_unkCameraSingletonPtr) : 0);
        }

        // a4 is the camera UnkObject in the known game versions; fall back to
        // the resolved singleton if it doesn't yield a camera.
        RE::CameraObject* cam = FetchCameraSafe(reinterpret_cast<RE::UnkObject*>(a4));
        if (!cam && g_unkCameraSingletonPtr) {
            cam = FetchCameraSafe(*g_unkCameraSingletonPtr);
        }

        if (!cam) {
            if (!s_loggedNullCamera) {
                s_loggedNullCamera = true;
                spdlog::warn("[Hook] frame={}: camera object is NULL (will log once)", s_frameCount);
            }
            return;
        }

        if (s_frameCount % 900 == 0) {
            spdlog::debug("[Hook] frame={} zoom={:.2f} flags={:X} state={} qpcCalls={}",
                s_frameCount, cam->desiredZoom, (uint32_t)cam->cameraModeFlags,
                CinematicManager::Get().GetStateName(),
                TimeManager::GetAndResetQpcCount());
        }

        RE::CameraDefinition* def = GetDefinitionSafe(cam);
        TickSafe(cam, def);
    }

    static float Hook_CalculateCameraPitch(RE::CameraObject* cam, uint8_t a2, uint8_t a3)
    {
        float pitch = OriginalCalculateCameraPitch(cam, a2, a3);

        if (!s_loggedFirstPitch) {
            s_loggedFirstPitch = true;
            spdlog::info("[Hook] CalculateCameraPitch detour is firing! game pitch={:.2f} deg", pitch);
        }

        return CameraController::Get().FilterPitch(pitch);
    }

    // ---- Install / Uninstall ----

    // Creates the hook but does NOT enable it. All hooks are enabled together
    // in one MH_EnableHook(MH_ALL_HOOKS) call (single thread-suspension window;
    // enabling hooks one by one during loading caused a crash race).
    static bool HookCallTarget(const char* name, const char* pattern,
                               void* detour, void** originalOut)
    {
        auto callSite = Pattern::Scan(pattern);
        if (!callSite) {
            spdlog::error("[Hooks] Failed to find {} call site pattern", name);
            return false;
        }
        uintptr_t target = Pattern::ResolveCall(*callSite);
        spdlog::info("[Hooks] {} call site {:X} -> function {:X}", name, *callSite, target);

        MH_STATUS status = MH_CreateHook(reinterpret_cast<void*>(target), detour, originalOut);
        if (status != MH_OK) {
            spdlog::error("[Hooks] MH_CreateHook({}) failed: {}", name, MH_StatusToString(status));
            return false;
        }
        return true;
    }

    bool Install()
    {
        spdlog::info("[Hooks] Scanning for patterns...");

        // --- GetCurrentCameraDefinition + camera singleton ---
        // The pattern is the function start itself (MOV RAX,[rip+off] is the
        // first instruction); the singleton address comes from that instruction.
        auto camDefAddr = Pattern::Scan(PAT_GetCurrentCameraDef);
        if (!camDefAddr) {
            spdlog::error("[Hooks] Failed to find GetCurrentCameraDefinition pattern");
            return false;
        }
        uintptr_t singletonPtrAddr = Pattern::ResolveRipRelative(*camDefAddr, 7);
        g_unkCameraSingletonPtr = reinterpret_cast<RE::UnkObject**>(singletonPtrAddr);
        GetCurrentCameraDefinition = reinterpret_cast<GetCurrentCameraDefinition_t>(*camDefAddr);
        spdlog::info("[Hooks] GetCurrentCameraDefinition at {:X}, singleton ptr at {:X}",
            *camDefAddr, singletonPtrAddr);

        // --- UpdateCamera (per-frame camera processing) ---
        if (!HookCallTarget("UpdateCamera", PAT_UpdateCamera,
                reinterpret_cast<void*>(&Hook_UpdateCamera),
                reinterpret_cast<void**>(&OriginalUpdateCamera))) {
            return false;
        }

        // --- CalculateCameraPitch (pitch override point) ---
        // Not fatal if missing: zoom/pan/fov still work without pitch control.
        if (!HookCallTarget("CalculateCameraPitch", PAT_CalculateCameraPitch,
                reinterpret_cast<void*>(&Hook_CalculateCameraPitch),
                reinterpret_cast<void**>(&OriginalCalculateCameraPitch))) {
            spdlog::warn("[Hooks] Pitch hook unavailable - cinematic pitch disabled");
        }

        spdlog::info("[Hooks] Game hooks created (enable pending)");
        return true;
    }

    void Uninstall()
    {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(MH_ALL_HOOKS);
        spdlog::info("[Hooks] All hooks removed");
    }
}
