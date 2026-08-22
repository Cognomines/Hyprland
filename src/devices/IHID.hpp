#pragma once

#include <cstdint>
#include <string>
#include <set>
#include "../defines.hpp"
#include "../helpers/signal/Signal.hpp"

struct libinput_device;

class CSeat;

enum eHIDCapabilityType : uint8_t {
    HID_INPUT_CAPABILITY_KEYBOARD = (1 << 0),
    HID_INPUT_CAPABILITY_POINTER  = (1 << 1),
    HID_INPUT_CAPABILITY_TOUCH    = (1 << 2),
    HID_INPUT_CAPABILITY_TABLET   = (1 << 3),
};

enum eHIDType : uint8_t {
    HID_TYPE_UNKNOWN = 0,
    HID_TYPE_POINTER,
    HID_TYPE_KEYBOARD,
    HID_TYPE_TOUCH,
    HID_TYPE_TABLET,
    HID_TYPE_TABLET_TOOL,
    HID_TYPE_TABLET_PAD,
};

/*
    Base class for a HID device.
    This could be a keyboard, a mouse, or a touchscreen.
*/
struct SDeviceIdentity {
    std::string name;      // kernel-reported device name
    uint16_t    vendor  = 0;
    uint16_t    product = 0;
    std::string idPath;    // stable physical port chain (udev ID_PATH or evdev phys)
    std::string serial;    // unit serial if reported, else empty
};

class IHID {
  public:
    virtual ~IHID() = default;

    virtual uint32_t        getCapabilities() = 0;
    virtual eHIDType        getType();
    virtual libinput_device* libinputHandle() const;

    // fills m_identity from the kernel via the libinput handle; no-op for virtual devices
    void fillIdentity();

    struct {
        CSignalT<> destroy;
    } m_events;

    std::string           m_deviceName;
    std::string           m_hlName;
    std::set<std::string> m_deviceTags;
    SDeviceIdentity       m_identity;
    WP<CSeat>             m_seat;
};
