#ifndef BROUK_CORE_DATA_FOLDER_IMPORT_H_
#define BROUK_CORE_DATA_FOLDER_IMPORT_H_

#include <filesystem>
#include <optional>
#include <string_view>

#include "core/result.h"
#include "core/secret_store.h"

namespace brouk {

// Bringing the direct download's data into the Microsoft Store app (032).
// The two copies of Brouk on Windows keep separate data folders, as the
// Store does not allow a package to write outside its own; the Store app
// imports the other folder whole, replacing its own data, at a start before
// anything is opened. Portable, so it is tested on every platform. Any
// thread, but only while no Brouk uses either folder.

// Move also deletes the source after a successful copy.
enum class DataImportMode { kCopy, kMove };

// The request file Brouk writes before it restarts for the import holds
// "copy" or "move"; anything else is not a request.
std::optional<DataImportMode> ParseDataImportRequest(std::string_view text);
std::string_view DataImportRequestText(DataImportMode mode);

// Whether an entry of the data folder, given relative to it, is left out:
// downloaded updates, logs, and Chromium's caches and lock file, which are
// large and rebuilt by themselves.
bool SkippedByDataImport(const std::filesystem::path& relative);

// Copies `from` into the new folder `to` (which must not exist), without
// the skipped entries and without following links. On failure nothing is
// left at `to`.
std::optional<Error> CopyDataFolder(const std::filesystem::path& from,
                                    const std::filesystem::path& to);

// Replaces the folder `to` with a copy of `from`: copies into a folder next
// to `to` first, so a failed copy leaves `to` as it was.
std::optional<Error> ReplaceDataFolder(const std::filesystem::path& from,
                                       const std::filesystem::path& to);

// Copies the encryption keys the data folder `data` needs (every profile's
// password key, the autofill key) from one keychain to the other, removing
// them from `from` when `remove_source`. Keys that do not exist are fine;
// Bitwarden sessions are not copied (the vault is unlocked again).
std::optional<Error> CopyDataKeys(const std::filesystem::path& data,
                                  SecretStore& from,
                                  SecretStore& to,
                                  bool remove_source);

}  // namespace brouk

#endif  // BROUK_CORE_DATA_FOLDER_IMPORT_H_
