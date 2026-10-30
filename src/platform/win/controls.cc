#include "platform/win/controls.h"

// Defines the accessibility GUIDs of oleacc.h here (no library has them).
#include <initguid.h>
#include <oleacc.h>

namespace brouk::win {

std::wstring WindowText(HWND window) {
  const int length = GetWindowTextLengthW(window);
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(window, text.data(), length + 1);
  text.resize(static_cast<size_t>(length));
  return text;
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
