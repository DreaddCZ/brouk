#include "core/task_list.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

namespace brouk {
namespace {

RawTask Task(std::int64_t id,
             TaskKind kind,
             std::int64_t memory,
             double cpu = 0,
             const char* title = "") {
  RawTask task;
  task.id = id;
  task.kind = kind;
  task.memory = memory;
  task.cpu = cpu;
  task.title = title;
  task.killable = kind != TaskKind::kBrowser;
  return task;
}

TaskPage Page(std::optional<std::int64_t> task_id, int pid = 0) {
  return TaskPage{.task_id = task_id, .pid = pid};
}

TEST(TaskListTest, MatchesPagesAndGroupsTasksOfOneProcess) {
  std::vector<RawTask> tasks = {
      Task(1, TaskKind::kBrowser, 500),
      Task(2, TaskKind::kGpu, 300),
      Task(3, TaskKind::kUtility, 40, 0, "Utility: Network Service"),
      // Two tabs in one renderer: the same process values.
      Task(10, TaskKind::kRenderer, 200, 1.5),
      Task(11, TaskKind::kRenderer, 200, 1.5),
      Task(12, TaskKind::kDedicatedWorker, 200, 1.5, "Dedicated Worker: a"),
      Task(20, TaskKind::kRenderer, 90, 0),
  };
  std::vector<TaskPage> pages = {Page(10), Page(11), Page(20)};
  std::vector<TaskRow> rows = BuildTaskRows(tasks, pages);
  ASSERT_EQ(rows.size(), 7u);
  EXPECT_EQ(rows[0].type, TaskRow::Type::kBrowser);
  EXPECT_EQ(rows[1].type, TaskRow::Type::kGpu);
  EXPECT_EQ(rows[2].type, TaskRow::Type::kUtility);
  EXPECT_EQ(rows[3].type, TaskRow::Type::kTab);
  EXPECT_EQ(rows[3].page, 0u);
  EXPECT_EQ(rows[4].page, 1u);
  EXPECT_EQ(rows[3].process, rows[4].process);
  // The worker runs in their process and belongs to its first tab.
  EXPECT_EQ(rows[5].type, TaskRow::Type::kWorker);
  EXPECT_EQ(rows[5].process, rows[3].process);
  EXPECT_EQ(rows[5].page, 0u);
  EXPECT_EQ(rows[6].page, 2u);
  EXPECT_NE(rows[6].process, rows[3].process);
  EXPECT_NE(rows[0].process, rows[1].process);
  EXPECT_NE(rows[1].process, rows[2].process);
}

TEST(TaskListTest, ReportedProcessIdsDecide) {
  // The same values by chance, but the renderers reported different IDs.
  std::vector<RawTask> tasks = {Task(10, TaskKind::kRenderer, 100),
                                Task(11, TaskKind::kRenderer, 100)};
  std::vector<TaskRow> rows =
      BuildTaskRows(tasks, {Page(10, 501), Page(11, 502)});
  ASSERT_EQ(rows.size(), 2u);
  EXPECT_NE(rows[0].process, rows[1].process);
  EXPECT_EQ(rows[0].pid, 501);
  EXPECT_EQ(rows[1].pid, 502);

  // Not measured yet, but one process ID.
  tasks = {Task(10, TaskKind::kRenderer, -1),
           Task(11, TaskKind::kRenderer, -1)};
  rows = BuildTaskRows(tasks, {Page(10, 501), Page(11, 501)});
  EXPECT_EQ(rows[0].process, rows[1].process);
}

TEST(TaskListTest, ProcessIdGoesToEveryRowOfTheProcess) {
  std::vector<RawTask> tasks = {
      Task(10, TaskKind::kRenderer, 100, 2),
      Task(12, TaskKind::kServiceWorker, 100, 2, "Service Worker: a"),
  };
  std::vector<TaskRow> rows = BuildTaskRows(tasks, {Page(10, 777)});
  EXPECT_EQ(rows[1].pid, 777);
}

TEST(TaskListTest, UnmeasuredTasksAreNotGrouped) {
  std::vector<RawTask> tasks = {
      Task(10, TaskKind::kRenderer, -1), Task(11, TaskKind::kRenderer, -1),
      Task(12, TaskKind::kUtility, 0), Task(13, TaskKind::kUtility, 0)};
  std::vector<TaskRow> rows = BuildTaskRows(tasks, {Page(10), Page(11)});
  EXPECT_NE(rows[0].process, rows[1].process);
  EXPECT_NE(rows[2].process, rows[3].process);
}

TEST(TaskListTest, SubframeInItsOwnProcessBelongsToThePageBeforeIt) {
  std::vector<RawTask> tasks = {
      Task(10, TaskKind::kRenderer, 100),
      Task(13, TaskKind::kRenderer, 60, 0, "Subframe: https://ads.example"),
      Task(14, TaskKind::kServiceWorker, 30, 0, "Service Worker: b"),
      Task(3, TaskKind::kUtility, 20),
      Task(15, TaskKind::kRenderer, 70, 0, "Subframe: https://x.example"),
  };
  std::vector<TaskRow> rows = BuildTaskRows(tasks, {Page(10)});
  EXPECT_EQ(rows[1].type, TaskRow::Type::kSubframe);
  EXPECT_EQ(rows[1].page, 0u);
  // A worker in a process of its own belongs to no page.
  EXPECT_EQ(rows[2].type, TaskRow::Type::kWorker);
  EXPECT_EQ(rows[2].page, std::nullopt);
  // After a utility process, a subframe has no page to belong to.
  EXPECT_EQ(rows[4].page, std::nullopt);
}

TEST(TaskListTest, UnknownRenderersAreOther) {
  std::vector<RawTask> tasks = {
      Task(30, TaskKind::kRenderer, 80, 0, "DevTools - example.com")};
  std::vector<TaskRow> rows = BuildTaskRows(tasks, {});
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].type, TaskRow::Type::kOther);
  EXPECT_EQ(rows[0].title, "DevTools - example.com");
}

TEST(TaskListTest, PagesWithoutTasksAreListedLastAsNotLoaded) {
  std::vector<RawTask> tasks = {Task(10, TaskKind::kRenderer, 100)};
  // A deferred tab, and a tab whose task Chromium does not list (yet).
  std::vector<TaskRow> rows =
      BuildTaskRows(tasks, {Page(std::nullopt), Page(10), Page(99)});
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].page, 1u);
  EXPECT_EQ(rows[1].page, 0u);
  EXPECT_EQ(rows[1].task_id, std::nullopt);
  EXPECT_EQ(rows[1].process, std::nullopt);
  EXPECT_EQ(rows[2].page, 2u);
  EXPECT_EQ(rows[2].process, std::nullopt);
}

TEST(TaskListTest, DuplicateTasksAreListedOnce) {
  std::vector<RawTask> tasks = {Task(10, TaskKind::kRenderer, 100),
                                Task(10, TaskKind::kRenderer, 100)};
  EXPECT_EQ(BuildTaskRows(tasks, {Page(10)}).size(), 1u);
}

TEST(TaskListTest, TotalsCountEachProcessOnce) {
  std::vector<RawTask> tasks = {
      Task(1, TaskKind::kBrowser, 500, 3),
      Task(10, TaskKind::kRenderer, 200, 10),
      Task(11, TaskKind::kRenderer, 200, 10),
      Task(20, TaskKind::kRenderer, -1, 0),
  };
  std::vector<TaskRow> rows =
      BuildTaskRows(tasks, {Page(10), Page(11), Page(std::nullopt)});
  TaskTotals totals = TotalOf(rows);
  EXPECT_EQ(totals.memory, 700);
  EXPECT_DOUBLE_EQ(totals.cpu, 13);
}

}  // namespace
}  // namespace brouk
