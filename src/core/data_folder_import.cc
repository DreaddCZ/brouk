#include "core/data_folder_import.h"

#include <array>
#include <string>
#include <system_error>
#include <vector>

namespace brouk {

namespace {

// Folders of Chromium's data (<data>\web) that are caches, and its lock
// file; skipped wherever they are below web.
constexpr std::array<std::string_view, 10> kChromiumCacheFolders = {
    "Cache",
    "Code Cache",
    "GPUCache",
    "DawnWebGPUCache",
    "DawnGraphiteCache",
    "GrShaderCache",
    "GraphiteDawnCache",
    "ShaderCache",
    "Crashpad",
    "BrowserMetrics"};
constexpr std::string_view kChromiumLockFile = "lockfile";

// Top-level entries of Brouk's data folder that are not data.
constexpr std::array<std::string_view, 2> kSkippedTopLevel = {"updates",
                                                              "logs"};

std::string_view Trimmed(std::string_view text) {
  constexpr std::string_view kSpace = " \t\r\n";
  const std::size_t first = text.find_first_not_of(kSpace);
  if (first == std::string_view::npos) {
    return {};
  }
  const std::size_t last = text.find_last_not_of(kSpace);
  return text.substr(first, last - first + 1);
}

Error FileError(std::string_view what,
                const std::filesystem::path& path,
                const std::error_code& error) {
  return Error{std::string(what) + " " + path.string() + ": " +
               error.message()};
}

std::optional<Error> CopyTree(const std::filesystem::path& from,
                              const std::filesystem::path& to,
                              const std::filesystem::path& relative) {
  std::error_code error;
  if (!std::filesystem::create_directory(to, error) && error) {
    return FileError("Could not create", to, error);
  }
  std::filesystem::directory_iterator it(from, error);
  if (error) {
    return FileError("Could not read", from, error);
  }
  for (const std::filesystem::directory_iterator end; it != end;
       it.increment(error)) {
    const std::filesystem::path name = it->path().filename();
    const std::filesystem::path entry_relative =
        relative.empty() ? name : relative / name;
    if (SkippedByDataImport(entry_relative)) {
      continue;
    }
    const std::filesystem::file_status status = it->symlink_status(error);
    if (error) {
      return FileError("Could not read", it->path(), error);
    }
    if (std::filesystem::is_directory(status)) {
      if (std::optional<Error> failed =
              CopyTree(it->path(), to / name, entry_relative)) {
        return failed;
      }
    } else if (std::filesystem::is_regular_file(status)) {
      if (!std::filesystem::copy_file(it->path(), to / name, error)) {
        return FileError("Could not copy", it->path(), error);
      }
    }
    // Links and anything else are left out: Brouk writes none.
  }
  if (error) {
    return FileError("Could not read", from, error);
  }
  return std::nullopt;
}

}  // namespace

std::optional<DataImportMode> ParseDataImportRequest(std::string_view text) {
  const std::string_view word = Trimmed(text);
  if (word == "copy") {
    return DataImportMode::kCopy;
  }
  if (word == "move") {
    return DataImportMode::kMove;
  }
  return std::nullopt;
}

std::string_view DataImportRequestText(DataImportMode mode) {
  return mode == DataImportMode::kMove ? "move" : "copy";
}

bool SkippedByDataImport(const std::filesystem::path& relative) {
  std::vector<std::string> parts;
  for (const std::filesystem::path& part : relative) {
    parts.push_back(part.string());
  }
  if (parts.empty()) {
    return false;
  }
  if (parts.size() == 1) {
    for (std::string_view skipped : kSkippedTopLevel) {
      if (parts[0] == skipped) {
        return true;
      }
    }
    return false;
  }
  if (parts[0] != "web") {
    return false;
  }
  const std::string& last = parts.back();
  if (last == kChromiumLockFile) {
    return true;
  }
  for (std::string_view cache : kChromiumCacheFolders) {
    if (last == cache) {
      return true;
    }
  }
  return false;
}

std::optional<Error> CopyDataFolder(const std::filesystem::path& from,
                                    const std::filesystem::path& to) {
  std::error_code error;
  if (!std::filesystem::is_directory(from, error)) {
    return Error{"There is no data folder " + from.string()};
  }
  if (std::filesystem::exists(to, error)) {
    return Error{"The folder " + to.string() + " is in the way"};
  }
  std::optional<Error> failed = CopyTree(from, to, {});
  if (failed) {
    std::filesystem::remove_all(to, error);
  }
  return failed;
}

std::optional<Error> ReplaceDataFolder(const std::filesystem::path& from,
                                       const std::filesystem::path& to) {
  std::filesystem::path staging = to;
  staging += ".import";
  std::error_code error;
  // Left over from an import that was cut short.
  std::filesystem::remove_all(staging, error);
  if (error) {
    return FileError("Could not remove", staging, error);
  }
  if (std::optional<Error> failed = CopyDataFolder(from, staging)) {
    return failed;
  }
  std::filesystem::remove_all(to, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove_all(staging, ignored);
    return FileError("Could not remove", to, error);
  }
  std::filesystem::rename(staging, to, error);
  if (error) {
    return FileError("Could not rename", staging, error);
  }
  return std::nullopt;
}

std::optional<Error> CopyDataKeys(const std::filesystem::path& data,
                                  SecretStore& from,
                                  SecretStore& to,
                                  bool remove_source) {
  std::vector<std::string> accounts = {std::string(kAutofillKeyAccount)};
  std::error_code error;
  std::filesystem::directory_iterator it(data / "profiles", error);
  if (!error) {
    for (const std::filesystem::directory_iterator end; it != end;
         it.increment(error)) {
      if (it->is_directory(error)) {
        accounts.push_back(PasswordKeyAccount(it->path().filename().string()));
      }
    }
  }
  for (const std::string& account : accounts) {
    Result<std::optional<std::string>> secret = from.Read(account);
    if (!secret) {
      return secret.error();
    }
    const std::optional<std::string>& key = secret.value();
    if (!key.has_value()) {
      continue;
    }
    if (std::optional<Error> failed = to.Write(account, key.value())) {
      return failed;
    }
  }
  if (remove_source) {
    for (const std::string& account : accounts) {
      from.Remove(account);
    }
  }
  return std::nullopt;
}

}  // namespace brouk
