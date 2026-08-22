#include "SeatMatching.hpp"
#include "../../config/ConfigManager.hpp"

#include <array>
#include <charconv>

bool wildcardMatch(std::string_view pattern, std::string_view str) {
    size_t p = 0, s = 0, starP = std::string_view::npos, starS = 0;

    while (s < str.size()) {
        if (p < pattern.size() && pattern[p] == str[s]) {
            ++p;
            ++s;
        } else if (p < pattern.size() && pattern[p] == '*') {
            starP = p++;
            starS = s;
        } else if (starP != std::string_view::npos) {
            p = starP + 1;
            s = ++starS;
        } else
            return false;
    }

    while (p < pattern.size() && pattern[p] == '*') {
        ++p;
    }

    return p == pattern.size();
}

bool hexTokenMatches(std::string_view pattern, uint16_t value) {
    if (pattern == "*")
        return true;

    if (pattern.starts_with("0x") || pattern.starts_with("0X"))
        pattern.remove_prefix(2);

    unsigned parsed      = 0;
    const auto [end, ec] = std::from_chars(pattern.data(), pattern.data() + pattern.size(), parsed, 16);

    return ec == std::errc{} && end == pattern.data() + pattern.size() && parsed == value;
}

namespace {
    // matcher fields are std::string; keep the param type matching so set::contains stays usable
    using TFieldCheck = bool (*)(const SSeatMatchInput&, const std::string&);

    // pairs each matcher field with its check; the single source of truth for
    // which identifiers a hl.seat{} entry can constrain and how.
    // Adding an identity field = member in SSeatMatcher + SSeatMatchInput + one row here.
    struct SSeatFieldCheck {
        std::string Config::SSeatMatcher::* field;
        TFieldCheck                         check;
    };

    inline constexpr std::array SEAT_FIELD_CHECKS{
        SSeatFieldCheck{&Config::SSeatMatcher::name,
                        [](const SSeatMatchInput& in, const std::string& v) { return wildcardMatch(v, in.hlName) || wildcardMatch(v, in.deviceName); }},
        SSeatFieldCheck{&Config::SSeatMatcher::vid, [](const SSeatMatchInput& in, const std::string& v) { return hexTokenMatches(v, in.vendor); }},
        SSeatFieldCheck{&Config::SSeatMatcher::pid, [](const SSeatMatchInput& in, const std::string& v) { return hexTokenMatches(v, in.product); }},
        SSeatFieldCheck{&Config::SSeatMatcher::path, [](const SSeatMatchInput& in, const std::string& v) { return wildcardMatch(v, in.idPath); }},
        SSeatFieldCheck{&Config::SSeatMatcher::tag, [](const SSeatMatchInput& in, const std::string& v) { return in.tags && in.tags->contains(v); }},
        SSeatFieldCheck{&Config::SSeatMatcher::serial, [](const SSeatMatchInput& in, const std::string& v) { return wildcardMatch(v, in.serial); }},
    };
}

bool seatMatcherMatches(const Config::SSeatMatcher& matcher, const SSeatMatchInput& input) {
    for (const auto& [field, check] : SEAT_FIELD_CHECKS) {
        const auto& value = matcher.*field;
        if (!value.empty() && !check(input, value))
            return false;
    }

    return true;
}

const std::vector<Config::SSeatMatcher>* seatConfigListFor(const Config::SSeatConfig& cfg, eHIDType type) {
    const auto IDX = sc<size_t>(type);

    // indexed by eHIDType; keep in enum order
    static constexpr std::array<const std::vector<Config::SSeatMatcher> Config::SSeatConfig::*, 7> TYPE_LISTS{
        nullptr,                         // HID_TYPE_UNKNOWN
        &Config::SSeatConfig::pointers,  // HID_TYPE_POINTER
        &Config::SSeatConfig::keyboards, // HID_TYPE_KEYBOARD
        &Config::SSeatConfig::touches,   // HID_TYPE_TOUCH
        &Config::SSeatConfig::tablets,   // HID_TYPE_TABLET
        &Config::SSeatConfig::tablets,   // HID_TYPE_TABLET_TOOL
        &Config::SSeatConfig::tablets,   // HID_TYPE_TABLET_PAD
    };

    if (IDX >= TYPE_LISTS.size() || !TYPE_LISTS[IDX])
        return nullptr;

    return &(cfg.*TYPE_LISTS[IDX]);
}

bool isDefaultLibinputSeatName(const std::string& name) {
    return name.empty() || name == LIBINPUT_DEFAULT_SEAT_NAME || name == LOGIND_PRIMARY_SEAT_NAME;
}
