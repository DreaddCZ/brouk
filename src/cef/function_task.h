#ifndef BROUK_CEF_FUNCTION_TASK_H_
#define BROUK_CEF_FUNCTION_TASK_H_

#include <functional>
#include <utility>

#include "include/cef_task.h"

namespace brouk {

// Runs a function as a CEF task (CefPostTask), e.g. to get from the IO
// thread to the UI thread.
class FunctionTask : public CefTask {
 public:
  explicit FunctionTask(std::function<void()> function)
      : function_(std::move(function)) {}
  void Execute() override { function_(); }

 private:
  std::function<void()> function_;
  IMPLEMENT_REFCOUNTING(FunctionTask);
};

}  // namespace brouk

#endif  // BROUK_CEF_FUNCTION_TASK_H_
