#ifndef BROUK_CORE_ADBLOCK_ENGINE_H_
#define BROUK_CORE_ADBLOCK_ENGINE_H_

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct BroukAdblockEngine;

namespace brouk {

// One filter list to build an engine from.
struct AdblockList {
  std::string_view text;
  // Hosts files and plain domain lists (Pi-hole style) instead of Adblock
  // Plus / uBlock Origin syntax.
  bool hosts_format = false;
  // May use scriptlets that need trust: uBlock's own lists and My filters,
  // as uBlock Origin allows.
  bool trusted = false;
};

// A rule that matched: its list (the index in the lists the engine was
// built from), its zero-based line, and its text. Unknown parts are -1 or
// empty (the engine may merge rules).
struct AdblockRule {
  int list = -1;
  int line = -1;
  std::string text;
};

struct AdblockMatch {
  bool block = false;
  bool important = false;
  std::optional<AdblockRule> filter;
  std::optional<AdblockRule> exception;
  // `$redirect`: a data: URL to serve instead of blocking outright.
  std::string redirect;
  // `$removeparam`: the URL to load instead; empty if unchanged.
  std::string rewritten_url;
};

// Brave's adblock-rust engine (007), through our C wrapper (src/adblock).
// Matching is thread-safe: one engine serves the IO thread and others at
// once. UseResources must not run while other threads use the engine.
class AdblockEngine {
 public:
  ~AdblockEngine();
  AdblockEngine(const AdblockEngine&) = delete;
  AdblockEngine& operator=(const AdblockEngine&) = delete;

  // Null if the engine could not be built.
  static std::unique_ptr<AdblockEngine> Build(
      std::span<const AdblockList> lists);
  // From Serialize() output (the disk cache); null if the data is not an
  // engine of this version.
  static std::unique_ptr<AdblockEngine> Deserialize(
      std::span<const std::uint8_t> data);
  std::vector<std::uint8_t> Serialize() const;

  // Redirect and scriptlet resources (adblock-resources' resources.json).
  bool UseResources(std::string_view json);

  // `type` is the engine's request type: "document", "subdocument",
  // "script", "image", "stylesheet", "xmlhttprequest", "font", "media",
  // "websocket", "ping", "other"…
  AdblockMatch Check(std::string_view url,
                     std::string_view source_url,
                     std::string_view type) const;

  // The page's cosmetic resources as JSON: hide_selectors,
  // procedural_actions, exceptions, injected_script, generichide.
  std::string CosmeticResourcesJson(std::string_view url) const;
  // Generic selectors for classes and ids on a page: `request` is
  // {"classes": [...], "ids": [...], "exceptions": [...]}, the answer a
  // JSON array.
  std::string HiddenSelectorsJson(std::string_view request) const;

 private:
  // Only Build and Deserialize make engines (std::make_unique needs a public
  // constructor; the key keeps it private in effect).
  struct Key {
    explicit Key() = default;
  };

 public:
  AdblockEngine(Key /*key*/, BroukAdblockEngine* engine) : engine_(engine) {}

 private:
  BroukAdblockEngine* engine_;  // Owned.
};

// A line of the user's own filters (My filters), checked for the editor.
struct FilterLineCheck {
  enum class Kind { kRule, kCommentOrEmpty, kInvalid };
  Kind kind = Kind::kRule;
  std::string error;  // kInvalid only.
};
FilterLineCheck CheckFilterLine(std::string_view line);

}  // namespace brouk

#endif  // BROUK_CORE_ADBLOCK_ENGINE_H_
