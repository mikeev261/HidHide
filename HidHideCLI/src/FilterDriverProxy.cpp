// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// FilterDriverProxy.cpp
#include "stdafx.h"
#include "FilterDriverProxy.h"
#include "HidHideIoctlContract.h"
#include "Utils.h"
#include "Volume.h"
#include "Logging.h"
#include "ConfigurationChannel.h"

namespace
{
    constexpr auto APP_PROFILES_KEY{ L"Software\\Nefarius Software Solutions e.U.\\HidHide\\AppProfiles" };

    typedef std::unique_ptr<std::remove_pointer<HANDLE>::type, decltype(&::CloseHandle)> CloseHandlePtr;

    // Get a file handle to the device driver
    // The flag allowFileNotFound is applied when the device couldn't be found and controls whether or not an exception is thrown on failure
    CloseHandlePtr Device(_In_ std::filesystem::path const& deviceName, _In_ bool allowFileNotFound)
    {
        TRACE_ALWAYS(L"");
        for (unsigned attempt = 0; ; ++attempt)
        {
            auto handle{ CloseHandlePtr(::CreateFileW(deviceName.native().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &::CloseHandle) };
            if (INVALID_HANDLE_VALUE != handle.get()) return handle;
            auto const error = ::GetLastError();
            if (allowFileNotFound && error == ERROR_FILE_NOT_FOUND) return handle;
            if ((error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION) && attempt < 3) { ::Sleep(25); continue; }
            if (error == ERROR_ACCESS_DENIED || error == ERROR_SHARING_VIOLATION)
                throw std::runtime_error("Driver configuration is busy or access was denied. Close the other configuration utility and retry");
            THROW_WIN32(error);
        }
    }

    // Get a file handle to the device driver; will throw when the device isn't found
    CloseHandlePtr Device(_In_ std::filesystem::path const& deviceName)
    {
        return (Device(deviceName, false));
    }

    // Get the current enabled state; returns true when the device is active in hiding devices on the black-list
    bool GetActive(_In_ HANDLE device)
    {
        TRACE_ALWAYS(L"");
        DWORD needed{};
        auto buffer{ std::vector<BOOLEAN>(1) };
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_GET_ACTIVE), nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(BOOLEAN)), &needed, nullptr)) THROW_WIN32_LAST_ERROR;
        if (sizeof(BOOLEAN) != needed) THROW_WIN32(ERROR_INVALID_PARAMETER);
        return (FALSE != buffer.at(0));
    }

    // Set the current enabled state
    void SetActive(_In_ HANDLE device, _In_ bool active)
    {
        TRACE_ALWAYS(L"");
        DWORD needed{};
        auto buffer{ std::vector<BOOLEAN>(1) };
        buffer.at(0) = (active ? TRUE : FALSE);
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_SET_ACTIVE), buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(BOOLEAN)), nullptr, 0, &needed, nullptr)) THROW_WIN32_LAST_ERROR;
    }

    // Get the device Instance Paths of the Human Interface Devices that are on the black-list (may reference not present devices)
    HidHide::DeviceInstancePaths GetBlacklist(_In_ HANDLE device)
    {
        TRACE_ALWAYS(L"");
        DWORD needed{};
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_GET_BLACKLIST), nullptr, 0, nullptr, 0, &needed, nullptr)) THROW_WIN32_LAST_ERROR;
        auto buffer{ std::vector<WCHAR>(needed / sizeof(WCHAR)) };
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_GET_BLACKLIST), nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(WCHAR)), &needed, nullptr)) THROW_WIN32_LAST_ERROR;
        return (HidHide::StringListToStringSet(HidHide::MultiStringToStringList(buffer)));
    }

    // Set the device Instance Paths of the Human Interface Devices that are on the black-list
    void SetBlacklist(_In_ HANDLE device, _In_ HidHide::DeviceInstancePaths const& deviceInstancePaths)
    {
        TRACE_ALWAYS(L"");
        DWORD needed{};
        auto buffer{ HidHide::StringListToMultiString(HidHide::StringSetToStringList(deviceInstancePaths)) };
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_SET_BLACKLIST), buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(WCHAR)), nullptr, 0, &needed, nullptr)) THROW_WIN32_LAST_ERROR;
    }

    // Get the applications on the white-list
    HidHide::FullImageNames GetWhitelist(_In_ HANDLE device)
    {
        TRACE_ALWAYS(L"");
        DWORD needed{};
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_GET_WHITELIST), nullptr, 0, nullptr, 0, &needed, nullptr)) THROW_WIN32_LAST_ERROR;
        auto buffer{ std::vector<WCHAR>(needed / sizeof(WCHAR)) };
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_GET_WHITELIST), nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(WCHAR)), &needed, nullptr)) THROW_WIN32_LAST_ERROR;
        return (HidHide::StringListToPathSet(HidHide::MultiStringToStringList(buffer)));
    }

    // Set the applications on the white-list
    void SetWhitelist(_In_ HANDLE device, _In_ HidHide::FullImageNames const& fullImageNames)
    {
        TRACE_ALWAYS(L"");
        DWORD needed{};
        auto buffer{ HidHide::StringListToMultiString(HidHide::PathSetToStringList(fullImageNames)) };
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_SET_WHITELIST), buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(WCHAR)), nullptr, 0, &needed, nullptr)) THROW_WIN32_LAST_ERROR;
    }

    // Set the application profiles
    void SetAppProfiles(_In_ HidHide::AppProfiles const& appProfiles)
    {
        TRACE_ALWAYS(L"");
        HidHide::Configuration state; state.profiles = appProfiles;
        HidHide::Protocol::Writer writer; writer.Number(HidHide::Protocol::Version); writer.State(state);
        HKEY key{};
        DWORD disposition{};
        auto status{ ::RegCreateKeyExW(HKEY_CURRENT_USER, APP_PROFILES_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition) };
        if (ERROR_SUCCESS != status) THROW_WIN32(status);
        status = ::RegSetValueExW(key, L"ConfigurationV1", 0, REG_BINARY, writer.data.data(), static_cast<DWORD>(writer.data.size()));
        ::RegCloseKey(key);
        if (ERROR_SUCCESS != status) THROW_WIN32(status);
    }

    // Read profiles from the per-user store. Scanning the full
    // REG_MULTI_SZ buffer (rather than stopping at the first empty string) also
    // recovers device entries that were placed after the old empty-string sentinel.
    HidHide::AppProfiles GetAppProfiles()
    {
        HidHide::AppProfiles result;
        HKEY key{};
        auto const opened = ::RegOpenKeyExW(HKEY_CURRENT_USER, APP_PROFILES_KEY, 0, KEY_READ, &key);
        if (opened == ERROR_FILE_NOT_FOUND) return result;
        if (opened != ERROR_SUCCESS) THROW_WIN32(opened);
        DWORD blobType{}, blobSize{};
        auto const blobStatus = ::RegQueryValueExW(key, L"ConfigurationV1", nullptr, &blobType, nullptr, &blobSize);
        if (blobStatus != ERROR_FILE_NOT_FOUND)
        {
            try
            {
                if (blobStatus != ERROR_SUCCESS || blobType != REG_BINARY || blobSize > HidHide::Protocol::MaxBytes)
                    throw std::runtime_error("Malformed or oversized profile store");
                std::vector<std::uint8_t> bytes(blobSize);
                if (::RegQueryValueExW(key, L"ConfigurationV1", nullptr, &blobType, bytes.data(), &blobSize) != ERROR_SUCCESS || blobType != REG_BINARY)
                    throw std::runtime_error("Cannot read profile store");
                bytes.resize(blobSize);
                HidHide::Protocol::Reader reader(bytes);
                if (reader.Number() != HidHide::Protocol::Version) throw std::runtime_error("Unsupported profile store version");
                result = reader.State().profiles; reader.End();
                ::RegCloseKey(key); return result;
            }
            catch (...) { ::RegCloseKey(key); throw; }
        }

        for (DWORD index = 0;; index++)
        {
            std::vector<WCHAR> valueName(32768);
            DWORD valueNameLength{ static_cast<DWORD>(valueName.size()) };
            DWORD type{};
            DWORD dataSize{};
            auto const status{ ::RegEnumValueW(key, index, valueName.data(), &valueNameLength, nullptr, &type, nullptr, &dataSize) };
            if (ERROR_NO_MORE_ITEMS == status) break;
            if ((ERROR_SUCCESS != status) || (REG_MULTI_SZ != type) || (0 == valueNameLength)) continue;

            std::vector<WCHAR> buffer((dataSize / sizeof(WCHAR)) + 1, L'\0');
            valueNameLength = static_cast<DWORD>(valueName.size());
            DWORD readSize{ dataSize };
            if (ERROR_SUCCESS != ::RegEnumValueW(key, index, valueName.data(), &valueNameLength, nullptr, &type,
                reinterpret_cast<LPBYTE>(buffer.data()), &readSize)) continue;

            std::filesystem::path imagePath{ std::wstring(valueName.data(), valueNameLength) };
            if (0 != _wcsnicmp(imagePath.native().c_str(), L"\\Device\\", 8))
            {
                try
                {
                    auto const normalized{ HidHide::FileNameToFullImageName(imagePath) };
                    if (!normalized.empty()) imagePath = normalized;
                }
                catch (...) {}
            }

            auto& devices{ result[imagePath] };
            size_t position{};
            size_t const characterCount{ readSize / sizeof(WCHAR) };
            while (position < characterCount)
            {
                size_t end{ position };
                while ((end < characterCount) && (L'\0' != buffer[end])) end++;
                if (end > position) devices.emplace(buffer.data() + position, end - position);
                position = end + 1;
            }
        }

        ::RegCloseKey(key);
        return result;
    }

    // Get the current whitelist inverse state; returns true when the whitelist logic is the inverse (effectively an application backlist)
    bool GetInverse(_In_ HANDLE device)
    {
        TRACE_ALWAYS(L"");
        DWORD needed{};
        auto buffer{ std::vector<BOOLEAN>(1) };
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_GET_WLINVERSE), nullptr, 0, buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(BOOLEAN)), &needed, nullptr))
        {
            auto const lastError{ ::GetLastError() };
            if (ERROR_INVALID_PARAMETER == lastError || ERROR_NOT_SUPPORTED == lastError || ERROR_INVALID_FUNCTION == lastError) return false;
            THROW_WIN32(lastError);
        }
        if (sizeof(BOOLEAN) != needed) return false;
        return (FALSE != buffer.at(0));
    }

    // Set the current whitelist inverse state
    void SetInverse(_In_ HANDLE device, _In_ bool inverse)
    {
        TRACE_ALWAYS(L"");
        DWORD needed{};
        auto buffer{ std::vector<BOOLEAN>(1) };
        buffer.at(0) = (inverse ? TRUE : FALSE);
        if (FALSE == ::DeviceIoControl(device, static_cast<DWORD>(IOCTL_SET_WLINVERSE), buffer.data(), static_cast<DWORD>(buffer.size() * sizeof(BOOLEAN)), nullptr, 0, &needed, nullptr))
        {
            auto const lastError{ ::GetLastError() };
            if (ERROR_INVALID_PARAMETER == lastError || ERROR_NOT_SUPPORTED == lastError || ERROR_INVALID_FUNCTION == lastError) return;
            THROW_WIN32(lastError);
        }
    }
}


namespace HidHide
{
    namespace
    {
        Configuration Snapshot(HANDLE device)
        {
            Configuration s;
            s.active = ::GetActive(device); s.inverse = ::GetInverse(device);
            s.blacklist = ::GetBlacklist(device); s.whitelist = ::GetWhitelist(device);
            s.profiles = ::GetAppProfiles();
            return s;
        }
        void RequireNoRecovery()
        {
            HKEY key{};
            auto const error = ::RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Nefarius Software Solutions e.U.\\HidHide\\AppProfileRuntime", 0, KEY_READ, &key);
            if (error == ERROR_SUCCESS) { ::RegCloseKey(key); throw std::runtime_error("Profile recovery is pending. Open the companion manager before changing settings"); }
            if (error != ERROR_FILE_NOT_FOUND) throw std::runtime_error("Cannot check profile recovery ownership");
        }
        Configuration Remote(Protocol::Writer request)
        {
            auto response = Channel::Exchange(std::move(request.data));
            Protocol::Reader reader(response);
            if (reader.Number() != Protocol::Version) throw std::runtime_error("Unsupported coordinator protocol");
            if (reader.Number())
            {
                auto error = reader.String(); reader.End();
                std::string message; for (auto c : error) message.push_back(c < 128 ? static_cast<char>(c) : '?');
                throw std::runtime_error(message);
            }
            auto state = reader.State(); reader.End(); return state;
        }
    }

    Configuration FilterDriverProxy::ReadDriverConfiguration()
    {
        auto device = ::Device(StringTable(IDS_CONTROL_DEVICE_NAME));
        return Snapshot(device.get());
    }

    void FilterDriverProxy::CommitDriverConfiguration(Configuration const& expected, Configuration const& desired)
    {
        auto device = ::Device(StringTable(IDS_CONTROL_DEVICE_NAME));
        auto const current = Snapshot(device.get());
        if (current != expected) throw std::runtime_error("Configuration changed outside this transaction. Refresh and resolve the conflict before retrying");
        // IOCTLs are not atomic. Disable first, enable last; a failed/partial write
        // does not advance confirmed state and will fail the next expected-state check.
        if (current.active && !desired.active) ::SetActive(device.get(), false);
        if (current.whitelist != desired.whitelist) ::SetWhitelist(device.get(), desired.whitelist);
        if (current.blacklist != desired.blacklist) ::SetBlacklist(device.get(), desired.blacklist);
        if (current.inverse != desired.inverse) ::SetInverse(device.get(), desired.inverse);
        if (current.profiles != desired.profiles) ::SetAppProfiles(desired.profiles);
        if (!current.active && desired.active) ::SetActive(device.get(), true);
        if (Snapshot(device.get()) != desired) throw std::runtime_error("Driver did not confirm the requested configuration; refresh before retrying");
    }

    _Use_decl_annotations_
    FilterDriverProxy::FilterDriverProxy(bool writeThrough, bool coordinator)
        : m_WriteThrough(writeThrough), m_Coordinator(coordinator)
    {
        Refresh();
        // Reading configuration has no side effects. The user's whitelist is
        // changed only by an explicit GUI/CLI command.
    }

    void FilterDriverProxy::SetCoordinator(std::function<Configuration()> read,
        std::function<void(Configuration const&, Configuration const&, bool)> commit)
    {
        m_Read = std::move(read); m_Commit = std::move(commit);
        Refresh();
    }

    Configuration FilterDriverProxy::Read()
    {
        if (m_Read) return m_Read();
        if (m_Coordinator) return ReadDriverConfiguration();
        Channel::Lease lease;
        if (lease.Acquired()) return ReadDriverConfiguration();
        Protocol::Writer request; request.Number(Protocol::Version); request.Number(static_cast<std::uint32_t>(Protocol::Command::Read));
        return Remote(std::move(request));
    }

    void FilterDriverProxy::Refresh() { auto fresh = Read(); m_Cache = fresh; m_Original = std::move(fresh); m_DisableRequested = false; }

    void FilterDriverProxy::Commit(Configuration const& desired)
    {
        if (m_Commit) m_Commit(m_Original, desired, m_DisableRequested);
        else if (m_Coordinator) CommitDriverConfiguration(m_Original, desired);
        else
        {
            Channel::Lease lease;
            if (lease.Acquired())
            {
                if (desired != m_Original || m_DisableRequested) { RequireNoRecovery(); CommitDriverConfiguration(m_Original, desired); }
            }
            else
            {
                Protocol::Writer request; request.Number(Protocol::Version); request.Number(static_cast<std::uint32_t>(Protocol::Command::Commit));
                request.State(m_Original); request.State(desired); request.Number(m_DisableRequested);
                auto confirmed = Remote(std::move(request));
                if (confirmed != desired) throw std::runtime_error("Coordinator did not confirm requested settings");
            }
        }
        m_Cache = desired; m_Original = desired; m_DisableRequested = false;
    }

    void FilterDriverProxy::Change(Configuration const& desired)
    {
        if (m_WriteThrough) Commit(desired);
        else m_Cache = desired;
    }

    void FilterDriverProxy::ApplyConfigurationChanges()
    {
        if (m_WriteThrough) THROW_WIN32(ERROR_INVALID_PARAMETER);
        // A read-only CLI invocation does not submit an obsolete snapshot.
        if (m_Cache != m_Original || m_DisableRequested) Commit(m_Cache);
    }

    std::vector<std::uint8_t> FilterDriverProxy::HandleRequest(std::vector<std::uint8_t> const& request)
    {
        Protocol::Writer response; response.Number(Protocol::Version);
        try
        {
            Protocol::Reader reader(request);
            if (reader.Number() != Protocol::Version) throw std::runtime_error("Unsupported coordinator protocol");
            auto command = static_cast<Protocol::Command>(reader.Number());
            if (command == Protocol::Command::Read) { reader.End(); response.Number(0); response.State(Read()); }
            else if (command == Protocol::Command::Commit)
            {
                auto expected = reader.State(); auto desired = reader.State(); auto disable = reader.Boolean(); reader.End();
                if (!m_Commit) throw std::runtime_error("Coordinator is not ready");
                m_Commit(expected, desired, disable);
                response.Number(0); response.State(desired);
                Refresh();
            }
            else throw std::runtime_error("Unknown configuration command");
        }
        catch (std::exception const& error)
        {
            response = {}; response.Number(Protocol::Version); response.Number(1);
            std::string message(error.what()); response.String(std::wstring(message.begin(), message.end()));
        }
        return response.data;
    }

    DWORD FilterDriverProxy::DeviceStatus()
    {
        auto handle = CloseHandlePtr(::CreateFileW(StringTable(IDS_CONTROL_DEVICE_NAME).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &::CloseHandle);
        return handle.get() == INVALID_HANDLE_VALUE ? ::GetLastError() : ERROR_SUCCESS;
    }

    bool FilterDriverProxy::GetActive() const { return m_Cache.active; }
    bool FilterDriverProxy::GetInverse() const { return m_Cache.inverse; }
    DeviceInstancePaths FilterDriverProxy::GetBlacklist() const { return m_Cache.blacklist; }
    FullImageNames FilterDriverProxy::GetWhitelist() const { return m_Cache.whitelist; }
    AppProfiles FilterDriverProxy::GetAppProfiles() const { return m_Cache.profiles; }
    _Use_decl_annotations_
    void FilterDriverProxy::SetActive(bool value) { auto desired = m_Cache; desired.active = value; m_DisableRequested = !value; Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::SetInverse(bool value) { auto desired = m_Cache; desired.inverse = value; Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::SetBlacklist(DeviceInstancePaths const& value) { auto desired = m_Cache; desired.blacklist = value; Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::SetWhitelist(FullImageNames const& value) { auto desired = m_Cache; desired.whitelist = value; Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::SetAppProfiles(AppProfiles const& value) { auto desired = m_Cache; desired.profiles = value; Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::BlacklistAddEntry(DeviceInstancePath const& value) { auto desired = m_Cache; desired.blacklist.insert(value); Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::BlacklistDelEntry(DeviceInstancePath const& value) { auto desired = m_Cache; desired.blacklist.erase(value); Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::WhitelistAddEntry(FullImageName const& value) { auto desired = m_Cache; desired.whitelist.insert(value); Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::WhitelistDelEntry(FullImageName const& value) { auto desired = m_Cache; desired.whitelist.erase(value); Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::AppProfileAdd(FullImageName const& value) { auto desired = m_Cache; desired.profiles.try_emplace(value); Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::AppProfileDelete(FullImageName const& value) { auto desired = m_Cache; desired.profiles.erase(value); Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::AppProfileAddEntry(FullImageName const& path, DeviceInstancePath const& device)
    { auto desired = m_Cache; desired.profiles[path].insert(device); Change(desired); }
    _Use_decl_annotations_
    void FilterDriverProxy::AppProfileDelEntry(FullImageName const& path, DeviceInstancePath const& device)
    { auto desired = m_Cache; auto it = desired.profiles.find(path); if (it != desired.profiles.end()) it->second.erase(device); Change(desired); }
}
