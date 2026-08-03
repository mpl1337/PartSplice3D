#include "safe_temp.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

namespace {

std::wstring secureToken() {
    std::array<unsigned char, 16> bytes{};
#ifdef _WIN32
    const NTSTATUS status = BCryptGenRandom(nullptr, bytes.data(),
        static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) throw std::runtime_error("Sicherer Windows-Zufallsgenerator ist nicht verfügbar.");
#else
    std::random_device random;
    for (unsigned char& value : bytes) value = static_cast<unsigned char>(random());
#endif
    std::wostringstream text;
    text << std::hex << std::setfill(L'0');
    for (const unsigned char value : bytes) text << std::setw(2) << static_cast<unsigned int>(value);
    return text.str();
}

bool reserveFile(const std::filesystem::path& path, std::error_code& error) {
#ifdef _WIN32
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                CREATE_NEW,
                                FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED |
                                    FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    CloseHandle(handle);
    error.clear();
    return true;
#else
    if (std::filesystem::exists(path, error)) return false;
    std::ofstream stream(path, std::ios::binary | std::ios::out);
    if (!stream) {
        error = std::make_error_code(std::errc::io_error);
        return false;
    }
    error.clear();
    return true;
#endif
}

std::filesystem::path reserveIn(const std::filesystem::path& directory,
                                std::wstring_view prefix,
                                std::wstring_view extension) {
    for (int attempt = 0; attempt < 32; ++attempt) {
        const std::filesystem::path candidate = directory /
            (std::wstring(prefix) + secureToken() + std::wstring(extension));
        std::error_code error;
        if (reserveFile(candidate, error)) return candidate;
#ifdef _WIN32
        if (error.value() != ERROR_FILE_EXISTS && error.value() != ERROR_ALREADY_EXISTS)
            throw std::filesystem::filesystem_error(
                "Temporäre Datei konnte nicht reserviert werden", candidate, error);
#else
        if (error != std::errc::file_exists)
            throw std::filesystem::filesystem_error(
                "Temporäre Datei konnte nicht reserviert werden", candidate, error);
#endif
    }
    throw std::runtime_error("Es konnte kein eindeutiger temporärer Dateiname reserviert werden.");
}

}  // namespace

std::filesystem::path reserveSecureTemporarySibling(const std::filesystem::path& target) {
    const std::filesystem::path directory = target.has_parent_path()
        ? target.parent_path() : std::filesystem::current_path();
    std::wstring base = target.filename().wstring();
    if (base.empty()) base = L"PartSplice3D";
    return reserveIn(directory, L"." + base + L".", L".tmp");
}

std::filesystem::path reserveSecureTemporaryFile(const std::filesystem::path& directory,
                                                  std::wstring_view prefix,
                                                  std::wstring_view extension) {
    return reserveIn(directory, prefix, extension);
}

std::filesystem::path createSecureTemporaryDirectory(const std::filesystem::path& parent,
                                                      std::wstring_view prefix) {
    for (int attempt = 0; attempt < 32; ++attempt) {
        const std::filesystem::path candidate = parent / (std::wstring(prefix) + secureToken());
        std::error_code error;
        if (std::filesystem::create_directory(candidate, error)) return candidate;
        if (error && error != std::errc::file_exists)
            throw std::filesystem::filesystem_error(
                "Temporärer Ordner konnte nicht erstellt werden", candidate, error);
    }
    throw std::runtime_error("Es konnte kein eindeutiger temporärer Ordner erstellt werden.");
}
