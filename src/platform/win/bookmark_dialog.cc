#include "platform/win/bookmark_dialog.h"

#include <commctrl.h>
#include <uxtheme.h>

#include <utility>

#include "platform/win/controls.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

constexpr wchar_t kWindowClass[] = L"BroukBookmarkDialog";
constexpr int kWidth = 380;
constexpr int kMargin = 16;
constexpr int kLabelWidth = 64;
constexpr int kRowHeight = 26;
constexpr int kRowGap = 10;
constexpr int kButtonWidth = 84;
constexpr int kButtonHeight = 28;

enum ControlId : int {
  kNameId = 1,
  kFolderId,
  kProfileId,
  kSecondaryId,
  kPrimaryId,
};

bool RegisterWindowClass() {
  static const bool registered = [] {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_DROPSHADOW;
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.lpszClassName = kWindowClass;
    return RegisterClassExW(&window_class) != 0;
  }();
  return registered;
}

}  // namespace

BookmarkDialog::BookmarkDialog(HWND owner,
                               const BookmarkStore& store,
                               Options options,
                               const Theme& theme,
                               HFONT font,
                               Done done)
    : owner_(owner),
      store_(store),
      options_(std::move(options)),
      theme_(theme),
      font_(font),
      done_(std::move(done)),
      own_profile_(options_.values.profile) {}

BookmarkDialog::~BookmarkDialog() {
  if (window_) {
    SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
    DestroyWindow(window_);
  }
  if (background_) {
    DeleteObject(background_);
  }
  if (field_) {
    DeleteObject(field_);
  }
}

int BookmarkDialog::Scale(int dips) const {
  return MulDiv(dips, static_cast<int>(GetDpiForWindow(owner_)), 96);
}

void BookmarkDialog::ShowBelow(const RECT& anchor) {
  if (!RegisterWindowClass()) {
    return;
  }
  const int rows = 3;  // Name, folder, profile.
  const int height = Scale(kMargin) * 2 + Scale(kRowHeight) +  // Title.
                     rows * (Scale(kRowHeight) + Scale(kRowGap)) +
                     Scale(kButtonHeight) + Scale(kRowGap);
  const int width = Scale(kWidth);
  window_ =
      CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, options_.title.c_str(),
                      WS_POPUP | WS_BORDER | WS_CLIPCHILDREN,
                      anchor.right - width, anchor.bottom, width, height,
                      owner_, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    Finish(Answer::kCancel);
    return;
  }
  SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  SetWindowLongPtrW(window_, GWLP_WNDPROC,
                    reinterpret_cast<LONG_PTR>(&BookmarkDialog::WindowProc));
  background_ = CreateSolidBrush(theme_.toolbar);
  field_ = CreateSolidBrush(theme_.field);
  CreateControls();
  ShowWindow(window_, SW_SHOW);
  SetFocus(name_);
  SendMessageW(name_, EM_SETSEL, 0, -1);
}

void BookmarkDialog::CreateControls() {
  HINSTANCE instance = GetModuleHandleW(nullptr);
  const int margin = Scale(kMargin);
  const int row = Scale(kRowHeight);
  const int gap = Scale(kRowGap);
  const int label_width = Scale(kLabelWidth);
  RECT client = {};
  GetClientRect(window_, &client);
  const int field_x = margin + label_width;
  const int field_width = client.right - field_x - margin;
  auto control = [&](const wchar_t* type, const wchar_t* text, DWORD style,
                     int x, int y, int w, int h, int id) {
    HWND created = CreateWindowExW(
        0, type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    SendMessageW(created, WM_SETFONT, reinterpret_cast<WPARAM>(font_), FALSE);
    if (theme_.dark) {
      SetWindowTheme(created,
                     type == std::wstring_view(WC_COMBOBOXW)
                         ? L"DarkMode_CFD"
                         : L"DarkMode_Explorer",
                     nullptr);
    }
    if (style & WS_TABSTOP) {
      SetWindowSubclass(created, &BookmarkDialog::ControlProc, 0,
                        reinterpret_cast<DWORD_PTR>(this));
    }
    return created;
  };
  int y = margin;
  title_ = control(WC_STATICW, options_.title.c_str(), SS_LEFT | SS_NOPREFIX,
                   margin, y + Scale(4), client.right - 2 * margin, row, 0);
  y += row + gap;
  auto label = [&](const wchar_t* text) {
    labels_.push_back(control(WC_STATICW, text, SS_LEFT, margin, y + Scale(4),
                              label_width, row, 0));
  };
  label(L"Name");
  name_ = control(WC_EDITW, Utf8ToWide(options_.values.name).c_str(),
                  WS_TABSTOP | ES_AUTOHSCROLL, field_x, y, field_width, row,
                  kNameId);
  SetAccessibleName(name_, L"Name");
  y += row + gap;
  label(L"Folder");
  folder_ =
      control(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
              field_x, y, field_width, Scale(300), kFolderId);
  SetAccessibleName(folder_, L"Folder");
  y += row + gap;
  label(L"Profile");
  profile_ =
      control(WC_COMBOBOXW, L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
              field_x, y, field_width, Scale(300), kProfileId);
  SetAccessibleName(profile_, L"Profile");
  SendMessageW(profile_, CB_ADDSTRING, 0,
               reinterpret_cast<LPARAM>(L"General (all profiles)"));
  for (const ProfileInfo& profile : options_.profiles) {
    SendMessageW(profile_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(Utf8ToWide(profile.name).c_str()));
  }
  y += row + gap;
  const bool bubble = options_.kind == Kind::kBubble;
  const int button_width = Scale(kButtonWidth);
  const int button_height = Scale(kButtonHeight);
  primary_ = control(WC_BUTTONW, bubble ? L"Done" : L"Save",
                     WS_TABSTOP | BS_DEFPUSHBUTTON,
                     client.right - margin - button_width, y, button_width,
                     button_height, kPrimaryId);
  secondary_ = control(WC_BUTTONW, bubble ? L"Remove" : L"Cancel",
                       WS_TABSTOP | BS_PUSHBUTTON,
                       client.right - margin - 2 * button_width - Scale(8), y,
                       button_width, button_height, kSecondaryId);
  FillFolders(options_.values.folder);
  UpdateProfile();
}

void BookmarkDialog::FillFolders(BookmarkId selected) {
  SendMessageW(folder_, CB_RESETCONTENT, 0, 0);
  const BookmarkModel& model = store_.model();
  int select = -1;
  int bar = 0;
  for (const BookmarkFolderEntry& entry :
       model.Folders(options_.active_profile)) {
    const BookmarkItem* folder = model.Find(entry.id);
    if (!folder) {
      continue;
    }
    // Nesting shown by indentation, as in the macOS folder list.
    const std::wstring text =
        std::wstring(static_cast<std::size_t>(entry.depth) * 4, L' ') +
        Utf8ToWide(folder->name);
    const auto index = static_cast<int>(SendMessageW(
        folder_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
    SendMessageW(folder_, CB_SETITEMDATA, index, static_cast<LPARAM>(entry.id));
    if (entry.id == selected) {
      select = index;
    }
    if (entry.id == BookmarkModel::kBarId) {
      bar = index;
    }
  }
  SendMessageW(folder_, CB_SETCURSEL, select >= 0 ? select : bar, 0);
}

void BookmarkDialog::UpdateProfile() {
  const BookmarkId folder = Values().folder;
  const ProfileId locked = store_.model().EffectiveProfile(folder);
  const ProfileId shown = locked.empty() ? own_profile_ : locked;
  int index = 0;
  for (std::size_t i = 0; i < options_.profiles.size(); ++i) {
    if (options_.profiles[i].id == shown) {
      index = static_cast<int>(i) + 1;
    }
  }
  SendMessageW(profile_, CB_SETCURSEL, index, 0);
  EnableWindow(profile_, locked.empty());
}

BookmarkDialogValues BookmarkDialog::Values() const {
  BookmarkDialogValues values;
  values.name = WideToUtf8(WindowText(name_));
  const LRESULT folder = SendMessageW(folder_, CB_GETCURSEL, 0, 0);
  if (folder != CB_ERR) {
    values.folder = static_cast<BookmarkId>(
        SendMessageW(folder_, CB_GETITEMDATA, static_cast<WPARAM>(folder), 0));
  }
  const LRESULT profile = SendMessageW(profile_, CB_GETCURSEL, 0, 0);
  if (profile != CB_ERR && profile > 0 &&
      static_cast<std::size_t>(profile) <= options_.profiles.size()) {
    values.profile =
        options_.profiles[static_cast<std::size_t>(profile) - 1].id;
  }
  return values;
}

void BookmarkDialog::Finish(Answer answer) {
  if (finished_) {
    return;
  }
  finished_ = true;
  const BookmarkDialogValues values = window_ ? Values() : options_.values;
  if (window_) {
    ShowWindow(window_, SW_HIDE);
  }
  // May delete this dialog later (posted): nothing here touches it after.
  Done done = std::move(done_);
  done(answer, values);
}

LRESULT CALLBACK BookmarkDialog::WindowProc(HWND window,
                                            UINT message,
                                            WPARAM wparam,
                                            LPARAM lparam) {
  auto* self = reinterpret_cast<BookmarkDialog*>(
      GetWindowLongPtrW(window, GWLP_USERDATA));
  if (!self) {
    return DefWindowProcW(window, message, wparam, lparam);
  }
  return self->HandleMessage(message, wparam, lparam);
}

// Tab moves between the fields and buttons, Enter answers with the
// primary button, Escape closes (the bubble keeps the bookmark, as in
// Chrome; the folder dialog cancels).
LRESULT CALLBACK BookmarkDialog::ControlProc(HWND window,
                                             UINT message,
                                             WPARAM wparam,
                                             LPARAM lparam,
                                             UINT_PTR /*id*/,
                                             DWORD_PTR data) {
  auto* self = reinterpret_cast<BookmarkDialog*>(data);
  switch (message) {
    case WM_GETDLGCODE:
      if (wparam == VK_TAB || wparam == VK_RETURN || wparam == VK_ESCAPE) {
        return DLGC_WANTALLKEYS;
      }
      break;
    case WM_KEYDOWN:
      if (wparam == VK_TAB) {
        const bool back = KeyDown(VK_SHIFT);
        if (HWND next = GetNextDlgTabItem(self->window_, window, back)) {
          SetFocus(next);
          if (next == self->name_) {
            SendMessageW(next, EM_SETSEL, 0, -1);
          }
        }
        return 0;
      }
      if (wparam == VK_RETURN) {
        // A focused combo box's open list takes Enter first.
        if (SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0)) {
          break;
        }
        self->Finish(window == self->secondary_
                         ? (self->options_.kind == Kind::kBubble
                                ? Answer::kRemove
                                : Answer::kCancel)
                         : Answer::kKeep);
        return 0;
      }
      if (wparam == VK_ESCAPE) {
        if (SendMessageW(window, CB_GETDROPPEDSTATE, 0, 0)) {
          break;
        }
        self->Finish(self->options_.kind == Kind::kBubble ? Answer::kKeep
                                                          : Answer::kCancel);
        return 0;
      }
      break;
    case WM_CHAR:
      if (wparam == L'\t' || wparam == L'\r' || wparam == 0x1B) {
        return 0;  // Handled above; without this the edit beeps.
      }
      break;
    case WM_NCDESTROY:
      RemoveWindowSubclass(window, &BookmarkDialog::ControlProc, 0);
      break;
    default:
      break;
  }
  return DefSubclassProc(window, message, wparam, lparam);
}

LRESULT BookmarkDialog::HandleMessage(UINT message,
                                      WPARAM wparam,
                                      LPARAM lparam) {
  switch (message) {
    case WM_COMMAND:
      switch (LOWORD(wparam)) {
        case kPrimaryId:
          Finish(Answer::kKeep);
          return 0;
        case kSecondaryId:
          Finish(options_.kind == Kind::kBubble ? Answer::kRemove
                                                : Answer::kCancel);
          return 0;
        case kFolderId:
          if (HIWORD(wparam) == CBN_SELCHANGE) {
            UpdateProfile();
          }
          return 0;
        case kProfileId:
          if (HIWORD(wparam) == CBN_SELCHANGE && IsWindowEnabled(profile_)) {
            own_profile_ = Values().profile;
          }
          return 0;
        default:
          break;
      }
      break;
    case WM_ACTIVATE:
      // Clicking elsewhere closes the bubble and keeps the bookmark, as
      // Chrome's; the folder dialog stays until answered.
      if (LOWORD(wparam) == WA_INACTIVE && options_.kind == Kind::kBubble &&
          !finished_) {
        Finish(Answer::kKeep);
      }
      break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
      HDC dc = reinterpret_cast<HDC>(wparam);
      const bool field = message != WM_CTLCOLORSTATIC;
      SetTextColor(dc, theme_.text);
      SetBkColor(dc, field ? theme_.field : theme_.toolbar);
      return reinterpret_cast<LRESULT>(field ? field_ : background_);
    }
    case WM_ERASEBKGND: {
      RECT client = {};
      GetClientRect(window_, &client);
      FillRect(reinterpret_cast<HDC>(wparam), &client, background_);
      return 1;
    }
    case WM_CLOSE:
      Finish(options_.kind == Kind::kBubble ? Answer::kKeep : Answer::kCancel);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace brouk::win
