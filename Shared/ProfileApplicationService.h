// SPDX-License-Identifier: MIT
#pragma once

#include "ProfileRepository.h"

namespace HidHide::Profiles
{
    // Sole application-layer owner of durable catalog mutation. Coordinators and
    // scanners receive only RepositoryView and immutable published snapshots.
    class ProfileApplicationService
    {
    public:
        struct SavedChange { SavedVersion version; LoadResult loaded; bool cleanupPending{}; };
        explicit ProfileApplicationService(std::filesystem::path root, ProfileRepository::FailureHook failureHook = {})
            : m_Repository(std::move(root), std::move(failureHook)) {}
        LoadResult OpenOrCreate(std::set<std::filesystem::path> allowed = {}) { return m_Repository.OpenOrCreate(std::move(allowed)); }
        LoadResult OpenOrCreateObserved(std::optional<std::set<std::filesystem::path>> allowed) { return m_Repository.OpenOrCreateObserved(std::move(allowed)); }
        SavedChange Apply(Profile draft, std::optional<SavedVersion> expected, std::optional<Settings> settings = std::nullopt,
            std::optional<SavedVersion> expectedSettings = std::nullopt)
        {
            auto version = m_Repository.Apply(std::move(draft), expected, std::move(settings), expectedSettings);
            auto pending = m_Repository.CleanupPending(); return { version, pending ? m_Repository.LoadCommittedAfterMutation() : m_Repository.Load(), pending };
        }
        SavedChange ApplySettings(Settings draft, SavedVersion expected)
        {
            auto version = m_Repository.ApplySettings(std::move(draft), expected);
            auto pending = m_Repository.CleanupPending(); return { version, pending ? m_Repository.LoadCommittedAfterMutation() : m_Repository.Load(), pending };
        }
        SavedChange Delete(std::wstring const& id, SavedVersion expected, Settings settings, SavedVersion expectedSettings)
        {
            m_Repository.Delete(id, expected, std::move(settings), expectedSettings);
            auto pending = m_Repository.CleanupPending(); return { {}, pending ? m_Repository.LoadCommittedAfterMutation() : m_Repository.Load(), pending };
        }
        SavedVersion Version(std::wstring const& id) const { return m_Repository.Version(id); }
        SavedVersion SettingsVersion() const { return m_Repository.SettingsVersion(); }
        std::filesystem::path const& Root() const { return m_Repository.Root(); }
        void ExportProfile(std::wstring const& id, std::filesystem::path const& destination) const { m_Repository.ExportProfile(id, destination); }
        void Backup(std::filesystem::path const& destination) const { m_Repository.Backup(destination); }
        SavedChange RestoreBackup(std::filesystem::path const& source)
        {
            auto loaded = m_Repository.RestoreBackup(source); auto pending = m_Repository.CleanupPending();
            return { m_Repository.SettingsVersion(), std::move(loaded), pending };
        }
    private:
        ProfileRepository m_Repository;
    };
}
