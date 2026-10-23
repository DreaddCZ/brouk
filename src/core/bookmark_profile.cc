#include "core/bookmark_profile.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace brouk {

namespace {

// Deeper items in a file are not read (the file is untrusted).
constexpr int kMaxDepth = 100;
constexpr int kMaxItems = 100000;

YamlNode Text(const std::string& text) {
  return YamlNode::Scalar(text, false);
}

YamlNode ItemYaml(const BookmarkModel& model, const BookmarkItem& item) {
  YamlNode node = YamlNode::Map();
  node.GetOrAdd("type") =
      YamlNode::Scalar(item.folder ? "folder" : "url", true);
  node.GetOrAdd("name") = Text(item.name);
  if (item.folder) {
    YamlNode children;
    children.kind = YamlNode::Kind::kSequence;
    for (BookmarkId child : item.children) {
      if (const BookmarkItem* inner = model.Find(child)) {
        children.items.push_back(ItemYaml(model, *inner));
      }
    }
    node.GetOrAdd("children") = children;
  } else {
    node.GetOrAdd("url") = Text(item.url);
  }
  return node;
}

void CollectLinked(const BookmarkModel& model,
                   BookmarkId folder_id,
                   const ProfileId& profile,
                   std::vector<BookmarkId>& out) {
  for (BookmarkId id : model.Find(folder_id)->children) {
    const BookmarkItem* item = model.Find(id);
    if (item->profile == profile) {
      out.push_back(id);  // Its contents go with it.
    } else if (item->folder) {
      CollectLinked(model, id, profile, out);
    }
  }
}

std::string ScalarOr(const YamlNode& map, const char* key) {
  const YamlNode* value = map.Find(key);
  return value && value->kind == YamlNode::Kind::kScalar ? value->scalar : "";
}

// A general folder named `name` in `parent`, created if there is none.
BookmarkId FolderNamed(BookmarkModel& model,
                       BookmarkId parent,
                       const std::string& name,
                       std::int64_t now) {
  for (BookmarkId id : model.Find(parent)->children) {
    const BookmarkItem* item = model.Find(id);
    if (item->folder && item->name == name &&
        model.EffectiveProfile(id).empty()) {
      return id;
    }
  }
  Result<BookmarkId> created =
      model.AddFolder(parent, BookmarkModel::kEnd, name, "", now);
  return created ? created.value() : 0;
}

struct Importer {
  BookmarkModel& model;
  std::int64_t now = 0;
  int items = 0;

  // Adds `node` (and its contents) at the end of `parent`; true if it was
  // valid.
  bool Add(const YamlNode& node,
           BookmarkId parent,
           const ProfileId& profile,
           int depth) {
    if (node.kind != YamlNode::Kind::kMap || depth > kMaxDepth ||
        items >= kMaxItems) {
      return false;
    }
    std::string type = ScalarOr(node, "type");
    std::string name = ScalarOr(node, "name");
    if (type == "url") {
      Result<BookmarkId> id =
          model.AddBookmark(parent, BookmarkModel::kEnd, name,
                            ScalarOr(node, "url"), profile, now);
      items += id ? 1 : 0;
      return id.ok();
    }
    if (type != "folder") {
      return false;
    }
    Result<BookmarkId> id =
        model.AddFolder(parent, BookmarkModel::kEnd, name, profile, now);
    if (!id) {
      return false;
    }
    ++items;
    if (const YamlNode* children = node.Find("children");
        children && children->kind == YamlNode::Kind::kSequence) {
      for (const YamlNode& child : children->items) {
        Add(child, id.value(), "", depth + 1);  // They follow the folder.
      }
    }
    return true;
  }
};

}  // namespace

YamlNode ExportProfileBookmarks(const BookmarkModel& model,
                                const ProfileId& profile) {
  YamlNode list;
  list.kind = YamlNode::Kind::kSequence;
  if (profile.empty()) {
    return list;
  }
  for (BookmarkId root : {BookmarkModel::kBarId, BookmarkModel::kOtherId}) {
    std::vector<BookmarkId> linked;
    CollectLinked(model, root, profile, linked);
    for (BookmarkId id : linked) {
      YamlNode entry = YamlNode::Map();
      entry.GetOrAdd("root") = YamlNode::Scalar(
          root == BookmarkModel::kBarId ? "bar" : "other", true);
      YamlNode path;
      path.kind = YamlNode::Kind::kSequence;
      std::vector<std::string> names;
      for (const BookmarkItem* folder = model.Find(model.Find(id)->parent);
           folder != nullptr && !BookmarkModel::IsRoot(folder->id);
           folder = model.Find(folder->parent)) {
        names.insert(names.begin(), folder->name);
      }
      for (const std::string& name : names) {
        path.items.push_back(Text(name));
      }
      entry.GetOrAdd("path") = path;
      entry.GetOrAdd("item") = ItemYaml(model, *model.Find(id));
      list.items.push_back(entry);
    }
  }
  return list;
}

ProfileBookmarksImport ImportProfileBookmarks(BookmarkModel& model,
                                              const YamlNode& node,
                                              const ProfileId& profile,
                                              std::int64_t now) {
  ProfileBookmarksImport result;
  if (node.kind != YamlNode::Kind::kSequence) {
    result.problems.emplace_back("Bookmarks: the section could not be read.");
    return result;
  }
  Importer importer{.model = model, .now = now};
  for (std::size_t i = 0; i < node.items.size(); ++i) {
    const YamlNode& entry = node.items[i];
    std::string problem =
        "Bookmarks: entry " + std::to_string(i + 1) + " could not be read.";
    if (entry.kind != YamlNode::Kind::kMap) {
      result.problems.push_back(problem);
      continue;
    }
    std::string root = ScalarOr(entry, "root");
    BookmarkId parent = root == "bar"     ? BookmarkModel::kBarId
                        : root == "other" ? BookmarkModel::kOtherId
                                          : 0;
    const YamlNode* path = entry.Find("path");
    const YamlNode* item = entry.Find("item");
    if (parent == 0 || item == nullptr ||
        (path != nullptr && path->kind != YamlNode::Kind::kSequence)) {
      result.problems.push_back(problem);
      continue;
    }
    bool ok = true;
    if (path != nullptr) {
      for (const YamlNode& name : path->items) {
        if (name.kind != YamlNode::Kind::kScalar || parent == 0) {
          ok = false;
          break;
        }
        parent = FolderNamed(model, parent, name.scalar, now);
      }
    }
    if (!ok || parent == 0 || !importer.Add(*item, parent, profile, 0)) {
      result.problems.push_back(problem);
    }
  }
  result.items = importer.items;
  return result;
}

}  // namespace brouk
