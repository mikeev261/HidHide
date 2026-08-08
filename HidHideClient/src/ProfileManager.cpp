// SPDX-License-Identifier: MIT
#include "stdafx.h"
#include "ProfileManager.h"

#include "Logging.h"
#include "Utils.h"
#include "Volume.h"

#include <TlHelp32.h>

namespace
{
    constexpr auto RUNTIME_KEY{ L"Software\\Nefarius Software Solutions e.U.\\HidHide\\AppProfileRuntime" };
    constexpr auto VALUE_OVERRIDE_ACTIVE{ L"OverrideActive" };
    constexpr auto VALUE_BASELINE_ACTIVE{ L"BaselineActive" };
    constexpr auto VALUE_BASELINE_BLACKLIST{ L"BaselineBlacklist" };
    constexpr auto RUN_KEY{ L"Software\\Microsoft\\Windows\\CurrentVersion\\Run" };
    constexpr auto RUN_VALUE{ L"HidHide App Profiles" };

    std::wstring Normalize(_In_ std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), ::towlower);
        return value;
    }

    void WriteDword(_In_ HKEY key, _In_ PCWSTR name, _In_ DWORD value)
    {
        auto const status{ ::RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<BYTE const*>(&value), sizeof(value)) };
        if (ERROR_SUCCESS != status) THROW_WIN32(status);
    }

    bool ReadDword(_In_ HKEY key, _In_ PCWSTR name, _Out_ DWORD& value)
    {
        DWORD type{};
        DWORD size{ sizeof(value) };
        return ERROR_SUCCESS == ::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size)
            && REG_DWORD == type && sizeof(value) == size;
    }

    void WriteDevicePaths(_In_ HKEY key, _In_ PCWSTR name, _In_ HidHide::DeviceInstancePaths const& paths)
    {
        auto buffer{ HidHide::StringListToMultiString(HidHide::StringSetToStringList(paths)) };
        if (buffer.size() < 2) buffer.emplace_back(L'\0');
        auto const status{ ::RegSetValueExW(key, name, 0, REG_MULTI_SZ, reinterpret_cast<BYTE const*>(buffer.data()),
            static_cast<DWORD>(buffer.size() * sizeof(WCHAR))) };
        if (ERROR_SUCCESS != status) THROW_WIN32(status);
    }

    _Success_(return)
    bool ReadDevicePaths(_In_ HKEY key, _In_ PCWSTR name, _Out_ HidHide::DeviceInstancePaths& paths)
    {
        paths.clear();
        DWORD type{};
        DWORD size{};
        if (ERROR_SUCCESS != ::RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) || REG_MULTI_SZ != type) return false;
        std::vector<WCHAR> buffer((size / sizeof(WCHAR)) + 2, L'\0');
        if (ERROR_SUCCESS != ::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(buffer.data()), &size)) return false;
        paths = HidHide::StringListToStringSet(HidHide::MultiStringToStringList(buffer));
        return true;
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

void CProfileManager::Recover()
{
    HKEY key{};
    if (ERROR_SUCCESS != ::RegOpenKeyExW(HKEY_CURRENT_USER, RUNTIME_KEY, 0, KEY_READ, &key)) return;

    DWORD overrideActive{};
    DWORD baselineActive{};
    HidHide::DeviceInstancePaths baselineBlacklist;
    bool const valid{ ReadDword(key, VALUE_OVERRIDE_ACTIVE, overrideActive)
        && ReadDword(key, VALUE_BASELINE_ACTIVE, baselineActive)
        && ReadDevicePaths(key, VALUE_BASELINE_BLACKLIST, baselineBlacklist) };
    ::RegCloseKey(key);

    if (valid && (0 != overrideActive))
    {
        m_FilterDriverProxy.SetBlacklist(baselineBlacklist);
        m_FilterDriverProxy.SetActive(0 != baselineActive);
    }
    ClearRecoveryState();
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
    if (INVALID_HANDLE_VALUE == snapshot) return result;

    PROCESSENTRY32W processEntry{};
    processEntry.dwSize = sizeof(processEntry);
    if (::Process32FirstW(snapshot, &processEntry))
    {
        do
        {
            auto const candidates{ profilesByFileName.find(Normalize(processEntry.szExeFile)) };
            if (profilesByFileName.end() == candidates) continue;

            bool hasFullPath{};
            std::filesystem::path processPath;
            HANDLE const process{ ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processEntry.th32ProcessID) };
            if (nullptr != process)
            {
                std::vector<WCHAR> path(32768);
                DWORD size{ static_cast<DWORD>(path.size()) };
                if (::QueryFullProcessImageNameW(process, 0, path.data(), &size))
                {
                    processPath = std::wstring(path.data(), size);
                    hasFullPath = true;
                }
                ::CloseHandle(process);
            }

            if (hasFullPath)
            {
                for (auto const index : candidates->second)
                {
                    auto const& prepared{ profiles[index] };
                    if (!prepared.displayPath.empty() && (0 == _wcsicmp(prepared.displayPath.native().c_str(), processPath.native().c_str())))
                        result.activeProfiles.emplace(prepared.profile);
                }
            }
            else if (1 == candidates->second.size())
            {
                // Some protected processes reject path queries. Fall back to the
                // executable name only when it identifies one configured profile.
                result.activeProfiles.emplace(profiles[candidates->second.front()].profile);
            }
        } while (::Process32NextW(snapshot, &processEntry));
    }

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
            lock.lock();
            if (m_StopRequested) break;
            if (preparedRevision != m_ProfileRevision) continue;
            m_CompletedResult = std::move(result);
            m_CompletedSequence++;
        }
    }
    catch (...)
    {
        // A failed scan must not terminate the application. Keep the last known
        // state; this path is reserved for unexpected failures such as allocation.
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

void CProfileManager::SaveRecoveryState() const
{
    HKEY key{};
    DWORD disposition{};
    auto const status{ ::RegCreateKeyExW(HKEY_CURRENT_USER, RUNTIME_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE, nullptr, &key, &disposition) };
    if (ERROR_SUCCESS != status) THROW_WIN32(status);

    try
    {
        WriteDevicePaths(key, VALUE_BASELINE_BLACKLIST, m_BaselineBlacklist);
        WriteDword(key, VALUE_BASELINE_ACTIVE, m_BaselineActive ? 1 : 0);
        // Write the marker last so an incomplete record is never treated as recoverable.
        WriteDword(key, VALUE_OVERRIDE_ACTIVE, 1);
    }
    catch (...)
    {
        ::RegCloseKey(key);
        throw;
    }
    ::RegCloseKey(key);
}

void CProfileManager::ClearRecoveryState() const noexcept
{
    ::RegDeleteTreeW(HKEY_CURRENT_USER, RUNTIME_KEY);
}

void CProfileManager::RestoreBaseline()
{
    m_FilterDriverProxy.SetBlacklist(m_BaselineBlacklist);
    m_FilterDriverProxy.SetActive(m_BaselineActive);
    m_LastProfileDevices.clear();
    m_LastAppliedBlacklist.clear();
    m_ActiveProfiles.clear();
    m_ActiveProfileCount = 0;
    m_OverrideActive = false;
    ClearRecoveryState();
}

void CProfileManager::Tick()
{
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
    {
        std::lock_guard<std::mutex> lock(m_WorkerMutex);
        if (m_AppliedSequence == m_CompletedSequence) return;
        result = m_CompletedResult;
        m_AppliedSequence = m_CompletedSequence;
    }
    ApplyScanResult(result);
}

_Use_decl_annotations_
void CProfileManager::ApplyScanResult(ScanResult const& result)
{
    auto const& activeDevices{ result.activeDevices };
    m_ActiveProfiles = result.activeProfiles;
    m_ActiveProfileCount = m_ActiveProfiles.size();

    if (m_OverrideActive)
    {
        // Changes made in the Devices tab while profiles are active become part of
        // the baseline, excluding devices supplied by the previous profile union.
        auto const currentBlacklist{ m_FilterDriverProxy.GetBlacklist() };
        if (currentBlacklist != m_LastAppliedBlacklist)
        {
            m_BaselineBlacklist = currentBlacklist;
            for (auto const& device : m_LastProfileDevices) m_BaselineBlacklist.erase(device);
            SaveRecoveryState();
        }
        if (!m_FilterDriverProxy.GetActive())
        {
            m_BaselineActive = false;
            SaveRecoveryState();
        }
    }

    if (activeDevices.empty())
    {
        if (m_OverrideActive) RestoreBaseline();
        return;
    }

    if (!m_OverrideActive)
    {
        m_BaselineBlacklist = m_FilterDriverProxy.GetBlacklist();
        m_BaselineActive = m_FilterDriverProxy.GetActive();
        m_OverrideActive = true;
        SaveRecoveryState();
    }

    HidHide::DeviceInstancePaths effective{ m_BaselineBlacklist };
    effective.insert(activeDevices.begin(), activeDevices.end());
    if (effective != m_FilterDriverProxy.GetBlacklist()) m_FilterDriverProxy.SetBlacklist(effective);
    if (!m_FilterDriverProxy.GetActive()) m_FilterDriverProxy.SetActive(true);

    m_LastProfileDevices = activeDevices;
    m_LastAppliedBlacklist = std::move(effective);
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
    if (!m_OverrideActive) return;
    try
    {
        RestoreBaseline();
    }
    catch (...)
    {
        // Leave the recovery marker intact so the next launch can restore safely.
    }
}
