#ifndef BROUK_CEF_CONTEXT_MENU_MODEL_H_
#define BROUK_CEF_CONTEXT_MENU_MODEL_H_

#include <optional>
#include <string>

#include "core/context_menu.h"
#include "include/cef_context_menu_handler.h"
#include "include/cef_menu_model.h"

namespace brouk {

// Reads what was right-clicked from Chromium's context menu parameters.
ContextMenuContext ContextFromParams(
    const CefRefPtr<CefContextMenuParams>& params,
    bool can_go_back,
    bool can_go_forward);

// Replaces `model` with `items`. Commands Chromium implements itself (back,
// copy, spelling suggestions…) get CEF's built-in ids; ours get ids in the
// MENU_ID_USER range.
void FillMenuModel(const CefRefPtr<CefMenuModel>& model,
                   const std::vector<MenuItem>& items);

// Our command for a menu id from FillMenuModel, or nullopt for ids that
// Chromium handles itself.
std::optional<MenuCommand> CustomCommandForId(int command_id);

}  // namespace brouk

#endif  // BROUK_CEF_CONTEXT_MENU_MODEL_H_
