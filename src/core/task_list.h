#ifndef BROUK_CORE_TASK_LIST_H_
#define BROUK_CORE_TASK_LIST_H_

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace brouk {

// What Chromium's task manager reports a task to be (020).
enum class TaskKind {
  kBrowser,
  kGpu,
  kUtility,
  kRenderer,
  kDedicatedWorker,
  kSharedWorker,
  kServiceWorker,
  kOther,
};

// One task as Chromium's task manager reports it, in its order: tasks of
// one process are next to each other, related processes (a subframe
// process after its page) kept together where possible.
struct RawTask {
  std::int64_t id = 0;
  TaskKind kind = TaskKind::kOther;
  bool killable = false;
  std::string title;
  // Of the task's process: CPU in % of one core, memory footprint and GPU
  // memory in bytes (-1: not measured yet).
  double cpu = 0;
  std::int64_t memory = -1;
  std::int64_t gpu_memory = -1;
  // The GPU process's value includes the memory of everything it serves.
  bool gpu_memory_inflated = false;
};

// A page Brouk shows (a tab or a window of a Brouk page): its main task,
// if it has a browser now, and the process ID its renderer reported.
struct TaskPage {
  // Chromium's task for the page's browser; nullopt when it has none (a tab
  // that has not loaded yet or was unloaded).
  std::optional<std::int64_t> task_id;
  int pid = 0;  // 0 when not reported (yet).
};

// One row of the task manager.
struct TaskRow {
  enum class Type {
    kTab,       // A page Brouk shows (`page` says which).
    kSubframe,  // A frame of a page that runs in a process of its own.
    kWorker,    // A dedicated, shared or service worker.
    kBrowser,
    kGpu,
    kUtility,
    kOther,  // A page Brouk does not know (e.g. a DevTools frontend).
  };
  Type type = Type::kOther;
  // Null for a page that is not loaded.
  std::optional<std::int64_t> task_id;
  TaskKind kind = TaskKind::kOther;
  bool killable = false;
  std::string title;  // Chromium's, for rows that are not pages.
  // Index into the `pages` given to BuildTaskRows: the page this row is
  // (kTab) or belongs to (workers and subframes in its process or right
  // after it). Null when it belongs to no page.
  std::optional<std::size_t> page;
  // Rows of one process share a number (0, 1, …); null when not loaded.
  std::optional<int> process;
  int pid = 0;
  double cpu = 0;
  std::int64_t memory = -1;
  std::int64_t gpu_memory = -1;
  bool gpu_memory_inflated = false;
};

// The task manager's rows: Chromium's tasks grouped by process, each
// matched with the page it shows or belongs to, followed by the pages that
// are not loaded. Chromium does not say which tasks share a process; tasks
// next to each other share one when their process values (memory and CPU)
// are the same, or when their pages reported the same process ID, and
// never when they reported different ones.
std::vector<TaskRow> BuildTaskRows(const std::vector<RawTask>& tasks,
                                   const std::vector<TaskPage>& pages);

// Brouk's total memory and CPU: each process counted once.
struct TaskTotals {
  std::int64_t memory = 0;
  double cpu = 0;
};
TaskTotals TotalOf(const std::vector<TaskRow>& rows);

}  // namespace brouk

#endif  // BROUK_CORE_TASK_LIST_H_
