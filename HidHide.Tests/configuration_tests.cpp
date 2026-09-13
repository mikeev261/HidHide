// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include "ConfigurationChannel.h"
#include <future>
#include <thread>
#pragma comment(lib, "advapi32.lib")

using namespace HidHide;

TEST(ConfigurationPolicy, OverlappingBaselineEditSurvivesProfileExit)
{
    Configuration baseline; baseline.blacklist = {L"A"};
    EXPECT_EQ((DeviceInstancePaths{L"A", L"B"}), EffectiveConfiguration(baseline, {L"A", L"B"}, false).blacklist);
    baseline.blacklist.insert(L"C");
    EXPECT_EQ((DeviceInstancePaths{L"A", L"B", L"C"}), EffectiveConfiguration(baseline, {L"A", L"B"}, false).blacklist);
    auto restored = EffectiveConfiguration(baseline, {}, false);
    EXPECT_EQ((DeviceInstancePaths{L"A", L"C"}), restored.blacklist);
    EXPECT_FALSE(restored.active);
}

TEST(ConfigurationPolicy, PauseRestoresDisabledBaselineWithoutChangingWhitelist)
{
    Configuration baseline; baseline.blacklist = {L"A"}; baseline.inverse = true;
    baseline.whitelist = {L"C:\\test.exe"};
    auto applied = EffectiveConfiguration(baseline, {L"B"}, false);
    EXPECT_TRUE(applied.active); EXPECT_TRUE(applied.inverse);
    EXPECT_EQ(baseline.whitelist, applied.whitelist);
    EXPECT_TRUE(baseline == EffectiveConfiguration(baseline, {L"B"}, true));
    EXPECT_TRUE(applied == EffectiveConfiguration(baseline, {L"B"}, false));
}

TEST(ConfigurationProtocol, RoundTripsProfilesAndDisconnectedSelections)
{
    Configuration input; input.active = true; input.inverse = true;
    input.blacklist = {L"HID\\ABSENT", L"USB\\COMPOSITE"};
    input.whitelist = {L"\\Device\\HarddiskVolume3\\feeder.exe"};
    input.profiles[L"C:\\empty.exe"] = {};
    input.profiles[L"C:\\game.exe"] = input.blacklist;
    Protocol::Writer writer; writer.State(input);
    Protocol::Reader reader(writer.data);
    EXPECT_TRUE(input == reader.State()); EXPECT_NO_THROW(reader.End());
}

TEST(ConfigurationProtocol, RejectsEveryTruncation)
{
    Configuration input; input.profiles[L"C:\\game.exe"] = {L"A", L"B"};
    Protocol::Writer writer; writer.State(input);
    for (size_t length = 0; length < writer.data.size(); ++length)
    {
        std::vector<std::uint8_t> bytes(writer.data.begin(), writer.data.begin() + length);
        Protocol::Reader reader(bytes);
        EXPECT_THROW(reader.State(), std::runtime_error) << length;
    }
}

TEST(ConfigurationProtocol, RejectsInvalidFlagsCountsAndTrailingData)
{
    Protocol::Writer writer; writer.Number(2);
    Protocol::Reader flag(writer.data); EXPECT_THROW(flag.Boolean(), std::runtime_error);
    writer = {}; writer.Number(Protocol::MaxEntries + 1);
    Protocol::Reader count(writer.data); EXPECT_THROW(count.Count(), std::runtime_error);
    writer = {}; writer.State({}); writer.Number(0);
    Protocol::Reader trailing(writer.data); trailing.State(); EXPECT_THROW(trailing.End(), std::runtime_error);
    std::vector<std::uint8_t> oversized(Protocol::MaxBytes + 1);
    EXPECT_THROW({ Protocol::Reader reader(oversized); }, std::runtime_error);
}

TEST(ConfigurationProtocol, RejectsDuplicateEntriesAndEmbeddedNulls)
{
    Protocol::Writer writer; writer.Number(2); writer.String(L"A"); writer.String(L"A");
    Protocol::Reader duplicate(writer.data);
    EXPECT_THROW(duplicate.Strings<DeviceInstancePaths>(), std::runtime_error);
    EXPECT_THROW(writer.String(std::wstring(L"A\0B", 3)), std::runtime_error);
}

TEST(ConfigurationOwnership, OnlyOneThreadOwnsLeaseAndReleaseAllowsHandoff)
{
    auto name = L"Local\\HidHide.Test.Lease." + std::to_wstring(::GetCurrentProcessId());
    {
        Channel::Lease owner(name.c_str()); ASSERT_TRUE(owner.Acquired());
        auto contender = std::async(std::launch::async, [&] { Channel::Lease lease(name.c_str()); return lease.Acquired(); });
        EXPECT_FALSE(contender.get());
    }
    auto successor = std::async(std::launch::async, [&] { Channel::Lease lease(name.c_str()); return lease.Acquired(); });
    EXPECT_TRUE(successor.get());
}

TEST(ConfigurationChannel, AuthenticatedLocalRoundTripAndReconnect)
{
    auto name = L"\\\\.\\pipe\\HidHide.Test." + std::to_wstring(::GetCurrentProcessId());
    Channel::Server server(name.c_str());
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        auto handler = [](auto const& bytes) { return bytes; };
        server.Pump(handler);
        std::vector<std::uint8_t> request{1, 2, 3, 4};
        auto client = std::async(std::launch::async, [&] { return Channel::Exchange(request, name.c_str()); });
        auto deadline = ::GetTickCount64() + 6000;
        while (client.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && ::GetTickCount64() < deadline)
        {
            server.Pump(handler); ::Sleep(5);
        }
        EXPECT_EQ(request, client.get());
        server.Pump(handler); // observe the disconnected client before reconnecting
    }
}
