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

    struct RunningApplication
    {
        HidHide::FullImageName fullImageName;
        std::wstring fileName;
        bool hasFullPath{};
    };

    std::vector<RunningApplication> RunningApplications()
    {
        std::vector<RunningApplication> result;
        HANDLE const snapshot{ ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if (INVALID_HANDLE_VALUE == snapshot) return result;

        PROCESSENTRY32W processEntry{};
        processEntry.dwSize = sizeof(processEntry);
        if (::Process32FirstW(snapshot, &processEntry))
        {
            do
            {
                RunningApplication running{};
                running.fileName = processEntry.szExeFile;

                HANDLE const process{ ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processEntry.th32ProcessID) };
                if (nullptr != process)
                {
                    std::vector<WCHAR> path(32768);
                    DWORD size{ static_cast<DWORD>(path.size()) };
                    if (::QueryFullProcessImageNameW(process, 0, path.data(), &size))
                    {
                        try
                        {
                            running.fullImageName = HidHide::FileNameToFullImageName(std::filesystem::path(std::wstring(path.data(), size)));
                            running.hasFullPath = !running.fullImageName.empty();
                        }
                        catch (...) {}
                    }
                    ::CloseHandle(process);
                }

                result.emplace_back(std::move(running));
            } while (::Process32NextW(snapshot, &processEntry));
        }

        ::CloseHandle(snapshot);
        return result;
    }

    bool SamePath(_In_ std::filesystem::path const& lhs, _In_ std::filesystem::path const& rhs)
    {
        return 0 == _wcsicmp(lhs.native().c_str(), rhs.native().c_str());
    }

    bool ProfileIsRunning(_In_ HidHide::FullImageName const& profile, _In_ std::vector<RunningApplication> const& runningApplications,
        _In_ std::map<std::wstring, size_t> const& profileFileNameCounts)
    {
        for (auto const& running : runningApplications)
        {
            if (running.hasFullPath && SamePath(profile, running.fullImageName)) return true;
        }

        // Some protected processes reject path queries. Only fall back to a file-name
        // match when that name identifies exactly one configured profile.
        auto const fileName{ profile.filename().native() };
        auto normalizedFileName{ fileName };
        std::transform(normalizedFileName.begin(), normalizedFileName.end(), normalizedFileName.begin(), ::towlower);
        auto const count{ profileFileNameCounts.find(normalizedFileName) };
        if ((profileFileNameCounts.end() == count) || (1 != count->second)) return false;

        for (auto const& running : runningApplications)
        {
            if (!running.hasFullPath && (0 == _wcsicmp(fileName.c_str(), running.fileName.c_str()))) return true;
        }
        return false;
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
CProfileManager::CProfileManager(HidHide::FilterDriverProxy& filterDriverProxy) noexcept
    : m_FilterDriverProxy(filterDriverProxy)
{}

CProfileManager::~CProfileManager()
{
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
HidHide::DeviceInstancePaths CProfileManager::ActiveProfileDevices(size_t& activeProfileCount) const
{
    auto const profiles{ m_FilterDriverProxy.GetAppProfiles() };
    auto const runningApplications{ RunningApplications() };

    std::map<std::wstring, size_t> profileFileNameCounts;
    for (auto const& [profile, devices] : profiles)
    {
        UNREFERENCED_PARAMETER(devices);
        auto fileName{ profile.filename().native() };
        std::transform(fileName.begin(), fileName.end(), fileName.begin(), ::towlower);
        profileFileNameCounts[fileName]++;
    }

    HidHide::DeviceInstancePaths result;
    activeProfileCount = 0;
    for (auto const& [profile, devices] : profiles)
    {
        if (!ProfileIsRunning(profile, runningApplications, profileFileNameCounts)) continue;
        activeProfileCount++;
        result.insert(devices.begin(), devices.end());
    }
    return result;
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
    m_ActiveProfileCount = 0;
    m_OverrideActive = false;
    ClearRecoveryState();
}

void CProfileManager::Tick()
{
    ConfigureAutoStart(!m_FilterDriverProxy.GetAppProfiles().empty());

    size_t activeProfileCount{};
    auto const activeDevices{ ActiveProfileDevices(activeProfileCount) };
    m_ActiveProfileCount = activeProfileCount;

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
