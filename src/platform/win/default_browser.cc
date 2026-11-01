#include "platform/win/default_browser.h"

#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <array>
#include <string>
#include <utility>
#include <vector>

#include "brouk/app_info.h"
#include "platform/win/wide_string.h"

namespace brouk::win {

namespace {

// The ProgIDs of Brouk's links and HTML files, as Chrome's ChromeHTML.
constexpr wchar_t kUrlProgId[] = L"BroukURL";
constexpr wchar_t kHtmlProgId[] = L"BroukHTML";
constexpr std::array<const wchar_t*, 2> kSchemes = {L"http", L"https"};
constexpr std::array<const wchar_t*, 5> kFileTypes = {
    L".htm", L".html", L".shtml", L".xht", L".xhtml"};

std::wstring ExecutablePath() {
  std::array<wchar_t, MAX_PATH> path = {};
  const DWORD length =
      GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  return length > 0 && length < path.size() ? std::wstring(path.data(), length)
                                            : std::wstring();
}

std::wstring AppName() {
  return Utf8ToWide(kAppName);
}

std::wstring BrowserKey() {
  return L"Software\\Clients\\StartMenuInternet\\" + AppName();
}

// Writes a string value (`name` empty: the key's default value).
bool SetString(const std::wstring& key,
               const wchar_t* name,
               const std::wstring& value) {
  return RegSetKeyValueW(
             HKEY_CURRENT_USER, key.c_str(), name, REG_SZ, value.c_str(),
             static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) ==
         ERROR_SUCCESS;
}

std::optional<std::wstring> GetString(const std::wstring& key,
                                      const wchar_t* name) {
  std::array<wchar_t, 2048> value = {};
  DWORD size = static_cast<DWORD>(value.size() * sizeof(wchar_t));
  if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), name, RRF_RT_REG_SZ, nullptr,
                   value.data(), &size) != ERROR_SUCCESS) {
    return std::nullopt;
  }
  return std::wstring(value.data());
}

// What opens a link or file: Brouk with it as its only argument, so a link
// cannot add switches of its own (Chrome's --single-argument).
std::wstring OpenCommand(const std::wstring& exe) {
  return L"\"" + exe + L"\" --single-argument %1";
}

bool RegisterProgId(const wchar_t* prog_id,
                    const std::wstring& description,
                    bool url_protocol,
                    const std::wstring& exe) {
  const std::wstring key = std::wstring(L"Software\\Classes\\") + prog_id;
  bool ok =
      SetString(key, nullptr, description) &&
      SetString(key + L"\\DefaultIcon", nullptr, exe + L",0") &&
      SetString(key + L"\\shell\\open\\command", nullptr, OpenCommand(exe)) &&
      SetString(key + L"\\Application", L"ApplicationName", AppName()) &&
      SetString(key + L"\\Application", L"ApplicationIcon", exe + L",0");
  if (url_protocol) {
    ok = ok && SetString(key, L"URL Protocol", L"");
  }
  return ok;
}

}  // namespace

std::optional<Error> RegisterAsBrowser() {
  const std::wstring exe = ExecutablePath();
  if (exe.empty()) {
    return Error{"Brouk could not find its own program file."};
  }
  const std::wstring name = AppName();
  const std::wstring key = BrowserKey();
  const std::wstring capabilities = key + L"\\Capabilities";
  bool ok =
      RegisterProgId(kUrlProgId, name + L" URL", true, exe) &&
      RegisterProgId(kHtmlProgId, name + L" HTML Document", false, exe) &&
      SetString(key, nullptr, name) &&
      SetString(key + L"\\DefaultIcon", nullptr, exe + L",0") &&
      SetString(key + L"\\shell\\open\\command", nullptr,
                L"\"" + exe + L"\"") &&
      SetString(capabilities, L"ApplicationName", name) &&
      SetString(capabilities, L"ApplicationDescription",
                L"A browser for work with many accounts") &&
      SetString(capabilities, L"ApplicationIcon", exe + L",0") &&
      SetString(capabilities + L"\\StartMenu", L"StartMenuInternet", name);
  for (const wchar_t* scheme : kSchemes) {
    ok = ok &&
         SetString(capabilities + L"\\URLAssociations", scheme, kUrlProgId);
  }
  for (const wchar_t* type : kFileTypes) {
    ok = ok &&
         SetString(capabilities + L"\\FileAssociations", type, kHtmlProgId);
  }
  ok = ok && SetString(L"Software\\RegisteredApplications", name.c_str(),
                       capabilities);
  if (!ok) {
    return Error{"Brouk could not register itself as a browser."};
  }
  // Explorer and Settings read the associations again.
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
  return std::nullopt;
}

void UnregisterAsBrowser() {
  const std::wstring classes = L"Software\\Classes\\";
  RegDeleteTreeW(HKEY_CURRENT_USER, (classes + kUrlProgId).c_str());
  RegDeleteTreeW(HKEY_CURRENT_USER, (classes + kHtmlProgId).c_str());
  RegDeleteTreeW(HKEY_CURRENT_USER, BrowserKey().c_str());
  RegDeleteKeyValueW(HKEY_CURRENT_USER, L"Software\\RegisteredApplications",
                     AppName().c_str());
  SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

void RefreshBrowserRegistration() {
  const std::optional<std::wstring> command =
      GetString(std::wstring(L"Software\\Classes\\") + kUrlProgId +
                    L"\\shell\\open\\command",
                nullptr);
  const std::wstring exe = ExecutablePath();
  if (command && !exe.empty() && *command != OpenCommand(exe)) {
    RegisterAsBrowser();
  }
}

bool IsDefaultBrowser() {
  Microsoft::WRL::ComPtr<IApplicationAssociationRegistration> registration;
  if (FAILED(CoCreateInstance(
          CLSID_ApplicationAssociationRegistration, nullptr,
          CLSCTX_INPROC_SERVER, IID_IApplicationAssociationRegistration,
          reinterpret_cast<void**>(registration.ReleaseAndGetAddressOf())))) {
    return false;
  }
  for (const wchar_t* scheme : kSchemes) {
    LPWSTR prog_id = nullptr;
    const bool ours = SUCCEEDED(registration->QueryCurrentDefault(
                          scheme, AT_URLPROTOCOL, AL_EFFECTIVE, &prog_id)) &&
                      prog_id && std::wstring(prog_id) == kUrlProgId;
    CoTaskMemFree(prog_id);
    if (!ours) {
      return false;
    }
  }
  return true;
}

void OpenDefaultAppsSettings() {
  // Windows 11 opens Brouk's own page; Windows 10 the list of default apps.
  // Fixed ms-settings: pages only, opened by the shell (no program runs
  // with arguments from anywhere else).
  const std::wstring page =
      L"ms-settings:defaultapps?registeredAppUser=" + AppName();
  const HINSTANCE opened = ShellExecuteW(  // nosemgrep: cpp-process-spawn
      nullptr, L"open", page.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  if (reinterpret_cast<INT_PTR>(opened) <= 32) {
    ShellExecuteW(  // nosemgrep: cpp-process-spawn
        nullptr, L"open", L"ms-settings:defaultapps", nullptr, nullptr,
        SW_SHOWNORMAL);
  }
}

}  // namespace brouk::win
