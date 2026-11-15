#include "engine/Files.h"

#include <windows.h>

#include <cstdio>
#include <cstring>

namespace zy {

std::string ReadWholeFile(const std::wstring& path) {
    std::string data;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return data;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < (64LL << 20)) {
        data.resize(static_cast<size_t>(size.QuadPart));
        DWORD read = 0;
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr)) read = 0;
        data.resize(read);
        // Tolerate a UTF-8 BOM (Notepad adds one).
        if (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);
    }
    CloseHandle(file);
    return data;
}

std::string DecodeText(const std::string& bytes) {
    auto narrow = [](const wchar_t* w, int n) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, w, n, nullptr, 0, nullptr, nullptr);
        std::string out(static_cast<size_t>(len > 0 ? len : 0), '\0');
        if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, n, out.data(), len, nullptr, nullptr);
        return out;
    };
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF && static_cast<unsigned char>(bytes[1]) == 0xFE) {
        std::wstring w((bytes.size() - 2) / 2, L'\0');
        if (!w.empty()) memcpy(w.data(), bytes.data() + 2, w.size() * sizeof(wchar_t));
        return narrow(w.data(), static_cast<int>(w.size()));
    }
    if (bytes.empty() ||
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0) > 0)
        return bytes;
    const int n = MultiByteToWideChar(950, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(950, 0, bytes.data(), static_cast<int>(bytes.size()), w.data(), n);
    return narrow(w.data(), static_cast<int>(w.size()));
}

std::string FileStamp(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA info = {};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info)) return "-";
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%08lx%08lx:%08lx%08lx", info.ftLastWriteTime.dwHighDateTime,
                  info.ftLastWriteTime.dwLowDateTime, info.nFileSizeHigh, info.nFileSizeLow);
    return buf;
}

bool FileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool WriteFileReplace(const std::wstring& path, const std::string& data) {
    const std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    size_t done = 0;
    while (ok && done < data.size()) {
        DWORD written = 0;
        ok = WriteFile(file, data.data() + done, static_cast<DWORD>(data.size() - done), &written, nullptr) && written;
        done += written;
    }
    ok = ok && FlushFileBuffers(file);
    CloseHandle(file);
    if (ok) ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

void EnsureDirectory(const std::wstring& dir) {
    if (!dir.empty()) CreateDirectoryW(dir.c_str(), nullptr);
}

}  // namespace zy
