// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include "ConfigurationSession.h"
#include "ConfigurationOwner.h"
#include <thread>
#include <functional>
#include <vector>
using namespace HidHide;
namespace
{
    struct MemoryBackend : ConfigurationBackend
    {
        bool active{}, inverse{}, leased{};
        DeviceInstancePaths blacklist;
        FullImageNames whitelist;
        AppProfiles profiles;
        std::string fail;
        std::vector<std::string> writes;
        std::function<void()> beforeAcquire;
        struct Lock : Lease
        {
            MemoryBackend& backend;
            explicit Lock(MemoryBackend& b) : backend(b)
            { if (b.leased) throw std::runtime_error("exclusive driver busy"); b.leased = true; }
            ~Lock() override { backend.leased = false; }
        };
        std::unique_ptr<Lease> Acquire() override
        {
            if (beforeAcquire) { auto action = std::move(beforeAcquire); beforeAcquire = {}; action(); }
            return std::make_unique<Lock>(*this);
        }
        void Writing(std::string const& field)
        {
            EXPECT_TRUE(leased);
            writes.push_back(field);
            if (fail == field) { fail.clear(); throw std::runtime_error("injected failure"); }
        }
        bool ReadActive() override { EXPECT_TRUE(leased); return active; }
        bool ReadInverse() override { EXPECT_TRUE(leased); return inverse; }
        DeviceInstancePaths ReadBlacklist() override { EXPECT_TRUE(leased); return blacklist; }
        FullImageNames ReadWhitelist() override { EXPECT_TRUE(leased); return whitelist; }
        AppProfiles ReadAppProfiles() override { EXPECT_TRUE(leased); return profiles; }
        void WriteActive(bool const& v) override { Writing("Active"); active = v; }
        void WriteInverse(bool const& v) override { Writing("Inverse"); inverse = v; }
        void WriteBlacklist(DeviceInstancePaths const& v) override { Writing("Blacklist"); blacklist = v; }
        void WriteWhitelist(FullImageNames const& v) override { Writing("Whitelist"); whitelist = v; }
        void WriteAppProfiles(AppProfiles const& v) override { Writing("Profiles"); profiles = v; }
    };
}
TEST(ConfigurationSession, LiveManagerSeesCliProfilesAfterCommit)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession manager(b, ConfigurationMode::Live);
    ConfigurationSession cli(b, ConfigurationMode::Snapshot);
    cli.AppProfileAddEntry(L"game.exe", L"device");
    EXPECT_TRUE(manager.GetAppProfiles().empty());
    cli.ApplyConfigurationChanges();
    EXPECT_EQ(cli.GetAppProfiles(), manager.GetAppProfiles());
    EXPECT_FALSE(b->leased); // Long-lived sessions do not monopolize exclusive device.
}
TEST(ConfigurationSession, LiveReadsObserveExternalDriverState)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession session(b, ConfigurationMode::Live);
    b->blacklist = {L"external"}; b->active = true; b->inverse = true;
    EXPECT_EQ(b->blacklist, session.GetBlacklist());
    EXPECT_TRUE(session.GetActive()); EXPECT_TRUE(session.GetInverse());
}
TEST(ConfigurationSession, FailedLiveDriverWritesCanRetryIdenticalRequest)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession session(b, ConfigurationMode::Live);
    b->fail = "Blacklist";
    EXPECT_THROW(session.BlacklistAddEntry(L"device"), std::runtime_error);
    EXPECT_TRUE(b->blacklist.empty());
    EXPECT_NO_THROW(session.BlacklistAddEntry(L"device"));
    EXPECT_EQ(DeviceInstancePaths({L"device"}), b->blacklist);
    b->fail = "Active";
    EXPECT_THROW(session.SetActive(true), std::runtime_error);
    EXPECT_NO_THROW(session.SetActive(true)); EXPECT_TRUE(b->active);
    b->fail = "Inverse";
    EXPECT_THROW(session.SetInverse(true), std::runtime_error);
    EXPECT_NO_THROW(session.SetInverse(true)); EXPECT_TRUE(b->inverse);
}
TEST(ConfigurationSession, FailedProfileReplacementAndDeleteRemainRetryable)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession session(b, ConfigurationMode::Live);
    b->fail = "Profiles";
    EXPECT_THROW(session.AppProfileAddEntry(L"game", L"device"), std::runtime_error);
    EXPECT_TRUE(b->profiles.empty());
    session.AppProfileAddEntry(L"game", L"device");
    b->fail = "Profiles";
    EXPECT_THROW(session.AppProfileDelete(L"game"), std::runtime_error);
    EXPECT_EQ(1u, b->profiles.size());
    session.AppProfileDelete(L"game"); EXPECT_TRUE(b->profiles.empty());
}
TEST(ConfigurationSession, SnapshotStagesAndWritesOnlyDirtyFields)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession cli(b, ConfigurationMode::Snapshot);
    cli.BlacklistAddEntry(L"a"); cli.BlacklistAddEntry(L"b");
    EXPECT_TRUE(b->blacklist.empty());
    b->active = true; b->profiles[L"external"] = {L"x"};
    EXPECT_FALSE(cli.GetActive()); // Explicit construction snapshot for CLI batching.
    cli.ApplyConfigurationChanges();
    EXPECT_TRUE(b->active); EXPECT_EQ(1u, b->profiles.size());
    EXPECT_EQ(std::vector<std::string>({"Blacklist"}), b->writes);
}
TEST(ConfigurationSession, BatchConflictPreflightsBeforeAnyWrites)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession cli(b, ConfigurationMode::Snapshot);
    cli.WhitelistAddEntry(L"new"); cli.BlacklistAddEntry(L"mine");
    b->blacklist = {L"external"};
    EXPECT_THROW(cli.ApplyConfigurationChanges(), ConfigurationConflict);
    EXPECT_TRUE(b->writes.empty()); EXPECT_EQ(DeviceInstancePaths({L"external"}), b->blacklist);
}
TEST(ConfigurationSession, BatchPartialFailureRetriesOnlyUnacknowledgedFields)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession cli(b, ConfigurationMode::Snapshot);
    cli.WhitelistAddEntry(L"app"); cli.BlacklistAddEntry(L"device"); cli.SetActive(true);
    b->fail = "Blacklist";
    EXPECT_THROW(cli.ApplyConfigurationChanges(), std::runtime_error);
    EXPECT_FALSE(b->active); EXPECT_EQ(1u, b->whitelist.size());
    b->whitelist.insert(L"external");
    EXPECT_NO_THROW(cli.ApplyConfigurationChanges());
    EXPECT_EQ(2u, b->whitelist.size()); EXPECT_TRUE(b->active);
    EXPECT_EQ(std::vector<std::string>({"Whitelist", "Blacklist", "Blacklist", "Active"}), b->writes);
}
TEST(ConfigurationSession, LiveStaleWholeMapReplacementConflicts)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession first(b, ConfigurationMode::Live), second(b, ConfigurationMode::Live);
    auto stale = first.GetAppProfiles(); stale[L"first"] = {};
    second.AppProfileAdd(L"second");
    EXPECT_THROW(first.SetAppProfiles(stale), ConfigurationConflict);
    EXPECT_EQ(1u, b->profiles.size()); EXPECT_TRUE(b->profiles.count(L"second"));
    first.AppProfileAdd(L"first"); EXPECT_EQ(2u, b->profiles.size());
}
TEST(ConfigurationSession, InterleavedMutationBetweenReadAndWriteConflicts)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession live(b, ConfigurationMode::Live);
    auto next = live.GetBlacklist(); next.insert(L"mine");
    b->beforeAcquire = [&] { b->blacklist.insert(L"external"); };
    EXPECT_THROW(live.SetBlacklist(next), ConfigurationConflict);
    EXPECT_EQ(DeviceInstancePaths({L"external"}), b->blacklist);
}
TEST(ConfigurationSession, OverrideExpectedPairSurvivesUnrelatedLiveRefresh)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession manager(b, ConfigurationMode::Live);
    DeviceInstancePaths expected;
    b->blacklist = {L"external"};
    manager.GetBlacklist(); // Must not reset explicit manager ownership evidence.
    EXPECT_THROW(manager.SetDriverState(expected, false, {L"profile"}, true, [] {}, [] {}), ConfigurationConflict);
    EXPECT_TRUE(b->writes.empty());
}
TEST(ConfigurationSession, OverrideInterleavingCannotOverwriteExternalState)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession manager(b, ConfigurationMode::Live);
    auto expected = manager.GetBlacklist(); auto active = manager.GetActive();
    b->beforeAcquire = [&] { b->blacklist = {L"external"}; };
    EXPECT_THROW(manager.SetDriverState(expected, active, {L"profile"}, true, [] {}, [] {}), ConfigurationConflict);
    EXPECT_TRUE(b->writes.empty());
}
TEST(ConfigurationSession, OverrideRetainsExclusiveLeaseAndAcknowledgesPartialSuccess)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession manager(b, ConfigurationMode::Live);
    DeviceInstancePaths expected; bool active{};
    auto savedBlacklist = [&] { expected = {L"profile"}; EXPECT_THROW(b->Acquire(), std::runtime_error); };
    auto savedActive = [&] { active = true; };
    b->fail = "Active";
    EXPECT_THROW(manager.SetDriverState(expected, active, {L"profile"}, true, savedBlacklist, savedActive), std::runtime_error);
    EXPECT_EQ(b->blacklist, expected); EXPECT_FALSE(active); EXPECT_FALSE(b->leased);
    EXPECT_NO_THROW(manager.SetDriverState(expected, active, {L"profile"}, true, savedBlacklist, savedActive));
    EXPECT_TRUE(active);
}
TEST(ConfigurationSession, RecoveryWriteFailureKeepsSuccessfulDriverWriteRetryable)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession manager(b, ConfigurationMode::Live);
    DeviceInstancePaths expected; bool active{}; bool fail = true;
    auto saved = [&] { expected = {L"profile"}; if (fail) { fail = false; throw std::runtime_error("recovery store failed"); } };
    EXPECT_THROW(manager.SetDriverState(expected, active, {L"profile"}, true, saved, [&] { active = true; }), std::runtime_error);
    EXPECT_FALSE(b->active);
    EXPECT_NO_THROW(manager.SetDriverState(expected, active, {L"profile"}, true, saved, [&] { active = true; }));
    EXPECT_TRUE(b->active);
}
TEST(ConfigurationSession, CompetingSnapshotSessionsCannotLoseProfileEdits)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession one(b, ConfigurationMode::Snapshot), two(b, ConfigurationMode::Snapshot);
    one.AppProfileAdd(L"one"); two.AppProfileAdd(L"two");
    one.ApplyConfigurationChanges();
    EXPECT_THROW(two.ApplyConfigurationChanges(), ConfigurationConflict);
    EXPECT_TRUE(b->profiles.count(L"one")); EXPECT_FALSE(b->profiles.count(L"two"));
}

TEST(ConfigurationOwner, AnotherThreadCannotOwnUntilFirstOwnerExits)
{
    auto const name = L"Local\\HidHide.Tests.Owner." + std::to_wstring(::GetCurrentProcessId());
    bool rejected{};
    {
        ConfigurationOwner owner(name.c_str());
        std::thread competitor([&] { try { ConfigurationOwner other(name.c_str()); } catch (std::system_error const&) { rejected = true; } });
        competitor.join();
        EXPECT_TRUE(rejected);
    }
    std::thread successor([&] { EXPECT_NO_THROW(ConfigurationOwner owner(name.c_str())); });
    successor.join();
}
TEST(ConfigurationOwner, AbandonedLeaseCanBeAcquired)
{
    auto const name = L"Local\\HidHide.Tests.Abandoned." + std::to_wstring(::GetCurrentProcessId());
    HANDLE abandoned{};
    std::thread previous([&] { abandoned = ::CreateMutexW(nullptr, TRUE, name.c_str()); });
    previous.join();
    ASSERT_NE(nullptr, abandoned);
    EXPECT_NO_THROW(ConfigurationOwner successor(name.c_str()));
    ::CloseHandle(abandoned);
}

TEST(ConfigurationSession, DisplayedProfileCannotLoseCliAdditionAfterStatusPoll)
{
    auto b = std::make_shared<MemoryBackend>();
    b->profiles[L"game"] = {L"A"};
    ConfigurationSession gui(b, ConfigurationMode::Live), cli(b, ConfigurationMode::Snapshot);
    auto const displayed = gui.GetAppProfiles();
    cli.AppProfileAddEntry(L"game", L"B");
    cli.ApplyConfigurationChanges();
    gui.GetAppProfiles(); // Status timer refresh must not acknowledge the stale tree.
    auto edited = displayed;
    edited.at(L"game") = ReplaceDisplayedDevicePaths(edited.at(L"game"), {L"A", L"B", L"C"}, {L"A", L"C"});
    EXPECT_THROW(gui.SetAppProfiles(displayed, edited), ConfigurationConflict);
    EXPECT_EQ(DeviceInstancePaths({L"A", L"B"}), b->profiles.at(L"game"));
}
TEST(ConfigurationSession, DisplayedProfileCannotResurrectDeletedApplication)
{
    auto b = std::make_shared<MemoryBackend>();
    b->profiles[L"game"] = {L"A"};
    ConfigurationSession gui(b, ConfigurationMode::Live);
    auto const displayed = gui.GetAppProfiles();
    b->profiles.clear(); gui.GetAppProfiles();
    auto edited = displayed; edited.at(L"game").insert(L"C");
    EXPECT_THROW(gui.SetAppProfiles(displayed, edited), ConfigurationConflict);
    EXPECT_TRUE(b->profiles.empty()); EXPECT_TRUE(b->writes.empty());
}
TEST(ConfigurationSession, FilteredProfileSavePreservesUndisplayedSelectionsAndCanRetry)
{
    auto b = std::make_shared<MemoryBackend>();
    b->profiles[L"game"] = {L"A", L"hidden"};
    ConfigurationSession gui(b, ConfigurationMode::Live);
    auto const displayed = gui.GetAppProfiles();
    auto edited = displayed;
    edited.at(L"game") = ReplaceDisplayedDevicePaths(edited.at(L"game"), {L"A", L"C"}, {L"C"});
    b->fail = "Profiles";
    EXPECT_THROW(gui.SetAppProfiles(displayed, edited), std::runtime_error);
    EXPECT_EQ(displayed, b->profiles);
    EXPECT_NO_THROW(gui.SetAppProfiles(displayed, edited));
    EXPECT_EQ(DeviceInstancePaths({L"C", L"hidden"}), b->profiles.at(L"game"));
}
TEST(ConfigurationSession, DisplayedListsSurviveManagerAndStatusGetterInterleaving)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession gui(b, ConfigurationMode::Live);
    auto const devices = gui.GetBlacklist(); auto const apps = gui.GetWhitelist();
    b->blacklist = {L"external"}; b->whitelist = {L"other"};
    gui.GetBlacklist(); gui.GetWhitelist();
    EXPECT_THROW(gui.SetBlacklist(devices, {L"mine"}), ConfigurationConflict);
    EXPECT_THROW(gui.SetWhitelist(apps, {L"mine"}), ConfigurationConflict);
    EXPECT_TRUE(b->writes.empty());
}
TEST(ConfigurationSession, ExclusiveContentionRetainsDisplayedExpectationForRetry)
{
    auto b = std::make_shared<MemoryBackend>();
    ConfigurationSession gui(b, ConfigurationMode::Live);
    auto const displayed = gui.GetAppProfiles();
    AppProfiles edited{{L"game", {L"A"}}};
    {
        auto owner = b->Acquire();
        EXPECT_THROW(gui.GetAppProfiles(), std::runtime_error);
        EXPECT_THROW(gui.SetAppProfiles(displayed, edited), std::runtime_error);
    }
    EXPECT_NO_THROW(gui.SetAppProfiles(displayed, edited));
    EXPECT_EQ(edited, gui.GetAppProfiles());
}
