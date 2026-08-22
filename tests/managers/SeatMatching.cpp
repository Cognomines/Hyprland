#include <managers/input/SeatMatching.hpp>
#include <config/ConfigManager.hpp>

#include <gtest/gtest.h>

using namespace Config;

// These tests are hardware-independent: every input is constructed by hand and
// run through pure functions. Realistic literals below (device names, VID:PID,
// sysfs paths) mirror production shapes so the data resembles real devices;
// they document observed conventions, not a required setup.

namespace {
    SSeatMatchInput makeInput(std::string_view hlName = "at-translated-set-2-keyboard", std::string_view deviceName = "AT Translated Set 2 keyboard") {
        // both names as libinput reports them for the emulated i8042 keyboard present on any Linux system
        SSeatMatchInput in;
        in.hlName     = hlName;
        in.deviceName = deviceName;
        return in;
    }
}

TEST(SeatMatching, wildcardExactAndSuffixGlobs) {
    EXPECT_TRUE(wildcardMatch("abc", "abc"));
    EXPECT_FALSE(wildcardMatch("abc", "abd"));
    EXPECT_TRUE(wildcardMatch("AT*", "AT Translated"));
    EXPECT_FALSE(wildcardMatch("AT*", "BT Translated"));
    EXPECT_TRUE(wildcardMatch("*Translated", "AT Translated"));
    // middle '*' = containment; "usb-0:14" must appear verbatim inside
    EXPECT_TRUE(wildcardMatch("*usb-0:14*", "pci-0000:00:14.0-usb-0:14:1.0"));
    EXPECT_FALSE(wildcardMatch("*usb-0:14*", "usb-0000:01:00.0-0:15:1.0"));
    EXPECT_TRUE(wildcardMatch("*", ""));
    EXPECT_TRUE(wildcardMatch("", ""));
    EXPECT_FALSE(wildcardMatch("", "x"));
    EXPECT_TRUE(wildcardMatch("a*b*c", "aXbYc"));
    EXPECT_TRUE(wildcardMatch("**b**", "ab"));
    // regex metacharacters are literal
    EXPECT_TRUE(wildcardMatch("a.c", "a.c"));
    EXPECT_FALSE(wildcardMatch("a.c", "abc"));
}

TEST(SeatMatching, hexTokensAcceptCasePrefixAndStar) {
    EXPECT_TRUE(hexTokenMatches("*", 0x062A));
    EXPECT_TRUE(hexTokenMatches("062a", 0x062a));
    EXPECT_TRUE(hexTokenMatches("062A", 0x062a));
    EXPECT_TRUE(hexTokenMatches("62a", 0x062a));
    EXPECT_TRUE(hexTokenMatches("0x062a", 0x062a));
    EXPECT_FALSE(hexTokenMatches("062b", 0x062a));
    EXPECT_FALSE(hexTokenMatches("xyz", 0x062a));
    EXPECT_FALSE(hexTokenMatches("062ax", 0x062a));
    EXPECT_FALSE(hexTokenMatches("", 0));
    EXPECT_TRUE(hexTokenMatches("ffff", 0xffff));
}

TEST(SeatMatching, matcherFieldsANDTogether) {
    SSeatMatchInput in = makeInput();
    in.vendor          = 0x062a; // VID:PID of a real keyboard (Chicony 062a:0201)
    in.product         = 0x0201;
    in.idPath          = "usb-0000:01:00.0-0:9:1.0"; // sysfs phys shape: bus-port.port-config.interface
    std::set<std::string> tags{"builtin"};
    in.tags = &tags;

    SSeatMatcher m;
    m.name = "AT*";
    m.vid  = "062a";
    m.pid  = "*";
    m.path = "*-0:9:*";
    m.tag  = "builtin";

    EXPECT_TRUE(seatMatcherMatches(m, in));

    // one failing field rejects the whole entry
    SSeatMatcher wrongVid = m;
    wrongVid.vid          = "1234";
    EXPECT_FALSE(seatMatcherMatches(wrongVid, in));

    SSeatMatcher wrongTag = m;
    wrongTag.tag          = "external";
    EXPECT_FALSE(seatMatcherMatches(wrongTag, in));

    // no tags at all fails any tag requirement
    std::set<std::string> noTags;
    in.tags = &noTags;
    EXPECT_FALSE(seatMatcherMatches(m, in));
    in.tags = nullptr;
    EXPECT_FALSE(seatMatcherMatches(m, in));
}

// separate case: unset fields are ignored entirely
TEST(SeatMatching, unsetMatcherFieldsAreIgnored) {
    SSeatMatchInput in = makeInput();

    SSeatMatcher    empty;
    EXPECT_TRUE(seatMatcherMatches(empty, in));

    SSeatMatcher vidOnly;
    vidOnly.vid = "062a";
    in.vendor   = 0x062a;
    EXPECT_TRUE(seatMatcherMatches(vidOnly, in));

    in.vendor = 0x1234;
    EXPECT_FALSE(seatMatcherMatches(vidOnly, in));
}

TEST(SeatMatching, nameFallsBackFromHlNameToDeviceName) {
    SSeatMatchInput in = makeInput("logitech-mx-master", "Logitech MX Master");

    SSeatMatcher    byHlName;
    byHlName.name = "logitech-mx*";
    EXPECT_TRUE(seatMatcherMatches(byHlName, in));

    SSeatMatcher byRawName;
    byRawName.name = "*MX Master";
    EXPECT_TRUE(seatMatcherMatches(byRawName, in));

    SSeatMatcher neither;
    neither.name = "keychron*";
    EXPECT_FALSE(seatMatcherMatches(neither, in));
}

TEST(SeatMatching, seatConfigListForMapsDeviceTypes) {
    SSeatConfig cfg;

    EXPECT_EQ(seatConfigListFor(cfg, HID_TYPE_KEYBOARD), &cfg.keyboards);
    EXPECT_EQ(seatConfigListFor(cfg, HID_TYPE_POINTER), &cfg.pointers);
    EXPECT_EQ(seatConfigListFor(cfg, HID_TYPE_TOUCH), &cfg.touches);
    EXPECT_EQ(seatConfigListFor(cfg, HID_TYPE_TABLET), &cfg.tablets);
    EXPECT_EQ(seatConfigListFor(cfg, HID_TYPE_TABLET_TOOL), &cfg.tablets);
    EXPECT_EQ(seatConfigListFor(cfg, HID_TYPE_TABLET_PAD), &cfg.tablets);
    EXPECT_EQ(seatConfigListFor(cfg, HID_TYPE_UNKNOWN), nullptr);
}

TEST(SeatMatching, libinputDefaultSeatNamesMapToOurDefault) {
    EXPECT_TRUE(isDefaultLibinputSeatName(""));
    EXPECT_TRUE(isDefaultLibinputSeatName("default"));
    EXPECT_TRUE(isDefaultLibinputSeatName("seat0"));
    EXPECT_FALSE(isDefaultLibinputSeatName("seat1"));
    EXPECT_FALSE(isDefaultLibinputSeatName("Hyprland"));
}
