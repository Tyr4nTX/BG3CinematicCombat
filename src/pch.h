#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <Psapi.h>

#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <optional>
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <filesystem>
#include <fstream>
#include <functional>
#include <algorithm>
#include <cmath>
#include <chrono>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <toml++/toml.hpp>
#include <nlohmann/json.hpp>
#include <MinHook.h>
