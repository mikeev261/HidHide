// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace HidHide::Profiles
{
    constexpr std::uint32_t SchemaVersion{ 1 };
    constexpr std::size_t MaxProfiles{ 512 };
    constexpr std::size_t MaxRulesPerProfile{ 4096 };
    constexpr std::size_t MaxTextCharacters{ 32768 };
    constexpr std::int32_t MinPriority{ -100000 };
    constexpr std::int32_t MaxPriority{ 100000 };

    enum class Kind { Application, Global };
    enum class Visibility { Hidden, Visible };
    enum class Mode { Automatic, UseGlobal };

    struct DeviceRule
    {
        std::wstring identity;
        std::wstring friendlyName;
        Visibility visibility{ Visibility::Visible };
        bool operator==(DeviceRule const& other) const
        {
            return identity == other.identity && friendlyName == other.friendlyName && visibility == other.visibility;
        }
    };

    struct Profile
    {
        std::wstring id;
        std::uint64_t revision{};
        std::wstring name;
        Kind kind{ Kind::Global };
        bool enabled{ true };
        std::int32_t priority{};
        std::filesystem::path executable;
        std::vector<DeviceRule> rules;

        bool operator==(Profile const& other) const
        {
            return id == other.id && revision == other.revision && name == other.name && kind == other.kind
                && enabled == other.enabled && priority == other.priority && executable == other.executable
                && rules == other.rules;
        }
    };

    struct Settings
    {
        std::uint64_t revision{};
        std::wstring selectedGlobalId;
        Mode mode{ Mode::Automatic };
        bool paused{};
        bool startWithWindows{ true };
        std::set<std::filesystem::path> allowedApplications;
        bool operator==(Settings const& other) const
        {
            return revision == other.revision && selectedGlobalId == other.selectedGlobalId && mode == other.mode
                && paused == other.paused && startWithWindows == other.startWithWindows
                && allowedApplications == other.allowedApplications;
        }
    };

    struct Snapshot
    {
        std::uint64_t generation{};
        std::map<std::wstring, Profile> profiles;
        Settings settings;
    };

    inline bool IsStableId(std::wstring const& id)
    {
        // Repository filenames use the canonical lower-case GUID form without braces.
        if (id.size() != 36 || id[8] != L'-' || id[13] != L'-' || id[18] != L'-' || id[23] != L'-') return false;
        for (std::size_t i{}; i < id.size(); ++i)
        {
            if (i == 8 || i == 13 || i == 18 || i == 23) continue;
            if (!((id[i] >= L'0' && id[i] <= L'9') || (id[i] >= L'a' && id[i] <= L'f'))) return false;
        }
        return true;
    }

    inline std::filesystem::path NormalizeExecutable(std::filesystem::path path)
    {
        if (path.empty()) return {};
        if (path.native().find(L'\0') != std::wstring::npos) throw std::invalid_argument("Application path contains an embedded NUL");
        path = path.lexically_normal();
        if (!path.is_absolute() || path.native().rfind(L"\\\\", 0) == 0 || _wcsicmp(path.extension().c_str(), L".exe") != 0)
            throw std::invalid_argument("Application profile executable must be an absolute local .exe path");
        auto text = path.native();
        std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        return text;
    }

    inline void Validate(Profile const& profile)
    {
        if (!IsStableId(profile.id)) throw std::invalid_argument("Profile id is not a canonical GUID");
        if (!profile.revision) throw std::invalid_argument("Profile revision must be positive");
        if (profile.name.empty() || profile.name.size() > 256 || profile.name.find(L'\0') != std::wstring::npos) throw std::invalid_argument("Profile name is missing or too long");
        if (profile.priority < MinPriority || profile.priority > MaxPriority) throw std::invalid_argument("Profile priority is out of range");
        if (profile.rules.size() > MaxRulesPerProfile) throw std::invalid_argument("Profile has too many device rules");
        if (profile.kind == Kind::Application)
        {
            if (profile.executable.empty() || !profile.executable.is_absolute()) throw std::invalid_argument("Application profile requires an absolute executable path");
        }
        else if (!profile.executable.empty()) throw std::invalid_argument("Global profile cannot have an executable trigger");
        std::vector<std::wstring> identities;
        for (auto const& rule : profile.rules)
        {
            if (rule.identity.empty() || rule.identity.size() > MaxTextCharacters || rule.identity.find(L'\0') != std::wstring::npos) throw std::invalid_argument("Device identity is missing or too long");
            if (rule.friendlyName.size() > 1024 || rule.friendlyName.find(L'\0') != std::wstring::npos) throw std::invalid_argument("Device friendly name is too long");
            if (std::any_of(identities.begin(), identities.end(), [&](auto const& identity) { return _wcsicmp(identity.c_str(), rule.identity.c_str()) == 0; }))
                throw std::invalid_argument("Duplicate device identity in profile");
            identities.emplace_back(rule.identity);
        }
    }

    inline void Validate(Snapshot const& snapshot)
    {
        if (snapshot.profiles.empty() || snapshot.profiles.size() > MaxProfiles) throw std::invalid_argument("Repository must contain a bounded profile set");
        bool selectedGlobal{};
        for (auto const& [id, profile] : snapshot.profiles)
        {
            Validate(profile);
            if (id != profile.id) throw std::invalid_argument("Profile map key does not match id");
            if (profile.kind == Kind::Global && profile.enabled && profile.id == snapshot.settings.selectedGlobalId) selectedGlobal = true;
        }
        if (!selectedGlobal) throw std::invalid_argument("Selected Global profile does not exist");
        if (snapshot.settings.allowedApplications.size() > MaxRulesPerProfile) throw std::invalid_argument("Too many allowed applications");
        for (auto const& application : snapshot.settings.allowedApplications)
            if (application.empty() || !application.is_absolute()) throw std::invalid_argument("Allowed application path is invalid");
    }

    struct ProcessObservation
    {
        std::uint32_t processId{};
        std::uint64_t lifetimeIdentity{};
        std::wstring fileName;
        std::filesystem::path verifiedPath;
        bool pathAccessible{ true };
    };

    // Process path caches must include a creation/lifetime identity. A recycled PID
    // can never inherit a prior path verification.
    class ProcessIdentityCache
    {
    public:
        void Remember(ProcessObservation const& observation)
        {
            if (!observation.lifetimeIdentity || !observation.pathAccessible || observation.verifiedPath.empty()) return;
            m_Entries[observation.processId] = { observation.lifetimeIdentity, observation.verifiedPath };
        }
        std::optional<std::filesystem::path> Find(std::uint32_t processId, std::uint64_t lifetimeIdentity) const
        {
            auto it = m_Entries.find(processId);
            if (it == m_Entries.end() || !lifetimeIdentity || it->second.first != lifetimeIdentity) return std::nullopt;
            return it->second.second;
        }
        void Retain(std::vector<ProcessObservation> const& live)
        {
            for (auto it = m_Entries.begin(); it != m_Entries.end();)
            {
                auto found = std::find_if(live.begin(), live.end(), [&](auto const& item)
                { return item.processId == it->first && item.lifetimeIdentity == it->second.first; });
                if (found == live.end()) it = m_Entries.erase(it); else ++it;
            }
        }
    private:
        std::map<std::uint32_t, std::pair<std::uint64_t, std::filesystem::path>> m_Entries;
    };

    enum class SelectionReason { Application, GlobalFallback, ManualGlobal, Paused, DetectionUncertain };
    struct Selection
    {
        std::wstring profileId;
        SelectionReason reason{ SelectionReason::GlobalFallback };
        std::wstring executableName;
        bool verified{};
        bool operator==(Selection const& other) const
        {
            return profileId == other.profileId && reason == other.reason && executableName == other.executableName
                && verified == other.verified;
        }
        bool operator!=(Selection const& other) const { return !(*this == other); }
    };

    inline Selection SelectWinner(Snapshot const& snapshot, std::vector<ProcessObservation> const& processes)
    {
        Validate(snapshot);
        if (snapshot.settings.paused) return { snapshot.settings.selectedGlobalId, SelectionReason::Paused, {}, true };
        if (snapshot.settings.mode == Mode::UseGlobal)
            return { snapshot.settings.selectedGlobalId, SelectionReason::ManualGlobal, {}, true };

        Profile const* winner{};
        bool uncertain{};
        for (auto const& [id, profile] : snapshot.profiles)
        {
            (void)id;
            if (profile.kind != Kind::Application || !profile.enabled) continue;
            auto normalized = NormalizeExecutable(profile.executable);
            auto wantedName = normalized.filename().native();
            for (auto const& process : processes)
            {
                if (_wcsicmp(process.fileName.c_str(), wantedName.c_str()) != 0) continue;
                if (!process.pathAccessible || process.verifiedPath.empty()) { uncertain = true; continue; }
                if (_wcsicmp(NormalizeExecutable(process.verifiedPath).c_str(), normalized.c_str()) != 0) continue;
                if (!winner || profile.priority > winner->priority
                    || (profile.priority == winner->priority && profile.id < winner->id)) winner = &profile;
            }
        }
        if (winner) return { winner->id, SelectionReason::Application, winner->executable.filename().native(), true };
        // An inaccessible same-name process is diagnostic uncertainty, never a
        // winner. The selected Global remains the complete effective policy.
        if (uncertain) return { snapshot.settings.selectedGlobalId, SelectionReason::DetectionUncertain, {}, true };
        return { snapshot.settings.selectedGlobalId, SelectionReason::GlobalFallback, {}, true };
    }

    inline std::set<std::wstring> HiddenDevices(Profile const& profile)
    {
        std::set<std::wstring> result;
        for (auto const& rule : profile.rules) if (rule.visibility == Visibility::Hidden) result.emplace(rule.identity);
        return result;
    }

    struct DesiredEnforcement
    {
        bool hidingEnabled{};
        std::set<std::wstring> hiddenDevices;
        std::set<std::filesystem::path> allowedApplications;
        bool operator==(DesiredEnforcement const& other) const
        {
            return hidingEnabled == other.hidingEnabled && hiddenDevices == other.hiddenDevices
                && allowedApplications == other.allowedApplications;
        }
    };

    struct EnforcementResult
    {
        bool success{};
        bool observedKnown{};
        DesiredEnforcement observed;
        std::wstring failure;
        bool conflict{};
    };

    class IEnforcement
    {
    public:
        virtual ~IEnforcement() = default;
        virtual EnforcementResult Observe() = 0;
        virtual EnforcementResult Reconcile(DesiredEnforcement const& desired) = 0;
        virtual EnforcementResult RestoreBaseline() = 0;
        virtual EnforcementResult AdoptCurrentAsBaseline() = 0;
    };

    class DeduplicatingEnforcement
    {
    public:
        explicit DeduplicatingEnforcement(IEnforcement& enforcement) : m_Enforcement(enforcement) {}
        EnforcementResult Reconcile(DesiredEnforcement const& desired)
        {
            if (m_LastObserved && *m_LastObserved == desired)
            {
                auto observed = m_Enforcement.Observe();
                if (observed.success && observed.observedKnown && observed.observed == desired) return observed;
                m_LastObserved.reset();
            }
            auto result = m_Enforcement.Reconcile(desired);
            if (result.success && result.observedKnown && result.observed == desired) m_LastObserved = desired;
            return result;
        }
        void Invalidate() { m_LastObserved.reset(); }
    private:
        IEnforcement& m_Enforcement;
        std::optional<DesiredEnforcement> m_LastObserved;
    };
}
