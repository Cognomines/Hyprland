#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "../../defines.hpp"
#include "../../input/Keys.hpp"

class IKeyboard;
class IPointer;
class ITouch;
class IHID;
class CTabletTool;
class CTabletPad;
class CTablet;

inline const std::string DEFAULT_SEAT_NAME = "Hyprland";

/*
    A logical seat. Groups input devices together with the state that belongs
    to them.

    Until seats are configurable (see logicalseats.md), exactly one implicit
    default seat exists and holds everything.
*/
class CSeat {
  public:
    CSeat(const std::string& name, bool isDefault);
    ~CSeat();

    const std::string& name() const;
    bool               isDefault() const;

    // devices connected to this seat
    std::vector<SP<IKeyboard>>   m_keyboards;
    std::vector<SP<IPointer>>    m_pointers;
    std::vector<SP<ITouch>>      m_touches;
    std::vector<SP<CTablet>>     m_tablets;
    std::vector<SP<CTabletTool>> m_tabletTools;
    std::vector<SP<CTabletPad>>  m_tabletPads;
    // general container for all HID devices connected to this seat
    std::vector<WP<IHID>>        m_hids;

    uint32_t m_capabilities = 0;

    struct SHeldPointerButton {
        uint32_t     button = 0;
        WP<IPointer> pointer;
    };

    // for holding focus while buttons are held / releasing mouse buttons
    bool                          m_focusHeldByButtons   = false;
    bool                          m_refocusHeldByButtons = false;
    std::list<SHeldPointerButton> m_currentlyHeldButtons;

    // discrete scrolling emulation using v120 data
    struct {
        bool     lastEventSign     = false;
        bool     lastEventAxis     = false;
        uint32_t lastEventTime     = 0;
        uint32_t accumulatedScroll = 0;
    } m_scrollWheelState;
    bool m_pointerAxisFramePending = false;

    // shared key/mod state across this seat's keyboards,
    // do not write directly
    std::vector<uint32_t> m_pressed;
    Input::ModifierMask   m_lastMods = Input::HL_MODIFIER_NONE;

  private:
    std::string m_name;
    bool        m_isDefault = false;
};
