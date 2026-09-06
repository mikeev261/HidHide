// SPDX-License-Identifier: MIT
#pragma once

#include "FilterDriverProxy.h"
#include "ConfigurationOwner.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

class CProfileManager
{
public:
    explicit CProfileManager(_In_ HidHide::FilterDriverProxy& filterDriverProxy);
    ~CProfileManager();

    CProfileManager(_In_ CProfileManager const&) = delete;
    CProfileManager& operator=(_In_ CProfileManager const&) = delete;

    // Restore a baseline left behind if a previous manager terminated unexpectedly.
    void Recover();

    // Reconcile the global signed-driver configuration with all running profiles.
    void Tick();

    // Restore the ordinary Devices-tab configuration before the manager exits.
    void Stop() noexcept;

    bool Conflict() const noexcept { return m_Conflict; }
    size_t ActiveProfileCount() const noexcept { return m_ActiveProfileCount; }
    bool ProfileIsActive(_In_ HidHide::FullImageName const& profile) const noexcept;
    bool OverrideActive() const noexcept { return m_OverrideActive; }

private:
    struct PreparedProfile
    {
        HidHide::FullImageName profile;
        std::filesystem::path displayPath;
        std::wstring normalizedFileName;
        HidHide::DeviceInstancePaths devices;
    };

    struct ScanResult
    {
        HidHide::FullImageNames activeProfiles;
        HidHide::DeviceInstancePaths activeDevices;
    };

    static std::vector<PreparedProfile> PrepareProfiles(_In_ HidHide::AppProfiles const& profiles);
    static ScanResult ScanProfiles(_In_ std::vector<PreparedProfile> const& profiles);
    void WorkerMain() noexcept;
    void StopWorker() noexcept;
    void ApplyScanResult(_In_ ScanResult const& result);
    void SaveRecoveryState();
    void ClearRecoveryState() const noexcept;
    void RestoreBaseline();
    void CheckOwnership();
    void Relinquish() noexcept;
    void ConfigureAutoStart(_In_ bool enabled) const;

    HidHide::ConfigurationOwner m_OwnerLease{ L"Global\\HidHide.ProfileManager" };
    HidHide::FilterDriverProxy& m_FilterDriverProxy;
    bool m_Conflict{};
    bool m_RecoveryDirty{};
    bool m_LastAppliedActive{};
    HidHide::DeviceInstancePaths m_BaselineBlacklist;
    HidHide::DeviceInstancePaths m_LastAppliedBlacklist;
    bool m_BaselineActive{ false };
    bool m_OverrideActive{ false };
    size_t m_ActiveProfileCount{};
    HidHide::FullImageNames m_ActiveProfiles;

    HidHide::AppProfiles m_SubmittedProfiles;
    bool m_HasSubmittedProfiles{};
    std::mutex m_WorkerMutex;
    std::condition_variable m_WorkerWake;
    HidHide::AppProfiles m_PendingProfiles;
    std::uint64_t m_ProfileRevision{};
    std::uint64_t m_CompletedSequence{};
    std::uint64_t m_AppliedSequence{};
    ScanResult m_CompletedResult;
    bool m_StopRequested{};
    std::thread m_Worker;
};
