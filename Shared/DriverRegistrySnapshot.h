// SPDX-License-Identifier: MIT
#pragma once
#include "Maintenance.h"
#include <algorithm>

namespace HidHide::Maintenance
{
    inline std::set<std::wstring> DecodeStoredDriverList(std::vector<wchar_t> const& data)
    {
        if (data.empty() || data.size() > 512 * 1024 || data.back() != L'\0') throw std::runtime_error("Invalid stored driver list size");
        std::set<std::wstring> values;
        size_t offset{};
        while (offset < data.size() && data[offset])
        {
            size_t end = offset; while (end < data.size() && data[end]) ++end;
            if (end == data.size() || end - offset > 32767 || values.size() >= 4096 || !values.emplace(data.data() + offset, end - offset).second)
                throw std::runtime_error("Invalid stored driver list entry");
            offset = end + 1;
        }
        if ((!values.empty() && offset == data.size()) || std::any_of(data.begin() + offset, data.end(), [](wchar_t c) { return c != L'\0'; }))
            throw std::runtime_error("Invalid stored driver list terminator");
        return values;
    }
    // Read only after ordinary-user profile recovery has been ruled out. A
    // persisted active profile must never be relabelled as a baseline.
    inline Snapshot ReadStoredDriverSnapshot()
    {
        Snapshot result;
        HKEY raw{};
        auto status = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\HidHide", 0, KEY_READ | KEY_WOW64_64KEY, &raw);
        if (status == ERROR_FILE_NOT_FOUND) return result;
        if (status != ERROR_SUCCESS) throw std::runtime_error("Cannot inspect stored driver ownership");
        ::RegCloseKey(raw);
        status = ::RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Services\\HidHide\\Parameters", 0, KEY_READ | KEY_WOW64_64KEY, &raw);
        if (status != ERROR_SUCCESS) throw std::runtime_error("Stored driver settings are incomplete");
        struct Key { HKEY value; ~Key() { ::RegCloseKey(value); } } key{raw};
        auto flag = [&](PCWSTR name, bool optional)
        {
            DWORD value{}, type{}, size = sizeof(value);
            auto error = ::RegQueryValueExW(key.value, name, nullptr, &type, reinterpret_cast<BYTE*>(&value), &size);
            if (optional && error == ERROR_FILE_NOT_FOUND) return false;
            if (error != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(value) || value > 1) throw std::runtime_error("Invalid stored driver flag");
            return value != 0;
        };
        auto strings = [&](PCWSTR name)
        {
            DWORD size{}, type{};
            if (::RegQueryValueExW(key.value, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_MULTI_SZ || size < sizeof(wchar_t) || size > 1024 * 1024 || size % sizeof(wchar_t))
                throw std::runtime_error("Invalid stored driver list");
            std::vector<wchar_t> data(size / sizeof(wchar_t)); DWORD read = size;
            if (::RegQueryValueExW(key.value, name, nullptr, &type, reinterpret_cast<BYTE*>(data.data()), &read) != ERROR_SUCCESS || read != size || type != REG_MULTI_SZ || data.back() != L'\0')
                throw std::runtime_error("Stored driver list changed");
            return DecodeStoredDriverList(data);
        };
        result.configuration.active = flag(L"Active", false);
        result.configuration.inverse = flag(L"WhitelistedInverse", true);
        auto whitelist = strings(L"WhitelistedFullImageNames");
        result.configuration.whitelist.insert(whitelist.begin(), whitelist.end());
        result.configuration.blacklist = strings(L"BlacklistedDeviceInstancePaths");
        result.storedBaselineAvailable = true;
        return result;
    }
}
