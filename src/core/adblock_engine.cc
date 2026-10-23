#include "core/adblock_engine.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "brouk_adblock.h"

namespace brouk {

namespace {

// Takes ownership of a string from the wrapper.
std::string TakeString(char* text) {
  if (text == nullptr) {
    return {};
  }
  std::string result(text);
  brouk_adblock_free_string(text);
  return result;
}

std::optional<AdblockRule> RuleOf(bool present,
                                  std::int32_t list,
                                  std::int32_t line,
                                  const char* text) {
  if (!present) {
    return std::nullopt;
  }
  return AdblockRule{.list = list,
                     .line = line,
                     .text = text != nullptr ? std::string(text) : ""};
}

const std::uint8_t* Bytes(std::string_view text) {
  return reinterpret_cast<const std::uint8_t*>(text.data());
}

}  // namespace

AdblockEngine::~AdblockEngine() {
  brouk_adblock_engine_free(engine_);
}

std::unique_ptr<AdblockEngine> AdblockEngine::Build(
    std::span<const AdblockList> lists) {
  std::vector<BroukAdblockList> raw;
  raw.reserve(lists.size());
  for (const AdblockList& list : lists) {
    raw.push_back(
        {.text = Bytes(list.text),
         .len = list.text.size(),
         .format = static_cast<std::uint8_t>(list.hosts_format ? 1 : 0),
         .trusted = static_cast<std::uint8_t>(list.trusted ? 1 : 0)});
  }
  BroukAdblockEngine* engine =
      brouk_adblock_engine_build(raw.data(), raw.size());
  if (engine == nullptr) {
    return nullptr;
  }
  return std::make_unique<AdblockEngine>(Key{}, engine);
}

std::unique_ptr<AdblockEngine> AdblockEngine::Deserialize(
    std::span<const std::uint8_t> data) {
  BroukAdblockEngine* engine =
      brouk_adblock_engine_deserialize(data.data(), data.size());
  if (engine == nullptr) {
    return nullptr;
  }
  return std::make_unique<AdblockEngine>(Key{}, engine);
}

std::vector<std::uint8_t> AdblockEngine::Serialize() const {
  std::size_t len = 0;
  std::uint8_t* data = brouk_adblock_engine_serialize(engine_, &len);
  if (data == nullptr) {
    return {};
  }
  std::vector<std::uint8_t> bytes(data, data + len);
  brouk_adblock_free_bytes(data, len);
  return bytes;
}

bool AdblockEngine::UseResources(std::string_view json) {
  return brouk_adblock_use_resources(engine_, Bytes(json), json.size()) != 0;
}

AdblockMatch AdblockEngine::Check(std::string_view url,
                                  std::string_view source_url,
                                  std::string_view type) const {
  // The wrapper takes NUL-terminated strings.
  std::string url_text(url);
  std::string source_text(source_url);
  std::string type_text(type);
  BroukAdblockMatch raw{};
  brouk_adblock_check(engine_, url_text.c_str(), source_text.c_str(),
                      type_text.c_str(), &raw);
  AdblockMatch match;
  match.block = raw.block != 0;
  match.important = raw.important != 0;
  match.filter = RuleOf(raw.has_filter != 0, raw.filter_list, raw.filter_line,
                        raw.filter_text);
  match.exception = RuleOf(raw.has_exception != 0, raw.exception_list,
                           raw.exception_line, raw.exception_text);
  if (raw.redirect != nullptr) {
    match.redirect = raw.redirect;
  }
  if (raw.rewritten_url != nullptr) {
    match.rewritten_url = raw.rewritten_url;
  }
  brouk_adblock_match_clear(&raw);
  return match;
}

std::string AdblockEngine::CosmeticResourcesJson(std::string_view url) const {
  std::string url_text(url);
  return TakeString(
      brouk_adblock_url_cosmetic_resources(engine_, url_text.c_str()));
}

std::string AdblockEngine::HiddenSelectorsJson(std::string_view request) const {
  return TakeString(brouk_adblock_hidden_class_id_selectors(
      engine_, Bytes(request), request.size()));
}

FilterLineCheck CheckFilterLine(std::string_view line) {
  std::string text(line);
  char* error = nullptr;
  std::uint8_t result = brouk_adblock_check_filter(text.c_str(), &error);
  FilterLineCheck check;
  switch (result) {
    case 0:
      check.kind = FilterLineCheck::Kind::kRule;
      break;
    case 1:
      check.kind = FilterLineCheck::Kind::kCommentOrEmpty;
      break;
    default:
      check.kind = FilterLineCheck::Kind::kInvalid;
      check.error = error != nullptr ? std::string(error) : "Invalid rule.";
      break;
  }
  brouk_adblock_free_string(error);
  return check;
}

}  // namespace brouk
