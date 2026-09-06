// SPDX-License-Identifier: MIT
#pragma once
#include "ConfigurationSession.h"

namespace HidHide
{
    // Permanent user intent is never inferred by subtracting a profile union.
    // Save persists baseline + expected state together, before applying intent.
    struct ProfilePolicy
    {
        DeviceInstancePaths baseline, expected, profileDevices;
        bool enabled{}, expectedEnabled{}, overriding{}, dirty{};

        DeviceInstancePaths Effective() const
        {
            auto result = baseline;
            result.insert(profileDevices.begin(), profileDevices.end());
            return result;
        }
        template<class Driver, class Save>
        void Apply(Driver& driver, Save save)
        {
            if (dirty) save();
            auto const effective = Effective();
            driver.SetDriverState(expected, expectedEnabled, effective, enabled,
                [&] { if (expected != effective || dirty) { expected = effective; save(); } },
                [&] { if (expectedEnabled != enabled || dirty) { expectedEnabled = enabled; save(); } });
        }
        // A failed initial save rolls intent back. Once saved, intent remains
        // accepted even if Apply fails; the next reconciliation retries it.
        template<class Save>
        void Edit(DeviceInstancePaths const& displayed, DeviceInstancePaths const& requested, Save save)
        {
            if (displayed != baseline) throw ConfigurationConflict("Permanent device selection changed");
            auto previous = baseline;
            baseline = requested;
            try { save(); }
            catch (...) { baseline = std::move(previous); throw; }
        }
        template<class Save>
        void Enable(bool displayed, bool requested, Save save)
        {
            // The switch displays effective state, including a failed pending write.
            if (displayed != expectedEnabled) throw ConfigurationConflict("Hiding switch changed");
            auto previous = enabled;
            enabled = requested;
            try { save(); }
            catch (...) { enabled = previous; throw; }
        }
    };
}
