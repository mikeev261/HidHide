// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// FilterDriverProxy.h
#pragma once
#include "Configuration.h"
#include "ConfigurationSession.h"
#include <functional>

namespace HidHide
{
    class FilterDriverProxy
    {
    public:

        FilterDriverProxy() noexcept = delete;
        FilterDriverProxy(_In_ FilterDriverProxy const& rhs) = delete;
        FilterDriverProxy(_In_ FilterDriverProxy&& rhs) noexcept = delete;
        FilterDriverProxy& operator=(_In_ FilterDriverProxy const& rhs) = delete;
        FilterDriverProxy& operator=(_In_ FilterDriverProxy&& rhs) = delete;

        // Snapshot configuration. Driver handles exist only inside transactions.
        explicit FilterDriverProxy(_In_ bool writeThrough, bool coordinator = false);
        ~FilterDriverProxy() = default;

        void Refresh();
        Configuration const& CachedConfiguration() const { return m_Cache; }
        void SetCoordinator(std::function<Configuration()> read,
            std::function<void(Configuration const&, Configuration const&, bool)> commit);
        std::vector<std::uint8_t> HandleRequest(std::vector<std::uint8_t> const& request);
        static Configuration ReadDriverConfiguration();
        static void CommitDriverConfiguration(Configuration const& expected, Configuration const& desired);

        // Get the control device state
        // Returns ERROR_SUCCESS when available for use
        // Returns FILE_NOT_FOUND when the device is disabled (assuming it is installed)
        // Returns ACCESS_DENIED when in use (assuming it is not an ACL issue)
        static DWORD DeviceStatus();

        // Apply the configuration changes (if any)
        // Throws when the class is using write-through
        void ApplyConfigurationChanges();

        // Get the device Instance Paths of the Human Interface Devices that are on the black-list (may reference not present devices)
        DeviceInstancePaths GetBlacklist() const;

        // Set the device Instance Paths of the Human Interface Devices that are on the black-list
        void SetBlacklist(_In_ DeviceInstancePaths const& deviceInstancePaths);

        // Add a device to the black-list
        void BlacklistAddEntry(_In_ DeviceInstancePath const& deviceInstancePath);

        // Delete a device from the white-list
        void BlacklistDelEntry(_In_ DeviceInstancePath const& deviceInstancePath);

        // Get the applications on the white-list
        FullImageNames GetWhitelist() const;

        // Set the applications on the white-list
        void SetWhitelist(_In_ FullImageNames const& fullImageNames);

        // Add an application to the white-list
        void WhitelistAddEntry(_In_ FullImageName const& fullImageName);

        // Delete an application from the white-list
        void WhitelistDelEntry(_In_ FullImageName const& fullImageName);

        // Get the app profiles
        AppProfiles GetAppProfiles() const;

        // Set the app profiles
        void SetAppProfiles(_In_ AppProfiles const& appProfiles);

        // Create an empty application profile.
        void AppProfileAdd(_In_ FullImageName const& fullImageName);

        // Delete an application profile and all of its device entries.
        void AppProfileDelete(_In_ FullImageName const& fullImageName);

        // Add a device to an app profile
        void AppProfileAddEntry(_In_ FullImageName const& fullImageName, _In_ DeviceInstancePath const& deviceInstancePath);

        // Delete a device from an app profile
        void AppProfileDelEntry(_In_ FullImageName const& fullImageName, _In_ DeviceInstancePath const& deviceInstancePath);

        // Get the current enabled state; returns true when the device is active in hiding devices on the black-list
        bool GetActive() const;

        // Set the current enabled state
        void SetActive(_In_ bool active);

        // Get the current whitelist inverse state; returns true when the whitelist logic is the inverse (effectively an application backlist)
        bool GetInverse() const;

        // Set the current whitelist inverse state
        void SetInverse(_In_ bool inverse);

        void SetBlacklist(DeviceInstancePaths const& expected, DeviceInstancePaths const& value);

        void SetWhitelist(FullImageNames const& expected, FullImageNames const& value);

        void SetAppProfiles(AppProfiles const& expected, AppProfiles const& value);

        void SetActive(bool const& expected, bool const& value);

        void SetInverse(bool const& expected, bool const& value);

    private:

        Configuration Read();
        void Commit(Configuration const& desired);
        void Change(Configuration const& desired);
        bool const m_WriteThrough;
        bool const m_Coordinator;
        Configuration m_Cache;
        Configuration m_Original;
        bool m_DisableRequested{};
        std::function<Configuration()> m_Read;
        std::function<void(Configuration const&, Configuration const&, bool)> m_Commit;
    };
}
