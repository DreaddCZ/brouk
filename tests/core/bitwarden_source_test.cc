#include "core/bitwarden_source.h"

#include <gtest/gtest.h>

#include <map>

#include "core/crypto.h"
#include "core/json.h"

namespace brouk {
namespace {

constexpr std::string_view kItems = R"([
  {"type":1,"id":"item-1","name":"www.example.com","reprompt":0,
   "login":{"uris":[{"uri":"https://www.example.com/login"}],
            "username":"alice","password":"pw-a"}}])";

// A fake `bw`: answers by command ("status", "unlock", "list items", ...),
// checks the session and records what it was asked.
struct FakeBw {
  std::string status = "locked";
  std::string server = "https://vault.example.com";
  std::string master_password = "correct";
  std::string valid_session = "session-1";
  std::map<std::string, std::string> answers = {
      {"list items", std::string(kItems)},
      {"list folders",
       R"([{"id":"f1","name":"Work"},{"id":null,"name":"No Folder"}])"},
      {"list collections",
       R"([{"id":"c1","organizationId":"o1","name":"Shared"}])"},
      {"list organizations", R"([{"id":"o1","name":"Company"}])"},
      {"create item", R"({"id":"item-2"})"},
      {"get item",
       R"({"id":"item-1","type":1,"login":{"username":"alice","password":"pw-a"}})"},
  };
  std::vector<ProcessRequest> requests;

  static std::string Env(const ProcessRequest& request, std::string_view name) {
    for (const auto& [key, value] : request.environment) {
      if (key == name) {
        return value;
      }
    }
    return "";
  }

  Result<ProcessOutput> Run(const ProcessRequest& request) {
    requests.push_back(request);
    ProcessOutput output;
    output.exit_code = 0;
    const std::vector<std::string>& a = request.arguments;
    std::string command = a[0] + (a.size() > 2 ? " " + a[1] : "");
    if (command == "status") {
      output.out =
          R"({"serverUrl":")" + server +
          R"(","userEmail":"user@example.com","userId":"u1","status":")" +
          status + R"("})";
    } else if (command == "unlock --passwordenv") {
      if (Env(request, "BW_PASSWORD") != master_password) {
        output.exit_code = 1;
        output.err = "Invalid master password.";
      } else {
        output.out = valid_session;
      }
    } else if (command == "lock") {
      valid_session = "ended";
    } else if (Env(request, "BW_SESSION") != valid_session) {
      output.exit_code = 1;
      output.err = "Vault is locked.";
    } else if (command == "sync") {
    } else if (answers.contains(command)) {
      output.out = answers[command];
    } else if (command == "edit item") {
      output.out = "{}";
    }
    return output;
  }

  std::vector<std::string> Commands() const {
    std::vector<std::string> commands;
    for (const ProcessRequest& request : requests) {
      commands.push_back(request.arguments[0]);
    }
    return commands;
  }
};

// Where the fake `bw` is, in the form each system's search accepts.
#if defined(_WIN32)
constexpr const char* kFakeCli = "C:\\tools\\bw.exe";
#else
constexpr const char* kFakeCli = "/opt/homebrew/bin/bw";
#endif

struct Fixture {
  Fixture() {
    BitwardenSource::Options options;
    options.cli_setting = kFakeCli;
    source.SetOptions(options);
  }

  FakeBw bw;
  std::vector<std::string> certificate_servers;
  BitwardenSource source{
      [this](const ProcessRequest& request) { return bw.Run(request); },
      [](const std::filesystem::path& path) { return path == kFakeCli; },
      [this](const std::string& server) {
        certificate_servers.push_back(server);
        return std::filesystem::path("/data/certificates.pem");
      }};
};

TEST(BitwardenSourceTest, StatusWithoutTheCli) {
  BitwardenSource source(
      [](const ProcessRequest&) -> Result<ProcessOutput> {
        ADD_FAILURE() << "bw must not run";
        return Error{"no"};
      },
      [](const std::filesystem::path&) { return false; }, nullptr);
  EXPECT_EQ(source.CheckStatus().state, BitwardenSource::State::kNoCli);
  EXPECT_TRUE(source.Unlock("pw", 0));
}

TEST(BitwardenSourceTest, StatusWhenLoggedOut) {
  Fixture f;
  f.bw.status = "unauthenticated";
  EXPECT_EQ(f.source.CheckStatus().state, BitwardenSource::State::kLoggedOut);
  std::optional<Error> error = f.source.Unlock("correct", 0);
  ASSERT_TRUE(error);
  EXPECT_EQ(error->message, "Bitwarden is not signed in.");
}

TEST(BitwardenSourceTest, UnlockFindAndLock) {
  Fixture f;
  BitwardenSource::Status status = f.source.CheckStatus();
  EXPECT_EQ(status.state, BitwardenSource::State::kLocked);
  EXPECT_EQ(status.user_email, "user@example.com");
  EXPECT_TRUE(f.source.Find("https://www.example.com/", 0).exact.empty());

  std::optional<Error> wrong = f.source.Unlock("wrong", 0);
  ASSERT_TRUE(wrong);
  EXPECT_EQ(wrong->message, "The master password is not correct.");
  EXPECT_FALSE(f.source.unlocked());

  ASSERT_FALSE(f.source.Unlock("correct", 0));
  EXPECT_TRUE(f.source.unlocked());
  EXPECT_EQ(f.source.session(), "session-1");
  EXPECT_EQ(f.source.CheckStatus().state, BitwardenSource::State::kUnlocked);
  PasswordMatches matches = f.source.Find("https://shop.example.com/", 10);
  ASSERT_EQ(matches.exact.size(), 1U);
  EXPECT_EQ(matches.exact[0].password, "pw-a");

  // Certificates are prepared once for the server, before it is contacted.
  EXPECT_EQ(f.certificate_servers,
            std::vector<std::string>{"https://vault.example.com"});
  EXPECT_EQ(FakeBw::Env(f.bw.requests.back(), "NODE_EXTRA_CA_CERTS"),
            "/data/certificates.pem");

  f.source.Lock(false);
  EXPECT_FALSE(f.source.unlocked());
  EXPECT_TRUE(f.source.Find("https://www.example.com/", 20).exact.empty());
  // Without ending the session, `bw lock` was not run: the session key
  // still works (kept for Touch ID).
  EXPECT_EQ(f.bw.valid_session, "session-1");
  ASSERT_FALSE(f.source.UseSession("session-1", 30));
  EXPECT_EQ(f.source.Find("https://www.example.com/", 30).exact.size(), 1U);

  f.source.Lock(true);
  EXPECT_EQ(f.bw.valid_session, "ended");
  std::optional<Error> stale = f.source.UseSession("session-1", 40);
  ASSERT_TRUE(stale);
  EXPECT_EQ(stale->message, kBitwardenLockedError);
  EXPECT_FALSE(f.source.unlocked());
}

TEST(BitwardenSourceTest, IdleAfterTheLockTime) {
  Fixture f;
  f.source.SetOptions(
      {.cli_setting = kFakeCli, .search_path = "", .lock_seconds = 900});
  ASSERT_FALSE(f.source.Unlock("correct", 1000));
  EXPECT_FALSE(f.source.IsIdle(1899));
  f.source.Find("https://www.example.com/", 1800);
  EXPECT_FALSE(f.source.IsIdle(2600));
  EXPECT_TRUE(f.source.IsIdle(2700));
}

TEST(BitwardenSourceTest, ASessionEndedElsewhereLocks) {
  Fixture f;
  ASSERT_FALSE(f.source.Unlock("correct", 0));
  f.bw.valid_session = "unlocked-in-terminal";
  std::optional<Error> error = f.source.Sync(1);
  ASSERT_TRUE(error);
  EXPECT_EQ(error->message, kBitwardenLockedError);
  EXPECT_FALSE(f.source.unlocked());
}

TEST(BitwardenSourceTest, SaveLocations) {
  Fixture f;
  ASSERT_FALSE(f.source.Unlock("correct", 0));
  Result<std::vector<BitwardenSaveLocation>> locations =
      f.source.SaveLocations(1);
  ASSERT_TRUE(locations);
  ASSERT_EQ(locations.value().size(), 3U);
  EXPECT_EQ(locations.value()[0].label, "No folder");
  EXPECT_EQ(locations.value()[1].label, "Work");
  EXPECT_EQ(locations.value()[1].folder_id, "f1");
  EXPECT_EQ(locations.value()[2].label, "Company / Shared");
  EXPECT_EQ(locations.value()[2].collection_id, "c1");
}

TEST(BitwardenSourceTest, AddSavesSyncsAndReturnsTheNewEntry) {
  Fixture f;
  ASSERT_FALSE(f.source.Unlock("correct", 0));
  f.bw.answers["list items"] =
      R"([{"type":1,"id":"item-1","login":{"uris":[{"uri":"https://www.example.com"}],"username":"alice","password":"pw-a"}},)"
      R"({"type":1,"id":"item-2","login":{"uris":[{"uri":"https://new.example.org"}],"username":"bob","password":"pw-b"}}])";
  PasswordEntry entry;
  entry.origin = "https://new.example.org";
  entry.username = "bob";
  entry.password = "pw-b";
  Result<std::int64_t> id = f.source.Add(entry, {"Work", "f1", "", ""}, 1);
  ASSERT_TRUE(id);
  PasswordMatches matches = f.source.Find("https://new.example.org/", 2);
  ASSERT_EQ(matches.exact.size(), 1U);
  EXPECT_EQ(matches.exact[0].id, id.value());

  const ProcessRequest* create = nullptr;
  for (const ProcessRequest& request : f.bw.requests) {
    if (request.arguments[0] == "create") {
      create = &request;
    }
  }
  ASSERT_NE(create, nullptr);
  std::optional<std::string> json = Base64Decode(create->input);
  ASSERT_TRUE(json);
  Result<JsonValue> item = ParseJsonDocument(*json);
  ASSERT_TRUE(item);
  EXPECT_EQ(item.value().StringOr("folderId", ""), "f1");
  EXPECT_EQ(item.value().Find("login")->StringOr("password", ""), "pw-b");
  std::vector<std::string> commands = f.bw.Commands();
  EXPECT_EQ(std::vector<std::string>(commands.end() - 3, commands.end()),
            (std::vector<std::string>{"create", "sync", "list"}));
}

TEST(BitwardenSourceTest, UpdateEditsTheItem) {
  Fixture f;
  ASSERT_FALSE(f.source.Unlock("correct", 0));
  PasswordEntry entry = f.source.Find("https://www.example.com/", 1).exact[0];
  entry.password = "changed";
  ASSERT_FALSE(f.source.Update(entry, 2));
  const ProcessRequest* edit = nullptr;
  for (const ProcessRequest& request : f.bw.requests) {
    if (request.arguments[0] == "edit") {
      edit = &request;
    }
  }
  ASSERT_NE(edit, nullptr);
  EXPECT_EQ(edit->arguments[2], "item-1");
  Result<JsonValue> item = ParseJsonDocument(*Base64Decode(edit->input));
  ASSERT_TRUE(item);
  EXPECT_EQ(item.value().Find("login")->StringOr("password", ""), "changed");

  entry.id = 999;
  EXPECT_TRUE(f.source.Update(entry, 3));
}

TEST(BitwardenSourceTest, NothingWorksWhileLocked) {
  Fixture f;
  PasswordEntry entry;
  EXPECT_FALSE(f.source.Add(entry, {}, 0));
  EXPECT_TRUE(f.source.Update(entry, 0));
  EXPECT_FALSE(f.source.SaveLocations(0));
  EXPECT_TRUE(f.source.Sync(0));
}

TEST(BitwardenSourceTest, SetupSteps) {
  BitwardenSource::Status status;
  status.state = BitwardenSource::State::kNoCli;
  EXPECT_NE(BitwardenSetupSteps(status, "").find("brew install bitwarden-cli"),
            std::string::npos);

  status.state = BitwardenSource::State::kLoggedOut;
  std::string steps = BitwardenSetupSteps(status, "");
  EXPECT_NE(steps.find("bw config server"), std::string::npos);
  EXPECT_NE(steps.find("bw login"), std::string::npos);

  status.server_url = "https://vault.example.com";
  steps = BitwardenSetupSteps(status, "/data/certificates.pem");
  EXPECT_NE(steps.find("https://vault.example.com"), std::string::npos);
  EXPECT_NE(
      steps.find("NODE_EXTRA_CA_CERTS=\"/data/certificates.pem\" bw login"),
      std::string::npos);
  EXPECT_EQ(steps.find("bw config server"), std::string::npos);
}

}  // namespace
}  // namespace brouk
