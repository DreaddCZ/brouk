#include "core/bookmark_html.h"

#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace brouk {

namespace {

constexpr std::size_t kMaxDepth = 100;
constexpr int kMaxItems = 100000;

std::string Escape(std::string_view text) {
  std::string out;
  for (char c : text) {
    switch (c) {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      default:
        out += c;
    }
  }
  return out;
}

void ExportChildren(const BookmarkModel& model,
                    const BookmarkItem& folder,
                    int depth,
                    std::string& out) {
  std::string indent(static_cast<std::size_t>(depth) * 4, ' ');
  for (BookmarkId id : folder.children) {
    const BookmarkItem* item = model.Find(id);
    if (item == nullptr) {
      continue;
    }
    if (item->folder) {
      out += indent + "<DT><H3 ADD_DATE=\"" + std::to_string(item->added) +
             "\">" + Escape(item->name) + "</H3>\n";
      out += indent + "<DL><p>\n";
      ExportChildren(model, *item, depth + 1, out);
      out += indent + "</DL><p>\n";
    } else {
      out += indent + "<DT><A HREF=\"" + Escape(item->url) + "\" ADD_DATE=\"" +
             std::to_string(item->added) + "\">" + Escape(item->name) +
             "</A>\n";
    }
  }
}

void AppendUtf8(std::string& out, std::uint32_t code) {
  if (code == 0 || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) {
    out += "\xEF\xBF\xBD";  // U+FFFD
  } else if (code < 0x80) {
    out += static_cast<char>(code);
  } else if (code < 0x800) {
    out += static_cast<char>(0xC0 | (code >> 6));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else if (code < 0x10000) {
    out += static_cast<char>(0xE0 | (code >> 12));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (code >> 18));
    out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (code & 0x3F));
  }
}

// Decodes character references and collapses white space.
std::string DecodeText(std::string_view text) {
  std::string out;
  bool space = false;
  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      space = !out.empty();
      continue;
    }
    if (space) {
      out += ' ';
      space = false;
    }
    if (c != '&') {
      out += c;
      continue;
    }
    std::size_t end = text.find(';', i);
    if (end == std::string_view::npos || end - i > 10) {
      out += c;
      continue;
    }
    std::string_view entity = text.substr(i + 1, end - i - 1);
    if (entity == "amp") {
      out += '&';
    } else if (entity == "lt") {
      out += '<';
    } else if (entity == "gt") {
      out += '>';
    } else if (entity == "quot") {
      out += '"';
    } else if (entity == "apos") {
      out += '\'';
    } else if (entity == "nbsp") {
      out += ' ';
    } else if (entity.size() > 1 && entity[0] == '#') {
      bool hex = entity[1] == 'x' || entity[1] == 'X';
      std::string_view digits = entity.substr(hex ? 2 : 1);
      std::uint32_t code = 0;
      auto [ptr, ec] = std::from_chars(
          digits.data(), digits.data() + digits.size(), code, hex ? 16 : 10);
      if (ec != std::errc() || ptr != digits.data() + digits.size()) {
        out += c;
        continue;
      }
      AppendUtf8(out, code);
    } else {
      out += c;
      continue;
    }
    i = end;
  }
  return out;
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::size_t FindIgnoreCase(std::string_view text,
                           std::string_view word,
                           std::size_t from) {
  for (std::size_t i = from; i + word.size() <= text.size(); ++i) {
    if (EqualsIgnoreCase(text.substr(i, word.size()), word)) {
      return i;
    }
  }
  return std::string_view::npos;
}

// The value of attribute `name` in a tag's attribute text, decoded.
std::string Attribute(std::string_view attributes, std::string_view name) {
  std::size_t i = 0;
  while (i < attributes.size()) {
    while (i < attributes.size() &&
           std::isspace(static_cast<unsigned char>(attributes[i])) != 0) {
      ++i;
    }
    std::size_t start = i;
    while (i < attributes.size() && attributes[i] != '=' &&
           std::isspace(static_cast<unsigned char>(attributes[i])) == 0) {
      ++i;
    }
    std::string_view key = attributes.substr(start, i - start);
    if (key.empty()) {
      ++i;
      continue;
    }
    std::string_view value;
    if (i < attributes.size() && attributes[i] == '=') {
      ++i;
      if (i < attributes.size() &&
          (attributes[i] == '"' || attributes[i] == '\'')) {
        char quote = attributes[i++];
        std::size_t end = attributes.find(quote, i);
        if (end == std::string_view::npos) {
          end = attributes.size();
        }
        value = attributes.substr(i, end - i);
        i = end + 1;
      } else {
        std::size_t value_start = i;
        while (i < attributes.size() &&
               std::isspace(static_cast<unsigned char>(attributes[i])) == 0) {
          ++i;
        }
        value = attributes.substr(value_start, i - value_start);
      }
    }
    if (EqualsIgnoreCase(key, name)) {
      return DecodeText(value);
    }
  }
  return "";
}

bool IsImportableUrl(std::string_view url) {
  std::size_t colon = url.find(':');
  if (colon == std::string_view::npos) {
    return false;
  }
  std::string_view scheme = url.substr(0, colon);
  return EqualsIgnoreCase(scheme, "http") ||
         EqualsIgnoreCase(scheme, "https") || EqualsIgnoreCase(scheme, "ftp") ||
         EqualsIgnoreCase(scheme, "file");
}

std::int64_t Seconds(std::string_view text, std::int64_t fallback) {
  std::int64_t value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  return ec == std::errc() && value > 0 ? value : fallback;
}

}  // namespace

std::string ExportBookmarksHtml(const BookmarkModel& model) {
  std::string out =
      "<!DOCTYPE NETSCAPE-Bookmark-file-1>\n"
      "<!-- This is an automatically generated file.\n"
      "     It will be read and overwritten.\n"
      "     DO NOT EDIT! -->\n"
      "<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; "
      "charset=UTF-8\">\n"
      "<TITLE>Bookmarks</TITLE>\n"
      "<H1>Bookmarks</H1>\n"
      "<DL><p>\n";
  const BookmarkItem* bar = model.Find(BookmarkModel::kBarId);
  out += R"(    <DT><H3 ADD_DATE="0" PERSONAL_TOOLBAR_FOLDER="true">)" +
         Escape(bar->name) + "</H3>\n    <DL><p>\n";
  ExportChildren(model, *bar, 2, out);
  out += "    </DL><p>\n";
  // Other bookmarks go at the top level, as Chrome exports them.
  ExportChildren(model, *model.Find(BookmarkModel::kOtherId), 1, out);
  out += "</DL><p>\n";
  return out;
}

HtmlImportResult ImportBookmarksHtml(BookmarkModel& model,
                                     std::string_view html,
                                     std::int64_t now) {
  HtmlImportResult result;
  Result<BookmarkId> imported = model.AddFolder(
      BookmarkModel::kOtherId, BookmarkModel::kEnd, "Imported", "", now);
  if (!imported) {
    return result;
  }
  // The open <DL> lists; the first one of the file is the "Imported" folder.
  std::vector<BookmarkId> stack = {imported.value()};
  BookmarkId pending_folder = 0;  // An <H3> waiting for its <DL>.
  std::size_t i = 0;
  int items = 0;
  while (items < kMaxItems) {
    std::size_t open = html.find('<', i);
    if (open == std::string_view::npos) {
      break;
    }
    std::size_t close = html.find('>', open);
    if (close == std::string_view::npos) {
      break;
    }
    std::string_view tag = html.substr(open + 1, close - open - 1);
    std::size_t name_end = 0;
    while (name_end < tag.size() &&
           std::isspace(static_cast<unsigned char>(tag[name_end])) == 0) {
      ++name_end;
    }
    std::string_view name = tag.substr(0, name_end);
    std::string_view attributes = tag.substr(name_end);
    i = close + 1;
    if (EqualsIgnoreCase(name, "DL")) {
      BookmarkId next = pending_folder != 0 ? pending_folder : stack.back();
      // Too deep: the folder's contents go into its parent.
      stack.push_back(stack.size() > kMaxDepth ? stack.back() : next);
      pending_folder = 0;
    } else if (EqualsIgnoreCase(name, "/DL")) {
      if (stack.size() > 1) {
        stack.pop_back();
      }
      pending_folder = 0;
    } else if (EqualsIgnoreCase(name, "H3") || EqualsIgnoreCase(name, "A")) {
      bool folder = EqualsIgnoreCase(name, "H3");
      std::size_t end = FindIgnoreCase(html, folder ? "</H3" : "</A", i);
      if (end == std::string_view::npos) {
        break;
      }
      std::string title = DecodeText(html.substr(i, end - i));
      i = end;
      if (title.size() > BookmarkModel::kMaxNameBytes) {
        title.resize(BookmarkModel::kMaxNameBytes);
      }
      std::int64_t added = Seconds(Attribute(attributes, "ADD_DATE"), now);
      if (folder) {
        Result<BookmarkId> id = model.AddFolder(
            stack.back(), BookmarkModel::kEnd, title, "", added);
        if (id) {
          pending_folder = stack.size() > kMaxDepth ? 0 : id.value();
          ++result.folders;
          ++items;
        }
      } else {
        std::string url = Attribute(attributes, "HREF");
        if (!IsImportableUrl(url)) {
          continue;
        }
        if (model.AddBookmark(stack.back(), BookmarkModel::kEnd, title, url, "",
                              added)) {
          ++result.bookmarks;
          ++items;
        }
      }
    }
  }
  if (result.bookmarks == 0 && result.folders == 0) {
    model.Remove(imported.value());
    return result;
  }
  result.folder = imported.value();
  return result;
}

}  // namespace brouk
