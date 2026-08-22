#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "../../devices/IHID.hpp"

namespace Config {
    struct SSeatMatcher;
    struct SSeatConfig;
}

// everything a hl.seat{} matcher can be tested against, decoupled from live devices
struct SSeatMatchInput {
    std::string_view             hlName;
    std::string_view             deviceName;
    uint16_t                     vendor  = 0;
    uint16_t                     product = 0;
    std::string_view             idPath;
    std::string_view             serial;
    const std::set<std::string>* tags = nullptr;
};

// '*' matches any sequence, including an empty one
bool wildcardMatch(std::string_view pattern, std::string_view str);

// "*" matches any id; otherwise the token must fully parse as hex (0x prefix allowed)
bool hexTokenMatches(std::string_view pattern, uint16_t value);

// all fields set on the matcher must match; unset fields are ignored
bool seatMatcherMatches(const Config::SSeatMatcher& matcher, const SSeatMatchInput& input);

// the hl.seat{} device list relevant for a HID type, or nullptr
const std::vector<Config::SSeatMatcher>* seatConfigListFor(const Config::SSeatConfig& cfg, eHIDType type);

// libinput's logical seat name for devices without a WL_SEAT udev tag
inline constexpr std::string_view LIBINPUT_DEFAULT_SEAT_NAME = "default";
// logind's primary session seat id; some setups tag devices with it instead of "default"
inline constexpr std::string_view LOGIND_PRIMARY_SEAT_NAME = "seat0";

// these libinput names mean "no explicit seat"; they all map to our default seat
bool isDefaultLibinputSeatName(const std::string& name);
