// Windows implementation of McBopomofo::MemoryMappedFile (replaces the POSIX MemoryMappedFile.cpp).
//
// The upstream header stores an `int fd_`; we keep it as an "is open" flag only. The file and
// mapping handles are closed right after MapViewOfFile — the view keeps the mapping alive — so
// no extra members are needed and the upstream header stays untouched.
//
// Paths are UTF-8 (converted to UTF-16 for CreateFileW) so non-ASCII folders work.
#include <windows.h>

#include <string>
#include <utility>

#include "MemoryMappedFile.h"

namespace McBopomofo {
namespace {

std::wstring Utf8ToWide(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    w.resize(static_cast<size_t>(n - 1));
    return w;
}

}  // namespace

MemoryMappedFile::MemoryMappedFile(MemoryMappedFile&& other) noexcept
    : fd_(std::exchange(other.fd_, -1)),
      data_(std::exchange(other.data_, nullptr)),
      length_(std::exchange(other.length_, 0)) {}

MemoryMappedFile& MemoryMappedFile::operator=(MemoryMappedFile&& other) noexcept {
    close();
    fd_ = std::exchange(other.fd_, -1);
    data_ = std::exchange(other.data_, nullptr);
    length_ = std::exchange(other.length_, 0);
    return *this;
}

MemoryMappedFile::~MemoryMappedFile() { close(); }

bool MemoryMappedFile::open(const char* path) {
    if (fd_ != -1 || !path) return false;

    std::wstring wpath = Utf8ToWide(path);
    if (wpath.empty()) return false;

    // FILE_SHARE_DELETE | FILE_SHARE_WRITE: let the settings app replace user files while mapped.
    HANDLE file = CreateFileW(wpath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) > SIZE_MAX) {
        CloseHandle(file);
        return false;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(file);
    if (!mapping) return false;

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    CloseHandle(mapping);
    if (!view) return false;

    fd_ = 1;
    data_ = view;
    length_ = static_cast<size_t>(size.QuadPart);
    return true;
}

void MemoryMappedFile::close() {
    if (fd_ == -1) return;
    if (data_) UnmapViewOfFile(data_);
    fd_ = -1;
    data_ = nullptr;
    length_ = 0;
}

}  // namespace McBopomofo
