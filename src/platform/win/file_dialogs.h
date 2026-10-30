#ifndef BROUK_PLATFORM_WIN_FILE_DIALOGS_H_
#define BROUK_PLATFORM_WIN_FILE_DIALOGS_H_

#include <windows.h>

#include <filesystem>
#include <optional>
#include <string>

namespace brouk::win {

// What the Save and Open dialogs show. `extension` has no dot
// ("brouk-profile"); only files of that type are listed.
struct FileDialogOptions {
  std::wstring title;
  std::wstring type_name;  // "Brouk profile"
  std::wstring extension;
  std::wstring file_name;  // Suggested name (Save only).
  std::wstring note;       // A line of text in the dialog (Save only).
};

// The system's Save and Open dialogs (IFileSaveDialog, IFileOpenDialog),
// modal to `owner`. Nullopt when the user cancels. They run their own
// message loop: call them from a posted task, never from inside a CEF
// callback. UI thread.
std::optional<std::filesystem::path> AskSavePath(
    HWND owner,
    const FileDialogOptions& options);
std::optional<std::filesystem::path> AskOpenPath(
    HWND owner,
    const FileDialogOptions& options);

// A file name made from `name` (a profile's name, which may contain
// anything) that Windows accepts: characters not allowed in file names
// become "-".
std::wstring SafeFileName(std::wstring name);

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_FILE_DIALOGS_H_
