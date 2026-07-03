#include "pch.h"
#include "TimeManager.h"

namespace TimeManager
{
    using QPC_t = BOOL(WINAPI*)(LARGE_INTEGER*);

    static QPC_t s_originalQPC = nullptr;
    static bool  s_created = false;
    static bool  s_installed = false;

    static SRWLOCK s_lock = SRWLOCK_INIT;
    // Virtual clock state: virt(t) = anchorVirt + (realQPC(t) - anchorReal) * scale
    static LONGLONG s_anchorReal = 0;
    static LONGLONG s_anchorVirt = 0;
    static double   s_scale = 1.0;

    static LONGLONG s_qpcFrequency = 0;

    static std::atomic<uint64_t> s_cntQpc{ 0 };

    static BOOL WINAPI Hook_QueryPerformanceCounter(LARGE_INTEGER* lpCount)
    {
        LARGE_INTEGER real;
        BOOL ok = s_originalQPC(&real);
        if (!ok || !lpCount) return ok;

        s_cntQpc.fetch_add(1, std::memory_order_relaxed);

        AcquireSRWLockShared(&s_lock);
        const LONGLONG anchorReal = s_anchorReal;
        const LONGLONG anchorVirt = s_anchorVirt;
        const double scale = s_scale;
        ReleaseSRWLockShared(&s_lock);

        lpCount->QuadPart = anchorVirt +
            static_cast<LONGLONG>(static_cast<double>(real.QuadPart - anchorReal) * scale);
        return ok;
    }

    bool Install()
    {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        s_qpcFrequency = freq.QuadPart;

        MH_STATUS status = MH_CreateHook(
            reinterpret_cast<void*>(&QueryPerformanceCounter),
            reinterpret_cast<void*>(&Hook_QueryPerformanceCounter),
            reinterpret_cast<void**>(&s_originalQPC));
        if (status != MH_OK) {
            spdlog::error("[Time] MH_CreateHook(QueryPerformanceCounter) failed: {}", MH_StatusToString(status));
            return false;
        }

        // Anchor virtual clock == real clock; hook is not active yet
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        s_anchorReal = now.QuadPart;
        s_anchorVirt = now.QuadPart;
        s_scale = 1.0;

        s_created = true;
        spdlog::info("[Time] QPC hook created (enable pending), freq={}", s_qpcFrequency);
        return true;
    }

    void OnHooksEnabled()
    {
        if (!s_created) return;
        s_installed = true;
        spdlog::info("[Time] QPC hook active - slow motion available");
    }

    void SetTimeScale(float scale)
    {
        if (!s_installed || !s_originalQPC) return;

        double clamped = std::clamp(static_cast<double>(scale), 0.05, 1.0);

        LARGE_INTEGER real;
        s_originalQPC(&real);

        AcquireSRWLockExclusive(&s_lock);
        if (clamped != s_scale) {
            // Re-anchor so the virtual clock stays continuous and monotonic
            const LONGLONG virtNow = s_anchorVirt +
                static_cast<LONGLONG>(static_cast<double>(real.QuadPart - s_anchorReal) * s_scale);
            s_anchorReal = real.QuadPart;
            s_anchorVirt = virtNow;
            s_scale = clamped;
        }
        ReleaseSRWLockExclusive(&s_lock);

        spdlog::debug("[Time] TimeScale set to {:.2f}", clamped);
    }

    float GetTimeScale()
    {
        AcquireSRWLockShared(&s_lock);
        const double scale = s_scale;
        ReleaseSRWLockShared(&s_lock);
        return static_cast<float>(scale);
    }

    double RealSeconds()
    {
        LARGE_INTEGER now;
        if (s_originalQPC) {
            s_originalQPC(&now);
        } else {
            QueryPerformanceCounter(&now);
        }
        if (s_qpcFrequency == 0) {
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            s_qpcFrequency = freq.QuadPart;
        }
        return static_cast<double>(now.QuadPart) / static_cast<double>(s_qpcFrequency);
    }

    bool IsInstalled()
    {
        return s_installed;
    }

    uint64_t GetAndResetQpcCount()
    {
        return s_cntQpc.exchange(0, std::memory_order_relaxed);
    }
}
