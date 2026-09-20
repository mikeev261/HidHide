// SPDX-License-Identifier: MIT
#include "stdafx.h"
#include "ProfilesDeviceSource.h"
#include "HID.h"

std::vector<ProfilesDeviceItem> CProductionProfilesDeviceSource::Enumerate()
{
    std::lock_guard<std::mutex> lock(m_Mutex); if (!m_Known) throw std::runtime_error("Device enumeration is unavailable"); return m_Cached;
}

void CProductionProfilesDeviceSource::Refresh()
{
    try
    {
        auto result = HidHide::Profiles::ProjectProfileDeviceGroups(HidHide::HidDevices(false));
        std::lock_guard<std::mutex> lock(m_Mutex); m_Cached = std::move(result); m_Known = true;
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(m_Mutex); m_Cached.clear(); m_Known = false; throw;
    }
}

