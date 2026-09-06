// SPDX-License-Identifier: MIT
#include <gtest/gtest.h>
#include "DeviceSelection.h"
#include "../HidHideClient/src/PendingDeviceRefresh.h"
using namespace HidHide;
namespace
{
    constexpr GUID HidClass{0x745a17a0, 0x74d3, 0x11d0, {0xb6, 0xfe, 0x00, 0xa0, 0xc9, 0x0f, 0x57, 0xda}};
    constexpr GUID XusbClass{0xd61ca365, 0x5af4, 0x4486, {0x99, 0x8b, 0x9d, 0xb4, 0x73, 0x4c, 0x6c, 0xa3}};
    HidDeviceInformation Device(std::wstring path, std::wstring container = L"base")
    {
        HidDeviceInformation d{};
        d.deviceInstancePath = path;
        d.baseContainerDeviceInstancePath = container;
        d.baseContainerClassGuid = HidClass;
        d.baseContainerDeviceCount = 2;
        d.present = d.gamingDevice = true;
        d.usage = L"Gamepad";
        return d;
    }
    FriendlyNamesAndHidDeviceInformation Composite()
    {
        return {{L"Controller", {Device(L"hid1"), Device(L"hid2")}}};
    }
}
TEST(DeviceSelection, PartialAndFullCompositeUseProductionExpansion)
{
    auto groups = BuildDeviceSelection(Composite(), {}, {false, false, false});
    groups[0].SetChecked(0, true);
    EXPECT_TRUE(groups[0].AnyChecked());
    EXPECT_FALSE(groups[0].AllChecked());
    EXPECT_EQ((std::set<DeviceInstancePath>{L"hid1"}), DeviceSelectionPaths({}, groups));
    groups[0].SetChecked(1, true);
    EXPECT_TRUE(groups[0].AllChecked());
    EXPECT_EQ((std::set<DeviceInstancePath>{L"base", L"hid1", L"hid2"}), DeviceSelectionPaths({}, groups));
    groups[0].SetChecked(2, false);
    EXPECT_FALSE(groups[0].AnyChecked());
}
TEST(DeviceSelection, ContainerOnlyCanBePartiallyDeselectedWithoutTouchingUnknownPaths)
{
    std::set<DeviceInstancePath> original{L"base", L"not-enumerated"};
    auto groups = BuildDeviceSelection(Composite(), original, {false, false, false});
    EXPECT_TRUE(groups[0].AllChecked());
    EXPECT_EQ(original, DeviceSelectionPaths(original, groups));
    groups[0].SetChecked(1, false);
    EXPECT_EQ((std::set<DeviceInstancePath>{L"hid1", L"not-enumerated"}), DeviceSelectionPaths(original, groups));
}
TEST(DeviceSelection, XusbAliasesSelectOnlyTheirAssociatedInterfaces)
{
    auto source = Composite();
    auto& devices = source.begin()->second;
    devices[0].xusbDeviceInstancePath = L"xusb1";
    devices[1].xusbDeviceInstancePath = L"xusb2";
    devices[0].baseContainerClassGuid = devices[1].baseContainerClassGuid = XusbClass;
    auto groups = BuildDeviceSelection(source, {L"xusb1"}, {false, false, false});
    EXPECT_TRUE(groups[0].checked[0]);
    EXPECT_FALSE(groups[0].checked[1]);
    groups[0].SetChecked(2, true);
    EXPECT_EQ((std::set<DeviceInstancePath>{L"base", L"hid1", L"hid2", L"xusb1", L"xusb2"}),
        DeviceSelectionPaths({L"xusb1"}, groups));
}
TEST(DeviceSelection, IncompleteAndUnsafeContainersRemainOutsideEditableScope)
{
    // These opaque paths can affect unenumerated/non-HID interfaces. Retain them
    // without presenting them as editable HID checkboxes or broadening selection.
    for (bool wrongClass : {false, true})
    {
        auto source = Composite();
        for (auto& device : source.begin()->second)
        {
            if (wrongClass) device.baseContainerClassGuid = GUID{};
            else device.baseContainerDeviceCount = 3;
        }
        auto groups = BuildDeviceSelection(source, {L"base"}, {false, false, false});
        EXPECT_FALSE(groups[0].AnyChecked());
        groups[0].SetChecked(2, true);
        EXPECT_EQ((std::set<DeviceInstancePath>{L"hid1", L"hid2"}),
            HidDevicePathsForSelection(source.begin()->second, {L"hid1", L"hid2"}));
        EXPECT_EQ((std::set<DeviceInstancePath>{L"base", L"hid1", L"hid2"}), DeviceSelectionPaths({L"base"}, groups));
    }
}
TEST(DeviceSelection, FilteringRetainsSelectedAndUnenumeratedPaths)
{
    auto source = Composite();
    auto other = Device(L"offline", L"other");
    other.present = other.gamingDevice = false;
    source.emplace(L"Other", std::vector<HidDeviceInformation>{other});
    std::set<DeviceInstancePath> original{L"offline", L"unknown"};
    auto profiles = BuildDeviceSelection(source, original, {true, true, false});
    ASSERT_EQ(1u, profiles.size());
    profiles[0].SetChecked(0, true);
    EXPECT_EQ((std::set<DeviceInstancePath>{L"hid1", L"offline", L"unknown"}), DeviceSelectionPaths(original, profiles));
    auto devices = BuildDeviceSelection(source, original, {true, true, true});
    EXPECT_EQ(2u, devices.size());
}
TEST(DeviceSelection, FilteredGroupKeepsItsSharedXusbAlias)
{
    auto online = Device(L"online", L"one");
    auto offline = Device(L"offline", L"two");
    online.xusbDeviceInstancePath = offline.xusbDeviceInstancePath = L"shared";
    offline.present = false;
    FriendlyNamesAndHidDeviceInformation source{{L"One", {online}}, {L"Two", {offline}}};
    auto groups = BuildDeviceSelection(source, {L"shared"}, {false, true, false});
    ASSERT_EQ(1u, groups.size());
    groups[0].SetChecked(0, false);
    EXPECT_EQ((std::set<DeviceInstancePath>{L"shared"}), DeviceSelectionPaths({L"shared"}, groups));
}
TEST(DeviceSelection, AmbiguousParentAndFallbackInterfaceLabelsUseFullIdentity)
{
    auto first = Device(L"HID\\A\\same", L"USB\\A\\same");
    auto second = Device(L"HID\\B\\same", L"USB\\B\\same");
    first.serialNumber = second.serialNumber = L"same serial";
    first.usage.clear(); second.usage.clear();
    first.description = second.description = L"Identical";
    FriendlyNamesAndHidDeviceInformation source{{L"Controller", {first, second}}, {L"Controller", {second}}};
    auto groups = BuildDeviceSelection(source, {}, {false, false, false});
    ASSERT_EQ(2u, groups.size());
    EXPECT_NE(groups[0].label, groups[1].label);
    EXPECT_NE(groups[0].labels[0], groups[0].labels[1]);
}
TEST(DeviceSelection, EmptyEnumerationPreservesAllConfiguration)
{
    auto groups = BuildDeviceSelection({}, {L"unplugged"}, {true, true, true});
    EXPECT_EQ((std::set<DeviceInstancePath>{L"unplugged"}), DeviceSelectionPaths({L"unplugged"}, groups));
}

TEST(DeviceSelection, AcknowledgedRepeatedEditsAndRollbackUseLastSavedChecks)
{
    auto groups = BuildDeviceSelection(Composite(), {}, {false, false, false});
    groups[0].SetChecked(0, true);
    auto saved = DeviceSelectionPaths({}, groups);
    groups[0].Acknowledge();
    groups[0].SetChecked(0, false);
    EXPECT_TRUE(DeviceSelectionPaths(saved, groups).empty());
    // A failed write leaves the acknowledgement intact; the UI restores these checks.
    groups[0].checked = groups[0].originalChecked;
    groups[0].SetChecked(1, true);
    EXPECT_EQ((std::set<DeviceInstancePath>{L"base", L"hid1", L"hid2"}), DeviceSelectionPaths(saved, groups));
}
TEST(DeviceSelection, UntouchedDisplayedGroupKeepsSharedAlias)
{
    auto one = Device(L"one", L"first");
    auto two = Device(L"two", L"second");
    one.xusbDeviceInstancePath = two.xusbDeviceInstancePath = L"shared";
    auto groups = BuildDeviceSelection({{L"One", {one}}, {L"Two", {two}}}, {L"shared"}, {false, false, false});
    groups[0].SetChecked(0, false);
    EXPECT_EQ((std::set<DeviceInstancePath>{L"shared"}), DeviceSelectionPaths({L"shared"}, groups));
}

TEST(DeviceSelection, SequentialSharedAliasDeselectionClearsLastRequirement)
{
    auto one = Device(L"one", L"first");
    auto two = Device(L"two", L"second");
    one.xusbDeviceInstancePath = two.xusbDeviceInstancePath = L"shared";
    auto groups = BuildDeviceSelection({{L"One", {one}}, {L"Two", {two}}}, {L"shared"}, {false, false, false});
    groups[0].SetChecked(0, false);
    auto saved = DeviceSelectionPaths({L"shared"}, groups);
    EXPECT_EQ((std::set<DeviceInstancePath>{L"shared"}), saved);
    for (auto& group : groups) group.Acknowledge();
    groups[1].SetChecked(0, false);
    EXPECT_TRUE(DeviceSelectionPaths(saved, groups).empty());
}

TEST(DeviceSelection, TopologyRefreshCoalescesBurstsAndRetriesUntilRecovery)
{
    PendingDeviceRefresh pending;
    unsigned attempts = 0;
    bool unavailable = true;
    auto enumerate = [&]
    {
        ++attempts;
        if (unavailable) throw std::runtime_error("transient enumeration or driver contention");
    };
    pending.RunIfDue(0, enumerate);
    pending.Request(100);
    pending.Request(300);
    pending.RunIfDue(599, enumerate);
    EXPECT_EQ(0u, attempts);
    EXPECT_THROW(pending.RunIfDue(600, enumerate), std::runtime_error);
    pending.Request(700);
    pending.RunIfDue(1099, enumerate);
    EXPECT_EQ(1u, attempts);
    EXPECT_THROW(pending.RunIfDue(1100, enumerate), std::runtime_error);
    unavailable = false;
    pending.RunIfDue(1600, enumerate);
    pending.RunIfDue(3000, enumerate);
    EXPECT_EQ(3u, attempts);
    pending.Request(3100);
    pending.RunIfDue(3600, enumerate);
    EXPECT_EQ(4u, attempts);
}

TEST(DeviceSelection, SuccessfulUserRefreshSatisfiesPendingTopologyRefresh)
{
    PendingDeviceRefresh pending;
    pending.Request(100);
    pending.Complete();
    pending.RunIfDue(600, [] { FAIL() << "User refresh already read the latest topology"; });
}
