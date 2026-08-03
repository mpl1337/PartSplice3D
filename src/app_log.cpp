#include "app_log.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

constexpr uintmax_t kMaximumLogBytes = 1024u * 1024u;
constexpr size_t kMaximumMessageBytes = 16u * 1024u;
std::mutex gLogMutex;

std::filesystem::path localDataRoot() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) {
        buffer.resize(length);
        return std::filesystem::path(buffer);
    }
#endif
    std::error_code error;
    const std::filesystem::path temporary = std::filesystem::temp_directory_path(error);
    return error ? std::filesystem::current_path() : temporary;
}

std::filesystem::path logDirectoryInternal() {
    return localDataRoot() / L"PartSplice3D" / L"logs";
}

std::filesystem::path logPath(unsigned generation = 0) {
    const std::wstring suffix = generation == 0
        ? L".log" : L"." + std::to_wstring(generation) + L".log";
    return logDirectoryInternal() / (L"PartSplice3D" + suffix);
}

void rotateIfNeeded() {
    std::error_code error;
    const std::filesystem::path current = logPath();
    const uintmax_t size = std::filesystem::file_size(current, error);
    if (error || size < kMaximumLogBytes) return;
    std::filesystem::remove(logPath(3), error);
    error.clear();
    if (std::filesystem::exists(logPath(2), error)) {
        error.clear();
        std::filesystem::rename(logPath(2), logPath(3), error);
    }
    error.clear();
    if (std::filesystem::exists(logPath(1), error)) {
        error.clear();
        std::filesystem::rename(logPath(1), logPath(2), error);
    }
    error.clear();
    std::filesystem::rename(current, logPath(1), error);
}

std::string timestamp() {
    const std::time_t now = std::chrono::system_clock::to_time_t(
        std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::ostringstream value;
    value << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return value.str();
}

void writeLog(std::string_view level, std::string_view message) noexcept {
    try {
        std::lock_guard lock(gLogMutex);
        std::error_code error;
        std::filesystem::create_directories(logDirectoryInternal(), error);
        if (error) return;
        rotateIfNeeded();
        std::string clean(message.substr(0, kMaximumMessageBytes));
        std::replace(clean.begin(), clean.end(), '\r', ' ');
        std::replace(clean.begin(), clean.end(), '\n', ' ');
        std::ofstream output(logPath(), std::ios::app | std::ios::binary);
        if (output) output << timestamp() << " [" << level << "] " << clean << '\n';
    } catch (...) {
        // Logging must never interfere with model editing or shutdown.
    }
}

} // namespace

void initializeAppLog() noexcept {
    writeLog("INFO", "PartSplice 3D gestartet.");
}

void appLogInfo(std::string_view message) noexcept { writeLog("INFO", message); }
void appLogWarning(std::string_view message) noexcept { writeLog("WARN", message); }
void appLogError(std::string_view message) noexcept { writeLog("ERROR", message); }

void appLogSystemError(std::string_view context, unsigned long errorCode) noexcept {
    try {
        std::string message(context);
        message += " (Systemfehler " + std::to_string(errorCode) + ")";
#ifdef _WIN32
        char* systemText = nullptr;
        const DWORD length = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, static_cast<DWORD>(errorCode), 0,
            reinterpret_cast<char*>(&systemText), 0, nullptr);
        if (length > 0 && systemText != nullptr) {
            std::string detail(systemText, length);
            while (!detail.empty() &&
                   (detail.back() == '\r' || detail.back() == '\n' || detail.back() == ' '))
                detail.pop_back();
            if (!detail.empty()) message += ": " + detail;
        }
        if (systemText != nullptr) LocalFree(systemText);
#endif
        writeLog("ERROR", message);
    } catch (...) {
        writeLog("ERROR", context);
    }
}

std::filesystem::path appLogDirectory() noexcept {
    try { return logDirectoryInternal(); } catch (...) { return {}; }
}

std::filesystem::path currentAppLogPath() noexcept {
    try { return logPath(); } catch (...) { return {}; }
}
