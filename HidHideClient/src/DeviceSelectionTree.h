// SPDX-License-Identifier: MIT
#pragma once
#include "DeviceSelection.h"
#include "Logging.h"

// Shared tree rendering and parent/interface checkbox rules. The owning dialog
// suppresses TVN_ITEMCHANGED while building/updating and handles persistence.
class DeviceSelectionTree
{
public:
    enum class Presentation { DeviceLocks, ProfileCheckboxes };
    void Build(CTreeCtrl& tree, HidHide::FriendlyNamesAndHidDeviceInformation const& devices,
        std::set<HidHide::DeviceInstancePath> const& selected, HidHide::DeviceSelectionOptions options,
        Presentation presentation)
    {
        auto groups = HidHide::BuildDeviceSelection(devices, selected, options);
        tree.DeleteAllItems();
        m_Groups = std::move(groups);
        m_Items.clear();
        m_Presentation = presentation;
        for (size_t index = 0; index < m_Groups.size(); ++index)
        {
            auto const& group = m_Groups[index];
            auto parent = tree.InsertItem(group.label.c_str());
            if (!parent) THROW_WIN32(ERROR_INVALID_PARAMETER);
            m_Items[parent] = { index, group.devices.size() };
            for (size_t child = 0; child < group.devices.size(); ++child)
            {
                auto item = tree.InsertItem(group.labels[child].c_str(), parent);
                if (!item) THROW_WIN32(ERROR_INVALID_PARAMETER);
                m_Items[item] = { index, child };
            }
        }
        Render(tree);
        // Both views begin collapsed. Background refresh must not move focus.
    }

    bool Change(CTreeCtrl& tree, HTREEITEM item)
    {
        auto const found = m_Items.find(item);
        if (found == m_Items.end()) return false;
        // Read the control first, including rollback restored by the owning dialog.
        Read(tree);
        m_Groups[found->second.first].SetChecked(found->second.second, FALSE != tree.GetCheck(item));
        Render(tree);
        return true;
    }

    std::set<HidHide::DeviceInstancePath> Selection(CTreeCtrl& tree, std::set<HidHide::DeviceInstancePath> retained)
    {
        Read(tree);
        return HidHide::DeviceSelectionPaths(std::move(retained), m_Groups);
    }

    void Acknowledge()
    {
        for (auto& group : m_Groups) group.Acknowledge();
    }

    size_t SelectedInterfaces(std::set<HidHide::DeviceInstancePath> const& selected) const
    {
        size_t count{};
        for (auto const& group : m_Groups)
            for (auto const& device : group.devices)
                if (group.IsSelected(device, selected)) ++count;
        return count;
    }
private:
    void Read(CTreeCtrl& tree)
    {
        for (auto const& [item, index] : m_Items)
            if (index.second < m_Groups[index.first].devices.size())
                m_Groups[index.first].checked[index.second] = FALSE != tree.GetCheck(item);
    }
    void Render(CTreeCtrl& tree)
    {
        for (auto const& [item, index] : m_Items)
        {
            auto const& group = m_Groups[index.first];
            bool const parent = index.second == group.devices.size();
            tree.SetCheck(item, (parent ? group.AllChecked() : group.checked[index.second]) ? TRUE : FALSE);
            if (m_Presentation == Presentation::DeviceLocks)
            {
                int const icon = parent ? (group.AnyChecked() ? 2 : 1) : 0;
                tree.SetItemImage(item, icon, icon);
            }
        }
    }
    std::vector<HidHide::DeviceSelectionGroup> m_Groups;
    std::map<HTREEITEM, std::pair<size_t, size_t>> m_Items;
    Presentation m_Presentation{ Presentation::ProfileCheckboxes };
};
