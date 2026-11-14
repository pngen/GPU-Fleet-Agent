// Atomic replacement of the persisted state file.
#include "gpufleetagent/persistence/store.hpp"

#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#undef ERROR
#endif

namespace gpufleet::detail {

#ifdef _WIN32
namespace {
// FILE_RENAME_FLAG_REPLACE_IF_EXISTS, FILE_RENAME_FLAG_POSIX_SEMANTICS and
// FileRenameInfoEx (Windows 10 1607+), spelled out so older SDK headers build.
constexpr DWORD kRenameReplaceIfExists = 0x00000001;
constexpr DWORD kRenamePosixSemantics = 0x00000002;
constexpr auto kFileRenameInfoEx = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);

std::wstring full_wide_path(const std::string& p) {
  int n = MultiByteToWideChar(CP_ACP, 0, p.c_str(), -1, nullptr, 0);
  if (n <= 0) return {};
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_ACP, 0, p.c_str(), -1, w.data(), n);
  w.resize(static_cast<std::size_t>(n - 1));
  DWORD len = GetFullPathNameW(w.c_str(), 0, nullptr, nullptr);
  if (len == 0) return {};
  std::wstring full(len, L'\0');
  len = GetFullPathNameW(w.c_str(), len, full.data(), nullptr);
  full.resize(len);
  return full;
}
}  // namespace
#endif

bool replace_file_atomically(const std::string& from, const std::string& to) {
#ifdef _WIN32
  const std::wstring wfrom = full_wide_path(from);
  const std::wstring wto = full_wide_path(to);
  if (wfrom.empty() || wto.empty()) return false;
  // Flush the new contents, then rename by handle with POSIX semantics: the
  // target name refers to the old or the new file at every moment (a classic
  // replace-rename briefly unlinks the old file first), and handles another
  // process holds on the old file do not make the replacement fail.
  HANDLE h = CreateFileW(wfrom.c_str(), GENERIC_WRITE | DELETE | SYNCHRONIZE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  if (!FlushFileBuffers(h)) { CloseHandle(h); return false; }
  const std::size_t name_bytes = wto.size() * sizeof(wchar_t);
  std::vector<unsigned char> buf(sizeof(FILE_RENAME_INFO) + name_bytes, 0);
  auto* info = reinterpret_cast<FILE_RENAME_INFO*>(buf.data());
  info->Flags = kRenameReplaceIfExists | kRenamePosixSemantics;
  info->RootDirectory = nullptr;
  info->FileNameLength = static_cast<DWORD>(name_bytes);
  std::memcpy(info->FileName, wto.c_str(), name_bytes);
  const BOOL renamed = SetFileInformationByHandle(h, kFileRenameInfoEx, info, static_cast<DWORD>(buf.size()));
  const DWORD err = renamed ? ERROR_SUCCESS : GetLastError();
  CloseHandle(h);
  if (renamed) return true;
  // File systems without POSIX rename semantics: the documented replace.
  if (err == ERROR_INVALID_PARAMETER || err == ERROR_NOT_SUPPORTED || err == ERROR_INVALID_FUNCTION)
    return MoveFileExW(wfrom.c_str(), wto.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
  return false;
#else
  std::error_code ec;
  std::filesystem::rename(from, to, ec);   // rename(2) replaces atomically
  return !ec;
#endif
}

}  // namespace gpufleet::detail
