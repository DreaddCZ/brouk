#ifndef BROUK_CEF_NETWORK_COUNTER_H_
#define BROUK_CEF_NETWORK_COUNTER_H_

#include <atomic>
#include <cstdint>
#include <memory>

#include "include/cef_resource_request_handler.h"

namespace brouk {

// The task manager's Network column (020): bytes of response bodies a tab
// received, counted as they arrive. Counting only happens while the task
// manager is open (SetNetworkCounting); it passes every body through a
// response filter that copies it unchanged.
using ByteCounter = std::shared_ptr<std::atomic<std::int64_t>>;

// Any thread.
void SetNetworkCounting(bool on);
bool IsNetworkCounting();

// For a request nothing else handles: counts its response into `counter`.
// IO thread.
CefRefPtr<CefResourceRequestHandler> MakeCountingRequestHandler(
    ByteCounter counter);

}  // namespace brouk

#endif  // BROUK_CEF_NETWORK_COUNTER_H_
