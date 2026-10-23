#include "core/bookmark_model.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

namespace {

constexpr std::int64_t kFileVersion = 1;

std::string Lower(std::string_view text) {
  std::string lower(text);
  for (char& c : lower) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return lower;
}

std::vector<std::string> Words(std::string_view text) {
  std::vector<std::string> words;
  std::string word;
  for (char c : text) {
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      if (!word.empty()) {
        words.push_back(Lower(word));
        word.clear();
      }
    } else {
      word += c;
    }
  }
  if (!word.empty()) {
    words.push_back(Lower(word));
  }
  return words;
}

std::optional<Error> CheckName(std::string_view name) {
  if (name.size() > BookmarkModel::kMaxNameBytes) {
    return Error{"The name is too long."};
  }
  return std::nullopt;
}

std::optional<Error> CheckUrl(std::string_view url) {
  if (url.empty()) {
    return Error{"The address is empty."};
  }
  if (url.size() > BookmarkModel::kMaxUrlBytes) {
    return Error{"The address is too long."};
  }
  return std::nullopt;
}

std::optional<Error> CheckProfile(const ProfileId& profile) {
  if (!profile.empty() && !IsValidProfileId(profile)) {
    return Error{"Unknown profile."};
  }
  return std::nullopt;
}

}  // namespace

BookmarkModel::BookmarkModel() {
  items_[kBarId] = {.id = kBarId,
                    .parent = 0,
                    .folder = true,
                    .name = "Bookmarks bar",
                    .url = "",
                    .profile = "",
                    .added = 0};
  items_[kOtherId] = {.id = kOtherId,
                      .parent = 0,
                      .folder = true,
                      .name = "Other bookmarks",
                      .url = "",
                      .profile = "",
                      .added = 0};
}

const BookmarkItem* BookmarkModel::Find(BookmarkId id) const {
  auto it = items_.find(id);
  return it == items_.end() ? nullptr : &it->second;
}

BookmarkItem* BookmarkModel::FindMutable(BookmarkId id) {
  auto it = items_.find(id);
  return it == items_.end() ? nullptr : &it->second;
}

Result<BookmarkId> BookmarkModel::Add(BookmarkItem item, std::size_t index) {
  BookmarkItem* parent = FindMutable(item.parent);
  if (parent == nullptr || !parent->folder) {
    return Error{"The folder does not exist."};
  }
  if (std::optional<Error> error = CheckName(item.name)) {
    return *error;
  }
  if (std::optional<Error> error = CheckProfile(item.profile)) {
    return *error;
  }
  if (!InheritedProfile(item.parent).empty() || !parent->profile.empty()) {
    item.profile.clear();
  }
  item.id = next_id_++;
  index = std::min(index, parent->children.size());
  parent->children.insert(
      parent->children.begin() + static_cast<std::ptrdiff_t>(index), item.id);
  BookmarkId id = item.id;
  items_[id] = std::move(item);
  Changed();
  return id;
}

Result<BookmarkId> BookmarkModel::AddBookmark(BookmarkId parent,
                                              std::size_t index,
                                              std::string name,
                                              std::string url,
                                              ProfileId profile,
                                              std::int64_t added) {
  if (std::optional<Error> error = CheckUrl(url)) {
    return *error;
  }
  return Add({.id = 0,
              .parent = parent,
              .folder = false,
              .name = std::move(name),
              .url = std::move(url),
              .profile = std::move(profile),
              .added = added},
             index);
}

Result<BookmarkId> BookmarkModel::AddFolder(BookmarkId parent,
                                            std::size_t index,
                                            std::string name,
                                            ProfileId profile,
                                            std::int64_t added) {
  return Add({.id = 0,
              .parent = parent,
              .folder = true,
              .name = std::move(name),
              .url = "",
              .profile = std::move(profile),
              .added = added},
             index);
}

std::optional<Error> BookmarkModel::Rename(BookmarkId id, std::string name) {
  BookmarkItem* item = FindMutable(id);
  if (item == nullptr || IsRoot(id)) {
    return Error{"This item cannot be renamed."};
  }
  if (std::optional<Error> error = CheckName(name)) {
    return error;
  }
  item->name = std::move(name);
  Changed();
  return std::nullopt;
}

std::optional<Error> BookmarkModel::SetUrl(BookmarkId id, std::string url) {
  BookmarkItem* item = FindMutable(id);
  if (item == nullptr || item->folder) {
    return Error{"Only a bookmark has an address."};
  }
  if (std::optional<Error> error = CheckUrl(url)) {
    return error;
  }
  item->url = std::move(url);
  Changed();
  return std::nullopt;
}

std::optional<Error> BookmarkModel::SetProfile(BookmarkId id,
                                               ProfileId profile) {
  BookmarkItem* item = FindMutable(id);
  if (item == nullptr || IsRoot(id)) {
    return Error{"This item cannot be linked to a profile."};
  }
  if (std::optional<Error> error = CheckProfile(profile)) {
    return error;
  }
  if (!InheritedProfile(id).empty()) {
    return Error{"The item follows the profile of its folder."};
  }
  item->profile = std::move(profile);
  if (item->folder && !item->profile.empty()) {
    ClearLinksInside(id);
  }
  Changed();
  return std::nullopt;
}

void BookmarkModel::ClearLinksInside(BookmarkId id) {
  const BookmarkItem* item = Find(id);
  if (item == nullptr) {
    return;
  }
  for (BookmarkId child : item->children) {
    if (BookmarkItem* inner = FindMutable(child)) {
      inner->profile.clear();
      ClearLinksInside(child);
    }
  }
}

bool BookmarkModel::IsInside(BookmarkId id, BookmarkId ancestor) const {
  for (const BookmarkItem* item = Find(id); item != nullptr;
       item = Find(item->parent)) {
    if (item->id == ancestor) {
      return true;
    }
  }
  return false;
}

std::optional<Error> BookmarkModel::Move(BookmarkId id,
                                         BookmarkId parent,
                                         std::size_t index) {
  BookmarkItem* item = FindMutable(id);
  BookmarkItem* target = FindMutable(parent);
  if (item == nullptr || IsRoot(id)) {
    return Error{"This item cannot be moved."};
  }
  if (target == nullptr || !target->folder) {
    return Error{"The folder does not exist."};
  }
  if (IsInside(parent, id)) {
    return Error{"A folder cannot be moved into itself."};
  }
  BookmarkItem* old_parent = FindMutable(item->parent);
  auto old_it = std::ranges::find(old_parent->children, id);
  auto old_index =
      static_cast<std::size_t>(old_it - old_parent->children.begin());
  old_parent->children.erase(old_it);
  if (parent == item->parent && index != kEnd && index > old_index) {
    --index;
  }
  index = std::min(index, target->children.size());
  target->children.insert(
      target->children.begin() + static_cast<std::ptrdiff_t>(index), id);
  item->parent = parent;
  if (!InheritedProfile(id).empty()) {
    item->profile.clear();
    ClearLinksInside(id);
  }
  Changed();
  return std::nullopt;
}

void BookmarkModel::Collect(BookmarkId id,
                            std::vector<BookmarkItem>& out) const {
  const BookmarkItem* item = Find(id);
  if (item == nullptr) {
    return;
  }
  out.push_back(*item);
  for (BookmarkId child : item->children) {
    Collect(child, out);
  }
}

std::optional<BookmarkRemoval> BookmarkModel::Remove(BookmarkId id) {
  const BookmarkItem* item = Find(id);
  if (item == nullptr || IsRoot(id)) {
    return std::nullopt;
  }
  BookmarkRemoval removal;
  removal.id = id;
  removal.parent = item->parent;
  BookmarkItem* parent = FindMutable(item->parent);
  auto it = std::ranges::find(parent->children, id);
  removal.index = static_cast<std::size_t>(it - parent->children.begin());
  parent->children.erase(it);
  Collect(id, removal.items);
  for (const BookmarkItem& removed : removal.items) {
    items_.erase(removed.id);
  }
  Changed();
  return removal;
}

BookmarkId BookmarkModel::Restore(const BookmarkRemoval& removal) {
  if (removal.items.empty() || Find(removal.id) != nullptr) {
    return 0;
  }
  BookmarkId parent_id = removal.parent;
  std::size_t index = removal.index;
  BookmarkItem* parent = FindMutable(parent_id);
  if (parent == nullptr || !parent->folder) {
    parent_id = kOtherId;
    parent = FindMutable(kOtherId);
    index = kEnd;
  }
  for (BookmarkItem item : removal.items) {
    if (item.id == removal.id) {
      item.parent = parent_id;
    }
    next_id_ = std::max(next_id_, item.id + 1);
    items_[item.id] = std::move(item);
  }
  index = std::min(index, parent->children.size());
  parent->children.insert(
      parent->children.begin() + static_cast<std::ptrdiff_t>(index),
      removal.id);
  if (!InheritedProfile(removal.id).empty()) {
    items_[removal.id].profile.clear();
    ClearLinksInside(removal.id);
  }
  Changed();
  return removal.id;
}

ProfileId BookmarkModel::InheritedProfile(BookmarkId id) const {
  const BookmarkItem* item = Find(id);
  if (item == nullptr) {
    return "";
  }
  for (const BookmarkItem* folder = Find(item->parent); folder != nullptr;
       folder = Find(folder->parent)) {
    if (!folder->profile.empty()) {
      return folder->profile;
    }
  }
  return "";
}

ProfileId BookmarkModel::EffectiveProfile(BookmarkId id) const {
  ProfileId inherited = InheritedProfile(id);
  if (!inherited.empty()) {
    return inherited;
  }
  const BookmarkItem* item = Find(id);
  return item == nullptr ? "" : item->profile;
}

bool BookmarkModel::IsVisibleIn(BookmarkId id, const ProfileId& profile) const {
  ProfileId effective = EffectiveProfile(id);
  return effective.empty() || effective == profile;
}

std::vector<BookmarkId> BookmarkModel::BookmarksWithUrl(
    std::string_view url,
    const ProfileId& profile) const {
  std::vector<BookmarkId> found;
  std::vector<BookmarkId> stack = {kOtherId, kBarId};
  while (!stack.empty()) {
    BookmarkId id = stack.back();
    stack.pop_back();
    const BookmarkItem* item = Find(id);
    if (item == nullptr || !IsVisibleIn(id, profile)) {
      continue;
    }
    if (!item->folder && item->url == url) {
      found.push_back(id);
    }
    stack.insert(stack.end(), item->children.rbegin(), item->children.rend());
  }
  return found;
}

std::vector<BookmarkId> BookmarkModel::Search(std::string_view query,
                                              const ProfileId& profile,
                                              std::size_t limit) const {
  std::vector<std::string> words = Words(query);
  std::vector<BookmarkId> found;
  if (words.empty()) {
    return found;
  }
  std::vector<BookmarkId> stack = {kOtherId, kBarId};
  while (!stack.empty() && found.size() < limit) {
    BookmarkId id = stack.back();
    stack.pop_back();
    const BookmarkItem* item = Find(id);
    if (item == nullptr || (!profile.empty() && !IsVisibleIn(id, profile))) {
      continue;
    }
    if (!item->folder) {
      std::string name = Lower(item->name);
      std::string url = Lower(item->url);
      bool all = std::ranges::all_of(words, [&](const std::string& word) {
        return name.find(word) != std::string::npos ||
               url.find(word) != std::string::npos;
      });
      if (all) {
        found.push_back(id);
      }
    }
    stack.insert(stack.end(), item->children.rbegin(), item->children.rend());
  }
  return found;
}

std::vector<BookmarkFolderEntry> BookmarkModel::Folders(
    const ProfileId& profile) const {
  std::vector<BookmarkFolderEntry> folders;
  std::vector<BookmarkFolderEntry> stack = {{.id = kOtherId, .depth = 0},
                                            {.id = kBarId, .depth = 0}};
  while (!stack.empty()) {
    BookmarkFolderEntry entry = stack.back();
    stack.pop_back();
    const BookmarkItem* item = Find(entry.id);
    if (item == nullptr || !item->folder ||
        (!profile.empty() && !IsVisibleIn(entry.id, profile))) {
      continue;
    }
    folders.push_back(entry);
    for (BookmarkId child : std::views::reverse(item->children)) {
      stack.push_back({.id = child, .depth = entry.depth + 1});
    }
  }
  return folders;
}

int BookmarkModel::CountLinkedTo(const ProfileId& profile) const {
  int count = 0;
  for (const auto& [id, item] : items_) {
    if (!profile.empty() && item.profile == profile) {
      ++count;
    }
  }
  return count;
}

int BookmarkModel::UnlinkProfile(const ProfileId& profile) {
  int count = 0;
  for (auto& [id, item] : items_) {
    if (!profile.empty() && item.profile == profile) {
      item.profile.clear();
      ++count;
    }
  }
  if (count > 0) {
    Changed();
  }
  return count;
}

namespace {

JsonValue ItemToJson(const BookmarkModel& model, const BookmarkItem& item) {
  JsonValue json = JsonValue::Object();
  json.Add("id", JsonValue::Integer(item.id));
  json.Add("type", JsonValue::String(item.folder ? "folder" : "url"));
  json.Add("name", JsonValue::String(item.name));
  if (!item.folder) {
    json.Add("url", JsonValue::String(item.url));
  }
  if (!item.profile.empty()) {
    json.Add("profile", JsonValue::String(item.profile));
  }
  json.Add("added", JsonValue::Integer(item.added));
  if (item.folder) {
    JsonValue children = JsonValue::Array();
    for (BookmarkId child : item.children) {
      if (const BookmarkItem* inner = model.Find(child)) {
        children.items.push_back(ItemToJson(model, *inner));
      }
    }
    json.Add("children", std::move(children));
  }
  return json;
}

}  // namespace

JsonValue BookmarkModel::ToJson() const {
  JsonValue roots = JsonValue::Object();
  roots.Add("bookmark_bar", ItemToJson(*this, *Find(kBarId)));
  roots.Add("other", ItemToJson(*this, *Find(kOtherId)));
  JsonValue json = JsonValue::Object();
  json.Add("version", JsonValue::Integer(kFileVersion));
  json.Add("next_id", JsonValue::Integer(next_id_));
  json.Add("roots", std::move(roots));
  return json;
}

Result<BookmarkModel> BookmarkModel::FromJson(const JsonValue& json) {
  if (json.kind != JsonValue::Kind::kObject) {
    return Error{"not a bookmark file"};
  }
  const JsonValue* roots = json.Find("roots");
  if (roots == nullptr || roots->kind != JsonValue::Kind::kObject) {
    return Error{"not a bookmark file"};
  }
  if (json.IntegerOr("version", 0) > kFileVersion) {
    return Error{"the bookmark file is from a newer version of the app"};
  }
  BookmarkModel model;
  model.next_id_ = std::max<BookmarkId>(3, json.IntegerOr("next_id", 3));
  // Ids are checked once the whole file is read: an item with a missing or
  // repeated id gets a new one.
  std::vector<BookmarkId> needs_id;
  struct Reader {
    BookmarkModel& model;
    std::vector<BookmarkId>& needs_id;
    BookmarkId temporary = -1;

    void ReadChildren(const JsonValue& folder, BookmarkId parent_id) {
      const JsonValue* children = folder.Find("children");
      if (children == nullptr || children->kind != JsonValue::Kind::kArray) {
        return;
      }
      for (const JsonValue& child : children->items) {
        if (child.kind != JsonValue::Kind::kObject) {
          continue;
        }
        std::string type = child.StringOr("type", "");
        BookmarkItem item;
        item.parent = parent_id;
        item.folder = type == "folder";
        item.name = child.StringOr("name", "");
        item.url = child.StringOr("url", "");
        item.profile = child.StringOr("profile", "");
        item.added = child.IntegerOr("added", 0);
        if ((type != "folder" && type != "url") ||
            (!item.folder && CheckUrl(item.url)) || CheckName(item.name)) {
          continue;
        }
        if (CheckProfile(item.profile)) {
          item.profile.clear();
        }
        BookmarkId id = child.IntegerOr("id", 0);
        if (id <= kOtherId || model.items_.contains(id)) {
          id = temporary--;
          needs_id.push_back(id);
        }
        item.id = id;
        model.next_id_ = std::max(model.next_id_, id + 1);
        model.items_[id] = item;
        model.items_[parent_id].children.push_back(id);
        if (item.folder) {
          ReadChildren(child, id);
        }
      }
    }
  };
  Reader reader{.model = model, .needs_id = needs_id};
  if (const JsonValue* bar = roots->Find("bookmark_bar")) {
    reader.ReadChildren(*bar, kBarId);
  }
  if (const JsonValue* other = roots->Find("other")) {
    reader.ReadChildren(*other, kOtherId);
  }
  for (BookmarkId temporary : needs_id) {
    BookmarkId id = model.next_id_++;
    auto node = model.items_.extract(temporary);
    node.key() = id;
    node.mapped().id = id;
    for (BookmarkId child : node.mapped().children) {
      model.items_[child].parent = id;
    }
    std::vector<BookmarkId>& siblings =
        model.items_[node.mapped().parent].children;
    std::ranges::replace(siblings, temporary, id);
    model.items_.insert(std::move(node));
  }
  // Items inside a linked folder follow it.
  for (auto& [id, item] : model.items_) {
    if (!model.InheritedProfile(id).empty()) {
      item.profile.clear();
    }
  }
  return model;
}

}  // namespace brouk
