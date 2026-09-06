// SPDX-License-Identifier: MIT
#pragma once
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <memory>
#include <stdexcept>

namespace HidHide
{
    using DeviceInstancePath = std::wstring;
    using DeviceInstancePaths = std::set<DeviceInstancePath>;
    using FullImageName = std::filesystem::path;
    using FullImageNames = std::set<FullImageName>;
    using AppProfiles = std::map<FullImageName, DeviceInstancePaths>;
    // A filtered view replaces only paths represented by its controls.
    inline DeviceInstancePaths ReplaceDisplayedDevicePaths(DeviceInstancePaths paths,
        DeviceInstancePaths const& displayed, DeviceInstancePaths const& selected)
    {
        for (auto const& path : displayed) paths.erase(path);
        paths.insert(selected.begin(), selected.end());
        return paths;
    }
    enum class ConfigurationMode { Live, Snapshot };
    class ConfigurationConflict : public std::runtime_error
    {
    public:
        explicit ConfigurationConflict(char const* field) : std::runtime_error(field) {}
    };
    // Acquire must serialize cooperating clients and retain exclusive driver access
    // through read/check/write. No transaction across different driver IOCTLs exists.
    class ConfigurationBackend
    {
    public:
        struct Lease { virtual ~Lease() = default; };
        virtual ~ConfigurationBackend() = default;
        virtual std::unique_ptr<Lease> Acquire() = 0;
        virtual bool ReadActive() = 0;
        virtual void WriteActive(bool const& value) = 0;
        virtual DeviceInstancePaths ReadBlacklist() = 0;
        virtual void WriteBlacklist(DeviceInstancePaths const& value) = 0;
        virtual FullImageNames ReadWhitelist() = 0;
        virtual void WriteWhitelist(FullImageNames const& value) = 0;
        virtual AppProfiles ReadAppProfiles() = 0;
        virtual void WriteAppProfiles(AppProfiles const& value) = 0;
        virtual bool ReadInverse() = 0;
        virtual void WriteInverse(bool const& value) = 0;
    };
    class ConfigurationSession
    {
        template<class T> struct Field { T observed{}; T desired{}; bool dirty{}; };
        std::shared_ptr<ConfigurationBackend> m_Backend;
        ConfigurationMode m_Mode;
        mutable Field<bool> m_Active;
        mutable Field<DeviceInstancePaths> m_Blacklist;
        mutable Field<FullImageNames> m_Whitelist;
        mutable Field<AppProfiles> m_AppProfiles;
        mutable Field<bool> m_Inverse;
        template<class T> static void Check(Field<T> const& field, T const& current, char const* name)
        {
            if (current != field.observed && current != field.desired) throw ConfigurationConflict(name);
        }
    public:
        ConfigurationSession(std::shared_ptr<ConfigurationBackend> backend, ConfigurationMode mode)
            : m_Backend(std::move(backend)), m_Mode(mode)
        {
            auto lease = m_Backend->Acquire();
            m_Active.observed = m_Active.desired = m_Backend->ReadActive();
            m_Blacklist.observed = m_Blacklist.desired = m_Backend->ReadBlacklist();
            m_Whitelist.observed = m_Whitelist.desired = m_Backend->ReadWhitelist();
            m_AppProfiles.observed = m_AppProfiles.desired = m_Backend->ReadAppProfiles();
            m_Inverse.observed = m_Inverse.desired = m_Backend->ReadInverse();
        }
        void ApplyConfigurationChanges()
        {
            if (m_Mode != ConfigurationMode::Snapshot) throw std::logic_error("Only snapshot sessions can commit");
            auto lease = m_Backend->Acquire();
            // Preflight every dirty field before the first write. Clean fields belong
            // to the backend, not this snapshot, and must never be written back.
            if (m_Active.dirty) Check(m_Active, m_Backend->ReadActive(), "Active");
            if (m_Blacklist.dirty) Check(m_Blacklist, m_Backend->ReadBlacklist(), "Blacklist");
            if (m_Whitelist.dirty) Check(m_Whitelist, m_Backend->ReadWhitelist(), "Whitelist");
            if (m_AppProfiles.dirty) Check(m_AppProfiles, m_Backend->ReadAppProfiles(), "AppProfiles");
            if (m_Inverse.dirty) Check(m_Inverse, m_Backend->ReadInverse(), "Inverse");
            if (m_Whitelist.dirty)
            {
                if (m_Backend->ReadWhitelist() != m_Whitelist.desired) m_Backend->WriteWhitelist(m_Whitelist.desired);
                m_Whitelist.observed = m_Whitelist.desired;
                m_Whitelist.dirty = false;
            }
            if (m_Blacklist.dirty)
            {
                if (m_Backend->ReadBlacklist() != m_Blacklist.desired) m_Backend->WriteBlacklist(m_Blacklist.desired);
                m_Blacklist.observed = m_Blacklist.desired;
                m_Blacklist.dirty = false;
            }
            if (m_AppProfiles.dirty)
            {
                if (m_Backend->ReadAppProfiles() != m_AppProfiles.desired) m_Backend->WriteAppProfiles(m_AppProfiles.desired);
                m_AppProfiles.observed = m_AppProfiles.desired;
                m_AppProfiles.dirty = false;
            }
            if (m_Active.dirty)
            {
                if (m_Backend->ReadActive() != m_Active.desired) m_Backend->WriteActive(m_Active.desired);
                m_Active.observed = m_Active.desired;
                m_Active.dirty = false;
            }
            if (m_Inverse.dirty)
            {
                if (m_Backend->ReadInverse() != m_Inverse.desired) m_Backend->WriteInverse(m_Inverse.desired);
                m_Inverse.observed = m_Inverse.desired;
                m_Inverse.dirty = false;
            }
        }
        bool GetActive() const
        {
            if (m_Mode == ConfigurationMode::Live)
            {
                auto lease = m_Backend->Acquire();
                m_Active.observed = m_Active.desired = m_Backend->ReadActive();
            }
            return m_Active.desired;
        }
        void SetActive(bool const& value)
        {
            if (m_Mode == ConfigurationMode::Snapshot)
            {
                m_Active.desired = value;
                m_Active.dirty = value != m_Active.observed;
                return;
            }
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadActive();
            if (current != m_Active.observed && current != value) throw ConfigurationConflict("Active");
            if (current != value) m_Backend->WriteActive(value);
            // A failed write never acknowledges the proposed value: same-call retries work.
            m_Active.observed = m_Active.desired = value;
        }
        DeviceInstancePaths GetBlacklist() const
        {
            if (m_Mode == ConfigurationMode::Live)
            {
                auto lease = m_Backend->Acquire();
                m_Blacklist.observed = m_Blacklist.desired = m_Backend->ReadBlacklist();
            }
            return m_Blacklist.desired;
        }
        void SetBlacklist(DeviceInstancePaths const& value)
        {
            if (m_Mode == ConfigurationMode::Snapshot)
            {
                m_Blacklist.desired = value;
                m_Blacklist.dirty = value != m_Blacklist.observed;
                return;
            }
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadBlacklist();
            if (current != m_Blacklist.observed && current != value) throw ConfigurationConflict("Blacklist");
            if (current != value) m_Backend->WriteBlacklist(value);
            // A failed write never acknowledges the proposed value: same-call retries work.
            m_Blacklist.observed = m_Blacklist.desired = value;
        }
        FullImageNames GetWhitelist() const
        {
            if (m_Mode == ConfigurationMode::Live)
            {
                auto lease = m_Backend->Acquire();
                m_Whitelist.observed = m_Whitelist.desired = m_Backend->ReadWhitelist();
            }
            return m_Whitelist.desired;
        }
        void SetWhitelist(FullImageNames const& value)
        {
            if (m_Mode == ConfigurationMode::Snapshot)
            {
                m_Whitelist.desired = value;
                m_Whitelist.dirty = value != m_Whitelist.observed;
                return;
            }
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadWhitelist();
            if (current != m_Whitelist.observed && current != value) throw ConfigurationConflict("Whitelist");
            if (current != value) m_Backend->WriteWhitelist(value);
            // A failed write never acknowledges the proposed value: same-call retries work.
            m_Whitelist.observed = m_Whitelist.desired = value;
        }
        AppProfiles GetAppProfiles() const
        {
            if (m_Mode == ConfigurationMode::Live)
            {
                auto lease = m_Backend->Acquire();
                m_AppProfiles.observed = m_AppProfiles.desired = m_Backend->ReadAppProfiles();
            }
            return m_AppProfiles.desired;
        }
        void SetAppProfiles(AppProfiles const& value)
        {
            if (m_Mode == ConfigurationMode::Snapshot)
            {
                m_AppProfiles.desired = value;
                m_AppProfiles.dirty = value != m_AppProfiles.observed;
                return;
            }
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadAppProfiles();
            if (current != m_AppProfiles.observed && current != value) throw ConfigurationConflict("AppProfiles");
            if (current != value) m_Backend->WriteAppProfiles(value);
            // A failed write never acknowledges the proposed value: same-call retries work.
            m_AppProfiles.observed = m_AppProfiles.desired = value;
        }
        // Views carry their own expected values: polling on this shared session
        // must never acknowledge external edits on behalf of a stale control.
        void SetBlacklist(DeviceInstancePaths const& expected, DeviceInstancePaths const& value)
        {
            if (m_Mode != ConfigurationMode::Live) throw std::logic_error("Conditional view save requires a live session");
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadBlacklist();
            if (current != expected) throw ConfigurationConflict("Blacklist");
            if (current != value) m_Backend->WriteBlacklist(value);
            m_Blacklist.observed = m_Blacklist.desired = value;
        }
        void SetWhitelist(FullImageNames const& expected, FullImageNames const& value)
        {
            if (m_Mode != ConfigurationMode::Live) throw std::logic_error("Conditional view save requires a live session");
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadWhitelist();
            if (current != expected) throw ConfigurationConflict("Whitelist");
            if (current != value) m_Backend->WriteWhitelist(value);
            m_Whitelist.observed = m_Whitelist.desired = value;
        }
        void SetAppProfiles(AppProfiles const& expected, AppProfiles const& value)
        {
            if (m_Mode != ConfigurationMode::Live) throw std::logic_error("Conditional view save requires a live session");
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadAppProfiles();
            if (current != expected) throw ConfigurationConflict("AppProfiles");
            if (current != value) m_Backend->WriteAppProfiles(value);
            m_AppProfiles.observed = m_AppProfiles.desired = value;
        }
        void SetActive(bool const& expected, bool const& value)
        {
            if (m_Mode != ConfigurationMode::Live) throw std::logic_error("Conditional view save requires a live session");
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadActive();
            if (current != expected) throw ConfigurationConflict("Active");
            if (current != value) m_Backend->WriteActive(value);
            m_Active.observed = m_Active.desired = value;
        }
        void SetInverse(bool const& expected, bool const& value)
        {
            if (m_Mode != ConfigurationMode::Live) throw std::logic_error("Conditional view save requires a live session");
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadInverse();
            if (current != expected) throw ConfigurationConflict("Inverse");
            if (current != value) m_Backend->WriteInverse(value);
            m_Inverse.observed = m_Inverse.desired = value;
        }
        bool GetInverse() const
        {
            if (m_Mode == ConfigurationMode::Live)
            {
                auto lease = m_Backend->Acquire();
                m_Inverse.observed = m_Inverse.desired = m_Backend->ReadInverse();
            }
            return m_Inverse.desired;
        }
        void SetInverse(bool const& value)
        {
            if (m_Mode == ConfigurationMode::Snapshot)
            {
                m_Inverse.desired = value;
                m_Inverse.dirty = value != m_Inverse.observed;
                return;
            }
            auto lease = m_Backend->Acquire();
            auto const current = m_Backend->ReadInverse();
            if (current != m_Inverse.observed && current != value) throw ConfigurationConflict("Inverse");
            if (current != value) m_Backend->WriteInverse(value);
            // A failed write never acknowledges the proposed value: same-call retries work.
            m_Inverse.observed = m_Inverse.desired = value;
        }
        void BlacklistAddEntry(DeviceInstancePath const& value) { auto next = GetBlacklist(); next.insert(value); SetBlacklist(next); }
        void BlacklistDelEntry(DeviceInstancePath const& value) { auto next = GetBlacklist(); next.erase(value); SetBlacklist(next); }
        void WhitelistAddEntry(FullImageName const& value) { auto next = GetWhitelist(); next.insert(value); SetWhitelist(next); }
        void WhitelistDelEntry(FullImageName const& value) { auto next = GetWhitelist(); next.erase(value); SetWhitelist(next); }
        // Profile overrides own a pair of fields. Check both expected values and
        // apply both writes under one exclusive lease, independent of live getters.
        // Each callback records only a successful write, including partial commits.
        template<class BlacklistSaved, class ActiveSaved>
        void SetDriverState(DeviceInstancePaths const& expectedBlacklist, bool expectedActive,
            DeviceInstancePaths const& blacklist, bool active, BlacklistSaved blacklistSaved, ActiveSaved activeSaved)
        {
            if (m_Mode != ConfigurationMode::Live) throw std::logic_error("Driver override requires a live session");
            auto lease = m_Backend->Acquire();
            if (m_Backend->ReadBlacklist() != expectedBlacklist || m_Backend->ReadActive() != expectedActive)
                throw ConfigurationConflict("Profile override changed externally");
            if (expectedBlacklist != blacklist) m_Backend->WriteBlacklist(blacklist);
            m_Blacklist.observed = m_Blacklist.desired = blacklist;
            blacklistSaved();
            if (expectedActive != active) m_Backend->WriteActive(active);
            m_Active.observed = m_Active.desired = active;
            activeSaved();
        }
        void AppProfileAdd(FullImageName const& image) { auto next = GetAppProfiles(); next.try_emplace(image); SetAppProfiles(next); }
        void AppProfileDelete(FullImageName const& image) { auto next = GetAppProfiles(); next.erase(image); SetAppProfiles(next); }
        void AppProfileAddEntry(FullImageName const& image, DeviceInstancePath const& device)
        { auto next = GetAppProfiles(); next[image].insert(device); SetAppProfiles(next); }
        void AppProfileDelEntry(FullImageName const& image, DeviceInstancePath const& device)
        { auto next = GetAppProfiles(); auto it = next.find(image); if (it != next.end()) it->second.erase(device); SetAppProfiles(next); }
    };
}
