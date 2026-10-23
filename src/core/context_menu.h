#ifndef BROUK_CORE_CONTEXT_MENU_H_
#define BROUK_CORE_CONTEXT_MENU_H_

#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// Commands of the page context menu (012, item 12).
enum class MenuCommand {
  kSeparator,
  // Page.
  kBack,
  kForward,
  kReload,
  kPrint,
  kViewSource,
  kInspect,
  // Link.
  kOpenLinkInNewTab,
  kOpenLinkInNewWindow,
  kSaveLinkAs,
  kCopyLinkAddress,
  // Image.
  kOpenImageInNewTab,
  kSaveImageAs,
  kCopyImage,
  kCopyImageAddress,
  // Selection and editing.
  kSearchForSelection,
  kSpellingSuggestion,  // `index` selects the suggestion.
  kAddToDictionary,
  kUndo,
  kRedo,
  kCut,
  kCopy,
  kPaste,
  kPasteAndMatchStyle,
  kSelectAll,
};

struct MenuItem {
  MenuCommand command = MenuCommand::kSeparator;
  std::string label;
  bool enabled = true;
  int index = 0;  // For kSpellingSuggestion.
};

// What was right-clicked, from Chromium's context menu parameters.
struct ContextMenuContext {
  bool has_link = false;
  bool has_image = false;
  bool is_editable = false;
  std::string selection_text;
  bool can_go_back = false;
  bool can_go_forward = false;
  // Editable fields only.
  bool can_undo = false;
  bool can_redo = false;
  bool can_cut = false;
  bool can_copy = false;
  bool can_paste = false;
  bool can_select_all = false;
  std::string misspelled_word;
  std::vector<std::string> spelling_suggestions;
  // Name of the default search engine, for "Search Google for …".
  std::string search_engine_name = "Google";
};

// Builds Chrome's context menu for what was clicked: link, image, selection
// and editable-field sections, or the page menu when nothing specific was
// clicked. "Inspect" ends every menu. No leading, trailing or doubled
// separators.
std::vector<MenuItem> BuildContextMenu(const ContextMenuContext& context);

// The context menu of internal pages (settings, profile manager, our dev
// tools): editing commands in fields, Copy for a selection, otherwise
// Reload; "Inspect" at the end, so the pages can be debugged like web pages
// (006). Nothing that opens web content.
std::vector<MenuItem> BuildInternalPageContextMenu(
    const ContextMenuContext& context);

// Chrome's label for searching the selection, e.g.
// Search Google for “some text”, with whitespace collapsed and long
// selections shortened to 50 characters plus "…".
std::string SearchSelectionLabel(std::string_view engine_name,
                                 std::string_view selection);

}  // namespace brouk

#endif  // BROUK_CORE_CONTEXT_MENU_H_
