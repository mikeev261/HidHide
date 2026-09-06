// (c) Eric Korff de Gidts
// SPDX-License-Identifier: MIT
// HID.h
#pragma once
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace HidHide
{
    typedef std::wstring DeviceInstancePath;

    struct HidDeviceInformation
    {
        bool                  present;
        bool                  gamingDevice;
        std::filesystem::path symbolicLink;
        std::wstring          vendor;
        std::wstring          product;
        std::wstring          serialNumber;
        std::wstring          usage;
        std::wstring          description;
        DeviceInstancePath    deviceInstancePath;
        DeviceInstancePath    xusbDeviceInstancePath;
        DeviceInstancePath    baseContainerDeviceInstancePath;
        GUID                  baseContainerClassGuid;
        size_t                baseContainerDeviceCount;
    };

    typedef std::multimap<std::wstring, std::vector<HidDeviceInformation>> FriendlyNamesAndHidDeviceInformation;

    // Get the device instance paths of the HID devices (present or not) and associated device information and cluster/group them on their device friendly names
    FriendlyNamesAndHidDeviceInformation HidDevices(_In_ bool gamingDevicesOnly);

    // Expand selected HID child paths to every path required to hide those interfaces correctly,
    // including associated XInput paths and a safe base-container path when the whole device is selected.
    inline std::set<DeviceInstancePath> HidDevicePathsForSelection(
        _In_ std::vector<HidDeviceInformation> const& devices,
        _In_ std::set<DeviceInstancePath> const& selectedHidDevicePaths)
    {
        constexpr GUID hidClass{ 0x745a17a0, 0x74d3, 0x11d0, {0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda} };
        constexpr GUID xusbClass{ 0xd61ca365, 0x5af4, 0x4486, {0x99, 0x8b, 0x9d, 0xb4, 0x73, 0x4c, 0x6c, 0xa3} };
        std::set<DeviceInstancePath> result;

        for (auto const& device : devices)
        {
            if (selectedHidDevicePaths.end() == selectedHidDevicePaths.find(device.deviceInstancePath)) continue;

            result.emplace(device.deviceInstancePath);
            if (!device.xusbDeviceInstancePath.empty()) result.emplace(device.xusbDeviceInstancePath);
        }

        if (devices.empty() || !std::all_of(devices.begin(), devices.end(), [&selectedHidDevicePaths](auto const& device)
        {
            return selectedHidDevicePaths.end() != selectedHidDevicePaths.find(device.deviceInstancePath);
        })) return result;

        auto const& first{ devices.front() };
        if (first.baseContainerDeviceInstancePath.empty()) return result;

        if ((hidClass == first.baseContainerClassGuid) || (xusbClass == first.baseContainerClassGuid))
        {
            if (devices.size() == first.baseContainerDeviceCount)
                result.emplace(first.baseContainerDeviceInstancePath);
        }

        return result;
    }
}
