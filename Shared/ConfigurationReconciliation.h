// SPDX-License-Identifier: MIT
#pragma once

#include "Configuration.h"

namespace HidHide::ConfigurationReconciliation
{
    // The profile catalog is per-user source data, not a driver setting. Refresh
    // it independently so an external catalog save neither goes unseen nor
    // changes the driver state protected by the coordinator.
    inline void ApplyLiveProfileCatalog(Configuration& baseline, Configuration& expected,
        AppProfiles const& liveProfiles)
    {
        baseline.profiles = liveProfiles;
        expected.profiles = liveProfiles;
    }

    // Automatic profile activation owns driver fields only. Keeping this adapter
    // in the shared, testable layer makes it impossible for startup/scan
    // transitions to pass a profile catalog to their commit operation.
    template<class Commit>
    inline void CommitAutomaticDriverTransition(Configuration const& expected,
        Configuration const& desired, Commit&& commit)
    {
        commit(DriverState(expected), DriverState(desired));
    }

    // Registry profile persistence and driver IOCTL state are independent
    // transaction domains. A single command must not partially commit both.
    template<class CommitProfiles, class CommitDriver>
    inline void CommitExplicitMutation(Configuration const& expected, Configuration const& desired,
        CommitProfiles&& commitProfiles, CommitDriver&& commitDriver)
    {
        bool const profilesChanged = expected.profiles != desired.profiles;
        bool const driverChanged = DriverState(expected) != DriverState(desired);
        if (profilesChanged && driverChanged)
            throw std::invalid_argument("Profile and driver settings must be changed in separate commands");
        if (profilesChanged) commitProfiles(expected.profiles, desired.profiles);
        else if (driverChanged) commitDriver(DriverState(expected), DriverState(desired));
    }
}
