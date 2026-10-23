#include "core/request_log.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace brouk {

namespace {

bool IsBase64UrlChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_';
}

}  // namespace

Headers RequestLog::Limit(Headers headers) {
  std::size_t total = 0;
  Headers kept;
  for (Header& header : headers) {
    total += header.first.size() + header.second.size();
    if (total > kMaxHeaderBytes) {
      break;
    }
    kept.push_back(std::move(header));
  }
  return kept;
}

RecordedRequest* RequestLog::Latest(const std::string& id) {
  for (RecordedRequest& entry : std::ranges::reverse_view(entries_)) {
    if (entry.id == id) {
      return &entry;
    }
  }
  return nullptr;
}

void RequestLog::OnRequest(const std::string& id,
                           std::string url,
                           std::string method,
                           std::string type,
                           Headers headers,
                           std::optional<std::string> post_data,
                           double wall_time,
                           double timestamp) {
  RecordedRequest entry;
  entry.seq = next_seq_++;
  entry.id = id;
  entry.url = std::move(url);
  entry.method = std::move(method);
  entry.type = std::move(type);
  entry.request_headers = Limit(std::move(headers));
  if (post_data) {
    if (post_data->size() > kMaxPostDataBytes) {
      post_data->resize(kMaxPostDataBytes);
      entry.post_data_truncated = true;
    }
    entry.post_data = std::move(post_data);
  }
  entry.started = wall_time;
  start_times_.emplace_back(entry.seq, timestamp);
  entries_.push_back(std::move(entry));
  while (entries_.size() > kMaxEntries) {
    std::uint64_t dropped = entries_.front().seq;
    entries_.pop_front();
    std::erase_if(start_times_,
                  [dropped](const auto& t) { return t.first == dropped; });
  }
  ++version_;
}

void RequestLog::OnRequestExtraHeaders(const std::string& id, Headers headers) {
  if (RecordedRequest* entry = Latest(id)) {
    entry->request_headers = Limit(std::move(headers));
    ++version_;
  }
}

void RequestLog::OnResponse(const std::string& id,
                            int status,
                            std::string mime_type,
                            Headers headers) {
  if (RecordedRequest* entry = Latest(id)) {
    entry->status = status;
    entry->mime_type = std::move(mime_type);
    if (entry->response_headers.empty()) {
      entry->response_headers = Limit(std::move(headers));
    }
    ++version_;
  }
}

void RequestLog::OnResponseExtraHeaders(const std::string& id,
                                        Headers headers) {
  if (RecordedRequest* entry = Latest(id)) {
    entry->response_headers = Limit(std::move(headers));
    ++version_;
  }
}

void RequestLog::OnFinished(const std::string& id,
                            double timestamp,
                            std::int64_t encoded_bytes) {
  if (RecordedRequest* entry = Latest(id)) {
    entry->finished = true;
    entry->encoded_bytes = encoded_bytes;
    for (const auto& [seq, start] : start_times_) {
      if (seq == entry->seq) {
        entry->duration = std::max(0.0, timestamp - start);
      }
    }
    ++version_;
  }
}

void RequestLog::OnFailed(const std::string& id,
                          double timestamp,
                          std::string error) {
  if (RecordedRequest* entry = Latest(id)) {
    entry->finished = true;
    entry->error = std::move(error);
    for (const auto& [seq, start] : start_times_) {
      if (seq == entry->seq) {
        entry->duration = std::max(0.0, timestamp - start);
      }
    }
    ++version_;
  }
}

void RequestLog::Clear() {
  entries_.clear();
  start_times_.clear();
  ++version_;
}

const RecordedRequest* RequestLog::Find(std::uint64_t seq) const {
  auto it = std::ranges::find(entries_, seq, &RecordedRequest::seq);
  return it == entries_.end() ? nullptr : &*it;
}

std::vector<std::string> FindJwts(std::string_view text) {
  std::vector<std::string> found;
  std::size_t pos = 0;
  while ((pos = text.find("eyJ", pos)) != std::string_view::npos) {
    // Must start a token, not be inside a longer base64url run.
    if (pos > 0 && IsBase64UrlChar(text[pos - 1])) {
      pos += 3;
      continue;
    }
    std::size_t end = pos;
    int dots = 0;
    std::size_t part_start = pos;
    bool ok = true;
    while (end < text.size() &&
           (IsBase64UrlChar(text[end]) || text[end] == '.')) {
      if (text[end] == '.') {
        if (end == part_start) {
          ok = false;  // Empty part.
        }
        ++dots;
        part_start = end + 1;
      }
      ++end;
    }
    // header.payload.signature (the signature may be empty: alg "none").
    if (ok && dots == 2 && end - pos >= 20) {
      found.emplace_back(text.substr(pos, end - pos));
    }
    pos = end;
  }
  return found;
}

std::vector<FoundToken> FindTokens(const RequestLog& log, std::size_t limit) {
  std::vector<FoundToken> tokens;
  std::set<std::string, std::less<>> seen;
  for (const RecordedRequest& entry :
       std::ranges::reverse_view(log.entries())) {
    const RecordedRequest* it = &entry;
    auto scan = [&](const Headers& headers, std::string_view side) {
      for (const auto& [name, value] : headers) {
        std::vector<std::string> found = FindJwts(value);
        // A bearer token that is not a JWT is listed too (shown as not
        // decodable).
        constexpr std::string_view kBearer = "Bearer ";
        if (found.empty() && value.size() > kBearer.size() &&
            value.starts_with(kBearer)) {
          std::string_view rest =
              std::string_view(value).substr(kBearer.size());
          found.emplace_back(
              rest.substr(0, std::min<std::size_t>(rest.size(), 4096)));
        }
        for (std::string& token : found) {
          if (tokens.size() < limit && seen.insert(token).second) {
            tokens.push_back({std::move(token), std::string(side) + " header " +
                                                    name + ": " + it->method +
                                                    " " + it->url});
          }
        }
      }
    };
    scan(it->request_headers, "Request");
    scan(it->response_headers, "Response");
    if (tokens.size() >= limit) {
      break;
    }
  }
  return tokens;
}

}  // namespace brouk
