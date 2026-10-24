#include "core/context_menu.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

namespace {

constexpr std::size_t kMaxSelectionChars = 50;

class MenuBuilder {
 public:
  void Add(MenuCommand command, std::string label, bool enabled = true) {
    items_.push_back({command, std::move(label), enabled, 0});
  }
  void AddSuggestion(int index, std::string label) {
    items_.push_back(
        {MenuCommand::kSpellingSuggestion, std::move(label), true, index});
  }
  // Separators only between non-empty sections.
  void Separator() {
    if (!items_.empty() && items_.back().command != MenuCommand::kSeparator) {
      items_.push_back({});
    }
  }
  std::vector<MenuItem> Take() {
    while (!items_.empty() &&
           items_.back().command == MenuCommand::kSeparator) {
      items_.pop_back();
    }
    return std::move(items_);
  }

 private:
  std::vector<MenuItem> items_;
};

bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}

// Number of bytes of the UTF-8 sequence starting with `lead`.
std::size_t Utf8Length(unsigned char lead) {
  if (lead < 0x80) {
    return 1;
  }
  if ((lead >> 5) == 0x6) {
    return 2;
  }
  if ((lead >> 4) == 0xE) {
    return 3;
  }
  if ((lead >> 3) == 0x1E) {
    return 4;
  }
  return 1;  // Invalid lead byte: treat as one character.
}

}  // namespace

std::string SearchSelectionLabel(std::string_view engine_name,
                                 std::string_view selection) {
  // Collapse whitespace runs into single spaces and trim.
  std::string collapsed;
  bool pending_space = false;
  for (char c : selection) {
    if (IsSpace(c)) {
      pending_space = !collapsed.empty();
      continue;
    }
    if (pending_space) {
      collapsed.push_back(' ');
      pending_space = false;
    }
    collapsed.push_back(c);
  }
  // Shorten to whole UTF-8 characters.
  std::size_t chars = 0;
  std::size_t pos = 0;
  while (pos < collapsed.size() && chars < kMaxSelectionChars) {
    pos += Utf8Length(static_cast<unsigned char>(collapsed[pos]));
    ++chars;
  }
  if (pos < collapsed.size()) {
    collapsed.resize(pos);
    collapsed += "…";
  }
  return "Search " + std::string(engine_name) + " for “" + collapsed + "”";
}

std::vector<MenuItem> BuildContextMenu(const ContextMenuContext& context) {
  MenuBuilder menu;
  bool has_selection = !context.selection_text.empty();

  if (context.is_editable) {
    if (!context.misspelled_word.empty()) {
      int index = 0;
      for (const std::string& suggestion : context.spelling_suggestions) {
        menu.AddSuggestion(index++, suggestion);
      }
      if (context.spelling_suggestions.empty()) {
        menu.Add(MenuCommand::kSpellingSuggestion, "No Guesses Found",
                 /*enabled=*/false);
      }
      menu.Add(MenuCommand::kAddToDictionary, "Add to Dictionary");
      menu.Separator();
    }
    if (has_selection) {
      menu.Add(MenuCommand::kSearchForSelection,
               SearchSelectionLabel(context.search_engine_name,
                                    context.selection_text));
      menu.Separator();
    }
    menu.Add(MenuCommand::kUndo, "Undo", context.can_undo);
    menu.Add(MenuCommand::kRedo, "Redo", context.can_redo);
    menu.Separator();
    menu.Add(MenuCommand::kCut, "Cut", context.can_cut);
    menu.Add(MenuCommand::kCopy, "Copy", context.can_copy);
    menu.Add(MenuCommand::kPaste, "Paste", context.can_paste);
    menu.Add(MenuCommand::kPasteAndMatchStyle, "Paste and Match Style",
             context.can_paste);
    menu.Add(MenuCommand::kSelectAll, "Select All", context.can_select_all);
    menu.Separator();
  } else {
    if (context.has_link) {
      menu.Add(MenuCommand::kOpenLinkInNewTab, "Open Link in New Tab");
      menu.Add(MenuCommand::kOpenLinkInNewWindow, "Open Link in New Window");
      menu.Separator();
      menu.Add(MenuCommand::kSaveLinkAs, "Save Link As…");
      menu.Add(MenuCommand::kCopyLinkAddress, "Copy Link Address");
      menu.Separator();
    }
    if (context.has_image) {
      menu.Add(MenuCommand::kOpenImageInNewTab, "Open Image in New Tab");
      menu.Add(MenuCommand::kSaveImageAs, "Save Image As…");
      menu.Add(MenuCommand::kCopyImage, "Copy Image");
      menu.Add(MenuCommand::kCopyImageAddress, "Copy Image Address");
      menu.Separator();
    }
    if (has_selection) {
      menu.Add(MenuCommand::kCopy, "Copy");
      menu.Add(MenuCommand::kSearchForSelection,
               SearchSelectionLabel(context.search_engine_name,
                                    context.selection_text));
      menu.Add(MenuCommand::kPrint, "Print…");
      menu.Separator();
    }
    if (!context.has_link && !context.has_image && !has_selection) {
      menu.Add(MenuCommand::kBack, "Back", context.can_go_back);
      menu.Add(MenuCommand::kForward, "Forward", context.can_go_forward);
      menu.Add(MenuCommand::kReload, "Reload");
      menu.Separator();
      menu.Add(MenuCommand::kPrint, "Print…");
      menu.Separator();
      menu.Add(MenuCommand::kViewSource, "View Page Source");
    }
    menu.Separator();
  }
  if (context.can_block_element && !context.is_editable) {
    menu.Add(MenuCommand::kBlockElement, "Block Element…");
  }
  menu.Add(MenuCommand::kInspect, "Inspect");
  return menu.Take();
}

std::vector<MenuItem> BuildInternalPageContextMenu(
    const ContextMenuContext& context) {
  MenuBuilder menu;
  if (context.is_editable) {
    menu.Add(MenuCommand::kUndo, "Undo", context.can_undo);
    menu.Add(MenuCommand::kRedo, "Redo", context.can_redo);
    menu.Separator();
    menu.Add(MenuCommand::kCut, "Cut", context.can_cut);
    menu.Add(MenuCommand::kCopy, "Copy", context.can_copy);
    menu.Add(MenuCommand::kPaste, "Paste", context.can_paste);
    menu.Add(MenuCommand::kSelectAll, "Select All", context.can_select_all);
  } else if (!context.selection_text.empty()) {
    menu.Add(MenuCommand::kCopy, "Copy");
  } else {
    menu.Add(MenuCommand::kReload, "Reload");
  }
  menu.Separator();
  menu.Add(MenuCommand::kInspect, "Inspect");
  return menu.Take();
}

}  // namespace brouk
