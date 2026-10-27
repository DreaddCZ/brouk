#include "core/task_list.h"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace brouk {

namespace {

constexpr std::string_view kSubframePrefix = "Subframe:";

TaskRow::Type TypeOf(const RawTask& task) {
  switch (task.kind) {
    case TaskKind::kBrowser:
      return TaskRow::Type::kBrowser;
    case TaskKind::kGpu:
      return TaskRow::Type::kGpu;
    case TaskKind::kUtility:
      return TaskRow::Type::kUtility;
    case TaskKind::kDedicatedWorker:
    case TaskKind::kSharedWorker:
    case TaskKind::kServiceWorker:
      return TaskRow::Type::kWorker;
    case TaskKind::kRenderer:
      return std::string_view(task.title).starts_with(kSubframePrefix)
                 ? TaskRow::Type::kSubframe
                 : TaskRow::Type::kOther;
    case TaskKind::kOther:
      break;
  }
  return TaskRow::Type::kOther;
}

// Whether `next` runs in the same process as `previous`, the task listed
// right before it (see BuildTaskRows).
bool SameProcess(const TaskRow& previous, const TaskRow& next) {
  if (previous.kind == TaskKind::kBrowser || next.kind == TaskKind::kBrowser ||
      previous.kind == TaskKind::kGpu || next.kind == TaskKind::kGpu) {
    return false;
  }
  if (previous.pid != 0 && next.pid != 0) {
    return previous.pid == next.pid;
  }
  return previous.memory >= 0 && previous.memory == next.memory &&
         previous.cpu == next.cpu;
}

}  // namespace

std::vector<TaskRow> BuildTaskRows(const std::vector<RawTask>& tasks,
                                   const std::vector<TaskPage>& pages) {
  std::map<std::int64_t, std::size_t> page_of_task;
  for (std::size_t i = 0; i < pages.size(); ++i) {
    if (pages[i].task_id) {
      page_of_task.emplace(*pages[i].task_id, i);
    }
  }
  std::vector<TaskRow> rows;
  rows.reserve(tasks.size() + pages.size());
  std::set<std::int64_t> seen;
  for (const RawTask& task : tasks) {
    if (!seen.insert(task.id).second) {
      continue;  // Listed twice: keep the first.
    }
    TaskRow row;
    row.task_id = task.id;
    row.kind = task.kind;
    row.killable = task.killable;
    row.title = task.title;
    row.cpu = task.cpu;
    row.memory = task.memory;
    row.gpu_memory = task.gpu_memory;
    row.gpu_memory_inflated = task.gpu_memory_inflated;
    row.type = TypeOf(task);
    if (auto it = page_of_task.find(task.id); it != page_of_task.end()) {
      row.type = TaskRow::Type::kTab;
      row.page = it->second;
      row.pid = pages[it->second].pid;
    }
    if (rows.empty()) {
      row.process = 0;
    } else {
      const TaskRow& previous = rows.back();
      row.process = SameProcess(previous, row) ? *previous.process
                                               : *previous.process + 1;
    }
    rows.push_back(std::move(row));
  }

  // A process's ID, from a page in it; the first page of a process owns its
  // workers and subframes.
  std::map<int, int> pid_of_process;
  std::map<int, std::size_t> page_of_process;
  for (const TaskRow& row : rows) {
    if (row.type != TaskRow::Type::kTab) {
      continue;
    }
    if (row.pid != 0) {
      pid_of_process.emplace(*row.process, row.pid);
    }
    page_of_process.emplace(*row.process, *row.page);
  }
  std::optional<std::size_t> last_page;
  for (TaskRow& row : rows) {
    if (auto it = pid_of_process.find(*row.process);
        it != pid_of_process.end()) {
      row.pid = it->second;
    }
    if (row.type == TaskRow::Type::kTab) {
      last_page = row.page;
      continue;
    }
    if (row.type != TaskRow::Type::kWorker &&
        row.type != TaskRow::Type::kSubframe) {
      if (row.type != TaskRow::Type::kOther) {
        last_page.reset();  // Browser, GPU and utility rows end a page's run.
      }
      continue;
    }
    if (auto it = page_of_process.find(*row.process);
        it != page_of_process.end()) {
      row.page = it->second;
    } else if (row.type == TaskRow::Type::kSubframe) {
      // A subframe in a process of its own follows its page; a worker in a
      // process of its own (e.g. a service worker) belongs to no page.
      row.page = last_page;
    }
  }

  for (std::size_t i = 0; i < pages.size(); ++i) {
    if (pages[i].task_id && page_of_task.contains(*pages[i].task_id) &&
        seen.contains(*pages[i].task_id)) {
      continue;
    }
    TaskRow row;
    row.type = TaskRow::Type::kTab;
    row.page = i;
    rows.push_back(std::move(row));
  }
  return rows;
}

TaskTotals TotalOf(const std::vector<TaskRow>& rows) {
  TaskTotals totals;
  std::set<int> counted;
  for (const TaskRow& row : rows) {
    if (!row.process || !counted.insert(*row.process).second) {
      continue;
    }
    if (row.memory > 0) {
      totals.memory += row.memory;
    }
    totals.cpu += row.cpu;
  }
  return totals;
}

}  // namespace brouk
