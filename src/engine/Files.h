#pragma once
#include <string>

namespace zy {

// Small Win32 file helpers (UTF-16 paths). Blocking: never call them on the key path.

// Whole file as bytes, without a UTF-8 BOM. Empty if missing or unreadable (or > 64 MB).
std::string ReadWholeFile(const std::wstring& path);
// Text from elsewhere as UTF-8: UTF-8 (BOM or not) is kept, UTF-16 LE (BOM) and Big5 are converted.
std::string DecodeText(const std::string& bytes);
// Changes whenever the file is written, replaced, created or deleted.
std::string FileStamp(const std::wstring& path);
bool FileExists(const std::wstring& path);
// Writes via a temporary file and a rename, so readers never see half a file.
bool WriteFileReplace(const std::wstring& path, const std::string& data);
// Creates the folder (one level) if needed.
void EnsureDirectory(const std::wstring& dir);

}  // namespace zy
