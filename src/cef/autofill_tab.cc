#include "cef/autofill_tab.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/password_forms.h"
#include "core/web_origin.h"
#include "include/cef_process_message.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

std::string FrameId(const CefRefPtr<CefFrame>& frame) {
  return frame->GetIdentifier().ToString();
}

std::string Shorten(const std::string& text) {
  return text.size() > 60 ? text.substr(0, 57) + "…" : text;
}

}  // namespace

AutofillTab::AutofillTab(AutofillManager* manager,
                         PasswordTabDelegate* menus,
                         AutofillTabDelegate* delegate)
    : manager_(manager), menus_(menus), delegate_(delegate) {}

void AutofillTab::Detach() {
  CEF_REQUIRE_UI_THREAD();
  menus_ = nullptr;
  delegate_ = nullptr;
  browser_ = nullptr;
  frames_.clear();
}

void AutofillTab::OnMainFrameLoadStart() {
  CEF_REQUIRE_UI_THREAD();
  frames_.clear();
}

void AutofillTab::HandleMessage(const CefRefPtr<CefBrowser>& browser,
                                const CefRefPtr<CefFrame>& frame,
                                const std::string& kind,
                                const std::string& json) {
  CEF_REQUIRE_UI_THREAD();
  if (!menus_ || !delegate_ || !frame) {
    return;
  }
  browser_ = browser;
  if (kind == kFormsKindAutofillForms) {
    OnForms(frame, json);
  } else if (kind == kFormsKindAutofillFocus) {
    OnFocus(frame, json);
  } else if (kind == kFormsKindAutofillSubmit) {
    OnSubmit(frame, json);
  }
}

void AutofillTab::OnForms(const CefRefPtr<CefFrame>& frame,
                          const std::string& json) {
  std::optional<std::vector<AutofillForm>> forms = ParseAutofillForms(json);
  std::optional<std::string> origin = WebOriginOf(frame->GetURL().ToString());
  if (!forms || !origin) {
    return;
  }
  Frame& state = frames_[FrameId(frame)];
  if (state.origin != *origin) {
    state = Frame{};
    state.origin = *origin;
  }
  state.forms = std::move(*forms);
}

void AutofillTab::OnFocus(const CefRefPtr<CefFrame>& frame,
                          const std::string& json) {
  const AutofillOptions& options = manager_->options();
  std::optional<AutofillFocus> focus = ParseAutofillFocus(json);
  std::string frame_id = FrameId(frame);
  auto it = frames_.find(frame_id);
  if (!options.enabled || !focus || it == frames_.end() || !browser_) {
    return;
  }
  // Only the main frame and frames of its origin (009).
  if (!frame->IsMain()) {
    CefRefPtr<CefFrame> main = browser_->GetMainFrame();
    if (!main || WebOriginOf(main->GetURL().ToString()) != it->second.origin) {
      return;
    }
  }
  int form_id = focus->form;
  auto form = std::ranges::find_if(
      it->second.forms, [&](const auto& f) { return f.id == form_id; });
  if (form == it->second.forms.end()) {
    return;
  }
  PasswordMenu menu;
  double zoom = std::pow(1.2, browser_->GetHost()->GetZoomLevel());
  menu.x = focus->x * zoom;
  menu.y = focus->y * zoom;
  menu.width = focus->width * zoom;
  menu.height = focus->height * zoom;
  std::vector<std::function<void()>> actions;
  std::weak_ptr<AutofillTab> weak = weak_from_this();

  if (focus->kind && options.entries) {
    // Recently used entries first (009: the default sort of the list).
    std::vector<const AutofillEntry*> entries;
    for (const AutofillEntry& entry : manager_->entries()) {
      if (!entry.ValueFor(*focus->kind).empty()) {
        entries.push_back(&entry);
      }
    }
    std::ranges::stable_sort(entries, [](const auto* a, const auto* b) {
      return a->last_used > b->last_used;
    });
    for (const AutofillEntry* entry : entries) {
      std::string title =
          entry->label.empty() ? entry->ValueFor(*focus->kind) : entry->label;
      menu.items.push_back({PasswordMenuItem::Kind::kAutofillEntry, title,
                            Shorten(entry->Preview()), 0});
      std::int64_t id = entry->id;
      actions.emplace_back([weak, frame_id, form_id, id] {
        if (std::shared_ptr<AutofillTab> self = weak.lock()) {
          self->FillForm(frame_id, form_id, id);
        }
      });
    }
  }
  if (options.custom_fields) {
    for (const CustomField& field : manager_->custom_fields()) {
      if (!CustomFieldMatches(field, focus->hint)) {
        continue;
      }
      menu.items.push_back({PasswordMenuItem::Kind::kCustomField, field.name,
                            Shorten(field.value), 0});
      int target = focus->field;
      std::int64_t id = field.id;
      std::string value = field.value;
      actions.emplace_back([weak, frame_id, target, id, value] {
        if (std::shared_ptr<AutofillTab> self = weak.lock()) {
          self->Send(frame_id, AutofillFieldMessage(target, value));
          self->manager_->MarkCustomFieldUsed(id);
        }
      });
    }
  }
  auto filled = it->second.filled.find(form_id);
  if (filled != it->second.filled.end() && !filled->second.empty()) {
    menu.items.push_back(
        {PasswordMenuItem::Kind::kClearFilled, "Clear filled fields", "", 0});
    actions.emplace_back([weak, frame_id, form_id] {
      if (std::shared_ptr<AutofillTab> self = weak.lock()) {
        self->ClearForm(frame_id, form_id);
      }
    });
  }
  if (menu.items.empty()) {
    return;
  }
  menu.items.push_back(
      {PasswordMenuItem::Kind::kManageAutofill, "Manage autofill…", "", 0});
  actions.emplace_back([weak] {
    std::shared_ptr<AutofillTab> self = weak.lock();
    if (self && self->delegate_) {
      self->delegate_->OpenAutofillSettings();
    }
  });
  menus_->ShowPasswordMenu(menu, [actions = std::move(actions)](int index) {
    if (index >= 0 && static_cast<std::size_t>(index) < actions.size()) {
      actions[static_cast<std::size_t>(index)]();
    }
  });
}

void AutofillTab::FillForm(const std::string& frame_id,
                           int form_id,
                           std::int64_t entry_id) {
  auto frame = frames_.find(frame_id);
  auto entry = std::ranges::find_if(
      manager_->entries(), [&](const auto& e) { return e.id == entry_id; });
  if (frame == frames_.end() || entry == manager_->entries().end()) {
    return;
  }
  auto form = std::ranges::find_if(
      frame->second.forms, [&](const auto& f) { return f.id == form_id; });
  if (form == frame->second.forms.end()) {
    return;
  }
  std::vector<int> filled;
  Send(frame_id, AutofillFillMessage(*form, *entry, &filled));
  std::vector<int>& record = frame->second.filled[form_id];
  for (int id : filled) {
    if (std::ranges::find(record, id) == record.end()) {
      record.push_back(id);
    }
  }
  manager_->MarkEntryUsed(entry_id);
}

void AutofillTab::ClearForm(const std::string& frame_id, int form_id) {
  auto frame = frames_.find(frame_id);
  if (frame == frames_.end()) {
    return;
  }
  std::vector<int> filled = std::move(frame->second.filled[form_id]);
  frame->second.filled.erase(form_id);
  Send(frame_id, AutofillClearMessage(filled));
}

void AutofillTab::Send(const std::string& frame_id,
                       const std::string& message) {
  if (!browser_) {
    return;
  }
  CefRefPtr<CefFrame> frame = browser_->GetFrameByIdentifier(frame_id);
  auto it = frames_.find(frame_id);
  if (!frame || !frame->IsValid() || it == frames_.end() ||
      WebOriginOf(frame->GetURL().ToString()) != it->second.origin) {
    return;
  }
  CefRefPtr<CefProcessMessage> fill =
      CefProcessMessage::Create(std::string(kFormsFillMessage));
  fill->GetArgumentList()->SetString(0, message);
  frame->SendProcessMessage(PID_RENDERER, fill);
}

void AutofillTab::OnSubmit(const CefRefPtr<CefFrame>& frame,
                           const std::string& json) {
  const AutofillOptions& options = manager_->options();
  std::optional<AutofillSubmit> submit = ParseAutofillSubmit(json);
  std::optional<std::string> origin = WebOriginOf(frame->GetURL().ToString());
  if (!options.enabled || !options.entries || !options.offer_to_save ||
      !submit || !origin || !manager_->loaded() || manager_->error()) {
    return;
  }
  AutofillValues stored = ToStoredValues(submit->values);
  if (!WorthSaving(stored)) {
    return;
  }
  // The same values submitted twice (submit event and Enter): asked once.
  std::string key;
  for (const auto& [field, value] : stored) {
    key += std::string(AutofillFieldName(field)) + "=" + value + "\n";
  }
  if (key == last_submit_) {
    return;
  }
  last_submit_ = key;
  AutofillComparison result =
      CompareWithEntries(stored, manager_->entries(), options.country);
  std::string site = HostOfOrigin(*origin);
  if (site.starts_with("www.")) {
    site = site.substr(4);
  }
  if (result.kind == AutofillComparison::Kind::kSame) {
    if (result.entry) {
      manager_->MarkEntryUsed(manager_->entries()[*result.entry].id);
    }
    return;
  }
  AutofillPrompt prompt;
  std::optional<std::int64_t> entry_id;
  auto line = [](AutofillField field, std::string old_value,
                 std::string new_value) {
    AutofillPrompt::Line out;
    out.field = std::string(AutofillFieldLabel(field));
    out.old_value = std::move(old_value);
    out.problem = ValidateAutofillValue(field, new_value).value_or("");
    out.new_value = std::move(new_value);
    return out;
  };
  if (result.kind == AutofillComparison::Kind::kDifferent || !result.entry) {
    prompt.kind = AutofillPrompt::Kind::kSaveNew;
    prompt.suggested_label = site;
    for (const auto& [field, value] : stored) {
      prompt.lines.push_back(line(field, "", value));
    }
  } else {
    const AutofillEntry& entry = manager_->entries()[*result.entry];
    entry_id = entry.id;
    prompt.entry_label = entry.label.empty() ? entry.Preview() : entry.label;
    prompt.suggested_label = site;
    prompt.kind = result.kind == AutofillComparison::Kind::kAddsInformation
                      ? AutofillPrompt::Kind::kAddTo
                      : AutofillPrompt::Kind::kUpdate;
    for (AutofillField field : result.changed_fields) {
      prompt.lines.push_back(line(field, entry.Get(field), stored[field]));
    }
    for (AutofillField field : result.new_fields) {
      prompt.lines.push_back(line(field, "", stored[field]));
    }
  }
  Ask(prompt, entry_id, std::move(stored), site);
}

void AutofillTab::Ask(const AutofillPrompt& prompt,
                      std::optional<std::int64_t> entry_id,
                      AutofillValues stored,
                      std::string site) {
  std::weak_ptr<AutofillTab> weak = weak_from_this();
  AutofillPrompt::Kind kind = prompt.kind;
  delegate_->ShowAutofillPrompt(
      prompt, [weak, kind, entry_id, stored = std::move(stored),
               site = std::move(site)](const AutofillPromptAnswer& answer) {
        std::shared_ptr<AutofillTab> self = weak.lock();
        if (!self || !self->delegate_) {
          return;
        }
        using Choice = AutofillPromptAnswer::Choice;
        if (answer.choice == Choice::kNotNow) {
          return;
        }
        if (answer.choice == Choice::kEdit) {
          self->delegate_->OpenAutofillSettings();
          return;
        }
        AutofillManager* manager = self->manager_;
        bool as_new = kind == AutofillPrompt::Kind::kSaveNew ||
                      answer.choice == Choice::kSaveAsNew;
        if (as_new) {
          AutofillEntry entry;
          entry.label = answer.label.empty() ? site : answer.label;
          entry.origin_site = site;
          for (const auto& [field, value] : stored) {
            entry.Set(field, value);
          }
          manager->AddEntry(std::move(entry), nullptr);
          return;
        }
        auto it = std::ranges::find_if(manager->entries(), [&](const auto& e) {
          return entry_id && e.id == *entry_id;
        });
        if (it == manager->entries().end()) {
          return;
        }
        AutofillEntry entry = *it;
        for (const auto& [field, value] : stored) {
          // Add to: only the new fields; Update: the changed ones too.
          if (kind == AutofillPrompt::Kind::kUpdate ||
              entry.Get(field).empty()) {
            entry.Set(field, value);
          }
        }
        manager->UpdateEntry(entry);
        manager->MarkEntryUsed(entry.id);
      });
}

}  // namespace brouk
