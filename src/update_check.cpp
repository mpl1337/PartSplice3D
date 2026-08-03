#include "update_check.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace {

constexpr std::string_view kReleasePage =
    "https://github.com/mpl1337/PartSplice3D/releases";

std::optional<std::array<int, 3>> parseVersion(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    if (!text.empty() && (text.front() == 'v' || text.front() == 'V')) text.remove_prefix(1);

    std::array<int, 3> version{};
    size_t position = 0;
    for (size_t component = 0; component < version.size(); ++component) {
        if (position >= text.size() || !std::isdigit(static_cast<unsigned char>(text[position])))
            return std::nullopt;
        int value = 0;
        while (position < text.size() &&
               std::isdigit(static_cast<unsigned char>(text[position]))) {
            const int digit = text[position++] - '0';
            if (value > (std::numeric_limits<int>::max() - digit) / 10) return std::nullopt;
            value = value * 10 + digit;
        }
        version[component] = value;
        if (component + 1 < version.size()) {
            if (position < text.size() && text[position] == '.') {
                ++position;
            } else {
                break;
            }
        }
    }
    if (position < text.size() && text[position] != '-' && text[position] != '+' &&
        !std::isspace(static_cast<unsigned char>(text[position]))) return std::nullopt;
    return version;
}

std::optional<std::string> jsonStringField(const std::string& json, std::string_view field) {
    const std::string key = "\"" + std::string(field) + "\"";
    size_t position = json.find(key);
    if (position == std::string::npos) return std::nullopt;
    position = json.find(':', position + key.size());
    if (position == std::string::npos) return std::nullopt;
    position = json.find('"', position + 1);
    if (position == std::string::npos) return std::nullopt;
    ++position;

    std::string value;
    while (position < json.size()) {
        const char character = json[position++];
        if (character == '"') return value;
        if (character != '\\') {
            value.push_back(character);
            continue;
        }
        if (position >= json.size()) return std::nullopt;
        const char escaped = json[position++];
        switch (escaped) {
        case '"': value.push_back('"'); break;
        case '\\': value.push_back('\\'); break;
        case '/': value.push_back('/'); break;
        case 'b': value.push_back('\b'); break;
        case 'f': value.push_back('\f'); break;
        case 'n': value.push_back('\n'); break;
        case 'r': value.push_back('\r'); break;
        case 't': value.push_back('\t'); break;
        default: return std::nullopt;
        }
    }
    return std::nullopt;
}

UpdateCheckResult failure(std::string message) {
    UpdateCheckResult result;
    result.state = UpdateCheckState::Failed;
    result.message = std::move(message);
    return result;
}

bool validReleaseUrl(std::string_view url) {
    if (!url.starts_with(kReleasePage)) return false;
    if (url.size() == kReleasePage.size()) return true;
    const char boundary = url[kReleasePage.size()];
    return boundary == '/' || boundary == '?' || boundary == '#';
}

#ifdef _WIN32
class InternetHandle {
public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET value) : handle_(value) {}
    ~InternetHandle() { if (handle_) WinHttpCloseHandle(handle_); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    operator HINTERNET() const { return handle_; }
    explicit operator bool() const { return handle_ != nullptr; }
private:
    HINTERNET handle_ = nullptr;
};
#endif

} // namespace

bool isNewerPartSpliceVersion(const std::string& candidate, const std::string& current) {
    const auto candidateVersion = parseVersion(candidate);
    const auto currentVersion = parseVersion(current);
    return candidateVersion && currentVersion && *candidateVersion > *currentVersion;
}

UpdateCheckResult checkForPartSpliceUpdate() {
#ifndef _WIN32
    return failure("Updateprüfung ist auf diesem Betriebssystem nicht verfügbar.");
#else
    InternetHandle session(WinHttpOpen(
        L"PartSplice3D/" PARTSPLICE_VERSION_WSTRING, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) return failure("Netzwerkdienst für die Updateprüfung konnte nicht geöffnet werden.");
    WinHttpSetTimeouts(session, 4000, 4000, 6000, 6000);

    InternetHandle connection(WinHttpConnect(
        session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!connection) return failure("Verbindung zu api.github.com konnte nicht aufgebaut werden.");

    InternetHandle request(WinHttpOpenRequest(
        connection, L"GET", L"/repos/mpl1337/PartSplice3D/releases/latest",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    if (!request) return failure("GitHub-Anfrage konnte nicht erstellt werden.");

    DWORD securityFeatures = WINHTTP_ENABLE_SSL_REVOCATION;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_ENABLE_FEATURE,
                          &securityFeatures, sizeof(securityFeatures))) {
        return failure("TLS-Sicherheitsprüfung konnte nicht aktiviert werden.");
    }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY,
                          &redirectPolicy, sizeof(redirectPolicy))) {
        return failure("Sichere Weiterleitungsregeln konnten nicht aktiviert werden.");
    }

    constexpr wchar_t headers[] =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    constexpr DWORD headerLength = static_cast<DWORD>(sizeof(headers) / sizeof(headers[0]) - 1);
    if (!WinHttpSendRequest(request, headers, headerLength,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        return failure("GitHub konnte nicht erreicht werden. Netzwerk, Proxy oder TLS-Verbindung prüfen.");
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        return failure("HTTP-Status der GitHub-Antwort konnte nicht gelesen werden.");
    }
    if (status == 404)
        return failure("Das GitHub-Release-Repository wurde nicht gefunden.");
    if (status == 403 || status == 429)
        return failure("GitHub hat die Updateprüfung wegen Rate-Limit oder Zugriffsbegrenzung abgewiesen.");
    if (status != 200)
        return failure("GitHub antwortete mit HTTP-Status " + std::to_string(status) + ".");

    std::string response;
    constexpr size_t maximumResponse = 2 * 1024 * 1024;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
            return failure("GitHub-Antwort konnte nicht vollständig gelesen werden.");
        if (available == 0) break;
        if (response.size() + available > maximumResponse)
            return failure("GitHub-Antwort überschreitet die zulässige Größe.");
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read))
            return failure("GitHub-Antwort wurde während der Übertragung unterbrochen.");
        response.append(buffer.data(), read);
    }

    const std::optional<std::string> tag = jsonStringField(response, "tag_name");
    if (!tag || !parseVersion(*tag))
        return failure("GitHub-Antwort enthält keine verständliche Release-Version.");

    UpdateCheckResult result;
    result.latestVersion = *tag;
    const std::optional<std::string> htmlUrl = jsonStringField(response, "html_url");
    result.releaseUrl = htmlUrl && validReleaseUrl(*htmlUrl)
        ? *htmlUrl : std::string(kReleasePage);
    if (isNewerPartSpliceVersion(*tag)) {
        result.state = UpdateCheckState::Available;
        result.message = "Eine neuere PartSplice-3D-Version ist verfügbar.";
    } else {
        result.state = UpdateCheckState::Current;
        result.message = "PartSplice 3D ist auf dem neuesten Stand.";
    }
    return result;
#endif
}
