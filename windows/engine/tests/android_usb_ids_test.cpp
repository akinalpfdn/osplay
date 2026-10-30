#include "usb/android_usb_ids.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using osplay::engine::usb::accessory_kind;
using osplay::engine::usb::AccessoryKind;
using osplay::engine::usb::cable_action;
using osplay::engine::usb::CableAction;
using osplay::engine::usb::parse_usb_ids;
using osplay::engine::usb::split_instance_id;
using osplay::engine::usb::UsbIds;

namespace {

constexpr UsbIds kGalaxyS23{.vendor_id = 0x04E8, .product_id = 0x6860};

// What Windows reported for a Galaxy S23 in its normal mode, USB debugging off.
const std::vector<std::wstring> kPhoneIds = {L"USB\\MS_COMP_MTP",
                                             L"USB\\Class_ff&SubClass_ff&Prot_00",
                                             L"USB\\Class_02&SubClass_02&Prot_01"};

} // namespace

TEST(AndroidUsbIds, VendorAndProductComeFromAnInstanceId) {
    EXPECT_EQ(parse_usb_ids(L"USB\\VID_04E8&PID_6860\\RFCW714678P"), kGalaxyS23);
    EXPECT_EQ(parse_usb_ids(L"USB\\Vid_18d1&Pid_2d01&MI_00"),
              (UsbIds{.vendor_id = 0x18D1, .product_id = 0x2D01}));
}

TEST(AndroidUsbIds, AnIdWithoutVendorAndProductIsRejected) {
    EXPECT_FALSE(parse_usb_ids(L"ROOT\\DISPLAY\\0000"));
    EXPECT_FALSE(parse_usb_ids(L"USB\\VID_04E8&PID_68"));
    EXPECT_FALSE(parse_usb_ids(L"USB\\VID_ZZZZ&PID_6860"));
}

TEST(AndroidUsbIds, AnInstanceIdSplitsTheWayUsbDkNamesDevices) {
    const auto parts = split_instance_id(L"USB\\VID_04E8&PID_6860\\RFCW714678P");
    ASSERT_TRUE(parts);
    EXPECT_EQ(parts->device_id, L"USB\\VID_04E8&PID_6860");
    EXPECT_EQ(parts->instance_id, L"RFCW714678P");
    EXPECT_FALSE(split_instance_id(L"USB\\VID_04E8&PID_6860\\"));
    EXPECT_FALSE(split_instance_id(L"no-slash"));
}

TEST(AndroidUsbIds, TheAccessoryIsToldFromTheOneWithUsbDebugging) {
    EXPECT_EQ(accessory_kind({.vendor_id = 0x18D1, .product_id = 0x2D00}),
              AccessoryKind::Accessory);
    EXPECT_EQ(accessory_kind({.vendor_id = 0x18D1, .product_id = 0x2D01}),
              AccessoryKind::AccessoryWithAdb);
    EXPECT_EQ(accessory_kind(kGalaxyS23), AccessoryKind::None);
}

TEST(AndroidUsbIds, APhoneWindowsKnowsAsMtpIsSwitched) {
    EXPECT_EQ(cable_action(kGalaxyS23, kPhoneIds), CableAction::Switch);
}

TEST(AndroidUsbIds, APhoneWithOnlyPtpIsSwitched) {
    EXPECT_EQ(
        cable_action(kGalaxyS23, std::vector<std::wstring>{L"usb\\class_06&subclass_01&prot_01"}),
        CableAction::Switch);
}

TEST(AndroidUsbIds, APhoneWithUsbDebuggingOnIsReportedAndNotSwitched) {
    std::vector<std::wstring> ids = kPhoneIds;
    ids.emplace_back(L"USB\\Class_ff&SubClass_42&Prot_01");
    EXPECT_EQ(cable_action(kGalaxyS23, ids), CableAction::DebuggingIsOn);
    EXPECT_EQ(
        cable_action(kGalaxyS23, std::vector<std::wstring>{L"usb\\class_ff&subclass_42&prot_01"}),
        CableAction::DebuggingIsOn);
}

TEST(AndroidUsbIds, KeyboardsHubsAndDrivesAreLeftAlone) {
    const UsbIds keyboard{.vendor_id = 0x046D, .product_id = 0xC232};
    EXPECT_EQ(
        cable_action(keyboard, std::vector<std::wstring>{L"USB\\Class_03&SubClass_01&Prot_01"}),
        CableAction::Leave);
    EXPECT_EQ(cable_action(keyboard, std::vector<std::wstring>{L"USB\\Class_09&SubClass_00"}),
              CableAction::Leave);
    EXPECT_EQ(
        cable_action(keyboard, std::vector<std::wstring>{L"USB\\Class_08&SubClass_06&Prot_50"}),
        CableAction::Leave);
    EXPECT_EQ(cable_action(keyboard, {}), CableAction::Leave);
}

TEST(AndroidUsbIds, IPhonesAndAccessoriesAreLeftAlone) {
    const UsbIds iphone{.vendor_id = 0x05AC, .product_id = 0x12A8};
    EXPECT_EQ(cable_action(iphone, std::vector<std::wstring>{L"USB\\Class_06&SubClass_01&Prot_01"}),
              CableAction::Leave);
    EXPECT_EQ(cable_action({.vendor_id = 0x18D1, .product_id = 0x2D00}, kPhoneIds),
              CableAction::Leave);
}
