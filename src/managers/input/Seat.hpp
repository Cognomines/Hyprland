#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <vector>

#include "../../defines.hpp"
#include "../../desktop/DesktopTypes.hpp"
#include "../../helpers/math/Math.hpp"
#include "../../helpers/signal/Signal.hpp"
#include "../../input/Keys.hpp"

class IKeyboard;
class IPointer;
class ITouch;
class IHID;
class CTabletTool;
class CTabletPad;
class CTablet;
class CWLSurfaceResource;
class CWLSeatResource;

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
    // true if at least one tracked HID is still alive
    bool hasLiveDevices() const;

    // devices connected to this seat
    std::vector<SP<IKeyboard>>   m_keyboards;
    std::vector<SP<IPointer>>    m_pointers;
    std::vector<SP<ITouch>>      m_touches;
    std::vector<SP<CTablet>>     m_tablets;
    std::vector<SP<CTabletTool>> m_tabletTools;
    std::vector<SP<CTabletPad>>  m_tabletPads;
    // general container for all HID devices connected to this seat
    std::vector<WP<IHID>> m_hids;

    uint32_t              m_capabilities = 0;

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
    // client that received this seat's latest key press: releases must go
    // back there even after focus moved on, or the client repeats forever
    wl_client*          m_pressedClient = nullptr;
    Input::ModifierMask m_lastMods      = Input::HL_MODIFIER_NONE;

    // P4-lite: independent cursor position for non-default seats. The default
    // seat's position lives in CPointerManager::m_pointerPos.
    Vector2D m_cursorPos    = {0, 0};
    bool     m_cursorActive = false;

    // P3-lite: independent input focus for this seat, mirrored from
    // CSeatManager::m_state which remains the default seat's focus
    WP<CWLSurfaceResource> m_keyboardFocus;
    WP<CWLSeatResource>    m_keyboardFocusResource;
    WP<CWLSurfaceResource> m_pointerFocus;
    WP<CWLSeatResource>    m_pointerFocusResource;
    Vector2D               m_lastPointerLocal = {0, 0};
    // window focused by this seat's keyboard, drives per-seat border colors
    PHLWINDOWREF m_focusWindow;
    // window this seat's cursor currently hovers, drives per-seat borders
    PHLWINDOWREF        m_hoverWindow;

    CHyprSignalListener m_kbFocusDestroyListener;
    CHyprSignalListener m_ptrFocusDestroyListener;

  private:
    std::string m_name;
    bool        m_isDefault = false;
};
