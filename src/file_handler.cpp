/**
 * @file file_handler.cpp
 * @brief Definitions for file handling functions.
 */

// standard includes
#include <filesystem>
#include <fstream>

#ifdef _WIN32
  // Windows.h pulls in CreateFileW / FlushFileBuffers / ReplaceFileW.
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <windows.h>
#endif

// local includes
#include "file_handler.h"
#include "logging.h"

namespace file_handler {
  std::string get_parent_directory(const std::string &path) {
    // remove any trailing path separators
    std::string trimmed_path = path;
    while (!trimmed_path.empty() && trimmed_path.back() == '/') {
      trimmed_path.pop_back();
    }

    std::filesystem::path p(trimmed_path);
    return p.parent_path().string();
  }

  bool make_directory(const std::string &path) {
    // first, check if the directory already exists
    if (std::filesystem::exists(path)) {
      return true;
    }

    return std::filesystem::create_directories(path);
  }

  std::string read_file(const char *path) {
    if (!std::filesystem::exists(path)) {
      BOOST_LOG(debug) << "Missing file: " << path;
      return {};
    }

    std::ifstream in(path);
    return std::string {(std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()};
  }

  int write_file(const char *path, const std::string_view &contents) {
    std::string tmp_path = std::string(path) + ".tmp";
    std::ofstream out(tmp_path);

    if (!out.is_open()) {
      return -1;
    }

    out << contents;

    if (out.fail()) {
      std::error_code ec;
      std::filesystem::remove(tmp_path, ec);
      return -1;
    }

    // Force the data we just streamed through C++/CRT buffers down to the
    // OS, then close the fstream cleanly. The actual durable-flush to disk
    // is performed below via the platform-native handle, since C++ has no
    // portable fsync.
    out.flush();
    out.close();

#ifdef _WIN32
    // Windows: fsync the temp file by reopening with FILE_FLAG_WRITE_THROUGH
    // and calling FlushFileBuffers. Then publish via ReplaceFileW which is
    // documented atomic on NTFS — std::filesystem::rename is NOT (it maps
    // to MoveFileExW, no REPLACEFILE_WRITE_THROUGH guarantee).
    std::wstring wtmp = std::filesystem::path(tmp_path).wstring();
    std::wstring wpath = std::filesystem::path(path).wstring();

    HANDLE h = CreateFileW(
      wtmp.c_str(),
      GENERIC_WRITE,
      FILE_SHARE_READ,
      NULL,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
      NULL);
    if (h == INVALID_HANDLE_VALUE) {
      std::error_code ec;
      std::filesystem::remove(tmp_path, ec);
      return -1;
    }
    if (!FlushFileBuffers(h)) {
      CloseHandle(h);
      std::error_code ec;
      std::filesystem::remove(tmp_path, ec);
      return -1;
    }
    CloseHandle(h);

    // ReplaceFileW requires the target to exist; fall back to MoveFileExW
    // with replace-existing + write-through when the destination is absent
    // (first-time write, e.g. fresh apps.json).
    if (std::filesystem::exists(path)) {
      if (!ReplaceFileW(
            wpath.c_str(),
            wtmp.c_str(),
            NULL,
            REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS,
            NULL,
            NULL)) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return -1;
      }
    } else {
      if (!MoveFileExW(
            wtmp.c_str(),
            wpath.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::error_code ec;
        std::filesystem::remove(tmp_path, ec);
        return -1;
      }
    }
#else
    // POSIX rename(2) is atomic for same-filesystem moves; the temp file
    // is created next to the target, so this guarantee holds.
    std::error_code ec;
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
      std::filesystem::remove(tmp_path, ec);
      return -1;
    }
#endif

    return 0;
  }
}  // namespace file_handler
