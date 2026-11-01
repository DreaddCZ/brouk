#include "core/session.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "core/profile_id.h"
#include "core/yaml_io.h"

namespace brouk {

namespace {

constexpr std::size_t kMaxWindows = 50;
constexpr std::size_t kMaxTabsPerWindow = 1000;
constexpr std::size_t kMaxUrlLength = 8192;
constexpr std::size_t kMaxTitleLength = 1024;
constexpr int kMaxCoordinate = 100000;
constexpr std::size_t kMaxFileBytes = std::size_t{16} * 1024 * 1024;

YamlNode Text(std::string_view text) {
  return YamlNode::Scalar(std::string(text), false);
}

YamlNode Plain(std::string text) {
  return YamlNode::Scalar(std::move(text), true);
}

const std::string* ScalarOf(const YamlNode& map, std::string_view key) {
  const YamlNode* node = map.Find(key);
  return node && node->kind == YamlNode::Kind::kScalar ? &node->scalar
                                                       : nullptr;
}

std::optional<int> NumberOf(const YamlNode& map, std::string_view key) {
  const std::string* text = ScalarOf(map, key);
  if (!text) {
    return std::nullopt;
  }
  int value = 0;
  auto [end, error] =
      std::from_chars(text->data(), text->data() + text->size(), value);
  if (error != std::errc() || end != text->data() + text->size() ||
      value < -kMaxCoordinate || value > kMaxCoordinate) {
    return std::nullopt;
  }
  return value;
}

std::optional<SessionTab> TabFromYaml(const YamlNode& node) {
  if (node.kind != YamlNode::Kind::kMap) {
    return std::nullopt;
  }
  const std::string* url = ScalarOf(node, "url");
  const std::string* profile = ScalarOf(node, "profile");
  if (!url || url->empty() || url->size() > kMaxUrlLength || !profile ||
      !IsValidProfileId(*profile)) {
    return std::nullopt;
  }
  SessionTab tab{.url = *url, .title = "", .profile = *profile};
  if (const std::string* title = ScalarOf(node, "title")) {
    tab.title = title->substr(0, kMaxTitleLength);
  }
  const std::string* pinned = ScalarOf(node, "pinned");
  tab.pinned = pinned && *pinned == "true";
  return tab;
}

std::optional<SessionWindow> WindowFromYaml(const YamlNode& node) {
  if (node.kind != YamlNode::Kind::kMap) {
    return std::nullopt;
  }
  SessionWindow window;
  const YamlNode* tabs = node.Find("tabs");
  if (!tabs || tabs->kind != YamlNode::Kind::kSequence) {
    return std::nullopt;
  }
  std::optional<int> active = NumberOf(node, "active");
  std::size_t active_index = 0;
  for (std::size_t i = 0;
       i < tabs->items.size() && window.tabs.size() < kMaxTabsPerWindow; ++i) {
    if (std::optional<SessionTab> tab = TabFromYaml(tabs->items[i])) {
      if (active && std::cmp_equal(*active, i)) {
        active_index = window.tabs.size();
      }
      window.tabs.push_back(std::move(*tab));
    }
  }
  if (window.tabs.empty()) {
    return std::nullopt;
  }
  window.active = active_index;
  if (const YamlNode* collapsed = node.Find("collapsed");
      collapsed && collapsed->kind == YamlNode::Kind::kSequence) {
    for (const YamlNode& item : collapsed->items) {
      if (item.kind == YamlNode::Kind::kScalar &&
          IsValidProfileId(item.scalar)) {
        window.collapsed.push_back(item.scalar);
      }
    }
  }
  std::optional<int> x = NumberOf(node, "x");
  std::optional<int> y = NumberOf(node, "y");
  std::optional<int> width = NumberOf(node, "width");
  std::optional<int> height = NumberOf(node, "height");
  if (x && y && width && height && *width >= 100 && *height >= 100) {
    window.x = *x;
    window.y = *y;
    window.width = *width;
    window.height = *height;
  }
  const std::string* maximized = ScalarOf(node, "maximized");
  window.maximized = maximized && *maximized == "true";
  return window;
}

}  // namespace

YamlNode SessionToYaml(const Session& session) {
  YamlNode root = YamlNode::Map();
  root.GetOrAdd("clean_exit") = Plain(session.clean_exit ? "true" : "false");
  YamlNode& windows = root.GetOrAdd("windows");
  windows.kind = YamlNode::Kind::kSequence;
  for (const SessionWindow& window : session.windows) {
    YamlNode node = YamlNode::Map();
    node.GetOrAdd("active") = Plain(std::to_string(window.active));
    if (window.width > 0) {
      node.GetOrAdd("x") = Plain(std::to_string(window.x));
      node.GetOrAdd("y") = Plain(std::to_string(window.y));
      node.GetOrAdd("width") = Plain(std::to_string(window.width));
      node.GetOrAdd("height") = Plain(std::to_string(window.height));
    }
    if (window.maximized) {
      node.GetOrAdd("maximized") = Plain("true");
    }
    if (!window.collapsed.empty()) {
      YamlNode& collapsed = node.GetOrAdd("collapsed");
      collapsed.kind = YamlNode::Kind::kSequence;
      for (const ProfileId& profile : window.collapsed) {
        collapsed.items.push_back(Text(profile));
      }
    }
    YamlNode& tabs = node.GetOrAdd("tabs");
    tabs.kind = YamlNode::Kind::kSequence;
    for (const SessionTab& tab : window.tabs) {
      YamlNode item = YamlNode::Map();
      item.GetOrAdd("url") = Text(tab.url);
      item.GetOrAdd("title") = Text(tab.title);
      item.GetOrAdd("profile") = Text(tab.profile);
      if (tab.pinned) {
        item.GetOrAdd("pinned") = Plain("true");
      }
      tabs.items.push_back(std::move(item));
    }
    windows.items.push_back(std::move(node));
  }
  return root;
}

Session SessionFromYaml(const YamlNode& root) {
  Session session;
  if (root.kind != YamlNode::Kind::kMap) {
    return session;
  }
  const std::string* clean = ScalarOf(root, "clean_exit");
  session.clean_exit = !clean || *clean != "false";
  const YamlNode* windows = root.Find("windows");
  if (!windows || windows->kind != YamlNode::Kind::kSequence) {
    return session;
  }
  for (const YamlNode& node : windows->items) {
    if (session.windows.size() >= kMaxWindows) {
      break;
    }
    if (std::optional<SessionWindow> window = WindowFromYaml(node)) {
      session.windows.push_back(std::move(*window));
    }
  }
  return session;
}

Result<Session> LoadSession(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return Session{};
  }
  if (std::filesystem::file_size(path, error) > kMaxFileBytes || error) {
    return Error{"The saved session is too large or unreadable."};
  }
  std::ifstream file(path, std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(file)),
                   std::istreambuf_iterator<char>());
  if (!file && !file.eof()) {
    return Error{"Could not read " + path.string()};
  }
  Result<YamlNode> root = ParseYaml(text);
  if (!root) {
    return Error{"The saved session could not be read: " +
                 root.error().message};
  }
  return SessionFromYaml(root.value());
}

std::optional<Error> SaveSession(const std::filesystem::path& path,
                                 const Session& session) {
  std::string yaml = EmitYaml(SessionToYaml(session));
  std::filesystem::path temp = path;
  temp += ".tmp";
  std::error_code error;
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    file << "# Windows and tabs of the last run (written by Brouk).\n" << yaml;
    file.close();
    if (!file) {
      std::filesystem::remove(temp, error);
      return Error{"cannot write " + temp.string()};
    }
  }
  std::filesystem::permissions(
      temp,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      error);
  std::filesystem::rename(temp, path, error);
  if (error) {
    std::error_code ignored;
    std::filesystem::remove(temp, ignored);
    return Error{"cannot replace " + path.string() + ": " + error.message()};
  }
  return std::nullopt;
}

StartupAction DecideStartup(std::string_view restore_setting,
                            const Session& last) {
  if (last.windows.empty()) {
    return StartupAction::kNewWindow;
  }
  if (restore_setting == "last_session") {
    return StartupAction::kRestore;
  }
  return last.clean_exit ? StartupAction::kNewWindow
                         : StartupAction::kNewWindowOfferRestore;
}

}  // namespace brouk
