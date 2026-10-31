#ifndef BROUK_PLATFORM_WIN_BOOKMARK_DIALOG_H_
#define BROUK_PLATFORM_WIN_BOOKMARK_DIALOG_H_

#include <windows.h>

#include <functional>
#include <string>
#include <vector>

#include "core/bookmark_model.h"
#include "core/profile_registry.h"
#include "platform/win/bookmark_store.h"
#include "platform/win/theme.h"

namespace brouk::win {

// What the bookmark dialog edits (011).
struct BookmarkDialogValues {
  std::string name;
  BookmarkId folder = 0;  // The folder the item is (or goes) in.
  ProfileId profile;      // "" for general.
};

// The native bookmark dialogs of 011 on Windows, a small window under the
// address bar's star, as on macOS:
// - the star's bubble (Ctrl+D): the bookmark already exists (Chrome saves
//   it when the bubble opens); Done or closing keeps the changes, Remove
//   deletes it;
// - Bookmark all tabs (Ctrl+Shift+D): the new folder's name, parent folder
//   and profile; Save or Cancel.
// A folder linked to a profile decides the profile of what goes in it, so
// the profile list follows the chosen folder. UI thread.
class BookmarkDialog {
 public:
  enum class Kind { kBubble, kNewFolder };
  enum class Answer { kKeep, kRemove, kCancel };
  struct Options {
    Kind kind = Kind::kBubble;
    std::wstring title;
    BookmarkDialogValues values;
    // The folders offered are those visible in this profile.
    ProfileId active_profile;
    std::vector<ProfileInfo> profiles;
  };
  // `done` runs once, when the dialog closes (with the values for kKeep);
  // the owner deletes the dialog afterwards, not from inside `done`.
  using Done =
      std::function<void(Answer answer, const BookmarkDialogValues& values)>;

  BookmarkDialog(HWND owner,
                 const BookmarkStore& store,
                 Options options,
                 const Theme& theme,
                 HFONT font,
                 Done done);
  ~BookmarkDialog();
  BookmarkDialog(const BookmarkDialog&) = delete;
  BookmarkDialog& operator=(const BookmarkDialog&) = delete;

  // Shows the dialog below `anchor` (screen pixels), its right edge at the
  // anchor's, with the name selected.
  void ShowBelow(const RECT& anchor);
  HWND hwnd() const { return window_; }

 private:
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  static LRESULT CALLBACK ControlProc(HWND window,
                                      UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam,
                                      UINT_PTR id,
                                      DWORD_PTR data);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void CreateControls();
  void FillFolders(BookmarkId selected);
  // The profile list follows the folder: locked to a linked folder's
  // profile, else the user's choice.
  void UpdateProfile();
  BookmarkDialogValues Values() const;
  // Closes the dialog with `answer`; only the first call counts.
  void Finish(Answer answer);
  int Scale(int dips) const;

  HWND owner_;
  const BookmarkStore& store_;
  Options options_;
  Theme theme_;
  HFONT font_;
  Done done_;
  HWND window_ = nullptr;
  HWND title_ = nullptr;
  HWND name_ = nullptr;
  HWND folder_ = nullptr;
  HWND profile_ = nullptr;
  HWND secondary_ = nullptr;  // Remove or Cancel.
  HWND primary_ = nullptr;    // Done or Save.
  std::vector<HWND> labels_;
  HBRUSH background_ = nullptr;
  HBRUSH field_ = nullptr;
  // The profile chosen while the folder does not decide it.
  ProfileId own_profile_;
  bool finished_ = false;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_BOOKMARK_DIALOG_H_
