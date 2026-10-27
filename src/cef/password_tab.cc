#include "cef/password_tab.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

#include "cef/breach_check.h"
#include "cef/devtools_channel.h"
#include "core/crypto.h"
#include "core/totp.h"
#include "core/web_origin.h"
#include "include/cef_process_message.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// How long a submitted login waits for the page to move on, and how long a
// user name from the first step of a login is remembered.
constexpr std::int64_t kSubmitSeconds = 60;
constexpr std::int64_t kRememberSeconds = std::int64_t{10} * 60;

std::int64_t Now() {
  return std::chrono::duration_cast<std::chrono::seconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string FrameId(const CefRefPtr<CefFrame>& frame) {
  return frame->GetIdentifier().ToString();
}

std::string DisplayHost(const std::string& origin) {
  std::string host = HostOfOrigin(origin);
  return host.starts_with("www.") ? host.substr(4) : host;
}

}  // namespace

PasswordTab::PasswordTab(PasswordManager* manager,
                         ProfileId profile,
                         PasswordTabDelegate* delegate)
    : manager_(manager), profile_(std::move(profile)), delegate_(delegate) {}

void PasswordTab::Detach() {
  CEF_REQUIRE_UI_THREAD();
  delegate_ = nullptr;
  browser_ = nullptr;
  frames_.clear();
  submitted_.reset();
  prompt_.reset();
}

void PasswordTab::HandleMessage(const CefRefPtr<CefBrowser>& browser,
                                const CefRefPtr<CefFrame>& frame,
                                const std::string& kind,
                                const std::string& json) {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_ || !frame) {
    return;
  }
  browser_ = browser;
  if (kind == kFormsKindForms) {
    OnForms(frame, json);
  } else if (kind == kFormsKindFocus) {
    OnFocus(frame, json);
  } else if (kind == kFormsKindBlur) {
    delegate_->HidePasswordMenu();
  } else if (kind == kFormsKindSubmit) {
    OnSubmit(frame, json);
  } else if (kind == kFormsKindGone) {
    if (submitted_ && submitted_->frame_id == FrameId(frame)) {
      EvaluateSubmitted();
    }
  }
}

void PasswordTab::OnForms(const CefRefPtr<CefFrame>& frame,
                          const std::string& json) {
  std::optional<std::vector<LoginForm>> forms = ParseLoginForms(json);
  std::optional<std::string> origin = WebOriginOf(frame->GetURL().ToString());
  if (!forms || !origin) {
    return;
  }
  std::string id = FrameId(frame);
  std::string url = frame->GetURL().ToString();
  Frame& state = frames_[id];
  if (state.origin != *origin) {
    state = Frame{};
    state.origin = *origin;
  }
  if (state.url != url) {
    state.url = url;
    // Bitwarden's match settings may look at the path.
    if (manager_->UsesBitwarden(profile_)) {
      state.lookup.reset();
    }
  }
  state.main = frame->IsMain();
  state.forms = std::move(*forms);
  if (!state.lookup || Stale(state)) {
    LookUp(id);
  } else {
    TryAutofill(id);
  }
}

void PasswordTab::LookUp(const std::string& frame_id,
                         std::function<void()> then) {
  auto it = frames_.find(frame_id);
  if (it == frames_.end()) {
    return;
  }
  std::string origin = it->second.origin;
  int generation = manager_->bitwarden_generation();
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  manager_->Lookup(
      profile_, origin, it->second.url,
      [weak, frame_id, origin, generation,
       then = std::move(then)](PasswordLookup lookup) {
        std::shared_ptr<PasswordTab> self = weak.lock();
        if (!self || !self->delegate_) {
          return;
        }
        auto frame = self->frames_.find(frame_id);
        if (frame == self->frames_.end() || frame->second.origin != origin) {
          return;
        }
        frame->second.lookup = std::move(lookup);
        frame->second.generation = generation;
        self->UpdateIcon();
        self->TryAutofill(frame_id);
        if (then) {
          then();
        }
      });
}

bool PasswordTab::Stale(const Frame& frame) const {
  return frame.lookup && manager_->UsesBitwarden(profile_) &&
         frame.generation != manager_->bitwarden_generation();
}

void PasswordTab::LookUpAll() {
  std::vector<std::string> ids;
  ids.reserve(frames_.size());
  for (const auto& [id, frame] : frames_) {
    ids.push_back(id);
  }
  for (const std::string& id : ids) {
    LookUp(id);
  }
}

std::vector<PasswordEntry> PasswordTab::Offered(const Frame& frame) const {
  std::vector<PasswordEntry> entries;
  if (!frame.lookup) {
    return entries;
  }
  entries = frame.lookup->matches.exact;
  // Other hosts of the site only in the main frame: a frame gets only
  // entries saved for its own origin (008, safety).
  if (frame.main) {
    entries.insert(entries.end(), frame.lookup->matches.same_site.begin(),
                   frame.lookup->matches.same_site.end());
  }
  return entries;
}

void PasswordTab::TryAutofill(const std::string& frame_id) {
  auto it = frames_.find(frame_id);
  if (it == frames_.end()) {
    return;
  }
  Frame& frame = it->second;
  std::optional<LoginForm> form = FormToFill(frame.forms);
  if (!frame.lookup || !form ||
      std::ranges::find(frame.filled_forms, form->id) !=
          frame.filled_forms.end()) {
    return;
  }
  // Logins that ask to confirm first ("re-prompt", Bitwarden) are never
  // filled automatically.
  std::vector<PasswordEntry> exact;
  for (const PasswordEntry& entry : frame.lookup->matches.exact) {
    if (!entry.reprompt) {
      exact.push_back(entry);
    }
  }
  std::optional<std::size_t> choice =
      AutofillChoice(exact, RememberedUsername(frame.origin),
                     IsSecureOriginForPasswords(frame.origin),
                     manager_->options().fill_on_click);
  if (!choice) {
    return;
  }
  frame.filled_forms.push_back(form->id);
  Fill(frame_id, *form, exact[*choice]);
}

void PasswordTab::Fill(const std::string& frame_id,
                       const LoginForm& form,
                       const PasswordEntry& entry) {
  std::string message = FillMessage(form, entry.username, entry.password);
  if (message.empty()) {
    return;
  }
  SendFill(frame_id, message);
  Wipe(message);
  manager_->MarkUsed(profile_, entry.id);
  if (manager_->options().copy_totp && !entry.totp.empty() &&
      form.Has(LoginFieldKind::kPassword)) {
    if (std::optional<TotpSecret> secret = ParseTotpSecret(entry.totp)) {
      delegate_->CopyTextToClipboard(TotpCode(*secret, Now()));
    }
  }
}

void PasswordTab::SendFill(const std::string& frame_id,
                           const std::string& message) {
  if (!browser_) {
    return;
  }
  CefRefPtr<CefFrame> frame = browser_->GetFrameByIdentifier(frame_id);
  auto it = frames_.find(frame_id);
  // The frame must still show the origin the values were chosen for.
  if (!frame || !frame->IsValid() || it == frames_.end() ||
      WebOriginOf(frame->GetURL().ToString()) != it->second.origin) {
    return;
  }
  CefRefPtr<CefProcessMessage> fill =
      CefProcessMessage::Create(std::string(kFormsFillMessage));
  fill->GetArgumentList()->SetString(0, message);
  frame->SendProcessMessage(PID_RENDERER, fill);
}

void PasswordTab::OnFocus(const CefRefPtr<CefFrame>& frame,
                          const std::string& json) {
  std::optional<FieldFocus> focus = ParseFieldFocus(json);
  std::string frame_id = FrameId(frame);
  auto it = frames_.find(frame_id);
  if (!focus || it == frames_.end() || !it->second.lookup) {
    return;
  }
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  if (Stale(it->second)) {
    LookUp(frame_id, [weak, frame, json] {
      if (std::shared_ptr<PasswordTab> self = weak.lock()) {
        self->OnFocus(frame, json);
      }
    });
    return;
  }
  const Frame& state = it->second;
  auto form = std::ranges::find_if(
      state.forms, [&](const LoginForm& f) { return f.id == focus->form; });
  if (form == state.forms.end()) {
    return;
  }
  PasswordMenu menu;
  double zoom = std::pow(1.2, browser_->GetHost()->GetZoomLevel());
  menu.x = focus->x * zoom;
  menu.y = focus->y * zoom;
  menu.width = focus->width * zoom;
  menu.height = focus->height * zoom;
  const bool secure = IsSecureOriginForPasswords(state.origin);

  // What each menu item does, in the same order as the items.
  std::vector<std::function<void()>> actions;
  const LoginForm& chosen_form = *form;

  if (state.lookup && state.lookup->locked) {
    menu.items.push_back({PasswordMenuItem::Kind::kUnlock, "Unlock Bitwarden…",
                          "To fill your logins", 0});
    delegate_->ShowPasswordMenu(menu, [weak](int index) {
      std::shared_ptr<PasswordTab> self = weak.lock();
      if (self && index == 0) {
        self->UnlockBitwarden();
      }
    });
    return;
  }
  std::string origin = state.origin;

  if (focus->kind == LoginFieldKind::kNewPassword) {
    std::string password = GeneratePassword(manager_->options().rules);
    menu.items.push_back({PasswordMenuItem::Kind::kGeneratedPassword,
                          "Use a strong password", password, 0});
    actions.emplace_back([weak, frame_id, chosen_form, origin, password] {
      std::shared_ptr<PasswordTab> self = weak.lock();
      if (!self || !self->delegate_) {
        return;
      }
      std::vector<int> fields;
      for (const LoginField& field : chosen_form.fields) {
        if (field.kind == LoginFieldKind::kNewPassword) {
          fields.push_back(field.id);
        }
      }
      std::string message = FillFieldsMessage(fields, password);
      self->SendFill(frame_id, message);
      // Saved at once, so it is not lost if the sign-up fails half-way.
      PasswordEntry entry;
      entry.origin = origin;
      entry.username = self->RememberedUsername(origin);
      entry.password = password;
      self->manager_->Add(self->profile_, entry,
                          [weak, origin, password](Result<std::int64_t> id) {
                            std::shared_ptr<PasswordTab> tab = weak.lock();
                            if (tab && id) {
                              tab->generated_ =
                                  Generated{id.value(), origin, password};
                            }
                          });
    });
  } else if (focus->kind == LoginFieldKind::kOneTimeCode) {
    std::vector<PasswordEntry> entries = Offered(state);
    for (const PasswordEntry& entry : entries) {
      std::optional<TotpSecret> secret = ParseTotpSecret(entry.totp);
      if (!secret) {
        continue;
      }
      std::int64_t now = Now();
      std::string code = TotpCode(*secret, now);
      menu.items.push_back({PasswordMenuItem::Kind::kCode,
                            entry.username.empty()
                                ? "2FA code"
                                : "2FA code for " + entry.username,
                            code, TotpSecondsLeft(*secret, now)});
      int field = focus->field;
      std::string totp = entry.totp;
      bool reprompt = entry.reprompt;
      actions.emplace_back([weak, frame_id, field, totp, reprompt] {
        auto fill = [weak, frame_id, field, totp] {
          std::shared_ptr<PasswordTab> self = weak.lock();
          std::optional<TotpSecret> current = ParseTotpSecret(totp);
          if (self && current) {
            // The code at the time of the choice, not of the menu.
            self->SendFill(frame_id, FillFieldsMessage(
                                         {field}, TotpCode(*current, Now())));
          }
        };
        std::shared_ptr<PasswordTab> self = weak.lock();
        if (self && reprompt) {
          self->ConfirmReprompt(fill);
        } else {
          fill();
        }
      });
    }
  } else {
    for (const PasswordEntry& entry : Offered(state)) {
      bool other_host = entry.origin != state.origin &&
                        std::ranges::find(entry.other_origins, state.origin) ==
                            entry.other_origins.end();
      menu.items.push_back(
          {PasswordMenuItem::Kind::kLogin,
           entry.username.empty() ? "(no user name)" : entry.username,
           other_host ? DisplayHost(entry.origin) : "", 0});
      actions.emplace_back([weak, frame_id, chosen_form, entry] {
        std::shared_ptr<PasswordTab> self = weak.lock();
        if (self && self->delegate_) {
          self->FillChosen(frame_id, chosen_form, entry);
        }
      });
    }
  }
  if (menu.items.empty()) {
    return;
  }
  if (!secure) {
    menu.items.insert(
        menu.items.begin(),
        {PasswordMenuItem::Kind::kWarning, "This connection is not secure",
         "Others on the network may see what you fill in.", 0});
    actions.insert(actions.begin(), [] {});
  }
  menu.items.push_back(
      {PasswordMenuItem::Kind::kManage, "Manage passwords…", "", 0});
  actions.emplace_back([weak] {
    std::shared_ptr<PasswordTab> self = weak.lock();
    if (self && self->delegate_) {
      self->delegate_->OpenPasswordsPage();
    }
  });
  delegate_->ShowPasswordMenu(menu, [actions = std::move(actions)](int index) {
    if (index >= 0 && static_cast<std::size_t>(index) < actions.size()) {
      actions[static_cast<std::size_t>(index)]();
    }
  });
}

void PasswordTab::OnSubmit(const CefRefPtr<CefFrame>& frame,
                           const std::string& json) {
  std::optional<SubmittedLogin> login = ParseSubmittedLogin(json);
  std::optional<std::string> origin = WebOriginOf(frame->GetURL().ToString());
  if (!login || !origin) {
    return;
  }
  if (!login->username.empty()) {
    // The first step of a multi-step login, or a full login: remembered
    // for the password step that may follow.
    remembered_ =
        Remembered{SiteOfHost(HostOfOrigin(*origin)), login->username, Now()};
  }
  if (login->password.empty() && login->new_password.empty()) {
    return;
  }
  if (submitted_) {
    Wipe(submitted_->login.password);
    Wipe(submitted_->login.new_password);
  }
  submitted_ = Submitted{FrameId(frame), *origin, frame->GetURL().ToString(),
                         std::move(*login), Now()};
}

void PasswordTab::OnMainFrameLoadStart() {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return;
  }
  delegate_->HidePasswordMenu();
  // An unanswered prompt lasts until the user moves on to another page
  // (a script forwarding right after the login does not count).
  if (prompt_ && Now() - prompt_->time > 5) {
    Wipe(prompt_->prompt.password);
    prompt_.reset();
  }
  if (submitted_) {
    EvaluateSubmitted();
  }
  frames_.clear();
  UpdateIcon();
}

void PasswordTab::EvaluateSubmitted() {
  if (!submitted_) {
    return;
  }
  Submitted submitted = std::move(*submitted_);
  submitted_.reset();
  if (Now() - submitted.time > kSubmitSeconds) {
    Wipe(submitted.login.password);
    Wipe(submitted.login.new_password);
    return;
  }
  std::string remembered = RememberedUsername(submitted.origin);
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  auto shared = std::make_shared<Submitted>(std::move(submitted));
  manager_->Lookup(
      profile_, shared->origin, shared->url,
      [weak, shared, remembered](PasswordLookup lookup) {
        std::shared_ptr<PasswordTab> self = weak.lock();
        SubmittedLogin& login = shared->login;
        auto wipe = [&login] {
          Wipe(login.password);
          Wipe(login.new_password);
        };
        // A locked Bitwarden vault cannot tell a new login from a changed
        // one: nothing is offered.
        if (!self || !self->delegate_ || lookup.error || lookup.locked) {
          wipe();
          return;
        }
        SaveDecision decision =
            DecideSave(login, lookup.matches.exact, remembered,
                       lookup.never_save, lookup.offer_to_save);
        wipe();
        // A generated password saved before the user name was known.
        if (self->generated_ && self->generated_->origin == shared->origin &&
            SecretsEqual(self->generated_->password, decision.password)) {
          auto entry = std::ranges::find_if(
              lookup.matches.exact, [&](const PasswordEntry& e) {
                return e.id == self->generated_->id;
              });
          if (entry != lookup.matches.exact.end() &&
              entry->username != decision.username) {
            PasswordEntry updated = *entry;
            updated.username = decision.username;
            self->manager_->Update(self->profile_, updated, nullptr);
          }
          self->generated_.reset();
          return;
        }
        if (decision.offer == SaveDecision::Offer::kNothing) {
          if (decision.entry_id) {
            self->manager_->MarkUsed(self->profile_, *decision.entry_id);
          }
          return;
        }
        std::optional<PasswordEntry> existing;
        auto found = std::ranges::find_if(
            lookup.matches.exact,
            [&](const PasswordEntry& e) { return e.id == decision.entry_id; });
        if (found != lookup.matches.exact.end()) {
          existing = *found;
        }
        bool update = decision.offer == SaveDecision::Offer::kUpdate;
        self->prompt_ = Prompt{{update,
                                DisplayHost(shared->origin),
                                decision.username,
                                decision.password,
                                {}},
                               shared->origin,
                               std::move(existing),
                               Now(),
                               {}};
        Wipe(decision.password);
        self->UpdateIcon();
        if (update || !self->manager_->UsesBitwarden(self->profile_)) {
          self->ShowPrompt();
          return;
        }
        // A new Bitwarden login: the user also chooses where it goes.
        self->manager_->BitwardenSaveLocations(
            [weak](Result<std::vector<BitwardenSaveLocation>> locations) {
              std::shared_ptr<PasswordTab> tab = weak.lock();
              if (!tab || !tab->prompt_) {
                return;
              }
              if (locations) {
                tab->prompt_->locations = std::move(locations).value();
              }
              if (tab->prompt_->locations.empty()) {
                tab->prompt_->locations.push_back({"No folder", "", "", ""});
              }
              for (const BitwardenSaveLocation& location :
                   tab->prompt_->locations) {
                tab->prompt_->prompt.locations.push_back(location.label);
              }
              tab->ShowPrompt();
            });
      });
}

void PasswordTab::ShowPrompt() {
  if (!prompt_ || !delegate_) {
    return;
  }
  const PasswordPrompt& prompt = prompt_->prompt;
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  delegate_->ShowPasswordPrompt(prompt, [weak](PasswordPromptAnswer answer) {
    std::shared_ptr<PasswordTab> self = weak.lock();
    if (!self || !self->prompt_) {
      Wipe(answer.password);
      return;
    }
    Prompt prompt = std::move(*self->prompt_);
    if (answer.choice == PasswordPromptAnswer::Choice::kNotNow) {
      // Still reachable from the key icon.
      self->prompt_ = std::move(prompt);
      Wipe(answer.password);
      return;
    }
    Wipe(prompt.prompt.password);
    if (answer.choice == PasswordPromptAnswer::Choice::kNever) {
      self->manager_->SetNeverSave(self->profile_, prompt.origin, true);
    } else if (prompt.prompt.update && prompt.existing) {
      PasswordEntry entry = std::move(*prompt.existing);
      entry.username = answer.username;
      entry.password = answer.password;
      self->manager_->Update(self->profile_, entry, nullptr);
      self->WarnIfBreached(prompt.prompt.host, entry.password);
      Wipe(entry.password);
      Wipe(answer.password);
    } else {
      PasswordEntry entry;
      entry.origin = prompt.origin;
      entry.username = answer.username;
      entry.password = answer.password;
      BitwardenSaveLocation location;
      if (answer.location >= 0 &&
          static_cast<std::size_t>(answer.location) < prompt.locations.size()) {
        location = prompt.locations[static_cast<std::size_t>(answer.location)];
      }
      self->manager_->Add(self->profile_, entry, nullptr, location);
      self->WarnIfBreached(prompt.prompt.host, entry.password);
      Wipe(entry.password);
      Wipe(answer.password);
    }
    self->UpdateIcon();
  });
}

void PasswordTab::OnKeyIconClicked() {
  CEF_REQUIRE_UI_THREAD();
  if (!delegate_) {
    return;
  }
  if (prompt_) {
    ShowPrompt();
    return;
  }
  auto main = std::ranges::find_if(
      frames_, [](const auto& pair) { return pair.second.main; });
  if (main == frames_.end()) {
    return;
  }
  std::string frame_id = main->first;
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  if (Stale(main->second)) {
    LookUp(frame_id, [weak] {
      if (std::shared_ptr<PasswordTab> self = weak.lock()) {
        self->OnKeyIconClicked();
      }
    });
    return;
  }
  if (main->second.lookup && main->second.lookup->locked) {
    UnlockBitwarden();
    return;
  }
  std::vector<PasswordEntry> entries = Offered(main->second);
  std::optional<LoginForm> form = FormToFill(main->second.forms);
  PasswordMenu menu;
  menu.at_key_icon = true;
  std::vector<std::function<void()>> actions;
  for (const PasswordEntry& entry : entries) {
    bool other_host = entry.origin != main->second.origin;
    menu.items.push_back(
        {PasswordMenuItem::Kind::kLogin,
         entry.username.empty() ? "(no user name)" : entry.username,
         other_host ? DisplayHost(entry.origin) : "", 0});
    actions.emplace_back([weak, frame_id, form, entry] {
      std::shared_ptr<PasswordTab> self = weak.lock();
      if (self && self->delegate_ && form) {
        self->FillChosen(frame_id, *form, entry);
      }
    });
  }
  std::vector<PasswordEntry> exact;
  if (main->second.lookup) {
    exact = main->second.lookup->matches.exact;
  }
  if (!exact.empty()) {
    menu.items.push_back({PasswordMenuItem::Kind::kReadQrCode,
                          "Read 2FA QR code from this page", "", 0});
    actions.emplace_back([weak, exact] {
      std::shared_ptr<PasswordTab> self = weak.lock();
      if (self && self->delegate_) {
        self->ReadQrCode(exact);
      }
    });
  }
  menu.items.push_back(
      {PasswordMenuItem::Kind::kManage, "Manage passwords…", "", 0});
  actions.emplace_back([weak] {
    std::shared_ptr<PasswordTab> self = weak.lock();
    if (self && self->delegate_) {
      self->delegate_->OpenPasswordsPage();
    }
  });
  delegate_->ShowPasswordMenu(menu, [actions = std::move(actions)](int index) {
    if (index >= 0 && static_cast<std::size_t>(index) < actions.size()) {
      actions[static_cast<std::size_t>(index)]();
    }
  });
}

void PasswordTab::ReadQrCode(std::vector<PasswordEntry> entries) {
  if (!browser_ || !delegate_) {
    return;
  }
  // Brouk's own rendering of the page (no screen recording permission),
  // through a short-lived DevTools session of the tab.
  auto channel = std::make_shared<CefRefPtr<DevToolsChannel>>(
      new DevToolsChannel(browser_));
  CefRefPtr<CefDictionaryValue> params = CefDictionaryValue::Create();
  params->SetString("format", "png");
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  auto shared =
      std::make_shared<std::vector<PasswordEntry>>(std::move(entries));
  (*channel)->Send(
      "Page.captureScreenshot", params,
      [weak, channel, shared](const CefRefPtr<CefDictionaryValue>& result,
                              const std::string& /*error*/) {
        (*channel)->Close();
        std::shared_ptr<PasswordTab> self = weak.lock();
        if (!self || !self->delegate_) {
          return;
        }
        std::optional<std::string> png =
            result && result->GetType("data") == VTYPE_STRING
                ? Base64Decode(result->GetString("data").ToString())
                : std::nullopt;
        if (!png) {
          self->delegate_->ShowPasswordMessage(
              "No QR code found", "The page could not be captured.");
          return;
        }
        std::vector<std::uint8_t> bytes(png->begin(), png->end());
        self->delegate_->DecodeQrCodes(
            std::move(bytes),
            [weak, shared](const std::vector<std::string>& texts) {
              std::shared_ptr<PasswordTab> tab = weak.lock();
              if (tab && tab->delegate_) {
                tab->OnQrCodes(*shared, texts);
              }
            });
      });
}

void PasswordTab::OnQrCodes(const std::vector<PasswordEntry>& entries,
                            const std::vector<std::string>& texts) {
  std::optional<TotpSecret> secret;
  for (const std::string& text : texts) {
    secret = ParseTotpSecret(text);
    if (secret) {
      break;
    }
  }
  if (!secret) {
    delegate_->ShowPasswordMessage(
        "No 2FA QR code found",
        "Show the site's 2FA setup QR code (\"scan this code with your "
        "authenticator app\") on the page, then try again.");
    return;
  }
  std::string totp = TotpSecretToUri(*secret);
  if (entries.size() == 1) {
    AddTotp(entries.front(), totp);
    return;
  }
  // Several logins for the site: which one?
  PasswordMenu menu;
  menu.at_key_icon = true;
  for (const PasswordEntry& entry : entries) {
    menu.items.push_back(
        {PasswordMenuItem::Kind::kLogin,
         "Add 2FA code to " + (entry.username.empty()
                                   ? std::string("(no user name)")
                                   : entry.username),
         "", 0});
  }
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  delegate_->ShowPasswordMenu(menu, [weak, entries, totp](int index) {
    std::shared_ptr<PasswordTab> self = weak.lock();
    if (self && index >= 0 &&
        static_cast<std::size_t>(index) < entries.size()) {
      self->AddTotp(entries[static_cast<std::size_t>(index)], totp);
    }
  });
}

void PasswordTab::AddTotp(PasswordEntry entry, const std::string& totp) {
  std::string user = entry.username.empty() ? "the login" : entry.username;
  entry.totp = totp;
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  manager_->Update(
      profile_, entry, [weak, user](const std::optional<Error>& error) {
        std::shared_ptr<PasswordTab> self = weak.lock();
        if (!self || !self->delegate_) {
          return;
        }
        self->delegate_->ShowPasswordMessage(
            error ? "The 2FA code was not saved" : "2FA code added",
            error ? error->message
                  : "Brouk now offers the codes for " + user +
                        " on this site's code fields.");
      });
  Wipe(entry.password);
}

void PasswordTab::WarnIfBreached(const std::string& host,
                                 const std::string& password) {
  if (!manager_->options().breach_check || password.empty()) {
    return;
  }
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  CheckBreaches({password}, [weak, host](std::vector<std::int64_t> counts) {
    std::shared_ptr<PasswordTab> self = weak.lock();
    if (!self || !self->delegate_ || counts.empty() || counts.front() == 0) {
      return;
    }
    self->delegate_->ShowPasswordMessage(
        "This password was found in a data breach",
        "It is on lists that attackers use to break into accounts. Change "
        "it on " +
            host + " and wherever else you use it.");
  });
}

void PasswordTab::UpdateIcon() {
  if (!delegate_) {
    return;
  }
  if (prompt_) {
    delegate_->OnPasswordIconChanged(PasswordIcon::kSavePending);
    return;
  }
  for (const auto& [id, frame] : frames_) {
    if (frame.main && frame.lookup && frame.lookup->locked &&
        !frame.forms.empty()) {
      delegate_->OnPasswordIconChanged(PasswordIcon::kLocked);
      return;
    }
    if (frame.main && frame.lookup &&
        (!frame.lookup->matches.exact.empty() ||
         !frame.lookup->matches.same_site.empty())) {
      delegate_->OnPasswordIconChanged(PasswordIcon::kLogins);
      return;
    }
  }
  delegate_->OnPasswordIconChanged(PasswordIcon::kHidden);
}

std::string PasswordTab::RememberedUsername(const std::string& origin) const {
  if (!remembered_ || Now() - remembered_->time > kRememberSeconds ||
      remembered_->site != SiteOfHost(HostOfOrigin(origin))) {
    return {};
  }
  return remembered_->username;
}

void PasswordTab::FillChosen(const std::string& frame_id,
                             const LoginForm& form,
                             const PasswordEntry& entry) {
  if (!entry.reprompt) {
    Fill(frame_id, form, entry);
    return;
  }
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  ConfirmReprompt([weak, frame_id, form, entry] {
    std::shared_ptr<PasswordTab> self = weak.lock();
    if (self && self->delegate_) {
      self->Fill(frame_id, form, entry);
    }
  });
}

void PasswordTab::UnlockBitwarden() {
  if (unlocking_ || !delegate_) {
    return;
  }
  unlocking_ = true;
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  manager_->BitwardenState([weak](const BitwardenUnlockState& state) {
    std::shared_ptr<PasswordTab> self = weak.lock();
    if (!self || !self->delegate_) {
      return;
    }
    switch (state.status.state) {
      case BitwardenSource::State::kNoCli:
      case BitwardenSource::State::kLoggedOut:
        self->unlocking_ = false;
        self->delegate_->ShowPasswordMessage("Set up Bitwarden",
                                             state.setup_steps);
        return;
      case BitwardenSource::State::kUnlocked:
        self->unlocking_ = false;
        self->LookUpAll();
        return;
      case BitwardenSource::State::kLocked:
        break;
    }
    if (!state.stored_session ||
        !self->manager_->options().bitwarden_touch_id) {
      self->AskMasterPassword(state, "");
      return;
    }
    self->delegate_->ConfirmWithTouchId(
        "unlock Bitwarden", [weak, state](TouchIdResult result) {
          std::shared_ptr<PasswordTab> tab = weak.lock();
          if (!tab || !tab->delegate_) {
            return;
          }
          if (result != TouchIdResult::kPassed) {
            // As in Bitwarden: the master password instead.
            tab->AskMasterPassword(state, "");
            return;
          }
          tab->manager_->UnlockBitwardenWithStoredSession(
              [weak, state](const std::optional<Error>& error) {
                std::shared_ptr<PasswordTab> again = weak.lock();
                if (!again || !again->delegate_) {
                  return;
                }
                if (error) {
                  again->AskMasterPassword(state, "");
                  return;
                }
                again->unlocking_ = false;
                again->LookUpAll();
              });
        });
  });
}

void PasswordTab::AskMasterPassword(const BitwardenUnlockState& state,
                                    const std::string& error) {
  BitwardenPasswordRequest request;
  request.account = state.status.user_email;
  request.server = state.status.server_url;
  request.error = error;
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  delegate_->AskBitwardenPassword(
      request, [weak, state](std::optional<std::string> password) {
        std::shared_ptr<PasswordTab> self = weak.lock();
        if (!self) {
          if (password) {
            Wipe(*password);
          }
          return;
        }
        if (!password || !self->delegate_) {
          self->unlocking_ = false;
          return;
        }
        self->manager_->UnlockBitwarden(
            std::move(*password),
            [weak, state](const std::optional<Error>& failed) {
              std::shared_ptr<PasswordTab> tab = weak.lock();
              if (!tab || !tab->delegate_) {
                return;
              }
              if (failed) {
                tab->AskMasterPassword(state, failed->message);
                return;
              }
              tab->unlocking_ = false;
              tab->LookUpAll();
            });
      });
}

void PasswordTab::ConfirmReprompt(std::function<void()> then) {
  if (!delegate_) {
    return;
  }
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  if (!manager_->options().bitwarden_touch_id) {
    AskRepromptPassword(std::move(then), "");
    return;
  }
  delegate_->ConfirmWithTouchId(
      "fill a protected Bitwarden login",
      [weak, then = std::move(then)](TouchIdResult result) {
        std::shared_ptr<PasswordTab> self = weak.lock();
        if (!self || !self->delegate_) {
          return;
        }
        if (result == TouchIdResult::kPassed) {
          then();
        } else if (result == TouchIdResult::kUnavailable) {
          self->AskRepromptPassword(then, "");
        }
      });
}

void PasswordTab::AskRepromptPassword(std::function<void()> then,
                                      const std::string& error) {
  BitwardenPasswordRequest request;
  request.reprompt = true;
  request.error = error;
  std::weak_ptr<PasswordTab> weak = weak_from_this();
  delegate_->AskBitwardenPassword(
      request,
      [weak, then = std::move(then)](std::optional<std::string> password) {
        std::shared_ptr<PasswordTab> self = weak.lock();
        if (!password) {
          return;
        }
        if (!self || !self->delegate_) {
          Wipe(*password);
          return;
        }
        // Checking the master password means unlocking again (a new
        // session); the logins stay the same.
        self->manager_->UnlockBitwarden(
            std::move(*password),
            [weak, then](const std::optional<Error>& failed) {
              std::shared_ptr<PasswordTab> tab = weak.lock();
              if (!tab || !tab->delegate_) {
                return;
              }
              if (failed) {
                tab->AskRepromptPassword(then, failed->message);
                return;
              }
              then();
            });
      });
}

}  // namespace brouk
