#include "core/bitwarden_cli.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "core/crypto.h"

namespace brouk {
namespace {

// Records what `bw` would be run with and answers with canned output.
struct FakeBw {
  std::vector<ProcessRequest> requests;
  ProcessOutput answer;

  BitwardenCli Cli() {
    return BitwardenCli("/opt/homebrew/bin/bw",
                        [this](const ProcessRequest& request) {
                          requests.push_back(request);
                          return Result<ProcessOutput>(answer);
                        });
  }
};

std::string Env(const ProcessRequest& request, std::string_view name) {
  for (const auto& [key, value] : request.environment) {
    if (key == name) {
      return value;
    }
  }
  return "(unset)";
}

bool ArgumentsContain(const ProcessRequest& request, std::string_view text) {
  return std::ranges::any_of(request.arguments, [&](const std::string& a) {
    return a.find(text) != std::string::npos;
  });
}

TEST(BitwardenCliTest, FindsTheProgram) {
  auto only = [](std::string_view path) {
    return [path](const std::filesystem::path& p) { return p == path; };
  };
#if defined(_WIN32)
  EXPECT_EQ(FindBitwardenCli("", "C:\\Windows;relative;\\nodrive;C:\\Tools",
                             only("C:\\Tools\\bw.exe")),
            "C:\\Tools\\bw.exe");
  EXPECT_FALSE(FindBitwardenCli("", "C:\\Tools", only("C:\\Tools\\bw")));
  EXPECT_FALSE(FindBitwardenCli("", "relative", only("relative\\bw.exe")));
  EXPECT_EQ(
      FindBitwardenCli(" C:\\custom\\bw.exe ", "", only("C:\\custom\\bw.exe")),
      "C:\\custom\\bw.exe");
#else
  EXPECT_EQ(FindBitwardenCli("", "/usr/bin:/bin", only("/opt/homebrew/bin/bw")),
            "/opt/homebrew/bin/bw");
  EXPECT_EQ(FindBitwardenCli("", "/usr/bin:relative:/home/u/.npm/bin",
                             only("/home/u/.npm/bin/bw")),
            "/home/u/.npm/bin/bw");
  EXPECT_EQ(FindBitwardenCli(" /custom/bw ", "", only("/custom/bw")),
            "/custom/bw");
  EXPECT_FALSE(
      FindBitwardenCli("/custom/bw", "", only("/opt/homebrew/bin/bw")));
  EXPECT_FALSE(FindBitwardenCli("", "/usr/bin", only("/elsewhere/bw")));
#endif
}

TEST(BitwardenCliTest, UnlockPassesThePasswordInTheEnvironment) {
  FakeBw bw;
  bw.answer.exit_code = 0;
  bw.answer.out = "c2Vzc2lvbktleQ==\n";
  BitwardenCli cli = bw.Cli();
  cli.SetCertificates("/data/bitwarden-certificates.pem");
  Result<std::string> session = cli.Unlock("master pw");
  ASSERT_TRUE(session);
  EXPECT_EQ(session.value(), "c2Vzc2lvbktleQ==");
  ASSERT_EQ(bw.requests.size(), 1U);
  const ProcessRequest& request = bw.requests[0];
  EXPECT_EQ(request.program, "/opt/homebrew/bin/bw");
  EXPECT_EQ(request.arguments,
            (std::vector<std::string>{"unlock", "--passwordenv", "BW_PASSWORD",
                                      "--raw", "--nointeraction"}));
  EXPECT_FALSE(ArgumentsContain(request, "master pw"));
  // Wiped after the run (the fake keeps a copy made before).
  EXPECT_EQ(Env(request, "BW_PASSWORD"), "master pw");
  EXPECT_EQ(Env(request, "NODE_EXTRA_CA_CERTS"),
            "/data/bitwarden-certificates.pem");
  EXPECT_EQ(Env(request, "BW_SESSION"), "(unset)");
  EXPECT_NE(std::ranges::find(request.remove_environment, "BW_SESSION"),
            request.remove_environment.end());
  EXPECT_NE(std::ranges::find(request.remove_environment, "NODE_OPTIONS"),
            request.remove_environment.end());
}

TEST(BitwardenCliTest, SessionGoesInTheEnvironment) {
  FakeBw bw;
  bw.answer.exit_code = 0;
  bw.answer.out = "[]";
  BitwardenCli cli = bw.Cli();
  ASSERT_TRUE(cli.ListLogins("the-session"));
  const ProcessRequest& request = bw.requests[0];
  EXPECT_EQ(request.arguments,
            (std::vector<std::string>{"list", "items", "--nointeraction"}));
  EXPECT_EQ(Env(request, "BW_SESSION"), "the-session");
  EXPECT_FALSE(ArgumentsContain(request, "the-session"));
  EXPECT_EQ(Env(request, "NODE_EXTRA_CA_CERTS"), "(unset)");
  EXPECT_TRUE(Env(request, "PATH").starts_with("/opt/homebrew/bin:"));
}

TEST(BitwardenCliTest, ItemsGoThroughStandardInput) {
  FakeBw bw;
  bw.answer.exit_code = 0;
  bw.answer.out = R"({"id":"new-id","login":{"password":"pw"}})";
  BitwardenCli cli = bw.Cli();
  Result<std::string> id =
      cli.CreateItem("s", R"({"login":{"password":"pw"}})");
  ASSERT_TRUE(id);
  EXPECT_EQ(id.value(), "new-id");
  const ProcessRequest& create = bw.requests[0];
  EXPECT_EQ(create.arguments,
            (std::vector<std::string>{"create", "item", "--nointeraction"}));
  EXPECT_EQ(create.input, Base64Encode(R"({"login":{"password":"pw"}})"));

  ASSERT_FALSE(cli.EditItem("s", "item-id", "{}"));
  EXPECT_EQ(
      bw.requests[1].arguments,
      (std::vector<std::string>{"edit", "item", "item-id", "--nointeraction"}));
  EXPECT_EQ(bw.requests[1].input, Base64Encode("{}"));
}

TEST(BitwardenCliTest, ErrorsInPlainWords) {
  FakeBw bw;
  BitwardenCli cli = bw.Cli();
  bw.answer.exit_code = 1;
  bw.answer.err = "Vault is locked.";
  EXPECT_EQ(cli.ListLogins("old").error().message, kBitwardenLockedError);
  bw.answer.err = "Invalid master password.";
  EXPECT_EQ(cli.Unlock("wrong").error().message,
            "The master password is not correct.");
  bw.answer.err = "You are not logged in.";
  EXPECT_EQ(cli.Sync("s")->message, "Bitwarden is not signed in.");
  bw.answer.err =
      "Error [X]: something\n    at frame (bw.js:1:2)\n    at other\n";
  EXPECT_EQ(cli.Sync("s")->message, "Bitwarden failed: Error [X]: something");
  bw.answer.err = "";
  bw.answer.timed_out = true;
  EXPECT_EQ(cli.Sync("s")->message, "Bitwarden did not answer in time.");
}

TEST(BitwardenCliTest, NoiseOnStandardErrorIsIgnoredWhenItWorked) {
  // Seen with a self-hosted server that lacks a newer API: bw logs an error
  // but the command works.
  FakeBw bw;
  bw.answer.exit_code = 0;
  bw.answer.err = "ERROR key_id_backfill: Failed to post the user key id: 404";
  bw.answer.out =
      R"({"serverUrl":"https://vault.example.com","status":"locked","userId":"u"})";
  BitwardenCli cli = bw.Cli();
  Result<BitwardenStatus> status = cli.Status();
  ASSERT_TRUE(status);
  EXPECT_EQ(status.value().state, BitwardenStatus::State::kLocked);
}

TEST(BitwardenCliTest, UnlockRejectsOutputThatIsNotAKey) {
  FakeBw bw;
  bw.answer.exit_code = 0;
  bw.answer.out = "Your vault is now unlocked!\n\nTo unlock...";
  BitwardenCli cli = bw.Cli();
  EXPECT_FALSE(cli.Unlock("pw"));
}

}  // namespace
}  // namespace brouk
