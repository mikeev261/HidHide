// SPDX-License-Identifier: MIT
#pragma once

#include "FilterDriverProxy.h"

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

    size_t ActiveProfileCount() const noexcept { return m_ActiveProfileCount; }
    bool ProfileIsActive(_In_ HidHide::FullImageName const& profile) const noexcept;
    bool ProfileIsUnresolved(HidHide::FullImageName const& profile) const noexcept;
    void SetEnabled(bool displayed, bool requested);
    bool OverrideActive() const noexcept { return m_OverrideActive; }
    std::wstring Status() const { return m_Status; }
    void AdoptExternalState();
    void Resume();
    void Pause();
    bool HasConflict() const { return m_Conflict; }
    bool EffectiveActive() const { return m_Expected.active; }
    void ExitSafely();
    void ReportFailure(std::string const& message) { m_Status = std::wstring(message.begin(), message.end()); }

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
        std::uint64_t revision{};
        bool complete{ true };
        HidHide::FullImageNames activeProfiles;
        HidHide::FullImageNames unresolvedProfiles;
        HidHide::DeviceInstancePaths activeDevices;
    };

    static std::vector<PreparedProfile> PrepareProfiles(_In_ HidHide::AppProfiles const& profiles);
    static ScanResult ScanProfiles(_In_ std::vector<PreparedProfile> const& profiles);
    void WorkerMain() noexcept;
    void StopWorker() noexcept;
    void ApplyScanResult(_In_ ScanResult const& result);
    void SaveRecoveryState(HidHide::Configuration const& baseline, HidHide::Configuration const& before, HidHide::Configuration const& after);
    void ClearRecoveryState();
    void RestoreBaseline();
    void ConfigureAutoStart(_In_ bool enabled) const;
    HidHide::Configuration ReadUserConfiguration();
    void CommitUserConfiguration(HidHide::Configuration const& expected, HidHide::Configuration const& desired, bool disable);
    void Observe();
    void Transition(HidHide::Configuration const& baseline, HidHide::DeviceInstancePaths const& devices, bool suspended);

    HidHide::FilterDriverProxy& m_FilterDriverProxy;
    HidHide::Configuration m_Baseline;
    HidHide::Configuration m_Expected;
    HidHide::DeviceInstancePaths m_LastProfileDevices;
    bool m_Suspended{};
    bool m_Conflict{};
    bool m_JournalPending{};
    std::wstring m_Status{ L"Starting" };
    bool m_OverrideActive{ false };
    size_t m_ActiveProfileCount{};
    HidHide::FullImageNames m_ActiveProfiles;
    HidHide::FullImageNames m_UnresolvedProfiles;

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
    bool m_WorkerFailed{};
    std::thread m_Worker;
};
