// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// FilterDriverProxy.cpp
#include "stdafx.h"
#include "FilterDriverProxy.h"
#include "HidHideIoctlContract.h"
#include "Utils.h"
#include "Volume.h"
#include "Logging.h"
#include <ktmw32.h>
#pragma comment(lib, "KtmW32.lib")

namespace
{
    constexpr auto APP_PROFILES_KEY{ L"Software\\Nefarius Software Solutions e.U.\\HidHide\\AppProfiles" };

    typedef std::unique_ptr<std::remove_pointer<HANDLE>::type, decltype(&::CloseHandle)> CloseHandlePtr;

    // Get a file handle to the device driver
    // The flag allowFileNotFound is applied when the device couldn't be found and controls whether or not an exception is thrown on failure
    CloseHandlePtr Device(_In_ std::filesystem::path const& deviceName, _In_ bool allowFileNotFound)
    {
        TRACE_ALWAYS(L"");
        auto handle{ CloseHandlePtr(::CreateFileW(deviceName.native().c_str(), GENERIC_READ, (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE), nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &::CloseHandle) };
        if ((INVALID_HANDLE_VALUE == handle.get()) && ((ERROR_FILE_NOT_FOUND != ::GetLastError()) || (!allowFileNotFound))) THROW_WIN32_LAST_ERROR;
        return (handle);
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
        // Preserve the established value-per-profile schema, but publish the whole
        // replacement atomically. Closing an uncommitted transaction rolls it back.
        CloseHandlePtr transaction{ ::CreateTransaction(nullptr, nullptr, 0, 0, 0, 0, nullptr), &::CloseHandle };
        if (INVALID_HANDLE_VALUE == transaction.get()) THROW_WIN32_LAST_ERROR;
        HKEY key{};
        DWORD disposition{};
        auto status{ ::RegCreateKeyTransactedW(HKEY_CURRENT_USER, APP_PROFILES_KEY, 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_QUERY_VALUE | KEY_SET_VALUE, nullptr, &key, &disposition, transaction.get(), nullptr) };
        if (ERROR_SUCCESS != status) THROW_WIN32(status);

        // Delete by repeatedly removing index zero; deleting a value compacts the enumeration.
        for (;;)
        {
            std::vector<WCHAR> valueName(32768);
            DWORD valueNameLength{ static_cast<DWORD>(valueName.size()) };
            status = ::RegEnumValueW(key, 0, valueName.data(), &valueNameLength, nullptr, nullptr, nullptr, nullptr);
            if (ERROR_NO_MORE_ITEMS == status) break;
            if (ERROR_SUCCESS != status)
            {
                ::RegCloseKey(key);
                THROW_WIN32(status);
            }
            status = ::RegDeleteValueW(key, valueName.data());
            if (ERROR_SUCCESS != status)
            {
                ::RegCloseKey(key);
                THROW_WIN32(status);
            }
        }

        for (auto const& [imagePath, devices] : appProfiles)
        {
            auto buffer{ HidHide::StringListToMultiString(HidHide::StringSetToStringList(devices)) };
            if (buffer.size() < 2) buffer.emplace_back(L'\0');
            status = ::RegSetValueExW(key, imagePath.native().c_str(), 0, REG_MULTI_SZ,
                reinterpret_cast<BYTE const*>(buffer.data()), static_cast<DWORD>(buffer.size() * sizeof(WCHAR)));
            if (ERROR_SUCCESS != status)
            {
                ::RegCloseKey(key);
                THROW_WIN32(status);
            }
        }

        ::RegCloseKey(key);
        if (!::CommitTransaction(transaction.get())) THROW_WIN32_LAST_ERROR;
    }

    // Read profiles from the per-user store. Scanning the full
    // REG_MULTI_SZ buffer (rather than stopping at the first empty string) also
    // recovers device entries that were placed after the old empty-string sentinel.
    HidHide::AppProfiles GetAppProfiles()
    {
        HidHide::AppProfiles result;
        HKEY key{};
        auto const openStatus = ::RegOpenKeyExW(HKEY_CURRENT_USER, APP_PROFILES_KEY, 0, KEY_READ, &key);
        if (ERROR_FILE_NOT_FOUND == openStatus) return result;
        if (ERROR_SUCCESS != openStatus) THROW_WIN32(openStatus);

        for (DWORD index = 0;; index++)
        {
            std::vector<WCHAR> valueName(32768);
            DWORD valueNameLength{ static_cast<DWORD>(valueName.size()) };
            DWORD type{};
            DWORD dataSize{};
            auto const status{ ::RegEnumValueW(key, index, valueName.data(), &valueNameLength, nullptr, &type, nullptr, &dataSize) };
            if (ERROR_NO_MORE_ITEMS == status) break;
            if (ERROR_SUCCESS != status) { ::RegCloseKey(key); THROW_WIN32(status); }
            if ((REG_MULTI_SZ != type) || (0 == valueNameLength)) continue;

            std::vector<WCHAR> buffer((dataSize / sizeof(WCHAR)) + 1, L'\0');
            valueNameLength = static_cast<DWORD>(valueName.size());
            DWORD readSize{ dataSize };
            auto const readStatus = ::RegEnumValueW(key, index, valueName.data(), &valueNameLength, nullptr, &type,
                reinterpret_cast<LPBYTE>(buffer.data()), &readSize);
            if (ERROR_SUCCESS != readStatus) { ::RegCloseKey(key); THROW_WIN32(readStatus); }

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
            // Unsupported writes must not be acknowledged as persisted changes.
            THROW_WIN32(lastError);
        }
    }
}

namespace
{
    class WindowsConfigurationBackend : public HidHide::ConfigurationBackend
    {
        HANDLE m_Device{};
        struct Operation : Lease
        {
            WindowsConfigurationBackend& backend;
            CloseHandlePtr mutex{ nullptr, &::CloseHandle };
            CloseHandlePtr device{ nullptr, &::CloseHandle };
            bool locked{};
            explicit Operation(WindowsConfigurationBackend& owner) : backend(owner)
            {
                mutex.reset(::CreateMutexW(nullptr, FALSE, L"Global\\HidHide.Configuration"));
                if (!mutex) THROW_WIN32_LAST_ERROR;
                DWORD const result = ::WaitForSingleObject(mutex.get(), 10000);
                if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) THROW_WIN32(ERROR_BUSY);
                locked = true;
                try { device = ::Device(HidHide::StringTable(IDS_CONTROL_DEVICE_NAME)); }
                catch (...) { ::ReleaseMutex(mutex.get()); locked = false; throw; }
                backend.m_Device = device.get();
            }
            ~Operation() override
            {
                backend.m_Device = nullptr;
                device.reset();
                if (locked) ::ReleaseMutex(mutex.get());
            }
        };
    public:
        std::unique_ptr<Lease> Acquire() override { return std::make_unique<Operation>(*this); }
        bool ReadActive() override { return ::GetActive(m_Device); }
        void WriteActive(bool const& value) override { ::SetActive(m_Device, value); }
        HidHide::DeviceInstancePaths ReadBlacklist() override { return ::GetBlacklist(m_Device); }
        void WriteBlacklist(HidHide::DeviceInstancePaths const& value) override { ::SetBlacklist(m_Device, value); }
        HidHide::FullImageNames ReadWhitelist() override { return ::GetWhitelist(m_Device); }
        void WriteWhitelist(HidHide::FullImageNames const& value) override { ::SetWhitelist(m_Device, value); }
        HidHide::AppProfiles ReadAppProfiles() override { return ::GetAppProfiles(); }
        void WriteAppProfiles(HidHide::AppProfiles const& value) override { ::SetAppProfiles(value); }
        bool ReadInverse() override { return ::GetInverse(m_Device); }
        void WriteInverse(bool const& value) override { ::SetInverse(m_Device, value); }
    };
    std::shared_ptr<HidHide::ConfigurationBackend> MakeBackend()
    {
        auto backend = std::make_shared<WindowsConfigurationBackend>();
        auto lease = backend->Acquire();
        auto whitelist = backend->ReadWhitelist();
        if (auto const image = HidHide::FileNameToFullImageName(HidHide::ModuleFileName()); !image.empty())
        {
            auto const inverse = backend->ReadInverse();
            if ((!inverse && whitelist.emplace(image).second)
                || (inverse && whitelist.erase(image))) backend->WriteWhitelist(whitelist);
        }
        return backend;
    }
}
namespace HidHide
{
    FilterDriverProxy::FilterDriverProxy(bool writeThrough)
        : ConfigurationSession(MakeBackend(), writeThrough ? ConfigurationMode::Live : ConfigurationMode::Snapshot) {}

    DWORD FilterDriverProxy::DeviceStatus()
    {
        CloseHandlePtr handle{ ::CreateFileW(HidHide::StringTable(IDS_CONTROL_DEVICE_NAME).c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &::CloseHandle };
        if (INVALID_HANDLE_VALUE != handle.get()) return ERROR_SUCCESS;
        auto const error = ::GetLastError();
        if (ERROR_ACCESS_DENIED != error && ERROR_FILE_NOT_FOUND != error) THROW_WIN32(error);
        return error;
    }
}
