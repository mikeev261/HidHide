// SPDX-License-Identifier: MIT
#pragma once

#include "FilterDriverProxy.h"

class CProfileManager
{
public:
    explicit CProfileManager(_In_ HidHide::FilterDriverProxy& filterDriverProxy) noexcept;
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
    bool OverrideActive() const noexcept { return m_OverrideActive; }

private:
    HidHide::DeviceInstancePaths ActiveProfileDevices(_Out_ size_t& activeProfileCount) const;
    void SaveRecoveryState() const;
    void ClearRecoveryState() const noexcept;
    void RestoreBaseline();
    void ConfigureAutoStart(_In_ bool enabled) const;

    HidHide::FilterDriverProxy& m_FilterDriverProxy;
    HidHide::DeviceInstancePaths m_BaselineBlacklist;
    HidHide::DeviceInstancePaths m_LastProfileDevices;
    HidHide::DeviceInstancePaths m_LastAppliedBlacklist;
    bool m_BaselineActive{ false };
    bool m_OverrideActive{ false };
    size_t m_ActiveProfileCount{};
};
