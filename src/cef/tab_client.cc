#include "cef/tab_client.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cef/context_menu_model.h"
#include "cef/permission_mapping.h"
#include "cef/profile_prefs.h"
#include "core/address_input.h"
#include "core/crash_error_code.h"
#include "core/external_protocol.h"
#include "core/page_events.h"
#include "include/cef_image.h"
#include "include/cef_parser.h"
#include "include/cef_request_context.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Favicons are drawn at 16 pt; 32 px covers Retina displays.
constexpr std::uint32_t kFaviconMaxSize = 32;

// Delivers a downloaded image as PNG at `scale` (2.0 for Retina favicons).
class ImageCallback : public CefDownloadImageCallback {
 public:
  using Done = void (*)(const CefRefPtr<TabClient>&,
                        const std::string&,
                        const std::vector<std::uint8_t>&);

  ImageCallback(CefRefPtr<TabClient> client, Done done, float scale)
      : client_(std::move(client)), done_(done), scale_(scale) {}
  ImageCallback(const ImageCallback&) = delete;
  ImageCallback& operator=(const ImageCallback&) = delete;

  // UI thread.
  void OnDownloadImageFinished(const CefString& image_url,
                               int /*http_status_code*/,
                               CefRefPtr<CefImage> image) override {
    CEF_REQUIRE_UI_THREAD();
    std::vector<std::uint8_t> png;
    if (image && !image->IsEmpty()) {
      int width = 0;
      int height = 0;
      CefRefPtr<CefBinaryValue> data =
          image->GetAsPNG(scale_, /*with_transparency=*/true, width, height);
      if (data && data->GetSize() > 0) {
        png.resize(data->GetSize());
        data->GetData(png.data(), png.size(), 0);
      }
    }
    done_(client_, image_url.ToString(), png);
  }

 private:
  CefRefPtr<TabClient> client_;
  Done done_;
  float scale_;

  IMPLEMENT_REFCOUNTING(ImageCallback);
};

}  // namespace

void TabClient::DetachDelegate() {
  CEF_REQUIRE_UI_THREAD();
  delegate_ = nullptr;
}

bool TabClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> /*browser*/,
                                         CefRefPtr<CefFrame> frame,
                                         CefProcessId source_process,
                                         CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  if (source_process != PID_RENDERER ||
      message->GetName().ToString() != kPageEventMessage) {
    return false;
  }
  // From a web page: untrusted, validated before use.
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  if (!args || args->GetSize() != 2 || args->GetType(0) != VTYPE_STRING ||
      args->GetType(1) != VTYPE_STRING) {
    return true;
  }
  std::optional<PageEvent> event = ParsePageEvent(
      args->GetString(0).ToString(), args->GetString(1).ToString());
  if (!event || !delegate_) {
    return true;
  }
  switch (event->type) {
    case PageEvent::Type::kPopupBlocked:
      delegate_->OnPopupBlocked(event->url);
      break;
    case PageEvent::Type::kAudible:
    case PageEvent::Type::kInaudible:
      if (frame) {
        SetFrameAudible(frame->GetIdentifier().ToString(),
                        event->type == PageEvent::Type::kAudible);
      }
      break;
  }
  return true;
}

void TabClient::SetFrameAudible(const std::string& frame_id, bool audible) {
  bool was_audible = !audible_frames_.empty();
  if (audible) {
    audible_frames_.insert(frame_id);
  } else {
    audible_frames_.erase(frame_id);
  }
  bool now_audible = !audible_frames_.empty();
  if (now_audible != was_audible && delegate_) {
    delegate_->OnAudibleChanged(now_audible);
  }
}

void TabClient::OnBeforeContextMenu(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> /*frame*/,
                                    CefRefPtr<CefContextMenuParams> params,
                                    CefRefPtr<CefMenuModel> model) {
  CEF_REQUIRE_UI_THREAD();
  ContextMenuContext context =
      ContextFromParams(params, browser->CanGoBack(), browser->CanGoForward());
  FillMenuModel(model, BuildContextMenu(context));
}

bool TabClient::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> /*frame*/,
                                     CefRefPtr<CefContextMenuParams> params,
                                     int command_id,
                                     EventFlags /*event_flags*/) {
  CEF_REQUIRE_UI_THREAD();
  std::optional<MenuCommand> command = CustomCommandForId(command_id);
  if (!command || !delegate_) {
    return false;  // Chromium's own command (back, copy, ...).
  }
  // URLs come from the page: they are only opened, copied or downloaded,
  // which is what the user asked for by choosing the command.
  std::string link = params->GetLinkUrl().ToString();
  std::string source = params->GetSourceUrl().ToString();
  switch (*command) {
    case MenuCommand::kOpenLinkInNewTab:
      delegate_->OpenUrl(link, OpenDisposition::kNewBackgroundTab);
      break;
    case MenuCommand::kOpenLinkInNewWindow:
      delegate_->OpenUrl(link, OpenDisposition::kNewWindow);
      break;
    case MenuCommand::kOpenImageInNewTab:
      delegate_->OpenUrl(source, OpenDisposition::kNewBackgroundTab);
      break;
    case MenuCommand::kCopyLinkAddress:
      delegate_->CopyTextToClipboard(link);
      break;
    case MenuCommand::kCopyImageAddress:
      delegate_->CopyTextToClipboard(source);
      break;
    case MenuCommand::kCopyImage:
      CopyImageAt(browser, source);
      break;
    case MenuCommand::kSaveLinkAs:
      save_as_urls_.insert(link);
      browser->GetHost()->StartDownload(link);
      break;
    case MenuCommand::kSaveImageAs:
      save_as_urls_.insert(source);
      browser->GetHost()->StartDownload(source);
      break;
    case MenuCommand::kSearchForSelection:
      delegate_->OpenUrl(BuildSearchUrl(params->GetSelectionText().ToString()),
                         OpenDisposition::kNewForegroundTab);
      break;
    case MenuCommand::kInspect:
      // Until the dev tools dock (006) exists, DevTools opens in its own
      // window, with the clicked element selected.
      browser->GetHost()->ShowDevTools(
          CefWindowInfo(), nullptr, CefBrowserSettings(),
          CefPoint(params->GetXCoord(), params->GetYCoord()));
      break;
    default:
      return false;
  }
  return true;
}

void TabClient::CopyImageAt(const CefRefPtr<CefBrowser>& browser,
                            const std::string& url) {
  auto done = [](const CefRefPtr<TabClient>& client,
                 const std::string& /*image_url*/,
                 const std::vector<std::uint8_t>& png) {
    if (client->delegate_ && !png.empty()) {
      client->delegate_->CopyImageToClipboard(png);
    }
  };
  // Full size (0 = no limit), from the cache when possible.
  browser->GetHost()->DownloadImage(url, /*is_favicon=*/false,
                                    /*max_image_size=*/0,
                                    /*bypass_cache=*/false,
                                    new ImageCallback(this, done, 1.0f));
}

void TabClient::OnAddressChange(CefRefPtr<CefBrowser> /*browser*/,
                                CefRefPtr<CefFrame> frame,
                                const CefString& url) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame->IsMain()) {
    return;
  }
  // "Allow this time" ends when the tab leaves the site.
  EndOneTimeGrants(frame->GetBrowser(), OriginOf(url.ToString()));

  if (delegate_) {
    delegate_->OnAddressChanged(url.ToString());
  }
}

void TabClient::OnTitleChange(CefRefPtr<CefBrowser> /*browser*/,
                              const CefString& title) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnTitleChanged(title.ToString());
  }
}

void TabClient::OnFaviconURLChange(CefRefPtr<CefBrowser> browser,
                                   const std::vector<CefString>& icon_urls) {
  CEF_REQUIRE_UI_THREAD();
  if (icon_urls.empty()) {
    favicon_url_.clear();
    if (delegate_) {
      delegate_->OnFaviconChanged({});
    }
    return;
  }
  DownloadFavicon(browser, icon_urls.front().ToString());
}

void TabClient::DownloadFavicon(const CefRefPtr<CefBrowser>& browser,
                                const std::string& url) {
  if (url == favicon_url_) {
    return;
  }
  favicon_url_ = url;
  auto done = [](const CefRefPtr<TabClient>& client,
                 const std::string& image_url,
                 const std::vector<std::uint8_t>& png) {
    // Ignore results for an icon the page has replaced meanwhile.
    if (client->delegate_ && image_url == client->favicon_url_) {
      client->delegate_->OnFaviconChanged(png);
    }
  };
  browser->GetHost()->DownloadImage(url, /*is_favicon=*/true, kFaviconMaxSize,
                                    /*bypass_cache=*/false,
                                    new ImageCallback(this, done, 2.0f));
}

bool TabClient::OnBeforeDownload(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return false;  // Tab is closing: Alloy style cancels the download.
  }
  std::string path = delegate_->ReserveDownloadPath(suggested_name.ToString());
  download_paths_[download_item->GetId()] = path;
  // Like Chrome: save to the downloads folder without asking, except for
  // "Save Link/Image As…", which shows the save dialog.
  bool ask =
      save_as_urls_.erase(download_item->GetOriginalUrl().ToString()) > 0 ||
      save_as_urls_.erase(download_item->GetURL().ToString()) > 0;
  callback->Continue(path, ask);
  return true;
}

void TabClient::OnDownloadUpdated(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefDownloadItem> download_item,
    CefRefPtr<CefDownloadItemCallback> /*callback*/) {
  CEF_REQUIRE_UI_THREAD();
  DownloadInfo info;
  info.id = download_item->GetId();
  info.full_path = download_item->GetFullPath().ToString();
  info.percent = download_item->GetPercentComplete();
  if (download_item->IsComplete()) {
    info.state = DownloadInfo::State::kComplete;
  } else if (download_item->IsCanceled()) {
    info.state = DownloadInfo::State::kCanceled;
  } else if (download_item->IsInterrupted()) {
    info.state = DownloadInfo::State::kFailed;
  }
  bool finished = info.state != DownloadInfo::State::kInProgress;
  if (finished) {
    auto it = download_paths_.find(info.id);
    if (it != download_paths_.end()) {
      if (delegate_) {
        delegate_->ReleaseDownloadPath(it->second);
      }
      download_paths_.erase(it);
    }
  }
  if (delegate_ && (!info.full_path.empty() || finished)) {
    delegate_->OnDownloadUpdated(info);
  }
}

void TabClient::OnFindResult(CefRefPtr<CefBrowser> /*browser*/,
                             int /*identifier*/,
                             int count,
                             const CefRect& /*selection_rect*/,
                             int active_match_ordinal,
                             bool final_update) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnFindResult(count, active_match_ordinal, final_update);
  }
}

bool TabClient::OnJSDialog(CefRefPtr<CefBrowser> /*browser*/,
                           const CefString& origin_url,
                           JSDialogType dialog_type,
                           const CefString& message_text,
                           const CefString& default_prompt_text,
                           CefRefPtr<CefJSDialogCallback> callback,
                           bool& suppress_message) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_ || dialog_guard_.IsSuppressed() || pending_dialog_) {
    // Suppressing (rather than answering) lets Chromium detect dialog spam.
    suppress_message = true;
    return false;
  }
  JsDialogRequest request;
  switch (dialog_type) {
    case JSDIALOGTYPE_CONFIRM:
      request.kind = JsDialogRequest::Kind::kConfirm;
      break;
    case JSDIALOGTYPE_PROMPT:
      request.kind = JsDialogRequest::Kind::kPrompt;
      break;
    default:
      request.kind = JsDialogRequest::Kind::kAlert;
      break;
  }
  // Chrome names the origin in the title, except for local files, which
  // have no meaningful origin ("This page says").
  std::string origin = origin_url.ToString();
  if (!origin.empty() && !origin.starts_with("file:")) {
    request.origin = CefFormatUrlForSecurityDisplay(origin_url).ToString();
  }
  request.message = message_text.ToString();
  request.default_prompt_text = default_prompt_text.ToString();
  request.offer_suppression = dialog_guard_.ShouldOfferSuppression();
  ShowDialog(request, callback);
  return true;
}

bool TabClient::OnBeforeUnloadDialog(CefRefPtr<CefBrowser> /*browser*/,
                                     const CefString& /*message_text*/,
                                     bool is_reload,
                                     CefRefPtr<CefJSDialogCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_ || pending_dialog_) {
    return false;
  }
  // Like Chrome, the page's own text is not shown (it could be misleading).
  JsDialogRequest request;
  request.kind = JsDialogRequest::Kind::kBeforeUnload;
  request.is_reload = is_reload;
  ShowDialog(request, callback);
  return true;
}

void TabClient::ShowDialog(const JsDialogRequest& request,
                           const CefRefPtr<CefJSDialogCallback>& callback) {
  pending_dialog_ = callback;
  CefRefPtr<TabClient> self(this);
  bool counts_for_spam = request.kind != JsDialogRequest::Kind::kBeforeUnload;
  delegate_->ShowJsDialog(
      request, [self, callback, counts_for_spam](
                   bool ok, const std::string& input, bool suppress_more) {
        CEF_REQUIRE_UI_THREAD();
        // Ignore answers to a dialog that was already reset by navigation.
        if (self->pending_dialog_ != callback) {
          return;
        }
        self->pending_dialog_ = nullptr;
        if (counts_for_spam) {
          self->dialog_guard_.OnDialogClosed(suppress_more);
        }
        callback->Continue(ok, input);
      });
}

void TabClient::OnResetDialogState(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  dialog_guard_.Reset();
  if (pending_dialog_) {
    pending_dialog_ = nullptr;
    if (delegate_) {
      delegate_->DismissJsDialog();
    }
  }
}

void TabClient::OnFullscreenModeChange(CefRefPtr<CefBrowser> /*browser*/,
                                       bool fullscreen) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnFullscreenModeChanged(fullscreen);
  }
}

bool TabClient::OnPreKeyEvent(CefRefPtr<CefBrowser> browser,
                              const CefKeyEvent& event,
                              CefEventHandle /*os_event*/,
                              bool* /*is_keyboard_shortcut*/) {
  CEF_REQUIRE_UI_THREAD();
  // Escape always leaves HTML full screen, before the page sees the key (as
  // in Chrome); with Alloy style this is the client's job.
  constexpr int kEscapeKeyCode = 0x1B;
  if (event.type == KEYEVENT_RAWKEYDOWN &&
      event.windows_key_code == kEscapeKeyCode &&
      browser->GetHost()->IsFullscreen()) {
    browser->GetHost()->ExitFullscreen(/*will_cause_resize=*/true);
    return true;
  }
  return false;
}

bool TabClient::OnKeyEvent(CefRefPtr<CefBrowser> /*browser*/,
                           const CefKeyEvent& event,
                           CefEventHandle os_event) {
  CEF_REQUIRE_UI_THREAD();
  return delegate_ && delegate_->OnUnhandledKeyEvent(event, os_event);
}

bool TabClient::OnBeforePopup(CefRefPtr<CefBrowser> browser,
                              CefRefPtr<CefFrame> /*frame*/,
                              int popup_id,
                              const CefString& target_url,
                              const CefString& /*target_frame_name*/,
                              WindowOpenDisposition target_disposition,
                              bool user_gesture,
                              const CefPopupFeatures& popup_features,
                              CefWindowInfo& window_info,
                              CefRefPtr<CefClient>& client,
                              CefBrowserSettings& /*settings*/,
                              CefRefPtr<CefDictionaryValue>& /*extra_info*/,
                              bool* /*no_javascript_access*/) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return true;  // Tab is closing: cancel.
  }
  // Chrome's popup blocker: popups the user did not ask for (no click or key
  // press) are blocked unless the site is allowed to open popups.
  if (!user_gesture) {
    std::string origin = OriginOf(browser->GetMainFrame()->GetURL().ToString());
    cef_content_setting_values_t allowed =
        origin.empty()
            ? CEF_CONTENT_SETTING_VALUE_DEFAULT
            : browser->GetHost()->GetRequestContext()->GetContentSetting(
                  origin, origin, CEF_CONTENT_SETTING_TYPE_POPUPS);
    if (allowed != CEF_CONTENT_SETTING_VALUE_ALLOW) {
      delegate_->OnPopupBlocked(target_url.ToString());
      return true;
    }
  }
  PopupRequest request;
  request.popup_id = popup_id;
  request.target_url = target_url.ToString();
  request.disposition = target_disposition;
  request.user_gesture = user_gesture;
  request.wants_popup_window =
      target_disposition == CEF_WOD_NEW_POPUP || popup_features.isPopup != 0;
  request.width = popup_features.widthSet ? popup_features.width : 0;
  request.height = popup_features.heightSet ? popup_features.height : 0;
  std::optional<PopupHost> host = delegate_->OnBeforePopup(request);
  if (!host || !host->parent_view || !host->client) {
    return true;  // Blocked.
  }
  // Keep the popup in the opener's renderer so window.opener works (sign-in
  // popups rely on it); only where it is hosted changes.
  window_info.SetAsChild(host->parent_view, host->bounds);
  window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
  client = host->client;
  return false;
}

void TabClient::OnBeforePopupAborted(CefRefPtr<CefBrowser> /*browser*/,
                                     int popup_id) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnPopupAborted(popup_id);
  }
}

void TabClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnBrowserCreated(browser);
  }
}

bool TabClient::DoClose(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  // The browser lives in a tab, not in its own window: never let CEF send a
  // close to the whole window. The platform removes the tab's view instead,
  // which completes the close.
  if (delegate_) {
    delegate_->OnCloseReady();
  }
  return true;
}

void TabClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();
  EndOneTimeGrants(browser, /*current_origin=*/"");
  if (delegate_) {
    delegate_->OnBrowserClosed();
  }
}

void TabClient::OnLoadStart(CefRefPtr<CefBrowser> /*browser*/,
                            CefRefPtr<CefFrame> frame,
                            TransitionType /*transition_type*/) {
  CEF_REQUIRE_UI_THREAD();
  // A new document in the main frame starts silent: the old page and its
  // frames are gone. (Not called for same-document navigations.)
  if (frame->IsMain() && !audible_frames_.empty()) {
    audible_frames_.clear();
    if (delegate_) {
      delegate_->OnAudibleChanged(false);
    }
  }
}

void TabClient::OnLoadingStateChange(CefRefPtr<CefBrowser> /*browser*/,
                                     bool is_loading,
                                     bool can_go_back,
                                     bool can_go_forward) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnLoadingStateChanged(is_loading, can_go_back, can_go_forward);
  }
}

bool TabClient::OnShowPermissionPrompt(
    CefRefPtr<CefBrowser> browser,
    uint64_t prompt_id,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefPermissionPromptCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return false;  // Default for Alloy style: ignore.
  }
  // Chromium asks only when there is no stored decision for the site.
  PermissionRequest request;
  request.id = prompt_id;
  request.origin = OriginForPrompt(requesting_origin.ToString());
  request.kinds = KindsFromPromptMask(requested_permissions);
  std::string origin = OriginOf(requesting_origin.ToString());
  CefRefPtr<TabClient> self(this);
  std::vector<PermissionKind> kinds = request.kinds;
  delegate_->ShowPermissionPrompt(request, [self, browser, callback, origin,
                                            kinds](PermissionChoice choice) {
    CEF_REQUIRE_UI_THREAD();
    switch (choice) {
      case PermissionChoice::kAllow:
        // Chromium remembers an accepted prompt as a site setting.
        callback->Continue(CEF_PERMISSION_RESULT_ACCEPT);
        break;
      case PermissionChoice::kAllowOnce:
        callback->Continue(CEF_PERMISSION_RESULT_ACCEPT);
        self->RecordOneTimeGrants(browser, origin, kinds,
                                  /*stored_in_content_settings=*/true);
        break;
      case PermissionChoice::kBlock:
        callback->Continue(CEF_PERMISSION_RESULT_DENY);
        break;
      case PermissionChoice::kDismiss:
        callback->Continue(CEF_PERMISSION_RESULT_DISMISS);
        break;
    }
  });
  return true;
}

void TabClient::OnDismissPermissionPrompt(
    CefRefPtr<CefBrowser> /*browser*/,
    uint64_t prompt_id,
    cef_permission_request_result_t /*result*/) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->DismissPermissionPrompt(prompt_id);
  }
}

bool TabClient::OnRequestMediaAccessPermission(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> /*frame*/,
    const CefString& requesting_origin,
    uint32_t requested_permissions,
    CefRefPtr<CefMediaAccessCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return false;  // Default for Alloy style: deny.
  }
  std::string origin = OriginOf(requesting_origin.ToString());
  std::vector<PermissionKind> kinds = KindsFromMediaMask(requested_permissions);
  CefRefPtr<CefRequestContext> context =
      browser->GetHost()->GetRequestContext();

  // Camera and microphone use the site's stored decision (shared with
  // Chromium's own permission checks); screen sharing always asks.
  if (IsRemembered(kinds)) {
    bool all_allowed = true;
    for (PermissionKind kind : kinds) {
      std::optional<cef_content_setting_types_t> type = ContentSettingFor(kind);
      cef_content_setting_values_t value =
          type ? context->GetContentSetting(origin, origin, *type)
               : CEF_CONTENT_SETTING_VALUE_DEFAULT;
      if (value == CEF_CONTENT_SETTING_VALUE_BLOCK) {
        callback->Cancel();
        return true;
      }
      if (value != CEF_CONTENT_SETTING_VALUE_ALLOW &&
          !HasOneTimeGrant(origin, kind)) {
        all_allowed = false;
      }
    }
    if (all_allowed) {
      callback->Continue(requested_permissions);
      return true;
    }
  }

  PermissionRequest request;
  request.id = next_media_prompt_id_++;
  request.origin = OriginForPrompt(requesting_origin.ToString());
  request.kinds = kinds;
  CefRefPtr<TabClient> self(this);
  delegate_->ShowPermissionPrompt(
      request, [self, browser, context, callback, origin, kinds,
                requested_permissions](PermissionChoice choice) {
        CEF_REQUIRE_UI_THREAD();
        switch (choice) {
          case PermissionChoice::kAllow:
          case PermissionChoice::kBlock: {
            cef_content_setting_values_t value =
                choice == PermissionChoice::kAllow
                    ? CEF_CONTENT_SETTING_VALUE_ALLOW
                    : CEF_CONTENT_SETTING_VALUE_BLOCK;
            for (PermissionKind kind : kinds) {
              if (auto type = ContentSettingFor(kind)) {
                context->SetContentSetting(origin, origin, *type, value);
              }
            }
            if (choice == PermissionChoice::kAllow) {
              callback->Continue(requested_permissions);
            } else {
              callback->Cancel();
            }
            break;
          }
          case PermissionChoice::kAllowOnce:
            self->RecordOneTimeGrants(browser, origin, kinds,
                                      /*stored_in_content_settings=*/false);
            callback->Continue(requested_permissions);
            break;
          case PermissionChoice::kDismiss:
            callback->Cancel();
            break;
        }
      });
  return true;
}

void TabClient::RecordOneTimeGrants(const CefRefPtr<CefBrowser>& /*browser*/,
                                    const std::string& origin,
                                    const std::vector<PermissionKind>& kinds,
                                    bool stored_in_content_settings) {
  for (PermissionKind kind : kinds) {
    one_time_grants_.push_back({origin, kind, stored_in_content_settings});
  }
}

bool TabClient::HasOneTimeGrant(const std::string& origin,
                                PermissionKind kind) const {
  return std::ranges::any_of(one_time_grants_, [&](const OneTimeGrant& g) {
    return g.origin == origin && g.kind == kind;
  });
}

void TabClient::EndOneTimeGrants(const CefRefPtr<CefBrowser>& browser,
                                 const std::string& current_origin) {
  if (one_time_grants_.empty()) {
    return;
  }
  CefRefPtr<CefRequestContext> context =
      browser->GetHost()->GetRequestContext();
  std::erase_if(one_time_grants_, [&](const OneTimeGrant& grant) {
    if (!current_origin.empty() && grant.origin == current_origin) {
      return false;
    }
    if (grant.stored_in_content_settings) {
      if (auto type = ContentSettingFor(grant.kind)) {
        context->SetContentSetting(grant.origin, grant.origin, *type,
                                   CEF_CONTENT_SETTING_VALUE_DEFAULT);
      }
    }
    return true;
  });
}

bool TabClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               CefRefPtr<CefRequest> request,
                               bool /*user_gesture*/,
                               bool /*is_redirect*/) {
  CEF_REQUIRE_UI_THREAD();
  std::string url = request->GetURL().ToString();
  switch (ClassifyScheme(url)) {
    case SchemeHandling::kBrowser:
      return false;
    case SchemeHandling::kBlocked:
      return true;  // Cancel silently, as Chrome does.
    case SchemeHandling::kExternal:
      break;
  }
  // Another app's link: never loaded in the tab (the page stays as it is);
  // opened after asking, unless the user always allowed it for this site.
  if (!delegate_ || asking_external_protocol_) {
    return true;
  }
  std::string origin = OriginOf(frame->GetURL().ToString());
  if (origin == "null") {
    origin.clear();  // Opaque origin (about:blank, data:): nothing to remember.
  }
  std::string scheme = SchemeOf(url);
  CefRefPtr<CefRequestContext> context =
      browser->GetHost()->GetRequestContext();
  std::string key = ExternalProtocolAllowKey(origin, scheme);
  if (!origin.empty() && IsExternalProtocolAllowed(context, key)) {
    delegate_->OpenExternalUrl(url);
    return true;
  }
  ExternalProtocolRequest ask;
  ask.url = url;
  ask.scheme = scheme;
  ask.origin = origin.empty() ? std::string() : OriginForPrompt(origin);
  ask.can_remember = !origin.empty();
  asking_external_protocol_ = true;
  CefRefPtr<TabClient> self(this);
  delegate_->AskOpenExternalProtocol(
      ask, [self, context, key, url](bool open, bool always) {
        CEF_REQUIRE_UI_THREAD();
        self->asking_external_protocol_ = false;
        if (!open) {
          return;
        }
        if (always) {
          AllowExternalProtocol(context, key);
        }
        if (self->delegate_) {
          self->delegate_->OpenExternalUrl(url);
        }
      });
  return true;
}

void TabClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> /*browser*/,
                                          TerminationStatus status,
                                          int error_code,
                                          const CefString& /*error_string*/) {
  CEF_REQUIRE_UI_THREAD();
  audible_frames_.clear();
  if (!delegate_) {
    return;
  }
  delegate_->OnAudibleChanged(false);
  RendererGoneReason reason = RendererGoneReason::kOther;
  switch (status) {
    case TS_PROCESS_CRASHED:
      reason = RendererGoneReason::kCrashed;
      break;
    case TS_PROCESS_WAS_KILLED:
      reason = RendererGoneReason::kKilled;
      break;
    case TS_PROCESS_OOM:
      reason = RendererGoneReason::kOutOfMemory;
      break;
    default:
      break;
  }
  delegate_->OnRendererGone(reason, CrashErrorCodeText(error_code));
}

bool TabClient::OnRenderProcessUnresponsive(
    CefRefPtr<CefBrowser> /*browser*/,
    CefRefPtr<CefUnresponsiveProcessCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return false;
  }
  delegate_->OnRendererUnresponsive([callback](bool exit_page) {
    if (exit_page) {
      callback->Terminate();
    } else {
      callback->Wait();
    }
  });
  return true;
}

void TabClient::OnRenderProcessResponsive(CefRefPtr<CefBrowser> /*browser*/) {
  CEF_REQUIRE_UI_THREAD();
  if (delegate_) {
    delegate_->OnRendererResponsive();
  }
}

}  // namespace brouk
