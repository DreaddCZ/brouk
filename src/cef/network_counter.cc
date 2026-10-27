#include "cef/network_counter.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

#include "include/cef_response_filter.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

std::atomic<bool> g_counting{false};

// Copies the body unchanged and counts it. IO thread.
class CountingFilter : public CefResponseFilter {
 public:
  explicit CountingFilter(ByteCounter counter) : counter_(std::move(counter)) {}

  bool InitFilter() override { return true; }

  FilterStatus Filter(void* data_in,
                      size_t data_in_size,
                      size_t& data_in_read,
                      void* data_out,
                      size_t data_out_size,
                      size_t& data_out_written) override {
    size_t size = std::min(data_in_size, data_out_size);
    if (size > 0) {
      std::memcpy(data_out, data_in, size);
    }
    data_in_read = size;
    data_out_written = size;
    counter_->fetch_add(static_cast<std::int64_t>(size),
                        std::memory_order_relaxed);
    // The rest of the input comes again in the next call.
    return size < data_in_size ? RESPONSE_FILTER_NEED_MORE_DATA
                               : RESPONSE_FILTER_DONE;
  }

 private:
  ByteCounter counter_;
  IMPLEMENT_REFCOUNTING(CountingFilter);
};

class CountingRequestHandler : public CefResourceRequestHandler {
 public:
  explicit CountingRequestHandler(ByteCounter counter)
      : counter_(std::move(counter)) {}

  CefRefPtr<CefResponseFilter> GetResourceResponseFilter(
      CefRefPtr<CefBrowser> /*browser*/,
      CefRefPtr<CefFrame> /*frame*/,
      CefRefPtr<CefRequest> /*request*/,
      CefRefPtr<CefResponse> /*response*/) override {
    CEF_REQUIRE_IO_THREAD();
    return new CountingFilter(counter_);
  }

 private:
  ByteCounter counter_;
  IMPLEMENT_REFCOUNTING(CountingRequestHandler);
};

}  // namespace

void SetNetworkCounting(bool on) {
  g_counting.store(on, std::memory_order_relaxed);
}

bool IsNetworkCounting() {
  return g_counting.load(std::memory_order_relaxed);
}

CefRefPtr<CefResourceRequestHandler> MakeCountingRequestHandler(
    ByteCounter counter) {
  return new CountingRequestHandler(std::move(counter));
}

}  // namespace brouk
