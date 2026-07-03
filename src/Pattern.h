#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <Windows.h>
#include <Psapi.h>

namespace Pattern
{
    struct ModuleInfo
    {
        uintptr_t base;
        size_t    size;
    };

    inline ModuleInfo GetMainModule()
    {
        HMODULE hModule = GetModuleHandleA(nullptr);
        MODULEINFO mi{};
        GetModuleInformation(GetCurrentProcess(), hModule, &mi, sizeof(mi));
        return { reinterpret_cast<uintptr_t>(mi.lpBaseOfDll), mi.SizeOfImage };
    }

    // Parse AOB string like "48 8B 05 ?? ?? ?? ?? 80" into bytes + mask
    inline std::pair<std::vector<uint8_t>, std::vector<bool>> Parse(const char* pattern)
    {
        std::vector<uint8_t> bytes;
        std::vector<bool>    mask;  // true = must match, false = wildcard

        for (const char* p = pattern; *p; ++p) {
            if (*p == ' ') continue;
            if (*p == '?') {
                bytes.push_back(0);
                mask.push_back(false);
                if (*(p + 1) == '?') ++p;
            } else {
                char hex[3] = { p[0], p[1], 0 };
                bytes.push_back(static_cast<uint8_t>(strtoul(hex, nullptr, 16)));
                mask.push_back(true);
                ++p;
            }
        }
        return { bytes, mask };
    }

    // Scan for pattern in main module. Returns first match or nullopt.
    inline std::optional<uintptr_t> Scan(const char* pattern)
    {
        auto [bytes, mask] = Parse(pattern);
        auto [base, size]  = GetMainModule();

        const uint8_t* start = reinterpret_cast<const uint8_t*>(base);
        const uint8_t* end   = start + size - bytes.size();

        for (const uint8_t* addr = start; addr < end; ++addr) {
            bool found = true;
            for (size_t i = 0; i < bytes.size(); ++i) {
                if (mask[i] && addr[i] != bytes[i]) {
                    found = false;
                    break;
                }
            }
            if (found) {
                return reinterpret_cast<uintptr_t>(addr);
            }
        }
        return std::nullopt;
    }

    // Resolve a relative CALL instruction (E8 xx xx xx xx) to its target address
    inline uintptr_t ResolveCall(uintptr_t callAddr)
    {
        int32_t offset = *reinterpret_cast<int32_t*>(callAddr + 1);
        return callAddr + 5 + offset;
    }

    // Resolve a RIP-relative MOV (48 8B 05/0D/15/1D/25/2D/35/3D xx xx xx xx) to its target
    inline uintptr_t ResolveRipRelative(uintptr_t instrAddr, int32_t instrLength = 7)
    {
        int32_t offset = *reinterpret_cast<int32_t*>(instrAddr + instrLength - 4);
        return instrAddr + instrLength + offset;
    }
}
