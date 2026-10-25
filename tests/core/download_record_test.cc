#include "core/download_record.h"

#include <gtest/gtest.h>

#include <filesystem>

#include "core/history_store.h"

namespace brouk {
namespace {

using State = DownloadRecord::State;

TEST(DownloadRecordTest, StatusTexts) {
  DownloadRecord record;
  record.received = 12'300'000;
  record.total = 40'000'000;
  record.speed = 230'000;
  EXPECT_EQ(DownloadStatusText(record), "12.3 / 40.0 MB, 2 min left");
  record.state = State::kPaused;
  EXPECT_EQ(DownloadStatusText(record), "Paused, 12.3 / 40.0 MB");
  record.state = State::kComplete;
  record.received = 40'000'000;
  EXPECT_EQ(DownloadStatusText(record), "Done, 40.0 MB");
  record.state = State::kFailed;
  record.error = DownloadInterruptText(22);
  EXPECT_EQ(DownloadStatusText(record), "Failed — network disconnected");
  record.state = State::kCancelled;
  EXPECT_EQ(DownloadStatusText(record), "Cancelled");
  DownloadRecord unknown;
  unknown.received = 1500;
  EXPECT_EQ(DownloadStatusText(unknown), "1.5 KB");
}

TEST(DownloadRecordTest, Formats) {
  EXPECT_EQ(FormatByteSize(999), "999 bytes");
  EXPECT_EQ(FormatByteSize(1'200'000'000), "1.20 GB");
  EXPECT_EQ(FormatTimeLeft(5), "5 s left");
  EXPECT_EQ(FormatTimeLeft(3900), "1 h 5 min left");
  EXPECT_EQ(DownloadInterruptText(33), "file not found on the server");
  EXPECT_EQ(DownloadInterruptText(3), "disk full");
}

TEST(DownloadRecordTest, FileNamesAndRecent) {
  DownloadRecord record;
  record.url = "https://example.com/files/report.pdf?x=1";
  EXPECT_EQ(record.FileName(), "report.pdf");
  record.path = "/Users/me/Downloads/report (1).pdf";
  EXPECT_EQ(record.FileName(), "report (1).pdf");

  DownloadRecord old;
  old.id = 1;
  old.start_time = 1000;
  old.state = State::kComplete;
  DownloadRecord running = old;
  running.id = 2;
  running.state = State::kInProgress;
  DownloadRecord fresh = old;
  fresh.id = 3;
  fresh.start_time = 100'000;
  std::vector<DownloadRecord> recent =
      RecentDownloads({old, running, fresh}, 100'500, 86'400);
  ASSERT_EQ(recent.size(), 2U);
  EXPECT_EQ(recent[0].id, 3);
  EXPECT_EQ(recent[1].id, 2);
}

TEST(DownloadRecordTest, StoredInTheHistory) {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() / "brouk_downloads_test.db";
  std::filesystem::remove(path);
  {
    Result<std::unique_ptr<HistoryStore>> opened = HistoryStore::Open(path);
    ASSERT_TRUE(opened);
    HistoryStore& store = *opened.value();
    DownloadRecord record;
    record.url = "https://example.com/a.zip";
    record.path = "/tmp/a.zip";
    record.start_time = 50;
    Result<std::int64_t> id = store.SaveDownload(record);
    ASSERT_TRUE(id);
    record.id = id.value();
    record.state = State::kComplete;
    record.received = 10;
    record.opened = true;
    ASSERT_TRUE(store.SaveDownload(record));
    DownloadRecord running;
    running.url = "https://example.com/b.zip";
    running.start_time = 60;
    ASSERT_TRUE(store.SaveDownload(running));
    std::vector<DownloadRecord> all = store.Downloads(100);
    ASSERT_EQ(all.size(), 2U);
    EXPECT_EQ(all[0].url, "https://example.com/b.zip");
    EXPECT_EQ(all[1].state, State::kComplete);
    EXPECT_TRUE(all[1].opened);
    EXPECT_EQ(store.CountDownloadsSince(0), 1);
    EXPECT_EQ(store.DeleteDownloadsSince(0), 1);  // The running one stays.
    EXPECT_EQ(store.Downloads(100).size(), 1U);
    EXPECT_TRUE(store.RemoveDownload(store.Downloads(100)[0].id));
  }
  std::filesystem::remove(path);
}

}  // namespace
}  // namespace brouk
