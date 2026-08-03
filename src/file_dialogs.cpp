#include "file_dialogs.h"
#include "localization.h"

#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>
#endif

#ifdef _WIN32
std::optional<std::filesystem::path> openModelDialog() {
    std::vector<wchar_t> buffer(32768, L'\0');
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    static constexpr wchar_t germanFilter[] =
        L"PartSplice-Projekte und 3D-Dateien (*.ps3d;*.stl;*.3mf;*.step;*.stp)\0*.ps3d;*.stl;*.3mf;*.step;*.stp\0"
        L"PartSplice-Projekte (*.ps3d)\0*.ps3d\0"
        L"3D-Modelle (*.stl;*.3mf;*.step;*.stp)\0*.stl;*.3mf;*.step;*.stp\0"
        L"3MF-Projekte (*.3mf)\0*.3mf\0STL-Dateien (*.stl)\0*.stl\0"
        L"STEP-Dateien (*.step;*.stp)\0*.step;*.stp\0Alle Dateien (*.*)\0*.*\0\0";
    static constexpr wchar_t englishFilter[] =
        L"PartSplice projects and 3D files (*.ps3d;*.stl;*.3mf;*.step;*.stp)\0*.ps3d;*.stl;*.3mf;*.step;*.stp\0"
        L"PartSplice projects (*.ps3d)\0*.ps3d\0"
        L"3D models (*.stl;*.3mf;*.step;*.stp)\0*.stl;*.3mf;*.step;*.stp\0"
        L"3MF projects (*.3mf)\0*.3mf\0STL files (*.stl)\0*.stl\0"
        L"STEP files (*.step;*.stp)\0*.step;*.stp\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFilter = currentLanguage() == AppLanguage::English ? englishFilter : germanFilter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER |
                OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"3mf";
    if (!GetOpenFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(buffer.data());
}

std::optional<std::filesystem::path> saveProjectDialog(const wchar_t* defaultName) {
    std::vector<wchar_t> buffer(32768, L'\0');
    wcsncpy_s(buffer.data(), buffer.size(), defaultName, _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    static constexpr wchar_t germanFilter[] = L"PartSplice-Projekte (*.ps3d)\0*.ps3d\0Alle Dateien (*.*)\0*.*\0\0";
    static constexpr wchar_t englishFilter[] = L"PartSplice projects (*.ps3d)\0*.ps3d\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFilter = currentLanguage() == AppLanguage::English ? englishFilter : germanFilter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER |
                OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"ps3d";
    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(buffer.data());
}

std::optional<std::filesystem::path> saveStlDialog(const wchar_t* defaultName) {
    std::vector<wchar_t> buffer(32768, L'\0');
    wcsncpy_s(buffer.data(), buffer.size(), defaultName, _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    static constexpr wchar_t germanFilter[] = L"STL-Dateien (*.stl)\0*.stl\0Alle Dateien (*.*)\0*.*\0\0";
    static constexpr wchar_t englishFilter[] = L"STL files (*.stl)\0*.stl\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFilter = currentLanguage() == AppLanguage::English ? englishFilter : germanFilter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER |
                OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"stl";
    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(buffer.data());
}

std::optional<std::filesystem::path> saveThreeMfDialog(const wchar_t* defaultName) {
    std::vector<wchar_t> buffer(32768, L'\0');
    wcsncpy_s(buffer.data(), buffer.size(), defaultName, _TRUNCATE);
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    static constexpr wchar_t germanFilter[] = L"3MF-Dateien (*.3mf)\0*.3mf\0Alle Dateien (*.*)\0*.*\0\0";
    static constexpr wchar_t englishFilter[] = L"3MF files (*.3mf)\0*.3mf\0All files (*.*)\0*.*\0\0";
    ofn.lpstrFilter = currentLanguage() == AppLanguage::English ? englishFilter : germanFilter;
    ofn.lpstrFile = buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(buffer.size());
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER |
                OFN_NOCHANGEDIR;
    ofn.lpstrDefExt = L"3mf";
    if (!GetSaveFileNameW(&ofn)) return std::nullopt;
    return std::filesystem::path(buffer.data());
}

std::optional<std::filesystem::path> selectFolderDialog() {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) return std::nullopt;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    dialog->SetTitle(currentLanguage() == AppLanguage::English
                         ? L"Select export folder" : L"Exportordner auswählen");
    const HRESULT shown = dialog->Show(nullptr);
    if (FAILED(shown)) {
        dialog->Release();
        return std::nullopt;
    }
    IShellItem* item = nullptr;
    if (FAILED(dialog->GetResult(&item))) {
        dialog->Release();
        return std::nullopt;
    }
    PWSTR pathText = nullptr;
    std::optional<std::filesystem::path> result;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathText))) {
        result = std::filesystem::path(pathText);
        CoTaskMemFree(pathText);
    }
    item->Release();
    dialog->Release();
    return result;
}
#else
std::optional<std::filesystem::path> openModelDialog() { return std::nullopt; }
std::optional<std::filesystem::path> saveProjectDialog(const wchar_t*) { return std::nullopt; }
std::optional<std::filesystem::path> saveStlDialog(const wchar_t*) { return std::nullopt; }
std::optional<std::filesystem::path> saveThreeMfDialog(const wchar_t*) { return std::nullopt; }
std::optional<std::filesystem::path> selectFolderDialog() { return std::nullopt; }
#endif
