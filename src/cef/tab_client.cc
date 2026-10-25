#include "cef/tab_client.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cef/context_menu_model.h"
#include "cef/function_task.h"
#include "cef/permission_mapping.h"
#include "cef/profile_prefs.h"
#include "core/address_input.h"
#include "core/crash_error_code.h"
#include "core/external_protocol.h"
#include "core/page_events.h"
#include "core/web_origin.h"
#include "include/cef_image.h"
#include "include/cef_navigation_entry.h"
#include "include/cef_parser.h"
#include "include/cef_process_message.h"
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

// static
CefRefPtr<CefDictionaryValue> TabClient::RendererConfig(bool json_viewer) {
  CefRefPtr<CefDictionaryValue> config = CefDictionaryValue::Create();
  config->SetBool(std::string(kRendererConfigJsonViewer), json_viewer);
  return config;
}

// static
void TabClient::SendRendererConfig(const CefRefPtr<CefBrowser>& browser,
                                   bool json_viewer) {
  CEF_REQUIRE_UI_THREAD();
  CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create(std::string(kRendererConfigMessage));
  message->GetArgumentList()->SetDictionary(0, RendererConfig(json_viewer));
  browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, message);
}

void TabClient::DetachDelegate() {
  CEF_REQUIRE_UI_THREAD();
  delegate_ = nullptr;
  if (passwords_) {
    passwords_->Detach();
  }
  if (autofill_) {
    autofill_->Detach();
  }
}

void TabClient::SetAutofill(AutofillManager* manager) {
  CEF_REQUIRE_UI_THREAD();
  autofill_manager_ = manager;
  autofill_ = manager && delegate_
                  ? std::make_shared<AutofillTab>(manager, delegate_, delegate_)
                  : nullptr;
}

void TabClient::SetPasswords(PasswordManager* manager,
                             const ProfileId& profile) {
  CEF_REQUIRE_UI_THREAD();
  if (passwords_) {
    passwords_->Detach();
  }
  passwords_ = manager && delegate_
                   ? std::make_shared<PasswordTab>(manager, profile, delegate_)
                   : nullptr;
}

void TabClient::OnPasswordKeyIconClicked() {
  CEF_REQUIRE_UI_THREAD();
  if (passwords_) {
    passwords_->OnKeyIconClicked();
  }
}

bool TabClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefProcessId source_process,
                                         CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();
  std::string name = message->GetName().ToString();
  if (source_process == PID_RENDERER && frame && name == kFormsMessage) {
    // Login forms (008): untrusted, validated by the password logic.
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (args && args->GetSize() == 2 && args->GetType(0) == VTYPE_STRING &&
        args->GetType(1) == VTYPE_STRING) {
      std::string kind = args->GetString(0).ToString();
      std::string json = args->GetString(1).ToString();
      // Autofill's kinds start with "af" (009); a blur closes either menu.
      if (kind.starts_with("af")) {
        if (autofill_) {
          autofill_->HandleMessage(browser, frame, kind, json);
        }
      } else if (passwords_) {
        passwords_->HandleMessage(browser, frame, kind, json);
      }
    }
    return true;
  }
  if (source_process == PID_RENDERER && frame && name == kAdblockPicked) {
    // Only the picker the user started, once, in the main frame (007).
    CefRefPtr<CefListValue> args = message->GetArgumentList();
    if (picker_active_ && frame->IsMain() && args && args->GetSize() == 1 &&
        args->GetType(0) == VTYPE_STRING && delegate_) {
      picker_active_ = false;
      std::string selector = args->GetString(0).ToString();
      bool clean = !selector.empty() && selector.size() <= 1024 &&
                   std::ranges::none_of(selector, [](char c) {
                     return static_cast<unsigned char>(c) < 0x20;
                   });
      if (clean) {
        delegate_->OnElementPicked(frame->GetURL().ToString(), selector);
      }
    }
    return true;
  }
  if (source_process == PID_RENDERER && frame && adblock_ &&
      (name == kAdblockCosmeticRequest || name == kAdblockClassesRequest)) {
    HandleAdblockMessage(browser, frame, name, message->GetArgumentList());
    return true;
  }
  if (source_process != PID_RENDERER || name != kPageEventMessage) {
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
    case PageEvent::Type::kDisplayMedia:
      if (frame) {
        ChooseDesktopSourceForFrame(frame, event->request_id);
      }
      break;
  }
  return true;
}

namespace {

// UI thread. Answers the page script's getDisplayMedia() request; an empty
// `source_id` means cancelled.
void SendDisplayMediaChoice(const CefRefPtr<CefFrame>& frame,
                            std::uint32_t request_id,
                            const std::string& source_id) {
  if (!frame->IsValid()) {
    return;  // The page is gone.
  }
  CefRefPtr<CefProcessMessage> message =
      CefProcessMessage::Create(std::string(kDisplayMediaChoiceMessage));
  CefRefPtr<CefListValue> args = message->GetArgumentList();
  args->SetDouble(0, request_id);  // Exact: any uint32 fits in a double.
  args->SetString(1, source_id);
  frame->SendProcessMessage(PID_RENDERER, message);
}

}  // namespace

void TabClient::ChooseDesktopSourceForFrame(const CefRefPtr<CefFrame>& frame,
                                            std::uint32_t request_id) {
  CEF_REQUIRE_UI_THREAD();
  std::string origin = OriginOf(frame->GetURL().ToString());
  // One picker at a time, and only for pages with an origin to grant to.
  if (choosing_desktop_source_ || !delegate_ || origin.empty() ||
      origin == "null") {
    SendDisplayMediaChoice(frame, request_id, "");
    return;
  }
  choosing_desktop_source_ = true;
  CefRefPtr<TabClient> self(this);
  delegate_->ChooseDesktopSource(
      [self, frame, request_id, origin](std::optional<DesktopSource> source) {
        CEF_REQUIRE_UI_THREAD();
        self->choosing_desktop_source_ = false;
        if (!source || !self->delegate_) {
          SendDisplayMediaChoice(frame, request_id, "");
          return;
        }
        // The script's getUserMedia() call for this source follows at once.
        self->screen_share_grant_.Issue(origin, ScreenShareGrant::Clock::now());
        SendDisplayMediaChoice(frame, request_id, DesktopSourceId(*source));
      });
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
  if (delegate_) {
    context.search_engine_name = delegate_->CurrentSearchEngine().name;
  }
  if (adblock_) {
    std::string page = browser->GetMainFrame()->GetURL().ToString();
    context.can_block_element =
        AdblockCosmeticFor(*adblock_, adblock_profile_, page, page).has_value();
  }
  FillMenuModel(model, BuildContextMenu(context));
  AddAutofillMenu(params, model);
}

// Autofill → each entry's values and each custom field, on text fields of
// web pages (009). Ids from MENU_ID_USER_FIRST + 1000 (ours below stay
// under 1000).
void TabClient::AddAutofillMenu(const CefRefPtr<CefContextMenuParams>& params,
                                const CefRefPtr<CefMenuModel>& model) {
  autofill_menu_values_.clear();
  autofill_menu_manage_ = false;
  if (!autofill_manager_ || !params->IsEditable() ||
      !autofill_manager_->options().enabled || !autofill_manager_->loaded()) {
    return;
  }
  constexpr int kFirst = MENU_ID_USER_FIRST + 1000;
  constexpr int kLast = MENU_ID_USER_LAST - 1;
  int next = kFirst + 1;
  const AutofillOptions& options = autofill_manager_->options();
  model->AddSeparator();
  CefRefPtr<CefMenuModel> menu = model->AddSubMenu(kFirst, "Autofill");
  if (options.entries) {
    for (const AutofillEntry& entry : autofill_manager_->entries()) {
      std::string title = entry.label.empty() ? entry.Preview() : entry.label;
      CefRefPtr<CefMenuModel> values = menu->AddSubMenu(next++, title);
      for (std::size_t i = 0; i < kAutofillStoredFields && next < kLast; ++i) {
        auto field = static_cast<AutofillField>(i);
        std::string value = entry.ValueFor(field);
        if (value.empty()) {
          continue;
        }
        values->AddItem(
            next, std::string(AutofillFieldLabel(field)) + ": " +
                      (value.size() > 50 ? value.substr(0, 47) + "…" : value));
        autofill_menu_values_[next++] = value;
      }
    }
  }
  if (options.custom_fields && !autofill_manager_->custom_fields().empty()) {
    menu->AddSeparator();
    for (const CustomField& field : autofill_manager_->custom_fields()) {
      if (next >= kLast) {
        break;
      }
      menu->AddItem(next, field.name);
      autofill_menu_values_[next++] = field.value;
    }
  }
  menu->AddSeparator();
  menu->AddItem(kLast, "Manage autofill…");
  autofill_menu_manage_ = true;
}

bool TabClient::OnContextMenuCommand(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> /*frame*/,
                                     CefRefPtr<CefContextMenuParams> params,
                                     int command_id,
                                     EventFlags /*event_flags*/) {
  CEF_REQUIRE_UI_THREAD();
  if (command_id == MENU_ID_USER_LAST - 1 && autofill_menu_manage_) {
    if (delegate_) {
      delegate_->OpenAutofillSettings();
    }
    return true;
  }
  if (auto value = autofill_menu_values_.find(command_id);
      value != autofill_menu_values_.end()) {
    // Into the focused field of the frame that was right-clicked.
    CefRefPtr<CefFrame> target = browser->GetFocusedFrame();
    if (target) {
      CefRefPtr<CefProcessMessage> fill =
          CefProcessMessage::Create(std::string(kFormsFillMessage));
      fill->GetArgumentList()->SetString(0,
                                         AutofillFocusedMessage(value->second));
      target->SendProcessMessage(PID_RENDERER, fill);
    }
    return true;
  }
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
      delegate_->OpenUrl(
          BuildSearchUrl(params->GetSelectionText().ToString(),
                         delegate_->CurrentSearchEngine().url_template),
          OpenDisposition::kNewForegroundTab);
      break;
    case MenuCommand::kBlockElement:
      StartElementPicker(browser);
      break;
    case MenuCommand::kInspect:
      // Turns the tab's dev tools on, with the clicked element selected in
      // Elements (006).
      delegate_->InspectElement(params->GetXCoord(), params->GetYCoord());
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
  // A new address without loading: a same-document navigation
  // (history.pushState, #fragment), which Chrome records as a visit.
  // Documents are recorded when they finish loading (OnLoadEnd).
  if (!loading_) {
    ReportVisit(frame->GetBrowser());
  }
}

void TabClient::ReportVisit(const CefRefPtr<CefBrowser>& browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_ || !browser) {
    return;
  }
  CefRefPtr<CefNavigationEntry> entry =
      browser->GetHost()->GetVisibleNavigationEntry();
  if (!entry || !entry->IsValid()) {
    return;
  }
  delegate_->OnPageVisited(entry->GetURL().ToString(),
                           entry->GetTitle().ToString(),
                           entry->GetTransitionType());
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
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefDownloadItem> download_item,
    const CefString& suggested_name,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return false;  // Tab is closing: Alloy style cancels the download.
  }
  DownloadTarget target =
      delegate_->ReserveDownloadPath(suggested_name.ToString());
  download_paths_[download_item->GetId()] = target.path;
  // Like Chrome: save to the downloads folder without asking, except for
  // "Save Link/Image As…", when the user chose to be asked (settings), or
  // when the profile's folder cannot be used.
  bool ask =
      save_as_urls_.erase(download_item->GetOriginalUrl().ToString()) > 0 ||
      save_as_urls_.erase(download_item->GetURL().ToString()) > 0 ||
      delegate_->AskWhereToSaveDownloads() || target.ask;
  callback->Continue(target.path, ask);
  if (downloads_) {
    std::string page =
        browser ? browser->GetMainFrame()->GetURL().ToString() : "";
    std::optional<std::string> origin = WebOriginOf(page);
    std::string site = origin ? HostOfOrigin(*origin) : "";
    if (site.starts_with("www.")) {
      site = site.substr(4);
    }
    downloads_->OnStarted(downloads_profile_, download_item, site);
  }
  return true;
}

void TabClient::OnDownloadUpdated(CefRefPtr<CefBrowser> /*browser*/,
                                  CefRefPtr<CefDownloadItem> download_item,
                                  CefRefPtr<CefDownloadItemCallback> callback) {
  CEF_REQUIRE_UI_THREAD();
  bool finished =
      download_item->IsComplete() || download_item->IsCanceled() ||
      (download_item->IsInterrupted() && !download_item->IsPaused());
  if (finished) {
    auto it = download_paths_.find(download_item->GetId());
    if (it != download_paths_.end()) {
      if (delegate_) {
        delegate_->ReleaseDownloadPath(it->second);
      }
      download_paths_.erase(it);
    }
  }
  if (downloads_) {
    downloads_->OnUpdated(download_item, callback);
  }
}

namespace {

std::int64_t SteadyMillis() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

}  // namespace

// Several downloads from one page without the user starting them ask for
// the "Automatic downloads" permission (014, as in Chrome). The first
// download of a page and downloads the user started (a navigation with a
// user gesture) always go through; the others wait for the answer.
bool TabClient::CanDownload(CefRefPtr<CefBrowser> browser,
                            const CefString& url,
                            const CefString& /*request_method*/) {
  CEF_REQUIRE_UI_THREAD();
  std::string target = url.ToString();
  bool gesture =
      target == gesture_url_ && SteadyMillis() - gesture_time_ < 5000;
  if (gesture || page_downloads_++ == 0) {
    return true;
  }
  std::string page = browser->GetMainFrame()->GetURL().ToString();
  CefRefPtr<CefRequestContext> context =
      browser->GetHost()->GetRequestContext();
  cef_content_setting_values_t value =
      context ? context->GetContentSetting(
                    page, page, CEF_CONTENT_SETTING_TYPE_AUTOMATIC_DOWNLOADS)
              : CEF_CONTENT_SETTING_VALUE_DEFAULT;
  if (value == CEF_CONTENT_SETTING_VALUE_ALLOW) {
    return true;
  }
  if (value == CEF_CONTENT_SETTING_VALUE_BLOCK || !delegate_) {
    return false;
  }
  held_downloads_.push_back(target);
  if (asking_downloads_) {
    return false;
  }
  asking_downloads_ = true;
  PermissionRequest request;
  request.id = next_media_prompt_id_++;
  request.origin = OriginForPrompt(OriginOf(page));
  request.kinds = {PermissionKind::kAutomaticDownloads};
  CefRefPtr<TabClient> self = this;
  delegate_->ShowPermissionPrompt(
      request, [self, owner = browser, page](PermissionChoice choice) {
        self->asking_downloads_ = false;
        std::vector<std::string> held = std::move(self->held_downloads_);
        self->held_downloads_.clear();
        CefRefPtr<CefRequestContext> context =
            owner->GetHost()->GetRequestContext();
        bool allow = choice == PermissionChoice::kAllow ||
                     choice == PermissionChoice::kAllowOnce;
        if (context && (choice == PermissionChoice::kAllow ||
                        choice == PermissionChoice::kBlock)) {
          context->SetContentSetting(
              page, page, CEF_CONTENT_SETTING_TYPE_AUTOMATIC_DOWNLOADS,
              allow ? CEF_CONTENT_SETTING_VALUE_ALLOW
                    : CEF_CONTENT_SETTING_VALUE_BLOCK);
        }
        if (!allow) {
          return;
        }
        // The held downloads start now; they count as the user's.
        for (const std::string& held_url : held) {
          self->gesture_url_ = held_url;
          self->gesture_time_ = SteadyMillis();
          owner->GetHost()->StartDownload(held_url);
        }
      });
  return false;
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
  // The password drop-down takes the arrow keys while it is open (008).
  return event.type == KEYEVENT_RAWKEYDOWN && delegate_ &&
         delegate_->HandlePasswordMenuKey(event.windows_key_code);
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
  if (frame->IsMain()) {
    main_load_failed_ = false;
    page_downloads_ = 0;
    if (passwords_) {
      passwords_->OnMainFrameLoadStart();
    }
    if (autofill_) {
      autofill_->OnMainFrameLoadStart();
    }
  }
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
  loading_ = is_loading;
  if (delegate_) {
    delegate_->OnLoadingStateChanged(is_loading, can_go_back, can_go_forward);
  }
}

void TabClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          int /*http_status_code*/) {
  CEF_REQUIRE_UI_THREAD();
  // Error pages are not recorded (013).
  if (frame->IsMain() && !main_load_failed_) {
    ReportVisit(browser);
  }
}

void TabClient::OnLoadError(CefRefPtr<CefBrowser> /*browser*/,
                            CefRefPtr<CefFrame> frame,
                            ErrorCode error_code,
                            const CefString& /*error_text*/,
                            const CefString& /*failed_url*/) {
  CEF_REQUIRE_UI_THREAD();
  // ERR_ABORTED: stopped by the user or turned into a download; nothing was
  // shown instead of the page.
  if (frame->IsMain() && error_code != ERR_ABORTED) {
    main_load_failed_ = true;
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

  // Screen capture right after the user chose a source in the picker: the
  // choice was the question (Chrome does not ask twice either).
  constexpr std::uint32_t kDesktopCapture =
      CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE |
      CEF_MEDIA_PERMISSION_DESKTOP_AUDIO_CAPTURE;
  if ((requested_permissions & CEF_MEDIA_PERMISSION_DESKTOP_VIDEO_CAPTURE) &&
      (requested_permissions & ~kDesktopCapture) == 0 &&
      screen_share_grant_.Consume(origin, ScreenShareGrant::Clock::now())) {
    callback->Continue(requested_permissions);
    return true;
  }
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
                               bool user_gesture,
                               bool /*is_redirect*/) {
  CEF_REQUIRE_UI_THREAD();
  std::string url = request->GetURL().ToString();
  if (user_gesture) {
    // A link the user clicked may turn out to be a download (014).
    gesture_url_ = url;
    gesture_time_ = SteadyMillis();
  }
  switch (ClassifyScheme(url)) {
    case SchemeHandling::kBrowser:
      if (route_first_navigation_ && frame->IsMain() && url != "about:blank") {
        route_first_navigation_ = false;
        return delegate_ && delegate_->RouteFirstNavigation(url);
      }
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

// Links clicked with a modifier or the middle button (Chrome: Cmd+click opens
// a background tab, Cmd+Shift+click a foreground tab, Shift+click a new
// window, Option+click downloads). Chromium has already worked out the
// disposition from the click; without this handler CEF would load the link
// in the current tab.
bool TabClient::OnOpenURLFromTab(CefRefPtr<CefBrowser> browser,
                                 CefRefPtr<CefFrame> /*frame*/,
                                 const CefString& target_url,
                                 WindowOpenDisposition target_disposition,
                                 bool user_gesture) {
  CEF_REQUIRE_UI_THREAD();
  std::string url = target_url.ToString();
  // Only clicks open new tabs, and only for pages the browser loads itself;
  // anything else (another app's link) goes through OnBeforeBrowse in the
  // current tab, which asks first.
  if (!delegate_ || !user_gesture ||
      ClassifyScheme(url) != SchemeHandling::kBrowser) {
    return false;
  }
  switch (target_disposition) {
    case CEF_WOD_NEW_BACKGROUND_TAB:
      delegate_->OpenUrl(url, OpenDisposition::kNewBackgroundTab);
      return true;
    case CEF_WOD_NEW_FOREGROUND_TAB:
      delegate_->OpenUrl(url, OpenDisposition::kNewForegroundTab);
      return true;
    case CEF_WOD_NEW_WINDOW:
      delegate_->OpenUrl(url, OpenDisposition::kNewWindow);
      return true;
    case CEF_WOD_SAVE_TO_DISK:
      browser->GetHost()->StartDownload(url);
      return true;
    default:
      return false;
  }
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

CefRefPtr<CefResourceRequestHandler> TabClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request,
    bool /*is_navigation*/,
    bool is_download,
    const CefString& request_initiator,
    bool& /*disable_default_handling*/) {
  CEF_REQUIRE_IO_THREAD();
  if (!adblock_ || is_download) {
    return nullptr;
  }
  cef_resource_type_t type = request->GetResourceType();
  std::string url = request->GetURL().ToString();
  // The page the request belongs to: a page load is its own page.
  std::string page_url;
  if (type == RT_MAIN_FRAME) {
    page_url = url;
  } else if (CefRefPtr<CefFrame> main =
                 browser ? browser->GetMainFrame() : nullptr) {
    page_url = main->GetURL().ToString();
  }
  if (page_url.empty()) {
    page_url = request_initiator.ToString();
  }
  (void)frame;
  AdblockRequestInfo info;
  CefRefPtr<CefResourceRequestHandler> handler = CheckAdblockRequest(
      *adblock_, adblock_profile_, url, page_url, type, &info);
  // Reported when it matters to the shield and the popup.
  if (!info.url.empty() &&
      (info.blocked || info.excepted || info.third_party || info.main_frame)) {
    CefRefPtr<TabClient> self(this);
    CefPostTask(TID_UI, new FunctionTask([self, info] {
                  if (!self->delegate_) {
                    return;
                  }
                  if (info.main_frame && info.blocked) {
                    self->delegate_->OnPageBlocked(info);
                  }
                  self->delegate_->OnAdblockRequest(info);
                }));
  }
  return handler;
}

// A frame's page script asks for its cosmetic filters, or for generic
// selectors (007). The frame's URL comes from the browser's own view of the
// frame, never from the message. UI thread.
void TabClient::HandleAdblockMessage(const CefRefPtr<CefBrowser>& browser,
                                     const CefRefPtr<CefFrame>& frame,
                                     const std::string& name,
                                     const CefRefPtr<CefListValue>& args) {
  CefRefPtr<CefFrame> main = browser ? browser->GetMainFrame() : nullptr;
  std::string page_url = main ? main->GetURL().ToString() : "";
  if (name == kAdblockCosmeticRequest) {
    std::optional<AdblockCosmeticAnswer> answer = AdblockCosmeticFor(
        *adblock_, adblock_profile_, frame->GetURL().ToString(), page_url);
    if (!answer) {
      return;
    }
    CefRefPtr<CefProcessMessage> reply =
        CefProcessMessage::Create(std::string(kAdblockCosmeticAnswer));
    reply->GetArgumentList()->SetString(0, answer->json);
    reply->GetArgumentList()->SetString(1, answer->scriptlets);
    frame->SendProcessMessage(PID_RENDERER, reply);
    return;
  }
  if (!args || args->GetSize() != 1 || args->GetType(0) != VTYPE_STRING) {
    return;
  }
  std::string request = args->GetString(0).ToString();
  if (request.size() > kMaxAdblockClassesBytes) {
    return;
  }
  std::optional<std::string> answer = AdblockGenericSelectorsFor(
      *adblock_, adblock_profile_, page_url, request);
  if (!answer) {
    return;
  }
  CefRefPtr<CefProcessMessage> reply =
      CefProcessMessage::Create(std::string(kAdblockClassesAnswer));
  reply->GetArgumentList()->SetString(0, *answer);
  frame->SendProcessMessage(PID_RENDERER, reply);
}

void TabClient::StartElementPicker(const CefRefPtr<CefBrowser>& browser) {
  CEF_REQUIRE_UI_THREAD();
  if (!browser || !adblock_) {
    return;
  }
  picker_active_ = true;
  browser->GetMainFrame()->SendProcessMessage(
      PID_RENDERER, CefProcessMessage::Create(std::string(kAdblockPick)));
}

}  // namespace brouk
