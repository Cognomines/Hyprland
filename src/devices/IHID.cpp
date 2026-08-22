#include "IHID.hpp"
#include "../defines.hpp"

#include <fstream>
#include <format>

namespace {
    // sysfs attributes of the evdev device backing a libinput handle
    inline constexpr std::string_view SYSFS_INPUT_DEVICE_FMT = "/sys/class/input/{}/device/{}";
    // evdev phys strings carry a trailing "/inputN" interface suffix, not part of the stable port chain
    inline constexpr std::string_view EVDEV_PHYS_IFACE_PREFIX = "/input";
}

eHIDType IHID::getType() {
    return HID_TYPE_UNKNOWN;
}

libinput_device* IHID::libinputHandle() const {
    return nullptr;
}

static std::string sysfsInputDeviceString(const char* sysname, const char* file) {
    if (!sysname)
        return "";

    std::ifstream f(std::vformat(SYSFS_INPUT_DEVICE_FMT, std::make_format_args(sysname, file)));
    if (!f.good())
        return "";

    std::string out;
    std::getline(f, out);
    return out;
}

void IHID::fillIdentity() {
    auto* handle = libinputHandle();
    if (!handle)
        return;

    auto& id   = m_identity;
    id.name    = libinput_device_get_name(handle);
    id.vendor  = libinput_device_get_id_vendor(handle);
    id.product = libinput_device_get_id_product(handle);

    const auto SYSNAME = libinput_device_get_sysname(handle);

    // stable physical port chain; strip a trailing /inputN interface suffix
    auto path = sysfsInputDeviceString(SYSNAME, "phys");
    if (!path.empty()) {
        const auto POS = path.rfind(EVDEV_PHYS_IFACE_PREFIX);
        if (POS != std::string::npos && path.find_first_not_of("0123456789", POS + EVDEV_PHYS_IFACE_PREFIX.size()) == std::string::npos)
            path.resize(POS);
        id.idPath = std::move(path);
    }

    id.serial = sysfsInputDeviceString(SYSNAME, "uniq");
}
