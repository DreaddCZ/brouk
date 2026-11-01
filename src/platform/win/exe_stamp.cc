// A build tool: gives the app's executable (CEF's prebuilt bootstrap.exe,
// renamed) Brouk's version information, which Windows shows as the app's
// name (the taskbar button's menu, Task Manager, the file's properties)
// instead of "CEF Bootstrap Application" (024). CEF supports changing the
// bootstrap's resources.
//
//   exe_stamp <exe> <name> <version> <internal name>
//
// The version is "major.minor.patch". Runs at build time only.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void Append(Bytes& out, const void* data, std::size_t size) {
  const auto* bytes = static_cast<const std::uint8_t*>(data);
  out.insert(out.end(), bytes, bytes + size);
}

void AppendWord(Bytes& out, WORD value) {
  Append(out, &value, sizeof(value));
}

void Align(Bytes& out) {
  while (out.size() % 4 != 0) {
    out.push_back(0);
  }
}

// One node of a VS_VERSIONINFO tree: length, value length, type, key,
// value and children, each part aligned to 32 bits.
Bytes Node(std::wstring_view key,
           WORD type,
           const Bytes& value,
           WORD value_length,
           const std::vector<Bytes>& children) {
  Bytes out;
  AppendWord(out, 0);  // wLength, set below.
  AppendWord(out, value_length);
  AppendWord(out, type);
  Append(out, key.data(), key.size() * sizeof(wchar_t));
  AppendWord(out, 0);
  Align(out);
  Append(out, value.data(), value.size());
  for (const Bytes& child : children) {
    Align(out);
    Append(out, child.data(), child.size());
  }
  const auto length = static_cast<WORD>(out.size());
  out[0] = static_cast<std::uint8_t>(length & 0xFF);
  out[1] = static_cast<std::uint8_t>(length >> 8);
  return out;
}

// A string value (type 1: text); its length counts the characters with
// the closing null.
Bytes StringNode(std::wstring_view key, const std::wstring& text) {
  Bytes value;
  Append(value, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
  return Node(key, 1, value, static_cast<WORD>(text.size() + 1), {});
}

bool ParseVersion(const char* text, WORD (&parts)[3]) {
  unsigned major = 0;
  unsigned minor = 0;
  unsigned patch = 0;
  if (std::sscanf(text, "%u.%u.%u", &major, &minor, &patch) != 3 ||
      major > 0xFFFF || minor > 0xFFFF || patch > 0xFFFF) {
    return false;
  }
  parts[0] = static_cast<WORD>(major);
  parts[1] = static_cast<WORD>(minor);
  parts[2] = static_cast<WORD>(patch);
  return true;
}

Bytes VersionInfo(const std::wstring& name,
                  const std::wstring& version,
                  const std::wstring& internal_name,
                  const WORD (&parts)[3]) {
  VS_FIXEDFILEINFO fixed = {};
  fixed.dwSignature = VS_FFI_SIGNATURE;
  fixed.dwStrucVersion = VS_FFI_STRUCVERSION;
  fixed.dwFileVersionMS = static_cast<DWORD>(MAKELONG(parts[1], parts[0]));
  fixed.dwFileVersionLS = static_cast<DWORD>(MAKELONG(0, parts[2]));
  fixed.dwProductVersionMS = fixed.dwFileVersionMS;
  fixed.dwProductVersionLS = fixed.dwFileVersionLS;
  fixed.dwFileFlagsMask = VS_FFI_FILEFLAGSMASK;
  fixed.dwFileOS = VOS_NT_WINDOWS32;
  fixed.dwFileType = VFT_APP;
  Bytes fixed_bytes;
  Append(fixed_bytes, &fixed, sizeof(fixed));

  // US English, Unicode (as the .rc files of most apps).
  const Bytes table =
      Node(L"040904b0", 1, {}, 0,
           {StringNode(L"FileDescription", name),
            StringNode(L"FileVersion", version),
            StringNode(L"InternalName", internal_name),
            StringNode(L"OriginalFilename", internal_name + L".exe"),
            StringNode(L"ProductName", name),
            StringNode(L"ProductVersion", version)});
  const Bytes strings = Node(L"StringFileInfo", 1, {}, 0, {table});
  Bytes translation;
  AppendWord(translation, 0x0409);
  AppendWord(translation, 1200);
  const Bytes var = Node(L"Translation", 0, translation, 4, {});
  const Bytes vars = Node(L"VarFileInfo", 1, {}, 0, {var});
  return Node(L"VS_VERSION_INFO", 0, fixed_bytes, sizeof(fixed),
              {strings, vars});
}

BOOL CALLBACK CollectLanguage(HMODULE /*module*/,
                              LPCWSTR /*type*/,
                              LPCWSTR /*name*/,
                              WORD language,
                              LONG_PTR param) {
  reinterpret_cast<std::vector<WORD>*>(param)->push_back(language);
  return TRUE;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  if (argc != 5) {
    std::fputs("usage: exe_stamp <exe> <name> <version> <internal name>\n",
               stderr);
    return 2;
  }
  const std::wstring exe = argv[1];
  const std::wstring name = argv[2];
  const std::wstring version = argv[3];
  const std::wstring internal_name = argv[4];
  std::string narrow_version;
  for (wchar_t c : version) {
    narrow_version.push_back(static_cast<char>(c));
  }
  WORD parts[3] = {};
  if (!ParseVersion(narrow_version.c_str(), parts)) {
    std::fputs("exe_stamp: the version must be major.minor.patch\n", stderr);
    return 2;
  }
  // The version resources there now (CEF's), to replace.
  std::vector<WORD> languages;
  if (HMODULE module = LoadLibraryExW(
          exe.c_str(), nullptr,
          LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
    EnumResourceLanguagesW(module, RT_VERSION, MAKEINTRESOURCEW(1),
                           &CollectLanguage,
                           reinterpret_cast<LONG_PTR>(&languages));
    FreeLibrary(module);
  }
  HANDLE update = BeginUpdateResourceW(exe.c_str(), FALSE);
  if (!update) {
    std::fprintf(stderr, "exe_stamp: cannot open the executable (%lu)\n",
                 GetLastError());
    return 1;
  }
  for (WORD language : languages) {
    UpdateResourceW(update, RT_VERSION, MAKEINTRESOURCEW(1), language, nullptr,
                    0);
  }
  Bytes info = VersionInfo(name, version, internal_name, parts);
  if (!UpdateResourceW(update, RT_VERSION, MAKEINTRESOURCEW(1),
                       MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                       info.data(), static_cast<DWORD>(info.size())) ||
      !EndUpdateResourceW(update, FALSE)) {
    std::fprintf(stderr, "exe_stamp: cannot write the version (%lu)\n",
                 GetLastError());
    return 1;
  }
  return 0;
}
