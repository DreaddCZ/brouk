#include "cef/context_menu_model.h"

#include <optional>
#include <string>
#include <vector>

namespace brouk {

namespace {

// CEF's built-in command id, or nullopt for commands we implement.
std::optional<int> BuiltInId(const MenuItem& item) {
  switch (item.command) {
    case MenuCommand::kBack:
      return MENU_ID_BACK;
    case MenuCommand::kForward:
      return MENU_ID_FORWARD;
    case MenuCommand::kReload:
      return MENU_ID_RELOAD;
    case MenuCommand::kPrint:
      return MENU_ID_PRINT;
    case MenuCommand::kViewSource:
      return MENU_ID_VIEW_SOURCE;
    case MenuCommand::kUndo:
      return MENU_ID_UNDO;
    case MenuCommand::kRedo:
      return MENU_ID_REDO;
    case MenuCommand::kCut:
      return MENU_ID_CUT;
    case MenuCommand::kCopy:
      return MENU_ID_COPY;
    case MenuCommand::kPaste:
      return MENU_ID_PASTE;
    case MenuCommand::kPasteAndMatchStyle:
      return MENU_ID_PASTE_MATCH_STYLE;
    case MenuCommand::kSelectAll:
      return MENU_ID_SELECT_ALL;
    case MenuCommand::kAddToDictionary:
      return MENU_ID_ADD_TO_DICTIONARY;
    case MenuCommand::kSpellingSuggestion:
      return item.enabled ? MENU_ID_SPELLCHECK_SUGGESTION_0 + item.index
                          : MENU_ID_NO_SPELLING_SUGGESTIONS;
    default:
      return std::nullopt;
  }
}

int CustomId(MenuCommand command) {
  return MENU_ID_USER_FIRST + static_cast<int>(command);
}

}  // namespace

ContextMenuContext ContextFromParams(
    const CefRefPtr<CefContextMenuParams>& params,
    bool can_go_back,
    bool can_go_forward) {
  ContextMenuContext context;
  cef_context_menu_type_flags_t type = params->GetTypeFlags();
  context.has_link =
      (type & CM_TYPEFLAG_LINK) != 0 && !params->GetLinkUrl().empty();
  context.has_image = params->GetMediaType() == CM_MEDIATYPE_IMAGE &&
                      params->HasImageContents() &&
                      !params->GetSourceUrl().empty();
  context.is_editable = params->IsEditable();
  context.selection_text = params->GetSelectionText().ToString();
  context.can_go_back = can_go_back;
  context.can_go_forward = can_go_forward;
  cef_context_menu_edit_state_flags_t edit = params->GetEditStateFlags();
  context.can_undo = (edit & CM_EDITFLAG_CAN_UNDO) != 0;
  context.can_redo = (edit & CM_EDITFLAG_CAN_REDO) != 0;
  context.can_cut = (edit & CM_EDITFLAG_CAN_CUT) != 0;
  context.can_copy = (edit & CM_EDITFLAG_CAN_COPY) != 0;
  context.can_paste = (edit & CM_EDITFLAG_CAN_PASTE) != 0;
  context.can_select_all = (edit & CM_EDITFLAG_CAN_SELECT_ALL) != 0;
  context.misspelled_word = params->GetMisspelledWord().ToString();
  std::vector<CefString> suggestions;
  if (params->GetDictionarySuggestions(suggestions)) {
    for (const CefString& s : suggestions) {
      context.spelling_suggestions.push_back(s.ToString());
    }
  }
  return context;
}

void FillMenuModel(const CefRefPtr<CefMenuModel>& model,
                   const std::vector<MenuItem>& items) {
  model->Clear();
  for (const MenuItem& item : items) {
    if (item.command == MenuCommand::kSeparator) {
      model->AddSeparator();
      continue;
    }
    int id = BuiltInId(item).value_or(CustomId(item.command));
    model->AddItem(id, item.label);
    model->SetEnabled(id, item.enabled);
  }
}

std::optional<MenuCommand> CustomCommandForId(int command_id) {
  int offset = command_id - MENU_ID_USER_FIRST;
  if (offset < 0 || offset > static_cast<int>(MenuCommand::kSelectAll)) {
    return std::nullopt;
  }
  return static_cast<MenuCommand>(offset);
}

}  // namespace brouk
