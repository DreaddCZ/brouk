#include "cef/task_manager_handler.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cef/devtools_call.h"
#include "cef/json_util.h"
#include "cef/network_counter.h"
#include "core/internal_pages.h"
#include "core/task_list.h"
#include "include/cef_parser.h"
#include "include/cef_task_manager.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_helpers.h"

namespace brouk {

namespace {

// Copy sends the table, with graph samples when copied as JSON.
constexpr std::size_t kMaxRequestBytes = std::size_t{8} * 1024 * 1024;
constexpr std::size_t kMaxKeys = 1000;
constexpr int kBadRequest = 400;

using Callback = CefMessageRouterBrowserSide::Callback;

std::string ToJson(const CefRefPtr<CefDictionaryValue>& dictionary) {
  CefRefPtr<CefValue> value = CefValue::Create();
  value->SetDictionary(dictionary);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}

double Now() {
  using std::chrono::duration;
  using std::chrono::system_clock;
  return duration<double>(system_clock::now().time_since_epoch()).count();
}

TaskKind KindOf(cef_task_type_t type) {
  switch (type) {
    case CEF_TASK_TYPE_BROWSER:
      return TaskKind::kBrowser;
    case CEF_TASK_TYPE_GPU:
      return TaskKind::kGpu;
    case CEF_TASK_TYPE_UTILITY:
      return TaskKind::kUtility;
    case CEF_TASK_TYPE_RENDERER:
    case CEF_TASK_TYPE_GUEST:
      return TaskKind::kRenderer;
    case CEF_TASK_TYPE_DEDICATED_WORKER:
      return TaskKind::kDedicatedWorker;
    case CEF_TASK_TYPE_SHARED_WORKER:
      return TaskKind::kSharedWorker;
    case CEF_TASK_TYPE_SERVICE_WORKER:
      return TaskKind::kServiceWorker;
    default:
      return TaskKind::kOther;
  }
}

const char* TypeName(const TaskRow& row, const TaskManagerPage* page) {
  switch (row.type) {
    case TaskRow::Type::kTab:
      return page && page->brouk_page ? "brouk" : "tab";
    case TaskRow::Type::kSubframe:
      return "subframe";
    case TaskRow::Type::kWorker:
      return "worker";
    case TaskRow::Type::kBrowser:
      return "browser";
    case TaskRow::Type::kGpu:
      return "gpu";
    case TaskRow::Type::kUtility:
      return "utility";
    case TaskRow::Type::kOther:
      break;
  }
  return "other";
}

std::vector<RawTask> ChromiumTasks(const CefRefPtr<CefTaskManager>& manager) {
  std::vector<RawTask> tasks;
  CefTaskManager::TaskIdList ids;
  if (!manager || !manager->GetTaskIdsList(ids)) {
    return tasks;
  }
  for (std::int64_t id : ids) {
    CefTaskInfo info;
    if (!manager->GetTaskInfo(id, info)) {
      continue;
    }
    RawTask task;
    task.id = id;
    task.kind = KindOf(info.type);
    task.killable = info.is_killable != 0;
    task.title = CefString(&info.title).ToString();
    task.cpu = info.cpu_usage;
    // CEF reports 0 for "not measured yet".
    task.memory = info.memory > 0 ? info.memory : -1;
    task.gpu_memory = info.gpu_memory > 0 ? info.gpu_memory : -1;
    task.gpu_memory_inflated = info.is_gpu_memory_inflated != 0;
    tasks.push_back(std::move(task));
  }
  return tasks;
}

std::optional<std::string> Text(const CefRefPtr<CefDictionaryValue>& message,
                                const char* key,
                                std::size_t max_size) {
  if (message->GetType(key) != VTYPE_STRING) {
    return std::nullopt;
  }
  std::string value = message->GetString(key).ToString();
  if (value.size() > max_size) {
    return std::nullopt;
  }
  return value;
}

std::optional<TaskPageAction> ActionNamed(const std::string& name) {
  static const std::map<std::string, TaskPageAction> kActions = {
      {"goto", TaskPageAction::kGoTo},
      {"pause", TaskPageAction::kPause},
      {"resume", TaskPageAction::kResume},
      {"unload", TaskPageAction::kUnload},
      {"mute", TaskPageAction::kMute},
      {"unmute", TaskPageAction::kUnmute},
      {"reload", TaskPageAction::kReload},
      {"devtools", TaskPageAction::kDevTools},
  };
  auto it = kActions.find(name);
  if (it == kActions.end()) {
    return std::nullopt;
  }
  return it->second;
}

// Task ids the page sent: whole numbers only.
std::optional<std::vector<std::int64_t>> TaskIds(
    const CefRefPtr<CefListValue>& list) {
  if (!list || list->GetSize() > kMaxKeys) {
    return std::nullopt;
  }
  std::vector<std::int64_t> ids;
  for (std::size_t i = 0; i < list->GetSize(); ++i) {
    double number = 0;
    if (list->GetType(i) == VTYPE_INT) {
      number = list->GetInt(i);
    } else if (list->GetType(i) == VTYPE_DOUBLE) {
      number = list->GetDouble(i);
    } else {
      return std::nullopt;
    }
    constexpr double kMax = 9.0e15;
    if (number < 0 || number > kMax ||
        number != static_cast<double>(static_cast<std::int64_t>(number))) {
      return std::nullopt;
    }
    ids.push_back(static_cast<std::int64_t>(number));
  }
  return ids;
}

// A number in a DevTools answer (small ones come as integers).
std::optional<double> Number(const CefRefPtr<CefDictionaryValue>& dictionary,
                             const char* key) {
  if (!dictionary) {
    return std::nullopt;
  }
  if (dictionary->GetType(key) == VTYPE_INT) {
    return dictionary->GetInt(key);
  }
  if (dictionary->GetType(key) == VTYPE_DOUBLE) {
    return dictionary->GetDouble(key);
  }
  return std::nullopt;
}

double RefreshSeconds(const std::string& choice) {
  static const std::map<std::string, double> kSeconds = {
      {"0.5s", 0.5}, {"1s", 1}, {"2s", 2}, {"5s", 5}, {"10s", 10}};
  auto it = kSeconds.find(choice);
  return it == kSeconds.end() ? 1 : it->second;
}

}  // namespace

TaskManagerHandler::TaskManagerHandler(
    std::unique_ptr<TaskManagerDelegate> delegate)
    : delegate_(std::move(delegate)),
      manager_(CefTaskManager::GetTaskManager()),
      heap_(std::make_shared<std::map<int, HeapUsage>>()) {
  SetNetworkCounting(true);
}

TaskManagerHandler::~TaskManagerHandler() {
  SetNetworkCounting(false);
}

void TaskManagerHandler::RequestHeapUsage(
    const std::vector<TaskManagerPage>& pages) {
  std::set<int> loaded;
  for (const TaskManagerPage& page : pages) {
    if (!page.browser) {
      continue;
    }
    int id = page.browser->GetIdentifier();
    loaded.insert(id);
    std::weak_ptr<std::map<int, HeapUsage>> weak = heap_;
    CallDevToolsMethod(
        page.browser, "Runtime.getHeapUsage", nullptr,
        [weak, id](bool ok, const std::string& result) {
          std::shared_ptr<std::map<int, HeapUsage>> heap = weak.lock();
          if (!heap) {
            return;
          }
          CefRefPtr<CefValue> value = ok ? ParseJson(result) : nullptr;
          CefRefPtr<CefDictionaryValue> answer =
              value && value->GetType() == VTYPE_DICTIONARY
                  ? value->GetDictionary()
                  : nullptr;
          std::optional<double> used = Number(answer, "usedSize");
          std::optional<double> total = Number(answer, "totalSize");
          if (used && total) {
            (*heap)[id] = {*used, *total};
          }
        });
  }
  std::erase_if(*heap_, [&loaded](const auto& entry) {
    return !loaded.contains(entry.first);
  });
}

std::string TaskManagerHandler::List(bool js_memory) {
  std::vector<TaskManagerPage> pages = delegate_->Pages();
  const CefRefPtr<CefTaskManager>& manager = manager_;
  std::vector<TaskPage> task_pages;
  task_pages.reserve(pages.size());
  for (const TaskManagerPage& page : pages) {
    TaskPage task_page;
    task_page.pid = page.pid;
    if (page.browser && manager) {
      std::int64_t id =
          manager->GetTaskIdForBrowserId(page.browser->GetIdentifier());
      if (id >= 0) {
        task_page.task_id = id;
      }
    }
    task_pages.push_back(task_page);
  }
  std::vector<RawTask> tasks = ChromiumTasks(manager);
  int processors = 1;
  if (manager && !tasks.empty()) {
    CefTaskInfo info;
    if (manager->GetTaskInfo(tasks.front().id, info)) {
      processors = info.number_of_processors;
    }
  }
  std::vector<TaskRow> rows = BuildTaskRows(tasks, task_pages);
  TaskTotals totals = TotalOf(rows);
  if (js_memory) {
    RequestHeapUsage(pages);
  }

  double now = Now();
  std::map<std::string, NetworkSample> network;
  CefRefPtr<CefDictionaryValue> page_list = CefDictionaryValue::Create();
  for (const TaskManagerPage& page : pages) {
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    item->SetString("title", page.title);
    item->SetString("url", page.url);
    item->SetString("icon", page.icon);
    item->SetString("profile", page.profile_name);
    item->SetString("color", page.profile_color);
    item->SetString("window", page.window);
    item->SetBool("loaded", page.browser != nullptr);
    item->SetBool("brouk", page.brouk_page);
    item->SetBool("ownWindow", page.own_window);
    item->SetBool("audible", page.audible);
    item->SetBool("muted", page.muted);
    item->SetBool("camera", page.camera);
    item->SetBool("microphone", page.microphone);
    item->SetBool("screen", page.screen);
    item->SetBool("paused", page.paused);
    item->SetBool("loading", page.loading);
    item->SetBool("crashed", page.crashed);
    item->SetBool("devtools", page.devtools);
    item->SetBool("shown", page.shown);
    // Bytes per second since the last list.
    double rate = -1;
    if (auto it = network_.find(page.key);
        it != network_.end() && now > it->second.time &&
        page.received_bytes >= it->second.bytes) {
      rate = static_cast<double>(page.received_bytes - it->second.bytes) /
             (now - it->second.time);
    }
    network[page.key] = {page.received_bytes, now};
    item->SetDouble("network", page.brouk_page ? -1 : rate);
    double js_used = -1;
    double js_total = -1;
    if (page.browser) {
      if (auto it = heap_->find(page.browser->GetIdentifier());
          it != heap_->end()) {
        js_used = it->second.used;
        js_total = it->second.total;
      }
    }
    item->SetDouble("jsUsed", js_used);
    item->SetDouble("jsTotal", js_total);
    page_list->SetDictionary(page.key, item);
  }
  network_ = std::move(network);

  CefRefPtr<CefListValue> row_list = CefListValue::Create();
  for (const TaskRow& row : rows) {
    const TaskManagerPage* page = row.page ? &pages[*row.page] : nullptr;
    CefRefPtr<CefDictionaryValue> item = CefDictionaryValue::Create();
    // Rows of pages keep their key when they load or unload.
    item->SetString("key",
                    row.type == TaskRow::Type::kTab && page
                        ? page->key
                        : "task-" + std::to_string(row.task_id.value_or(0)));
    if (row.task_id) {
      item->SetDouble("task", static_cast<double>(*row.task_id));
    } else {
      item->SetNull("task");
    }
    item->SetString("type", TypeName(row, page));
    item->SetString("title", row.title);
    if (page) {
      item->SetString("page", page->key);
    } else {
      item->SetNull("page");
    }
    if (row.process) {
      item->SetInt("process", *row.process);
    } else {
      item->SetNull("process");
    }
    item->SetInt("pid", row.pid);
    item->SetDouble("cpu", row.cpu);
    item->SetDouble("memory", static_cast<double>(row.memory));
    item->SetDouble("gpuMemory", static_cast<double>(row.gpu_memory));
    item->SetBool("gpuInflated", row.gpu_memory_inflated);
    // The browser and GPU processes are never ended (020).
    item->SetBool("killable", row.killable && row.kind != TaskKind::kBrowser &&
                                  row.kind != TaskKind::kGpu);
    row_list->SetDictionary(row_list->GetSize(), item);
  }

  CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
  answer->SetDouble("time", now * 1000);
  answer->SetInt("processors", processors);
  answer->SetList("rows", row_list);
  answer->SetDictionary("pages", page_list);
  answer->SetDouble("totalMemory", static_cast<double>(totals.memory));
  answer->SetDouble("totalCpu", totals.cpu);
  return ToJson(answer);
}

bool TaskManagerHandler::OnQuery(CefRefPtr<CefBrowser> /*browser*/,
                                 CefRefPtr<CefFrame> frame,
                                 int64_t /*query_id*/,
                                 const CefString& request,
                                 bool persistent,
                                 CefRefPtr<Callback> callback) {
  CEF_REQUIRE_UI_THREAD();
  if (!frame || InternalPageOf(frame->GetURL().ToString()) != "task-manager") {
    return false;
  }
  std::string text = request.ToString();
  CefRefPtr<CefValue> parsed =
      text.size() <= kMaxRequestBytes ? ParseJson(text) : nullptr;
  CefRefPtr<CefDictionaryValue> message =
      parsed && parsed->GetType() == VTYPE_DICTIONARY ? parsed->GetDictionary()
                                                      : nullptr;
  std::optional<std::string> type =
      message ? Text(message, "type", 64) : std::nullopt;
  if (!type || persistent) {
    callback->Failure(kBadRequest, "Invalid request.");
    return true;
  }

  if (*type == "tasks.list") {
    bool js_memory = message->GetType("jsMemory") == VTYPE_BOOL &&
                     message->GetBool("jsMemory");
    callback->Success(List(js_memory));
    return true;
  }

  if (*type == "tasks.action") {
    std::optional<std::string> name = Text(message, "action", 32);
    std::optional<TaskPageAction> action =
        name ? ActionNamed(*name) : std::nullopt;
    CefRefPtr<CefListValue> keys = message->GetType("pages") == VTYPE_LIST
                                       ? message->GetList("pages")
                                       : nullptr;
    if (!action || !keys || keys->GetSize() > kMaxKeys) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    std::set<std::string> known;
    for (const TaskManagerPage& page : delegate_->Pages()) {
      known.insert(page.key);
    }
    for (std::size_t i = 0; i < keys->GetSize(); ++i) {
      if (keys->GetType(i) != VTYPE_STRING) {
        continue;
      }
      std::string key = keys->GetString(i).ToString();
      if (known.contains(key)) {
        delegate_->Act(key, *action);
      }
    }
    callback->Success("");
    return true;
  }

  if (*type == "tasks.end") {
    std::optional<std::vector<std::int64_t>> ids = TaskIds(
        message->GetType("tasks") == VTYPE_LIST ? message->GetList("tasks")
                                                : nullptr);
    if (!ids) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    const CefRefPtr<CefTaskManager>& manager = manager_;
    int ended = 0;
    for (std::int64_t id : *ids) {
      CefTaskInfo info;
      if (!manager || !manager->GetTaskInfo(id, info) || !info.is_killable ||
          info.type == CEF_TASK_TYPE_BROWSER ||
          info.type == CEF_TASK_TYPE_GPU) {
        continue;
      }
      if (manager->KillTask(id)) {
        ++ended;
      }
    }
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetInt("ended", ended);
    callback->Success(ToJson(answer));
    return true;
  }

  if (*type == "tasks.settings") {
    const Settings& settings = delegate_->GlobalSettings();
    CefRefPtr<CefDictionaryValue> answer = CefDictionaryValue::Create();
    answer->SetString("columns", settings.GetString("task_manager.columns"));
    answer->SetString("sort", settings.GetString("task_manager.sort"));
    answer->SetDouble(
        "refresh",
        RefreshSeconds(settings.GetString("task_manager.refresh_interval")));
    answer->SetInt(
        "history",
        static_cast<int>(settings.GetInt("task_manager.history_length")));
    answer->SetBool("graph", settings.GetBool("task_manager.graph"));
    callback->Success(ToJson(answer));
    return true;
  }

  if (*type == "tasks.set") {
    std::optional<std::string> key = Text(message, "key", 16);
    std::optional<Error> error;
    if (key == "columns" || key == "sort") {
      std::optional<std::string> value = Text(message, "value", 256);
      if (!value) {
        callback->Failure(kBadRequest, "Invalid request.");
        return true;
      }
      error = delegate_->SetGlobalSetting("task_manager." + *key,
                                          SettingValue(*value));
    } else if (key == "graph" && message->GetType("value") == VTYPE_BOOL) {
      error = delegate_->SetGlobalSetting(
          "task_manager.graph", SettingValue(message->GetBool("value")));
    } else {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    if (error) {
      callback->Failure(kBadRequest, error->message);
    } else {
      callback->Success("");
    }
    return true;
  }

  if (*type == "tasks.copy") {
    std::optional<std::string> value = Text(message, "text", kMaxRequestBytes);
    if (!value) {
      callback->Failure(kBadRequest, "Invalid request.");
      return true;
    }
    delegate_->CopyText(*value);
    callback->Success("");
    return true;
  }

  callback->Failure(kBadRequest, "Invalid request.");
  return true;
}

}  // namespace brouk
