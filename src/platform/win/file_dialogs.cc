#include "platform/win/file_dialogs.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include <array>
#include <memory>
#include <string_view>

namespace brouk::win {

namespace {

using Microsoft::WRL::ComPtr;

// Frees the strings the shell allocates for us.
struct CoTaskMemDeleter {
  void operator()(wchar_t* text) const { CoTaskMemFree(text); }
};

// The id of the note in the Save dialog (IFileDialogCustomize).
constexpr DWORD kNoteId = 1;

bool Configure(IFileDialog& dialog, const FileDialogOptions& options) {
  const std::wstring pattern = L"*." + options.extension;
  const std::array<COMDLG_FILTERSPEC, 1> types = {
      {{options.type_name.c_str(), pattern.c_str()}}};
  FILEOPENDIALOGOPTIONS flags = 0;
  return SUCCEEDED(dialog.SetTitle(options.title.c_str())) &&
         SUCCEEDED(dialog.SetFileTypes(types.size(), types.data())) &&
         SUCCEEDED(dialog.SetDefaultExtension(options.extension.c_str())) &&
         SUCCEEDED(dialog.GetOptions(&flags)) &&
         SUCCEEDED(dialog.SetOptions(flags | FOS_FORCEFILESYSTEM |
                                     FOS_STRICTFILETYPES));
}

// Shows the dialog and returns the chosen file's path.
std::optional<std::filesystem::path> Run(IFileDialog& dialog, HWND owner) {
  // Cancelling returns HRESULT_FROM_WIN32(ERROR_CANCELLED).
  if (FAILED(dialog.Show(owner))) {
    return std::nullopt;
  }
  ComPtr<IShellItem> item;
  if (FAILED(dialog.GetResult(&item))) {
    return std::nullopt;
  }
  wchar_t* raw_path = nullptr;
  if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw_path)) || !raw_path) {
    return std::nullopt;
  }
  std::unique_ptr<wchar_t, CoTaskMemDeleter> path(raw_path);
  return std::filesystem::path(path.get());
}

}  // namespace

std::optional<std::filesystem::path> AskSavePath(
    HWND owner,
    const FileDialogOptions& options) {
  ComPtr<IFileSaveDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) ||
      !Configure(*dialog.Get(), options)) {
    return std::nullopt;
  }
  FILEOPENDIALOGOPTIONS flags = 0;
  if (FAILED(dialog->GetOptions(&flags)) ||
      FAILED(dialog->SetOptions(flags | FOS_OVERWRITEPROMPT)) ||
      FAILED(dialog->SetFileName(options.file_name.c_str()))) {
    return std::nullopt;
  }
  if (!options.note.empty()) {
    // What the file contains; optional, so a failure is ignored.
    ComPtr<IFileDialogCustomize> customize;
    if (SUCCEEDED(dialog.As(&customize))) {
      customize->AddText(kNoteId, options.note.c_str());
    }
  }
  return Run(*dialog.Get(), owner);
}

std::optional<std::filesystem::path> AskOpenPath(
    HWND owner,
    const FileDialogOptions& options) {
  ComPtr<IFileOpenDialog> dialog;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr,
                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))) ||
      !Configure(*dialog.Get(), options)) {
    return std::nullopt;
  }
  FILEOPENDIALOGOPTIONS flags = 0;
  if (FAILED(dialog->GetOptions(&flags)) ||
      FAILED(dialog->SetOptions(flags | FOS_FILEMUSTEXIST))) {
    return std::nullopt;
  }
  return Run(*dialog.Get(), owner);
}

std::wstring SafeFileName(std::wstring name) {
  constexpr std::wstring_view kNotAllowed = L"<>:\"/\\|?*";
  for (wchar_t& c : name) {
    if (c < 0x20 || kNotAllowed.find(c) != std::wstring_view::npos) {
      c = L'-';
    }
  }
  // Windows drops trailing dots and spaces from file names.
  while (!name.empty() && (name.back() == L'.' || name.back() == L' ')) {
    name.pop_back();
  }
  return name.empty() ? L"Profile" : name;
}

}  // namespace brouk::win
