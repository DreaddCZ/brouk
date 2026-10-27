#include "core/bitwarden_vault.h"

#include <gtest/gtest.h>

#include "core/json.h"

namespace brouk {
namespace {

// The shape of `bw list items` output (bw 2026.10), with made-up data.
constexpr std::string_view kItems = R"([
  {"type":1,"name":"www.example.com","favorite":false,"reprompt":0,
   "id":"11111111-1111-1111-1111-111111111111","collectionIds":[],
   "object":"item","fields":[],
   "login":{"uris":[{"uri":"https://www.example.com/login"}],
            "fido2Credentials":[],"username":"alice ","password":"pw-a",
            "passwordRevisionDate":null},
   "passwordHistory":[],"attachments":[]},
  {"type":1,"name":"Bank","reprompt":1,"id":"22222222-2222-2222-2222-222222222222",
   "folderId":"f1","organizationId":null,
   "login":{"uris":[{"match":1,"uri":"https://bank.example.org"}],
            "username":"bob","password":"pw-b","totp":"otpauth://totp/x?secret=JBSWY3DPEHPK3PXP"}},
  {"type":2,"name":"A note","id":"33333333-3333-3333-3333-333333333333"},
  {"type":1,"name":"Deleted","id":"44444444-4444-4444-4444-444444444444",
   "deletedDate":"2026-10-01T00:00:00.000Z",
   "login":{"uris":[{"uri":"https://www.example.com"}],"username":"old","password":"x"}},
  {"type":1,"name":"Broken","id":"55555555-5555-5555-5555-555555555555","login":"nope"},
  "not an object"
])";

BitwardenLogin Login(std::vector<BitwardenUri> uris) {
  BitwardenLogin login;
  login.id = "id";
  login.uris = std::move(uris);
  return login;
}

BitwardenFit Fit(const BitwardenLogin& login, std::string_view url) {
  return MatchBitwardenLogin(login, url).fit;
}

TEST(BitwardenVaultTest, ParsesStatus) {
  std::optional<BitwardenStatus> status = ParseBitwardenStatus(
      R"({"serverUrl":"https://vault.example.com","lastSync":"2026-10-27T09:44:04.593Z",)"
      R"("userEmail":"user@example.com","userId":"u1","status":"locked"})");
  ASSERT_TRUE(status);
  EXPECT_EQ(status->state, BitwardenStatus::State::kLocked);
  EXPECT_EQ(status->server_url, "https://vault.example.com");
  EXPECT_EQ(status->user_email, "user@example.com");
  EXPECT_EQ(status->user_id, "u1");

  status = ParseBitwardenStatus(
      R"({"serverUrl":null,"lastSync":null,"status":"unauthenticated"})");
  ASSERT_TRUE(status);
  EXPECT_EQ(status->state, BitwardenStatus::State::kUnauthenticated);
  EXPECT_EQ(status->server_url, "");
  EXPECT_FALSE(ParseBitwardenStatus(R"({"status":"sleeping"})"));
  EXPECT_FALSE(ParseBitwardenStatus("You are not logged in."));
}

TEST(BitwardenVaultTest, ParsesLoginsAndSkipsTheRest) {
  Result<std::vector<BitwardenLogin>> logins = ParseBitwardenLogins(kItems);
  ASSERT_TRUE(logins);
  ASSERT_EQ(logins.value().size(), 2U);
  const BitwardenLogin& first = logins.value()[0];
  EXPECT_EQ(first.id, "11111111-1111-1111-1111-111111111111");
  EXPECT_EQ(first.username, "alice ");  // Kept as is, as Bitwarden fills it.
  EXPECT_EQ(first.password, "pw-a");
  EXPECT_EQ(first.totp, "");
  EXPECT_FALSE(first.reprompt);
  EXPECT_EQ(first.folder_id, "");
  ASSERT_EQ(first.uris.size(), 1U);
  EXPECT_EQ(first.uris[0].match, BitwardenUriMatch::kDefault);

  const BitwardenLogin& second = logins.value()[1];
  EXPECT_TRUE(second.reprompt);
  EXPECT_EQ(second.folder_id, "f1");
  EXPECT_EQ(second.organization_id, "");
  EXPECT_EQ(second.uris[0].match, BitwardenUriMatch::kHost);
  EXPECT_FALSE(second.totp.empty());

  EXPECT_FALSE(ParseBitwardenLogins(R"({"not":"a list"})"));
  EXPECT_FALSE(ParseBitwardenLogins("mac failed."));
}

TEST(BitwardenVaultTest, UnknownMatchSettingNeverMatches) {
  Result<std::vector<BitwardenLogin>> logins = ParseBitwardenLogins(
      R"([{"type":1,"id":"a","login":{"uris":[{"match":9,"uri":"example.com"}]}}])");
  ASSERT_TRUE(logins);
  EXPECT_EQ(logins.value()[0].uris[0].match, BitwardenUriMatch::kNever);
}

TEST(BitwardenVaultTest, BaseDomainFillsOnlyItsOwnHost) {
  BitwardenLogin login = Login({{"https://www.example.com/login", {}}});
  EXPECT_EQ(Fit(login, "https://www.example.com/anything?x=1"),
            BitwardenFit::kExact);
  // Other hosts of the site: offered, never filled automatically (008).
  BitwardenMatchResult other =
      MatchBitwardenLogin(login, "https://shop.example.com/");
  EXPECT_EQ(other.fit, BitwardenFit::kSameSite);
  EXPECT_EQ(other.origin, "https://www.example.com");
  EXPECT_EQ(Fit(login, "https://example.org/"), BitwardenFit::kNone);
  EXPECT_EQ(Fit(login, "https://wwwexample.com/"), BitwardenFit::kNone);
  // Never an https login on an http page.
  EXPECT_EQ(Fit(login, "http://www.example.com/"), BitwardenFit::kNone);
  // An http login is offered on the https page.
  EXPECT_EQ(
      Fit(Login({{"http://www.example.com", {}}}), "https://www.example.com/"),
      BitwardenFit::kSameSite);
  EXPECT_EQ(
      Fit(Login({{"https://www.example.com", BitwardenUriMatch::kDomain}}),
          "https://www.example.com/"),
      BitwardenFit::kExact);
}

TEST(BitwardenVaultTest, UriWithoutSchemeTakesThePagesScheme) {
  BitwardenLogin login = Login({{" www.example.com ", {}}});
  EXPECT_EQ(Fit(login, "https://www.example.com/"), BitwardenFit::kExact);
  EXPECT_EQ(Fit(login, "http://www.example.com/"), BitwardenFit::kExact);
}

TEST(BitwardenVaultTest, HostMatchNeedsTheSameHostAndPort) {
  BitwardenLogin login =
      Login({{"https://app.example.com:8443", BitwardenUriMatch::kHost}});
  EXPECT_EQ(Fit(login, "https://app.example.com:8443/x"), BitwardenFit::kExact);
  EXPECT_EQ(Fit(login, "https://app.example.com/x"), BitwardenFit::kNone);
  EXPECT_EQ(Fit(login, "https://other.example.com:8443/"), BitwardenFit::kNone);
}

TEST(BitwardenVaultTest, StartsWithExactAndNever) {
  BitwardenLogin starts =
      Login({{"https://example.com/admin/", BitwardenUriMatch::kStartsWith}});
  EXPECT_EQ(Fit(starts, "https://example.com/admin/users"),
            BitwardenFit::kExact);
  EXPECT_EQ(Fit(starts, "https://example.com/shop"), BitwardenFit::kNone);

  BitwardenLogin exact =
      Login({{"https://example.com/login", BitwardenUriMatch::kExact}});
  EXPECT_EQ(Fit(exact, "https://example.com/login"), BitwardenFit::kExact);
  EXPECT_EQ(Fit(exact, "https://example.com/login?next=/"),
            BitwardenFit::kNone);

  EXPECT_EQ(Fit(Login({{"https://example.com", BitwardenUriMatch::kNever}}),
                "https://example.com/"),
            BitwardenFit::kNone);
}

TEST(BitwardenVaultTest, RegularExpressions) {
  BitwardenLogin login = Login({{R"(^https://[a-z]+\.example\.com/app)",
                                 BitwardenUriMatch::kRegularExpression}});
  EXPECT_EQ(Fit(login, "https://eu.example.com/app/x"), BitwardenFit::kExact);
  EXPECT_EQ(Fit(login, "https://EU.example.com/app"), BitwardenFit::kExact);
  EXPECT_EQ(Fit(login, "https://eu.example.org/app"), BitwardenFit::kNone);
  // An invalid pattern matches nothing (and does not crash).
  EXPECT_EQ(Fit(Login({{"([", BitwardenUriMatch::kRegularExpression}}),
                "https://example.com/"),
            BitwardenFit::kNone);
  // A pattern that is slow in backtracking engines runs in linear time.
  EXPECT_EQ(Fit(Login({{"(a+)+$", BitwardenUriMatch::kRegularExpression}}),
                "https://example.com/" + std::string(5000, 'a') + "!"),
            BitwardenFit::kNone);
}

TEST(BitwardenVaultTest, ExactUriWinsOverOtherHosts) {
  BitwardenLogin login = Login(
      {{"https://login.example.com", {}}, {"https://www.example.com", {}}});
  EXPECT_EQ(Fit(login, "https://www.example.com/"), BitwardenFit::kExact);
  EXPECT_EQ(Fit(Login({{"", {}}, {"not a url", {}}}), "https://example.com/"),
            BitwardenFit::kNone);
  EXPECT_EQ(Fit(login, "brouk://settings"), BitwardenFit::kNone);
}

TEST(BitwardenVaultTest, VaultFindsAndKeepsNumbers) {
  BitwardenVault vault;
  vault.Replace(ParseBitwardenLogins(kItems).value());
  PasswordMatches matches = vault.Find("https://www.example.com/login");
  ASSERT_EQ(matches.exact.size(), 1U);
  EXPECT_EQ(matches.exact[0].username, "alice ");
  EXPECT_EQ(matches.exact[0].password, "pw-a");
  EXPECT_EQ(matches.exact[0].origin, "https://www.example.com");
  EXPECT_TRUE(matches.same_site.empty());
  std::int64_t number = matches.exact[0].id;
  EXPECT_EQ(vault.ItemId(number), "11111111-1111-1111-1111-111111111111");
  EXPECT_EQ(vault.EntryId("11111111-1111-1111-1111-111111111111"), number);

  matches = vault.Find("https://bank.example.org/");
  ASSERT_EQ(matches.exact.size(), 1U);
  EXPECT_TRUE(matches.exact[0].reprompt);

  // The same item keeps its number after a refresh; a new item gets a new
  // one.
  std::vector<BitwardenLogin> refreshed = ParseBitwardenLogins(kItems).value();
  refreshed.push_back(Login({{"https://www.example.com", {}}}));
  refreshed.back().id = "66666666-6666-6666-6666-666666666666";
  vault.Replace(std::move(refreshed));
  matches = vault.Find("https://www.example.com/");
  ASSERT_EQ(matches.exact.size(), 2U);
  EXPECT_EQ(vault.EntryId("11111111-1111-1111-1111-111111111111"), number);
  EXPECT_NE(vault.EntryId("66666666-6666-6666-6666-666666666666"), number);

  vault.Clear();
  EXPECT_TRUE(vault.empty());
  EXPECT_TRUE(vault.Find("https://www.example.com/").exact.empty());
  EXPECT_FALSE(vault.ItemId(-1));
}

TEST(BitwardenVaultTest, NewItemJson) {
  PasswordEntry entry;
  entry.origin = "https://www.example.com";
  entry.username = "alice";
  entry.password = "secret \"quoted\"";
  Result<JsonValue> item =
      ParseJsonDocument(NewBitwardenItemJson(entry, "f1", "", ""));
  ASSERT_TRUE(item);
  const JsonValue& value = item.value();
  EXPECT_EQ(value.IntegerOr("type", 0), 1);
  EXPECT_EQ(value.StringOr("name", ""), "www.example.com");
  EXPECT_EQ(value.StringOr("folderId", ""), "f1");
  EXPECT_EQ(value.Find("organizationId")->kind, JsonValue::Kind::kNull);
  const JsonValue* login = value.Find("login");
  ASSERT_NE(login, nullptr);
  EXPECT_EQ(login->StringOr("username", ""), "alice");
  EXPECT_EQ(login->StringOr("password", ""), "secret \"quoted\"");
  EXPECT_EQ(login->Find("totp")->kind, JsonValue::Kind::kNull);
  EXPECT_EQ(login->Find("uris")->items[0].StringOr("uri", ""),
            "https://www.example.com");

  // In an organisation's collection: no folder.
  item = ParseJsonDocument(NewBitwardenItemJson(entry, "f1", "o1", "c1"));
  ASSERT_TRUE(item);
  EXPECT_EQ(item.value().StringOr("organizationId", ""), "o1");
  EXPECT_EQ(item.value().Find("collectionIds")->items[0].string, "c1");
  EXPECT_EQ(item.value().Find("folderId")->kind, JsonValue::Kind::kNull);
}

TEST(BitwardenVaultTest, EditedItemKeepsEverythingElse) {
  PasswordEntry entry;
  entry.username = "alice";
  entry.password = "new";
  entry.totp = "otpauth://totp/x?secret=JBSWY3DPEHPK3PXP";
  Result<std::string> edited = EditedBitwardenItemJson(
      R"({"id":"a","type":1,"name":"Example","notes":"keep",)"
      R"("login":{"uris":[{"uri":"https://example.com","match":3}],)"
      R"("username":"old","password":"old","totp":null},"fields":[{"name":"x"}]})",
      entry);
  ASSERT_TRUE(edited);
  Result<JsonValue> item = ParseJsonDocument(edited.value());
  ASSERT_TRUE(item);
  EXPECT_EQ(item.value().StringOr("notes", ""), "keep");
  EXPECT_EQ(item.value().Find("fields")->items.size(), 1U);
  const JsonValue* login = item.value().Find("login");
  EXPECT_EQ(login->StringOr("username", ""), "alice");
  EXPECT_EQ(login->StringOr("password", ""), "new");
  EXPECT_EQ(login->StringOr("totp", ""), entry.totp);
  EXPECT_EQ(login->Find("uris")->items[0].IntegerOr("match", 0), 3);

  EXPECT_FALSE(EditedBitwardenItemJson(R"({"type":2,"name":"note"})", entry));
  EXPECT_FALSE(EditedBitwardenItemJson("[]", entry));
}

TEST(BitwardenVaultTest, ParsesFoldersCollectionsAndOrganizations) {
  Result<std::vector<BitwardenFolder>> folders = ParseBitwardenFolders(
      R"([{"object":"folder","id":"f1","name":"Work"},)"
      R"({"object":"folder","id":null,"name":"No Folder"}])");
  ASSERT_TRUE(folders);
  ASSERT_EQ(folders.value().size(), 2U);
  EXPECT_EQ(folders.value()[0].name, "Work");
  EXPECT_EQ(folders.value()[1].id, "");

  Result<std::vector<BitwardenCollection>> collections =
      ParseBitwardenCollections(
          R"([{"id":"c1","organizationId":"o1","name":"Shared"},)"
          R"({"id":"c2","organizationId":"o1","name":"View only","readOnly":true},)"
          R"({"id":"c3","name":"No organisation"}])");
  ASSERT_TRUE(collections);
  ASSERT_EQ(collections.value().size(), 1U);
  EXPECT_EQ(collections.value()[0].name, "Shared");

  auto organizations =
      ParseBitwardenOrganizations(R"([{"id":"o1","name":"Company"}])");
  ASSERT_TRUE(organizations);
  EXPECT_EQ(organizations.value().at("o1"), "Company");
  EXPECT_FALSE(ParseBitwardenFolders("{}"));
}

}  // namespace
}  // namespace brouk
