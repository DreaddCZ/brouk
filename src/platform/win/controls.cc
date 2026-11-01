#include "platform/win/controls.h"

// Defines the accessibility GUIDs of oleacc.h here (no library has them).
#include <initguid.h>
#include <oleacc.h>

#include <cstring>

namespace brouk::win {

std::wstring WindowText(HWND window) {
  const int length = GetWindowTextLengthW(window);
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(window, text.data(), length + 1);
  text.resize(static_cast<size_t>(length));
  return text;
}

void SetClipboardText(HWND owner, const std::wstring& text) {
  if (!OpenClipboard(owner)) {
    return;
  }
  EmptyClipboard();
  const SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
  if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
    if (void* data = GlobalLock(memory)) {
      std::memcpy(data, text.c_str(), bytes);
      GlobalUnlock(memory);
      // The clipboard owns the memory once it has taken it.
      if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        GlobalFree(memory);
      }
    } else {
      GlobalFree(memory);
    }
  }
  CloseClipboard();
}

bool KeyDown(int key) {
  return (GetKeyState(key) & 0x8000) != 0;
}

void SetAccessibleName(HWND control, const wchar_t* name) {
  IAccPropServices* services = nullptr;
  if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr,
                              CLSCTX_INPROC_SERVER, IID_IAccPropServices,
                              reinterpret_cast<void**>(&services)))) {
    return;
  }
  services->SetHwndPropStr(control, static_cast<DWORD>(OBJID_CLIENT),
                           CHILDID_SELF, PROPID_ACC_NAME, name);
  services->Release();
}

}  // namespace brouk::win
