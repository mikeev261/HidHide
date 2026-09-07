// SPDX-License-Identifier: MIT
#pragma once
#include "HID.h"

namespace HidHide
{
    struct DeviceSelectionOptions
    {
        bool gamingOnly;
        bool presentOnly;
        // Devices keeps hidden devices visible; profiles honors both filters.
        bool keepSelectedVisible;
    };

    // Checkboxes express independent interface selection intent. Expansion may
    // also hide interfaces sharing an XUSB/container target. Reloading persisted
    // aliases conservatively marks affected editable interfaces as selected.
    struct DeviceSelectionGroup
    {
        std::wstring label;
        std::vector<HidDeviceInformation> devices;
        std::vector<std::wstring> labels;
        std::vector<bool> checked;
        std::vector<bool> originalChecked;
        std::set<DeviceInstancePath> editablePaths;
        std::set<DeviceInstancePath> selectablePaths;
        bool IsSelected(HidDeviceInformation const& device, std::set<DeviceInstancePath> const& paths) const
        {
            return paths.count(device.deviceInstancePath) ||
                (!device.xusbDeviceInstancePath.empty() && paths.count(device.xusbDeviceInstancePath)) ||
                (selectablePaths.count(device.baseContainerDeviceInstancePath) && paths.count(device.baseContainerDeviceInstancePath));
        }
        void Acknowledge() { originalChecked = checked; }
        bool AllChecked() const { return !checked.empty() && std::all_of(checked.begin(), checked.end(), [](bool value) { return value; }); }
        bool AnyChecked() const { return std::any_of(checked.begin(), checked.end(), [](bool value) { return value; }); }
        void SetChecked(size_t child, bool value)
        {
            if (child == devices.size()) std::fill(checked.begin(), checked.end(), value);
            else checked.at(child) = value;
        }
    };

    inline std::vector<DeviceSelectionGroup> BuildDeviceSelection(
        FriendlyNamesAndHidDeviceInformation const& source, std::set<DeviceInstancePath> const& paths,
        DeviceSelectionOptions options)
    {
        std::vector<DeviceSelectionGroup> result;
        std::set<DeviceInstancePath> filteredPaths;
        std::map<std::wstring, size_t> names;
        for (auto const& entry : source) ++names[entry.first.empty() ? L"HID device" : entry.first];
        for (auto const& entry : source)
        {
            if (entry.second.empty()) continue;
            DeviceSelectionGroup group;
            group.devices = entry.second;
            std::set<DeviceInstancePath> allHid;
            for (auto const& device : group.devices) allHid.insert(device.deviceInstancePath);
            group.selectablePaths = HidDevicePathsForSelection(group.devices, allHid);
            // Opaque/unsafe container paths remain retained configuration, outside
            // this selector's editable scope (including unenumerated interfaces).
            group.label = entry.first.empty() ? L"HID device" : entry.first;
            if (names[group.label] > 1)
            {
                // Full instance paths also distinguish identical serial numbers or suffixes.
                group.label += L" \u2014 " + (entry.second.front().baseContainerDeviceInstancePath.empty()
                    ? entry.second.front().deviceInstancePath : entry.second.front().baseContainerDeviceInstancePath);
            }
            std::map<std::wstring, size_t> usages;
            for (auto const& device : entry.second)
            {
                auto label = device.usage.empty() ? device.description : device.usage;
                if (label.empty()) label = L"HID interface";
                ++usages[label];
                group.labels.push_back(std::move(label));
                group.checked.push_back(group.IsSelected(device, paths));
            }
            if (!(options.keepSelectedVisible && group.AnyChecked()))
            {
                if ((options.gamingOnly && std::none_of(entry.second.begin(), entry.second.end(), [](auto const& d) { return d.gamingDevice; })) ||
                    (options.presentOnly && std::none_of(entry.second.begin(), entry.second.end(), [](auto const& d) { return d.present; })))
                {
                    filteredPaths.insert(group.selectablePaths.begin(), group.selectablePaths.end());
                    continue;
                }
            }
            for (size_t i = 0; i < group.devices.size(); ++i)
            {
                if (usages[group.labels[i]] > 1) group.labels[i] += L" \u2014 " + group.devices[i].deviceInstancePath;
                if (!group.devices[i].present) group.labels[i] += L" (disconnected)";
            }
            group.originalChecked = group.checked;
            result.push_back(std::move(group));
        }
        for (auto& group : result)
        {
            group.editablePaths = group.selectablePaths;
            // A shared alias may still serve a filtered-out interface.
            for (auto const& path : filteredPaths) group.editablePaths.erase(path);
        }
        return result;
    }

    inline std::set<DeviceInstancePath> DeviceSelectionPaths(std::set<DeviceInstancePath> retained,
        std::vector<DeviceSelectionGroup> const& groups)
    {
        std::set<DeviceInstancePath> displayed, selected;
        for (auto const& group : groups)
        {
            // Preserve the exact persisted representation of untouched groups.
            if (group.checked == group.originalChecked)
            {
                std::set<DeviceInstancePath> checkedHid;
                for (size_t i = 0; i < group.devices.size(); ++i)
                    if (group.checked[i]) checkedHid.insert(group.devices[i].deviceInstancePath);
                for (auto const& path : HidDevicePathsForSelection(group.devices, checkedHid))
                    if (retained.count(path)) selected.insert(path);
                continue;
            }
            std::set<DeviceInstancePath> checkedHid;
            for (size_t i = 0; i < group.devices.size(); ++i)
            {
                auto const& d = group.devices[i];
                if (group.checked[i]) checkedHid.insert(d.deviceInstancePath);
            }
            displayed.insert(group.editablePaths.begin(), group.editablePaths.end());
            auto const expanded = HidDevicePathsForSelection(group.devices, checkedHid);
            selected.insert(expanded.begin(), expanded.end());
        }
        for (auto const& path : displayed) retained.erase(path);
        retained.insert(selected.begin(), selected.end());
        return retained;
    }
}
