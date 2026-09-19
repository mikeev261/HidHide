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
        EXPECT_EQ(static_cast<DWORD>(WAIT_OBJECT_0), ::WaitForSingleObject(server.WakeHandle(), 1000));
        auto deadline = ::GetTickCount64() + 6000;
        while (client.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && ::GetTickCount64() < deadline)
        {
            server.Pump(handler); ::Sleep(5);
        }
        EXPECT_EQ(request, client.get());
        server.Pump(handler); // observe the disconnected client before reconnecting
    }
}

TEST(ConfigurationChannel, StalledClientExpiresAndNextClientCanConnect)
{
    auto name = L"\\\\.\\pipe\\HidHide.Test.Stalled." + std::to_wstring(::GetCurrentProcessId());
    Channel::Server server(name.c_str());
    auto stalled = Channel::Own(::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
    auto handler = [](auto const& bytes) { return bytes; };
    ASSERT_EQ(static_cast<DWORD>(WAIT_OBJECT_0), ::WaitForSingleObject(server.WakeHandle(), 1000));
    server.Pump(handler); // The client connects but sends no request.
    ASSERT_TRUE(server.Connected());
    ASSERT_EQ(static_cast<DWORD>(WAIT_TIMEOUT), ::WaitForSingleObject(server.WakeHandle(), server.WaitTimeoutMs()));
    server.Pump(handler);
    EXPECT_FALSE(server.Connected());
    stalled.reset();
    std::vector<std::uint8_t> request{5, 6, 7};
    auto next = std::async(std::launch::async, [&] { return Channel::Exchange(request, name.c_str()); });
    auto deadline = ::GetTickCount64() + 6000;
    while (next.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready && ::GetTickCount64() < deadline)
    {
        (void)::WaitForSingleObject(server.WakeHandle(), 100);
        server.Pump(handler);
    }
    EXPECT_EQ(request, next.get());
}

TEST(ConfigurationChannel, DestructionDrainsPendingConnectReadAndWrite)
{
    for (unsigned iteration{}; iteration < 12; ++iteration)
    {
        auto name = L"\\\\.\\pipe\\HidHide.Test.Teardown." + std::to_wstring(::GetCurrentProcessId()) + L"." + std::to_wstring(iteration);
        auto server = std::make_unique<Channel::Server>(name.c_str());
        if (iteration % 3 == 0) { server.reset(); continue; } // Pending connect.
        auto client = Channel::Own(::CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
            SECURITY_SQOS_PRESENT | SECURITY_IDENTIFICATION, nullptr));
        ASSERT_EQ(static_cast<DWORD>(WAIT_OBJECT_0), ::WaitForSingleObject(server->WakeHandle(), 1000));
        server->Pump([](auto const& bytes) { return bytes; });
        if (iteration % 3 == 1) { server.reset(); continue; } // Pending read.
        std::uint8_t byte{42}; DWORD written{};
        ASSERT_TRUE(::WriteFile(client.get(), &byte, 1, &written, nullptr));
        ASSERT_EQ(1u, written);
        ASSERT_EQ(static_cast<DWORD>(WAIT_OBJECT_0), ::WaitForSingleObject(server->WakeHandle(), 1000));
        server->Pump([](auto const&) { return std::vector<std::uint8_t>(Protocol::MaxBytes, 42); });
        server.reset(); // A full response may still be pending when the owner closes.
    }
}
