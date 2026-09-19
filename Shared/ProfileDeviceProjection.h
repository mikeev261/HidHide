// SPDX-License-Identifier: MIT
#pragma once

#include "HID.h"
#include <cwctype>

namespace HidHide::Profiles
{
    struct ProfileHidUsage
    {
        bool known{};
        USHORT page{}, usage{};
    };
    struct ProfileDeviceGroup
    {
        std::wstring identity, friendly;
        bool connected{};
        std::vector<std::wstring> policyIdentities;
        std::wstring kind{L"unknown"};
        std::vector<ProfileHidUsage> hidUsages;
    };

    // Presentation only: use already-enumerated product/usage information.
    // Classification never broadens the policy paths or changes hiding behavior.
    inline std::wstring ProfileDeviceKind(std::wstring name, std::vector<HidDeviceInformation> const& devices)
    {
        for (auto const& device : devices) name += L" " + device.product + L" " + device.description;
        std::transform(name.begin(), name.end(), name.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        auto has = [&](wchar_t const* word) { return name.find(word) != std::wstring::npos; };
        if (has(L"stream deck") || has(L"streamdeck") || has(L"button box") || has(L"macro pad")) return L"keypad";
        if (has(L"handbrake")) return L"handbrake";
        if (has(L"pedal") || has(L"heusinkveld") && (has(L"sprint") || has(L"ultimate"))) return L"pedals";
        if (has(L"steering") || has(L"wheel") || has(L"simucube")) return L"wheel";
        if (has(L"webcam") || has(L"camera") || has(L"brio")) return L"camera";
        if (has(L"headphone") || has(L"headset") || has(L"arctis")) return L"headphones";
        if (has(L"microphone")) return L"microphone";
        if (has(L"speaker")) return L"speaker";
        if (has(L"keyboard")) return L"keyboard";
        if (has(L"mouse") || has(L"trackball") || has(L"touchpad")) return L"mouse";
        if (has(L"joystick") || has(L"hotas") || has(L"flight stick")) return L"joystick";
        if (has(L"gamepad") || has(L"xbox") || has(L"dualshock") || has(L"dualsense")) return L"gamepad";
        std::wstring usage;
        for (auto const& device : devices) usage += L" " + device.usage;
        std::transform(usage.begin(), usage.end(), usage.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        if (usage.find(L"keyboard") != std::wstring::npos) return L"keyboard";
        if (usage.find(L"mouse") != std::wstring::npos) return L"mouse";
        if (usage.find(L"joystick") != std::wstring::npos) return L"joystick";
        if (std::any_of(devices.begin(), devices.end(), [](auto const& device) { return device.gamingDevice; })) return L"gamepad";
        return L"unknown";
    }

    inline std::vector<ProfileDeviceGroup> ProjectProfileDeviceGroups(FriendlyNamesAndHidDeviceInformation const& source)
    {
        std::vector<ProfileDeviceGroup> result;
        for (auto const& [friendly, devices] : source)
        {
            std::set<DeviceInstancePath> selectedHid;
            for (auto const& device : devices) if (!device.deviceInstancePath.empty()) selectedHid.emplace(device.deviceInstancePath);
            auto expanded = HidDevicePathsForSelection(devices, selectedHid); if (expanded.empty()) continue;
            auto connected = std::any_of(devices.begin(), devices.end(), [](auto const& device) { return device.present; });
            auto label = friendly + (expanded.size() > 1
                ? L" — composite group (" + std::to_wstring(expanded.size()) + L" required paths)"
                : L" — individual HID interface");
            result.push_back({ *expanded.begin(), std::move(label), connected, { expanded.begin(), expanded.end() }, ProfileDeviceKind(friendly, devices) });
            for (auto const& device : devices)
                result.back().hidUsages.push_back({device.usageKnown, device.usagePage, device.usageId});
        }
        return result;
    }
}
