#ifndef BROUK_PLATFORM_WIN_DOWNLOADS_POPUP_H_
#define BROUK_PLATFORM_WIN_DOWNLOADS_POPUP_H_

#include <windows.h>

#include <string>
#include <vector>

#include "cef/download_service.h"
#include "core/profile_id.h"
#include "platform/win/theme.h"

namespace brouk::win {

// The list under the toolbar's downloads button (014), Chrome's downloads
// bubble and the macOS popover: the last 24 hours of downloads of all
// profiles, each with its profile's colour, icon, name, site and status,
// and the actions that suit its state (pause, resume, cancel, retry, show
// in folder, and a menu with the rest). Clicking a finished download opens
// it, dragging it drops the file into File Explorer or another app. Up/Down
// select, Enter opens, Delete removes from the list, the menu key or Shift+F10
// shows the menu, Escape or clicking elsewhere closes it. UI thread.
class DownloadsPopup {
 public:
  // What the list does through its window.
  class Delegate {
   public:
    virtual DownloadService* PopupDownloads() = 0;
    virtual COLORREF PopupProfileColor(const ProfileId& profile) = 0;
    virtual void PopupRetryDownload(const DownloadEntry& entry) = 0;
    // "Show all downloads": the downloads page.
    virtual void PopupShowAllDownloads() = 0;

   protected:
    ~Delegate() = default;
  };

  explicit DownloadsPopup(Delegate& delegate);
  ~DownloadsPopup();
  DownloadsPopup(const DownloadsPopup&) = delete;
  DownloadsPopup& operator=(const DownloadsPopup&) = delete;

  // Shows the list under `anchor` (screen coordinates, the button), its
  // right edge at the button's. The fonts are the window's, which outlive
  // the list.
  void Show(HWND owner,
            const RECT& anchor,
            std::vector<DownloadEntry> entries,
            const Theme& theme,
            HFONT text_font,
            HFONT icon_font);
  // New states while it is open.
  void Update(std::vector<DownloadEntry> entries);
  void Close();
  bool visible() const { return window_ && IsWindowVisible(window_); }
  // Clicking the button while the list is open closes the list first (it
  // loses the activation); the click must not open it again.
  bool JustClosed() const { return GetTickCount64() - hidden_at_ < 300; }

 private:
  // One action button of a row.
  enum class Command {
    kNone,
    kOpen,
    kReveal,
    kPause,
    kResume,
    kCancel,
    kRetry,
    kMore,
    kCopyLink,
    kRemove,
    kDelete,
  };
  struct Row {
    DownloadEntry entry;
    bool missing = false;  // Finished, but the file is gone.
    std::wstring name;
    std::wstring status;
    HICON icon = nullptr;
    std::vector<Command> buttons;
  };

  // The last 24 hours, and running downloads (as on macOS).
  static std::vector<DownloadEntry> Recent(std::vector<DownloadEntry> entries);
  static const wchar_t* GlyphOf(Command command);
  static LRESULT CALLBACK WindowProc(HWND window,
                                     UINT message,
                                     WPARAM wparam,
                                     LPARAM lparam);
  LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam);
  void Fill(std::vector<DownloadEntry> entries);
  void ClearRows();
  void Resize();
  void Paint();
  void PaintRow(HDC dc, int index, const RECT& area, bool selected);
  // The row (index into rows_) at a client point, or -1; `button` gets the
  // action button under it.
  int RowAt(POINT point, Command* button) const;
  RECT RowRect(int index) const;
  RECT FooterRect() const;
  void Select(int index);
  void Run(int index, Command command);
  void ShowMenu(int index, POINT screen);
  // Drags the finished file of a row out (File Explorer, other apps).
  void DragFile(int index);
  int VisibleRows() const;
  int Scale(int dips) const;

  Delegate& delegate_;
  HWND window_ = nullptr;
  HWND owner_ = nullptr;
  RECT anchor_ = {};
  std::vector<Row> rows_;
  int selected_ = -1;
  int hovered_ = -1;
  Command hovered_button_ = Command::kNone;
  bool footer_hovered_ = false;
  int scroll_ = 0;  // The first row shown.
  ULONGLONG hidden_at_ = 0;
  // The row the left button went down on (a click or a drag), or -1.
  int pressed_ = -1;
  POINT press_point_ = {};
  Theme theme_;
  HFONT text_font_ = nullptr;
  HFONT icon_font_ = nullptr;
};

}  // namespace brouk::win

#endif  // BROUK_PLATFORM_WIN_DOWNLOADS_POPUP_H_
