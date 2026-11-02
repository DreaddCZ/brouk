// A build tool: gives the app's executable (CEF's prebuilt bootstrap.exe,
// renamed) Brouk's version information, which Windows shows as the app's
// name (the taskbar button's menu, Task Manager, the file's properties)
// instead of "CEF Bootstrap Application" (024), and Brouk's icon. CEF
// supports changing the bootstrap's resources.
//
//   exe_stamp <exe> <name> <version> <internal name> <icon .ico>
//
// The version is "major.minor.patch". Runs at build time only.

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

// RT_VERSION, RT_ICON and RT_GROUP_ICON as wide resource types (the
// RT_ macros follow UNICODE, which Brouk does not define).
const LPCWSTR kVersionType = MAKEINTRESOURCEW(16);
const LPCWSTR kIconType = MAKEINTRESOURCEW(3);
const LPCWSTR kGroupIconType = MAKEINTRESOURCEW(14);

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

WORD ReadWord(const Bytes& data, std::size_t at) {
  return static_cast<WORD>(data[at] | (data[at + 1] << 8));
}

DWORD ReadDword(const Bytes& data, std::size_t at) {
  return static_cast<DWORD>(ReadWord(data, at)) |
         (static_cast<DWORD>(ReadWord(data, at + 2)) << 16);
}

// One image of an .ico file and its directory entry.
struct IconImage {
  Bytes entry;  // width, height, colours, reserved, planes, bit count.
  Bytes data;
};

// The images of an .ico file, or nothing when it is not one.
std::vector<IconImage> ReadIcon(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  const Bytes data((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  if (data.size() < 6 || ReadWord(data, 0) != 0 || ReadWord(data, 2) != 1) {
    return {};
  }
  const WORD count = ReadWord(data, 4);
  std::vector<IconImage> images;
  for (WORD i = 0; i < count; ++i) {
    const std::size_t entry = 6 + 16 * static_cast<std::size_t>(i);
    if (entry + 16 > data.size()) {
      return {};
    }
    const DWORD size = ReadDword(data, entry + 8);
    const DWORD offset = ReadDword(data, entry + 12);
    if (offset > data.size() || size > data.size() - offset) {
      return {};
    }
    IconImage image;
    image.entry.assign(data.begin() + static_cast<std::ptrdiff_t>(entry),
                       data.begin() + static_cast<std::ptrdiff_t>(entry + 8));
    const auto begin = data.begin() + static_cast<std::ptrdiff_t>(offset);
    image.data.assign(begin, begin + static_cast<std::ptrdiff_t>(size));
    images.push_back(std::move(image));
  }
  return images;
}

// A resource name: a number or a string, kept beyond the enumeration.
struct ResourceName {
  WORD id = 0;  // 0: a string name.
  std::wstring text;
  LPCWSTR Get() const { return id != 0 ? MAKEINTRESOURCEW(id) : text.c_str(); }
};

// A resource of the executable before the update, to remove.
struct OldResource {
  LPCWSTR type = nullptr;
  ResourceName name;
  std::vector<WORD> languages;
};

BOOL CALLBACK CollectName(HMODULE /*module*/,
                          LPCWSTR /*type*/,
                          LPWSTR name,
                          LONG_PTR param) {
  ResourceName entry;
  if (IS_INTRESOURCE(name)) {
    entry.id = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));
  } else {
    entry.text = name;
  }
  reinterpret_cast<std::vector<ResourceName>*>(param)->push_back(entry);
  return TRUE;
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
  if (argc != 6) {
    std::fputs(
        "usage: exe_stamp <exe> <name> <version> <internal name> <icon>\n",
        stderr);
    return 2;
  }
  const std::wstring exe = argv[1];
  const std::wstring name = argv[2];
  const std::wstring version = argv[3];
  const std::wstring internal_name = argv[4];
  const std::vector<IconImage> icon = ReadIcon(argv[5]);
  if (icon.empty()) {
    std::fputs("exe_stamp: the icon is not an .ico file\n", stderr);
    return 2;
  }
  std::string narrow_version;
  for (wchar_t c : version) {
    narrow_version.push_back(static_cast<char>(c));
  }
  WORD parts[3] = {};
  if (!ParseVersion(narrow_version.c_str(), parts)) {
    std::fputs("exe_stamp: the version must be major.minor.patch\n", stderr);
    return 2;
  }

  // The version and icon resources there now (CEF's), to replace. Each
  // icon group keeps its name (code may load it by it) and gets Brouk's
  // images; Windows shows the first group as the file's icon.
  std::vector<WORD> version_languages;
  std::vector<OldResource> old_resources;
  if (HMODULE module = LoadLibraryExW(
          exe.c_str(), nullptr,
          LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) {
    EnumResourceLanguagesW(module, kVersionType, MAKEINTRESOURCEW(1),
                           &CollectLanguage,
                           reinterpret_cast<LONG_PTR>(&version_languages));
    for (LPCWSTR type : {kGroupIconType, kIconType}) {
      std::vector<ResourceName> names;
      EnumResourceNamesW(module, type, &CollectName,
                         reinterpret_cast<LONG_PTR>(&names));
      for (ResourceName& resource_name : names) {
        OldResource old{type, std::move(resource_name), {}};
        EnumResourceLanguagesW(module, type, old.name.Get(), &CollectLanguage,
                               reinterpret_cast<LONG_PTR>(&old.languages));
        old_resources.push_back(std::move(old));
      }
    }
    FreeLibrary(module);
  }
  std::vector<ResourceName> groups;
  for (const OldResource& old : old_resources) {
    if (old.type == kGroupIconType) {
      groups.push_back(old.name);
    }
  }
  if (groups.empty()) {
    groups.push_back({1, {}});
  }

  HANDLE update = BeginUpdateResourceW(exe.c_str(), FALSE);
  if (!update) {
    std::fprintf(stderr, "exe_stamp: cannot open the executable (%lu)\n",
                 GetLastError());
    return 1;
  }
  for (WORD language : version_languages) {
    UpdateResourceW(update, kVersionType, MAKEINTRESOURCEW(1), language,
                    nullptr, 0);
  }
  for (const OldResource& old : old_resources) {
    for (WORD language : old.languages) {
      UpdateResourceW(update, old.type, old.name.Get(), language, nullptr, 0);
    }
  }
  const WORD language = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
  auto write = [&update, language](LPCWSTR type, LPCWSTR resource_name,
                                   Bytes data) {
    return UpdateResourceW(update, type, resource_name, language, data.data(),
                           static_cast<DWORD>(data.size())) != FALSE;
  };
  bool written = write(kVersionType, MAKEINTRESOURCEW(1),
                       VersionInfo(name, version, internal_name, parts));
  // The images as RT_ICON 1…n, and a group directory pointing at them:
  // the .ico's entries with the image's ID in place of its file offset.
  Bytes group;
  AppendWord(group, 0);  // Reserved.
  AppendWord(group, 1);  // Icons.
  AppendWord(group, static_cast<WORD>(icon.size()));
  for (std::size_t i = 0; i < icon.size() && written; ++i) {
    const auto id = static_cast<WORD>(i + 1);
    written = write(kIconType, MAKEINTRESOURCEW(id), icon[i].data);
    Append(group, icon[i].entry.data(), icon[i].entry.size());
    const auto size = static_cast<DWORD>(icon[i].data.size());
    Append(group, &size, sizeof(size));
    AppendWord(group, id);
  }
  for (const ResourceName& group_name : groups) {
    written = written && write(kGroupIconType, group_name.Get(), group);
  }
  if (!written) {
    std::fprintf(stderr, "exe_stamp: cannot write the resources (%lu)\n",
                 GetLastError());
    EndUpdateResourceW(update, TRUE);  // Discards the changes.
    return 1;
  }
  if (!EndUpdateResourceW(update, FALSE)) {
    std::fprintf(stderr, "exe_stamp: cannot save the executable (%lu)\n",
                 GetLastError());
    return 1;
  }
  return 0;
}
