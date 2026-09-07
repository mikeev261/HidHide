// SPDX-License-Identifier: MIT
#include "stdafx.h"
#include "ProfileManager.h"

#include "Logging.h"
#include "Utils.h"
#include "Volume.h"
#include "ConfigurationChannel.h"
#include "ProfileProcessMatch.h"

#include <TlHelp32.h>

namespace
{
    constexpr auto RUNTIME_KEY{ L"Software\\Nefarius Software Solutions e.U.\\HidHide\\AppProfileRuntime" };
    constexpr auto RUN_KEY{ L"Software\\Microsoft\\Windows\\CurrentVersion\\Run" };
    constexpr auto RUN_VALUE{ L"HidHide App Profiles" };
    constexpr auto CONTROL_KEY{ L"Software\\Nefarius Software Solutions e.U.\\HidHide\\AppProfileControl" };

    void WritePaused(bool paused)
    {
        HKEY key{};
        auto status = ::RegCreateKeyExW(HKEY_CURRENT_USER, CONTROL_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS) THROW_WIN32(status);
        DWORD value = paused ? 1 : 0;
        status = ::RegSetValueExW(key, L"Paused", 0, REG_DWORD, reinterpret_cast<BYTE const*>(&value), sizeof(value));
        ::RegCloseKey(key);
        if (status != ERROR_SUCCESS) THROW_WIN32(status);
    }

    bool ReadPaused()
    {
        DWORD value{}, size = sizeof(value);
        auto status = ::RegGetValueW(HKEY_CURRENT_USER, CONTROL_KEY, L"Paused", RRF_RT_REG_DWORD, nullptr, &value, &size);
        if (status == ERROR_FILE_NOT_FOUND) return false;
        if (status != ERROR_SUCCESS || value > 1) throw std::runtime_error("Cannot read automatic-profile pause setting");
        return value != 0;
    }

    std::wstring Normalize(_In_ std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), ::towlower);
        return value;
    }

}

_Use_decl_annotations_
CProfileManager::CProfileManager(HidHide::FilterDriverProxy& filterDriverProxy)
    : m_FilterDriverProxy(filterDriverProxy)
{
    m_Worker = std::thread(&CProfileManager::WorkerMain, this);
}

CProfileManager::~CProfileManager()
{
    StopWorker();
    Stop();
}

_Use_decl_annotations_
std::vector<CProfileManager::PreparedProfile> CProfileManager::PrepareProfiles(HidHide::AppProfiles const& profiles)
{
    std::vector<PreparedProfile> result;
    result.reserve(profiles.size());
    for (auto const& [profile, devices] : profiles)
    {
        PreparedProfile prepared{};
        prepared.profile = profile;
        prepared.normalizedFileName = Normalize(profile.filename().native());
        prepared.devices = devices;
        try
        {
            prepared.displayPath = HidHide::FullImageNameToFileName(profile);
        }
        catch (...) {}
        result.emplace_back(std::move(prepared));
    }
    return result;
}

_Use_decl_annotations_
CProfileManager::ScanResult CProfileManager::ScanProfiles(std::vector<PreparedProfile> const& profiles)
{
    ScanResult result;
    if (profiles.empty()) return result;

    std::map<std::wstring, std::vector<size_t>> profilesByFileName;
    for (size_t index{}; index < profiles.size(); index++)
        profilesByFileName[profiles[index].normalizedFileName].emplace_back(index);

    HANDLE const snapshot{ ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
    if (INVALID_HANDLE_VALUE == snapshot) { result.complete = false; return result; }

    PROCESSENTRY32W processEntry{};
    processEntry.dwSize = sizeof(processEntry);
    if (::Process32FirstW(snapshot, &processEntry))
    {
        do
        {
            auto const candidates{ profilesByFileName.find(Normalize(processEntry.szExeFile)) };
            if (profilesByFileName.end() == candidates) continue;

            auto const processPath = HidHide::ResolveProfileProcessPath(processEntry.th32ProcessID,
                ::OpenProcess, ::QueryFullProcessImageNameW, ::CloseHandle);
            for (auto const index : candidates->second)
            {
                auto const& prepared = profiles[index];
                auto const match = HidHide::MatchProfileProcess(prepared.displayPath, processPath);
                if (match == HidHide::ProfileProcessMatch::Exact) result.activeProfiles.emplace(prepared.profile);
                else if (match == HidHide::ProfileProcessMatch::Unresolved) result.unresolvedProfiles.emplace(prepared.profile);
            }
        } while (::Process32NextW(snapshot, &processEntry));
    }

    if (::GetLastError() != ERROR_NO_MORE_FILES) result.complete = false;
    ::CloseHandle(snapshot);

    for (auto const& prepared : profiles)
        if (result.activeProfiles.end() != result.activeProfiles.find(prepared.profile))
            result.activeDevices.insert(prepared.devices.begin(), prepared.devices.end());

    return result;
}

void CProfileManager::WorkerMain() noexcept
{
    try
    {
        std::vector<PreparedProfile> preparedProfiles;
        std::uint64_t preparedRevision{ static_cast<std::uint64_t>(-1) };
        std::unique_lock<std::mutex> lock(m_WorkerMutex);

        while (!m_StopRequested)
        {
            m_WorkerWake.wait_for(lock, std::chrono::milliseconds(500), [this, preparedRevision]
            {
                return m_StopRequested || (m_ProfileRevision != preparedRevision);
            });
            if (m_StopRequested) break;

            if (preparedRevision != m_ProfileRevision)
            {
                auto const profiles{ m_PendingProfiles };
                preparedRevision = m_ProfileRevision;
                lock.unlock();
                preparedProfiles = PrepareProfiles(profiles);
                lock.lock();
                if (m_StopRequested) break;
                if (preparedRevision != m_ProfileRevision) continue;
            }

            lock.unlock();
            auto result{ ScanProfiles(preparedProfiles) };
            result.revision = preparedRevision;
            lock.lock();
            if (m_StopRequested) break;
            if (preparedRevision != m_ProfileRevision) continue;
            m_CompletedResult = std::move(result);
            m_CompletedSequence++;
        }
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(m_WorkerMutex);
        m_WorkerFailed = true;
    }
}

void CProfileManager::StopWorker() noexcept
{
    {
        std::lock_guard<std::mutex> lock(m_WorkerMutex);
        m_StopRequested = true;
    }
    m_WorkerWake.notify_one();
    if (m_Worker.joinable()) m_Worker.join();
}

void CProfileManager::Tick()
{
    Observe();
    if (m_Conflict) return;
    m_FilterDriverProxy.Refresh();
    auto const profiles{ m_FilterDriverProxy.GetAppProfiles() };
    if (!m_HasSubmittedProfiles || (profiles != m_SubmittedProfiles))
    {
        ConfigureAutoStart(!profiles.empty());
        m_SubmittedProfiles = profiles;
        m_HasSubmittedProfiles = true;
        {
            std::lock_guard<std::mutex> lock(m_WorkerMutex);
            m_PendingProfiles = profiles;
            m_ProfileRevision++;
        }
        m_WorkerWake.notify_one();
    }

    ScanResult result;
    std::uint64_t sequence{};
    {
        std::lock_guard<std::mutex> lock(m_WorkerMutex);
        if (m_WorkerFailed) { m_Status = L"Process monitoring stopped after a worker failure; restart the manager"; return; }
        if (m_AppliedSequence == m_CompletedSequence) return;
        result = m_CompletedResult;
        sequence = m_CompletedSequence;
        if (result.revision != m_ProfileRevision) return;
    }
    ApplyScanResult(result);
    m_AppliedSequence = sequence;
}

_Use_decl_annotations_
bool CProfileManager::ProfileIsActive(HidHide::FullImageName const& profile) const noexcept
{
    return m_ActiveProfiles.end() != m_ActiveProfiles.find(profile);
}

_Use_decl_annotations_
void CProfileManager::ConfigureAutoStart(bool enabled) const
{
    HKEY key{};
    DWORD disposition{};
    auto status{ ::RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition) };
    if (ERROR_SUCCESS != status) THROW_WIN32(status);

    if (!enabled)
    {
        status = ::RegDeleteValueW(key, RUN_VALUE);
        ::RegCloseKey(key);
        if ((ERROR_SUCCESS != status) && (ERROR_FILE_NOT_FOUND != status)) THROW_WIN32(status);
        return;
    }

    std::vector<WCHAR> modulePath(32768);
    DWORD const length{ ::GetModuleFileNameW(nullptr, modulePath.data(), static_cast<DWORD>(modulePath.size())) };
    if ((0 == length) || (length >= modulePath.size()))
    {
        auto const error{ ::GetLastError() };
        ::RegCloseKey(key);
        THROW_WIN32(0 == error ? ERROR_INSUFFICIENT_BUFFER : error);
    }

    std::wstring const command{ L"\"" + std::wstring(modulePath.data(), length) + L"\" --background" };
    DWORD existingSize{};
    DWORD existingType{};
    if (ERROR_SUCCESS == ::RegQueryValueExW(key, RUN_VALUE, nullptr, &existingType, nullptr, &existingSize)
        && REG_SZ == existingType && 0 != existingSize)
    {
        std::vector<WCHAR> existing((existingSize / sizeof(WCHAR)) + 1, L'\0');
        if (ERROR_SUCCESS == ::RegQueryValueExW(key, RUN_VALUE, nullptr, &existingType,
            reinterpret_cast<BYTE*>(existing.data()), &existingSize)
            && command == existing.data())
        {
            ::RegCloseKey(key);
            return;
        }
    }

    status = ::RegSetValueExW(key, RUN_VALUE, 0, REG_SZ, reinterpret_cast<BYTE const*>(command.c_str()),
        static_cast<DWORD>((command.size() + 1) * sizeof(WCHAR)));
    ::RegCloseKey(key);
    if (ERROR_SUCCESS != status) THROW_WIN32(status);
}

void CProfileManager::Stop() noexcept
{
    if (m_Conflict || (!m_OverrideActive && !m_JournalPending)) return;
    try
    {
        RestoreBaseline();
    }
    catch (...)
    {
        // Leave the recovery marker intact so the next launch can restore safely.
    }
}

void CProfileManager::ExitSafely()
{
    if (m_Conflict) throw std::runtime_error("Resolve the ownership conflict before exiting and restoring settings");
    if (m_OverrideActive || m_JournalPending) RestoreBaseline();
}

void CProfileManager::Observe()
{
    auto current = HidHide::FilterDriverProxy::ReadDriverConfiguration();
    if (current != m_Expected)
    {
        if (m_OverrideActive || m_JournalPending)
        {
            m_Conflict = true;
            m_Status = L"Conflict: external settings changed. Use the tray menu to accept current settings";
        }
        else if (!m_Conflict) { m_Baseline = current; m_Expected = std::move(current); }
    }
}

HidHide::Configuration CProfileManager::ReadUserConfiguration()
{
    Observe();
    return m_Baseline;
}

void CProfileManager::CommitUserConfiguration(HidHide::Configuration const& expected, HidHide::Configuration const& desired, bool disable)
{
    Observe();
    if (m_Conflict) throw std::runtime_error("Profile ownership conflict. Accept current driver settings from the manager tray menu before editing");
    if (expected != m_Baseline) throw HidHide::ConfigurationConflict("Settings changed since this command started. Refresh and retry");
    // An explicit off command suspends automatic overrides, including when the
    // saved baseline was already off and a running profile temporarily enabled it.
    bool const suspend = m_Suspended || disable;
    if (disable) { WritePaused(true); m_Suspended = true; }
    Transition(desired, m_LastProfileDevices, suspend);
}

void CProfileManager::Transition(HidHide::Configuration const& baseline, HidHide::DeviceInstancePaths const& devices, bool suspended)
{
    Observe();
    if (m_Conflict) throw std::runtime_error("Profile reconciliation is suspended because driver settings changed externally");
    auto desired = HidHide::EffectiveConfiguration(baseline, devices, suspended);
    bool const overrideActive = !suspended && !devices.empty();
    if ((overrideActive || m_JournalPending) && (desired != m_Expected || baseline != m_Baseline || !m_JournalPending))
        SaveRecoveryState(baseline, m_Expected, desired);
    HidHide::FilterDriverProxy::CommitDriverConfiguration(m_Expected, desired);
    // Advance ownership only after read-back confirmation.
    m_Baseline = baseline; m_Expected = std::move(desired);
    m_LastProfileDevices = devices; m_Suspended = suspended; m_OverrideActive = overrideActive;
    if (!overrideActive && m_JournalPending) ClearRecoveryState();
    m_Status = suspended ? L"Paused: baseline settings applied" : overrideActive ? L"Profiles applied globally" : L"Monitoring: baseline settings applied";
}

_Use_decl_annotations_
void CProfileManager::ApplyScanResult(ScanResult const& result)
{
    if (!result.complete) { m_Status = L"Process scan failed; preserving current settings"; return; }
    Transition(m_Baseline, result.activeDevices, m_Suspended);
    m_ActiveProfiles = result.activeProfiles;
    m_UnresolvedProfiles = result.unresolvedProfiles;
    m_ActiveProfileCount = m_ActiveProfiles.size();
}

void CProfileManager::RestoreBaseline()
{
    Transition(m_Baseline, {}, true);
    m_ActiveProfiles.clear(); m_ActiveProfileCount = 0;
}

void CProfileManager::AdoptExternalState()
{
    // Explicit conflict resolution: preserve the actual driver state, discard the
    // obsolete restoration claim, and require a separate resume action.
    auto current = HidHide::FilterDriverProxy::ReadDriverConfiguration();
    WritePaused(true);
    ClearRecoveryState();
    m_Baseline = current; m_Expected = std::move(current);
    m_Conflict = false; m_Suspended = true; m_OverrideActive = false;
    m_LastProfileDevices.clear(); m_ActiveProfiles.clear(); m_ActiveProfileCount = 0;
    m_Status = L"Paused: current driver settings accepted as baseline";
    m_FilterDriverProxy.Refresh();
}

void CProfileManager::Resume()
{
    Observe();
    if (m_Conflict) throw std::runtime_error("Resolve the ownership conflict before resuming profiles");
    WritePaused(false);
    m_Suspended = false;
    m_Status = L"Monitoring: waiting for a fresh process scan";
    // Wake the worker; no stale process observation is used to resume.
    { std::lock_guard<std::mutex> lock(m_WorkerMutex); ++m_ProfileRevision; }
    m_WorkerWake.notify_one();
}

void CProfileManager::Pause()
{
    Observe();
    if (m_Conflict) throw std::runtime_error("Resolve the ownership conflict before restoring the baseline");
    WritePaused(true); m_Suspended = true;
    RestoreBaseline();
}

void CProfileManager::SaveRecoveryState(HidHide::Configuration const& baseline,
    HidHide::Configuration const& before, HidHide::Configuration const& after)
{
    HidHide::Protocol::Writer record;
    record.Number(HidHide::Protocol::Version); record.String(HidHide::Channel::CurrentSid());
    record.State(baseline); record.State(before); record.State(after);
    HKEY key{};
    auto error = ::RegCreateKeyExW(HKEY_CURRENT_USER, RUNTIME_KEY, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (error != ERROR_SUCCESS) THROW_WIN32(error);
    error = ::RegSetValueExW(key, L"TransactionV1", 0, REG_BINARY, record.data.data(), static_cast<DWORD>(record.data.size()));
    if (error == ERROR_SUCCESS) error = ::RegFlushKey(key);
    ::RegCloseKey(key);
    if (error != ERROR_SUCCESS) THROW_WIN32(error);
    m_JournalPending = true;
}

void CProfileManager::ClearRecoveryState()
{
    auto const error = ::RegDeleteTreeW(HKEY_CURRENT_USER, RUNTIME_KEY);
    if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND) THROW_WIN32(error);
    m_JournalPending = false;
}

void CProfileManager::Recover()
{
    m_Baseline = HidHide::FilterDriverProxy::ReadDriverConfiguration();
    m_Suspended = ReadPaused();
    m_Expected = m_Baseline;
    HKEY key{};
    auto const opened = ::RegOpenKeyExW(HKEY_CURRENT_USER, RUNTIME_KEY, 0, KEY_QUERY_VALUE, &key);
    if (opened != ERROR_FILE_NOT_FOUND)
    {
        m_JournalPending = true;
        try
        {
            if (opened != ERROR_SUCCESS) throw std::runtime_error("Cannot read recovery journal");
            DWORD type{}, size{};
            if (::RegQueryValueExW(key, L"TransactionV1", nullptr, &type, nullptr, &size) != ERROR_SUCCESS
                || type != REG_BINARY || size > HidHide::Protocol::MaxBytes) throw std::runtime_error("Legacy or malformed recovery record");
            std::vector<std::uint8_t> bytes(size);
            if (::RegQueryValueExW(key, L"TransactionV1", nullptr, &type, bytes.data(), &size) != ERROR_SUCCESS || type != REG_BINARY)
                throw std::runtime_error("Incomplete recovery record");
            bytes.resize(size);
            HidHide::Protocol::Reader reader(bytes);
            if (reader.Number() != HidHide::Protocol::Version || reader.String() != HidHide::Channel::CurrentSid())
                throw std::runtime_error("Recovery record owner or version mismatch");
            auto baseline = reader.State(); auto before = reader.State(); auto after = reader.State(); reader.End();
            if (m_Expected != before && m_Expected != after && m_Expected != baseline) throw std::runtime_error("Recovery state differs from actual driver settings");
            HidHide::FilterDriverProxy::CommitDriverConfiguration(m_Expected, baseline);
            m_Baseline = baseline; m_Expected = std::move(baseline);
            ::RegCloseKey(key); key = nullptr;
            ClearRecoveryState();
        }
        catch (...)
        {
            m_Conflict = true;
            m_Status = L"Recovery conflict: record retained. Accept current settings from the tray menu to continue";
        }
        if (key) ::RegCloseKey(key);
    }
    m_FilterDriverProxy.SetCoordinator([this] { return ReadUserConfiguration(); },
        [this](auto const& expected, auto const& desired, bool disable) { CommitUserConfiguration(expected, desired, disable); });
}

bool CProfileManager::ProfileIsUnresolved(HidHide::FullImageName const& profile) const noexcept
{
    return !ProfileIsActive(profile) && m_UnresolvedProfiles.count(profile) != 0;
}

void CProfileManager::SetEnabled(bool displayed, bool requested)
{
    Observe();
    if (m_Expected.active != displayed) throw HidHide::ConfigurationConflict("Hiding state changed; refresh and retry");
    auto const expected = m_Baseline;
    auto desired = expected;
    desired.active = requested;
    CommitUserConfiguration(expected, desired, !requested);
    m_FilterDriverProxy.Refresh();
}
