#pragma once

#include "../DesktopTypes.hpp"
#include "../../helpers/signal/Signal.hpp"

class CWLSurfaceResource;

namespace Desktop {
    enum eFocusReason : uint8_t {
        FOCUS_REASON_UNKNOWN = 0,
        FOCUS_REASON_FFM,
        FOCUS_REASON_KEYBIND,
        FOCUS_REASON_DISPATCH_FOCUSWINDOW,
        FOCUS_REASON_DISPATCH_MOVEWINDOWINTOGROUP,
        FOCUS_REASON_CLICK,
        FOCUS_REASON_OTHER,
        FOCUS_REASON_DESKTOP_STATE_CHANGE,
        FOCUS_REASON_SWITCH_TO_WINDOW_SOFT,
        FOCUS_REASON_SWITCH_TO_WINDOW_HARD,
        FOCUS_REASON_GROUP_CURRENT_WINDOW_CHANGE,
        FOCUS_REASON_WORKSPACE_CHANGE,
        FOCUS_REASON_TOGGLE_SPECIAL_WORKSPACE,
        FOCUS_REASON_UNMAP_WINDOW_TILING,
        FOCUS_REASON_UNMAP_WINDOW_FLOATING,
        FOCUS_REASON_UNMAP_GROUPED_WINDOW,
        FOCUS_REASON_NEW_WINDOW,
        FOCUS_REASON_GHOSTS,
    };

    bool isHardInputFocusReason(eFocusReason r);

    class CFocusState {
      public:
        CFocusState();
        ~CFocusState() = default;

        CFocusState(CFocusState&&)      = delete;
        CFocusState(CFocusState&)       = delete;
        CFocusState(const CFocusState&) = delete;

        void                   fullWindowFocus(PHLWINDOW w, eFocusReason reason, SP<CWLSurfaceResource> surface = nullptr, bool forceFSCycle = false);
        void                   rawWindowFocus(PHLWINDOW w, eFocusReason reason, SP<CWLSurfaceResource> surface = nullptr);
        void                   rawSurfaceFocus(SP<CWLSurfaceResource> s, PHLWINDOW pWindowOwner = nullptr);
        void                   rawMonitorFocus(PHLMONITOR m);

        void                   resetWindowFocus();

        bool                   isWindowActive(PHLWINDOW w) const;

        SP<CWLSurfaceResource> surface();
        PHLWINDOW              window();
        PHLMONITOR             monitor();

        // logical seats: the focus a bind on a non-default seat should act on.
        // Same as window()/monitor(), except the ambient seat's own focus is
        // preferred over the (default seat) singleton when one exists. Falls
        // back to window()/monitor() semantics everywhere else, so behavior
        // outside a foreign-seat bind scope is unchanged.
        PHLWINDOW  activeWindow();
        PHLMONITOR activeMonitor();

        void                   setAmbientOverride(PHLWINDOW window, PHLMONITOR monitor);
        void                   clearAmbientOverride();

      private:
        WP<CWLSurfaceResource> m_focusSurface;
        PHLWINDOWREF           m_focusWindow;
        PHLMONITORREF          m_focusMonitor;

        PHLWINDOW              m_ambientWindowOverride  = nullptr;
        PHLMONITOR             m_ambientMonitorOverride = nullptr;

        CHyprSignalListener    m_windowOpen, m_windowClose;
    };

    SP<CFocusState> focusState();
};
