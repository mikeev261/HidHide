// SPDX-License-Identifier: MIT
#pragma once
#include "ProfileDeviceProjection.h"
#include <mutex>

using ProfilesDeviceItem = HidHide::Profiles::ProfileDeviceGroup;
class IProfilesDeviceSource { public: virtual ~IProfilesDeviceSource() = default; virtual std::vector<ProfilesDeviceItem> Enumerate() = 0; virtual void Refresh() {} virtual std::uint64_t RefreshCount() const { return 0; } };
class CProductionProfilesDeviceSource final : public IProfilesDeviceSource
{
public:
    std::vector<ProfilesDeviceItem> Enumerate() override;
    void Refresh() override;
private:
    std::mutex m_Mutex;
    std::vector<ProfilesDeviceItem> m_Cached;
    bool m_Known{};
};

