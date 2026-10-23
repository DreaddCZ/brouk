#ifndef BROUK_CORE_REQUEST_LOG_H_
#define BROUK_CORE_REQUEST_LOG_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

// A header as sent or received: name and value (names keep their case).
using Header = std::pair<std::string, std::string>;
using Headers = std::vector<Header>;

// One network request of a tab, recorded while its dev tools are on (006).
struct RecordedRequest {
  std::uint64_t seq = 0;  // Order of recording, starting at 1.
  std::string id;         // The protocol's requestId.
  std::string url;
  std::string method;
  std::string type;  // Resource type: Document, XHR, Fetch, Script...
  Headers request_headers;
  std::optional<std::string> post_data;  // Up to kMaxPostDataBytes.
  bool post_data_truncated = false;
  int status = 0;  // 0 until a response arrived.
  std::string mime_type;
  Headers response_headers;
  bool finished = false;
  std::string error;     // Why it failed, if it did.
  double started = 0;    // Wall time, seconds since 1970.
  double duration = -1;  // Seconds, -1 while running.
  std::int64_t encoded_bytes = -1;
};

// The requests of one tab, newest last, at most kMaxEntries (the oldest
// are dropped). Fed from the protocol's Network events; everything comes
// from the page and is only stored and shown.
class RequestLog {
 public:
  static constexpr std::size_t kMaxEntries = 2000;
  static constexpr std::size_t kMaxPostDataBytes = std::size_t{256} * 1024;
  static constexpr std::size_t kMaxHeaderBytes = std::size_t{64} * 1024;

  // Network.requestWillBeSent. A redirect reuses the id: the earlier hop
  // is kept as its own entry.
  void OnRequest(const std::string& id,
                 std::string url,
                 std::string method,
                 std::string type,
                 Headers headers,
                 std::optional<std::string> post_data,
                 double wall_time,
                 double timestamp);
  // Network.requestWillBeSentExtraInfo: the headers actually sent (with
  // cookies), replacing the ones the page set.
  void OnRequestExtraHeaders(const std::string& id, Headers headers);
  void OnResponse(const std::string& id,
                  int status,
                  std::string mime_type,
                  Headers headers);
  void OnResponseExtraHeaders(const std::string& id, Headers headers);
  void OnFinished(const std::string& id,
                  double timestamp,
                  std::int64_t encoded_bytes);
  void OnFailed(const std::string& id, double timestamp, std::string error);
  void Clear();

  const std::deque<RecordedRequest>& entries() const { return entries_; }
  const RecordedRequest* Find(std::uint64_t seq) const;
  // Bumped on every change, so viewers can skip unchanged lists.
  std::uint64_t version() const { return version_; }

 private:
  RecordedRequest* Latest(const std::string& id);
  static Headers Limit(Headers headers);

  std::deque<RecordedRequest> entries_;
  std::uint64_t next_seq_ = 1;
  std::uint64_t version_ = 0;
  // Protocol timestamps (monotonic) of requests, for durations.
  std::vector<std::pair<std::uint64_t, double>> start_times_;
};

// A JWT found in the tab's requests (006, Tokens tool).
struct FoundToken {
  std::string token;
  std::string where;  // "Request header Authorization: GET https://..."
};

// JWT-shaped strings (three base64url parts, the first starting with
// "eyJ") in request and response headers, and other "Bearer" tokens,
// newest first, each once.
std::vector<FoundToken> FindTokens(const RequestLog& log, std::size_t limit);
// JWT-shaped strings in a text (a header or storage value).
std::vector<std::string> FindJwts(std::string_view text);

}  // namespace brouk

#endif  // BROUK_CORE_REQUEST_LOG_H_
