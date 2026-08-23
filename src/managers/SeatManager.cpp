#include "SeatManager.hpp"
#include "../protocols/core/Seat.hpp"
#include "../protocols/core/DataDevice.hpp"
#include "../protocols/DataDeviceWlr.hpp"
#include "../protocols/ExtDataDevice.hpp"
#include "../protocols/PrimarySelection.hpp"
#include "../protocols/core/Compositor.hpp"
#include "../protocols/LayerShell.hpp"
#include "../protocols/InputCapture.hpp"
#include "../Compositor.hpp"
#include "../desktop/state/FocusState.hpp"
#include "../devices/IKeyboard.hpp"
#include "../desktop/view/LayerSurface.hpp"
#include "../desktop/view/WLSurface.hpp"
#include "../desktop/view/window/Window.hpp"
#include "../managers/input/InputManager.hpp"
#include "../state/MonitorState.hpp"
#include "devices/IHID.hpp"
#include "input/SeatMatching.hpp"
#include "input/SeatContext.hpp"
#include "wlr-layer-shell-unstable-v1.hpp"
#include <algorithm>
#include <hyprutils/utils/ScopeGuard.hpp>
#include <ranges>

using namespace Hyprutils::Utils;

static bool surfaceInTree(SP<CWLSurfaceResource> root, SP<CWLSurfaceResource> surface) {
    if (!root || !surface)
        return false;

    if (root == surface)
        return true;

    return !!root->findFirstPreorder([surface](SP<CWLSurfaceResource> candidate) { return candidate == surface; });
}

CSeatManager::CSeatManager() {
    m_seats.emplace_back(makeShared<CSeat>(DEFAULT_SEAT_NAME, true));

    m_listeners.newSeatResource = PROTO::seat->m_events.newSeatResource.listen([this](const auto& resource) { onNewSeatResource(resource); });
}

SP<CSeat> CSeatManager::defaultSeat() {
    return m_seats.front();
}

std::vector<SP<CSeat>>& CSeatManager::seats() {
    return m_seats;
}

SP<CSeat> CSeatManager::seatByName(const std::string& name) {
    for (auto const& s : m_seats) {
        if (s->name() == name)
            return s;
    }

    return nullptr;
}

SP<CSeat> CSeatManager::ensureSeat(const std::string& name) {
    if (isDefaultLibinputSeatName(name) || name == DEFAULT_SEAT_NAME)
        return defaultSeat();

    if (const auto SEAT = seatByName(name); SEAT)
        return SEAT;

    Log::logger->log(Log::DEBUG, "seatManager: creating implicit seat {}", name);
    return m_seats.emplace_back(makeShared<CSeat>(name, false));
}

CSeatManager::SSeatResourceContainer::SSeatResourceContainer(SP<CWLSeatResource> res) : resource(res) {
    listeners.destroy = res->m_events.destroy.listen(
        [this] { std::erase_if(g_pSeatManager->m_seatResources, [this](const auto& e) { return e->resource.expired() || e->resource == resource; }); });
}

void CSeatManager::onNewSeatResource(SP<CWLSeatResource> resource) {
    m_seatResources.emplace_back(makeShared<SSeatResourceContainer>(resource));
}

SP<CSeatManager::SSeatResourceContainer> CSeatManager::containerForResource(SP<CWLSeatResource> seatResource) {
    for (auto const& c : m_seatResources) {
        if (c->resource == seatResource)
            return c;
    }

    return nullptr;
}

uint32_t CSeatManager::nextSerial(SP<CWLSeatResource> seatResource, bool enter) {
    if (!seatResource)
        return 0;

    auto container = containerForResource(seatResource);

    ASSERT(container);

    auto serial = wl_display_next_serial(g_pCompositor->m_wlDisplay);

    if (enter)
        container->enterSerial = serial;
    else {
        container->serials.emplace_back(serial);

        if (container->serials.size() > MAX_SERIAL_STORE_LEN)
            container->serials.erase(container->serials.begin());
    }

    return serial;
}

bool CSeatManager::serialValid(SP<CWLSeatResource> seatResource, uint32_t serial, bool erase) {
    if (!seatResource)
        return false;

    auto container = containerForResource(seatResource);

    ASSERT(container);

    if (container->enterSerial == serial)
        return true;

    for (auto it = container->serials.begin(); it != container->serials.end(); ++it) {
        if (*it == serial) {
            if (erase)
                container->serials.erase(it);
            return true;
        }
    }

    return false;
}

void CSeatManager::recordPointerButtonSerial(SP<CWLSeatResource> seatResource, uint32_t serial, SP<CWLSurfaceResource> surface, uint32_t button) {
    if (!seatResource || !surface || !serial)
        return;

    auto container = containerForResource(seatResource);

    ASSERT(container);

    container->pointerButtonSerials.emplace_back(SPointerButtonSerial{
        .serial  = serial,
        .button  = button,
        .surface = surface,
    });

    if (container->pointerButtonSerials.size() > MAX_SERIAL_STORE_LEN)
        container->pointerButtonSerials.erase(container->pointerButtonSerials.begin());
}

void CSeatManager::clearPointerButtonSerials(SP<CWLSeatResource> seatResource, SP<CWLSurfaceResource> surface, uint32_t button) {
    if (!seatResource || !surface)
        return;

    auto container = containerForResource(seatResource);

    ASSERT(container);

    std::erase_if(container->pointerButtonSerials, [surface, button](const auto& serial) { return serial.button == button && surfaceInTree(surface, serial.surface.lock()); });
}

bool CSeatManager::pointerButtonSerialValid(SP<CWLSeatResource> seatResource, uint32_t serial, SP<CWLSurfaceResource> surface, bool erase) {
    if (!seatResource || !surface || !serial)
        return false;

    auto container = containerForResource(seatResource);

    ASSERT(container);

    for (auto it = container->pointerButtonSerials.begin(); it != container->pointerButtonSerials.end(); ++it) {
        if (it->serial != serial)
            continue;

        const bool VALID = surfaceInTree(surface, it->surface.lock());
        if (erase)
            container->pointerButtonSerials.erase(it);

        return VALID;
    }

    return false;
}

void CSeatManager::updateCapabilities(uint32_t capabilities) {
    PROTO::seat->updateCapabilities(capabilities);
}

void CSeatManager::setMouse(SP<IPointer> MAUZ) {
    if (m_mouse == MAUZ)
        return;

    m_mouse = MAUZ;
}

void CSeatManager::setKeyboard(SP<IKeyboard> KEEB) {
    if (m_keyboard == KEEB)
        return;

    if (m_keyboard)
        m_keyboard->m_active = false;
    m_keyboard = KEEB;

    if (KEEB)
        KEEB->m_active = true;

    updateActiveKeyboardData();
}

void CSeatManager::updateActiveKeyboardData() {
    if (m_keyboard)
        PROTO::seat->updateRepeatInfo(m_keyboard->m_repeatRate, m_keyboard->m_repeatDelay);
    PROTO::seat->updateKeymap();
    PROTO::inputCapture->updateKeymap();
}

// P3-lite: resources bound by clients of seats other than OWNER are skipped,
// so default-seat focus changes never stomp other seats' enters
static bool resourceOwnedBy(const WP<CWLSeatResource>& res, SP<CSeat> owner) {
    const auto SEAT = res ? res->m_owner.lock() : nullptr;
    if (!SEAT)
        return !owner || owner->isDefault(); // untagged (shouldn't happen) counts as default
    return SEAT == owner;
}

// P3-lite delivery fallback: true when another seat currently drives this
// resource through a fallback enter — its owner must not sendLeave() and
// break that seat's focus
static bool resourceHeldByOtherSeat(CSeatManager* mgr, const WP<CWLSeatResource>& res, const SP<CSeat>& self) {
    if (!res)
        return false;

    for (auto const& s : mgr->seats()) {
        if (!s || s == self)
            continue;

        const bool KB = s->m_keyboardFocusResource.lock() == res && s->m_keyboardFocus;
        const bool PT = s->m_pointerFocusResource.lock() == res && s->m_pointerFocus;
        if (KB || PT)
            return true;
    }

    return false;
}

void CSeatManager::setKeyboardFocus(SP<CSeat> seat, SP<CWLSurfaceResource> surf) {
    if (!seat || seat->isDefault()) {
        setKeyboardFocusDefault(surf);
        return;
    }

    if (surf && seat->m_keyboards.empty()) {
        Log::logger->log(Log::ERR, "setKeyboardFocus for seat {} without a keyboard", seat->name());
        return;
    }

    if (seat->m_keyboardFocus == surf)
        return;

    seat->m_kbFocusDestroyListener.reset();

    // P3-lite delivery fallback: leave resources owned by this seat, plus
    // any resource we may have entered through another seat when the client
    // had no seat-owned resources; sendLeave() no-ops elsewhere
    wl_client* OLDC = seat->m_keyboardFocus ? seat->m_keyboardFocus->client() : nullptr;
    for (auto const& r : m_seatResources) {
        const bool OWNED = r->resource->m_owner.lock() == seat;
        if (!OWNED && (!OLDC || r->resource->client() != OLDC))
            continue;
        if (!OWNED && r->resource->m_owner.lock() && r->resource->m_owner.lock()->isDefault() && resourceHeldByOtherSeat(this, r->resource, seat))
            continue;

        for (auto const& k : r->resource->m_keyboards) {
            if (!k)
                continue;

            k->sendMods(0, 0, 0, 0);
            k->sendLeave();
        }
    }

    seat->m_keyboardFocusResource.reset();
    seat->m_keyboardFocus = surf;

    if (!surf) {
        m_events.keyboardFocusChange.emit();
        return;
    }

    wl_array keys;
    wl_array_init(&keys);
    CScopeGuard x([&keys] { wl_array_release(&keys); });

    const auto& PRESSED = seat->m_pressed;
    static_assert(std::is_same_v<std::decay_t<decltype(PRESSED)>::value_type, uint32_t>, "Element type different from keycode type uint32_t");

    const auto PRESSEDARRSIZE = PRESSED.size() * sizeof(uint32_t);
    if (PRESSEDARRSIZE > 0) {
        const auto PKEYS = wl_array_add(&keys, PRESSEDARRSIZE);
        if (PKEYS)
            std::ranges::copy(PRESSED, sc<uint32_t*>(PKEYS));
    }

    auto client = surf->client();

    // enter through this seat's own resources when the client has them,
    // falling back to whatever it does have (e.g. a bar spawned on the
    // default seat is still clickable/typable from other seats)
    bool hasOwned = false;
    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() == client && r->resource->m_owner.lock() == seat) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() != client)
            continue;
        if (hasOwned && r->resource->m_owner.lock() != seat)
            continue;

        seat->m_keyboardFocusResource = r->resource;
        for (auto const& k : r->resource->m_keyboards) {
            if (!k)
                continue;

            k->sendEnter(surf, &keys);
            uint32_t depressed = 0;
            uint32_t latched   = 0;
            uint32_t locked    = 0;
            for (auto const& kb : seat->m_keyboards) {
                if (!kb->m_enabled || !kb->shareStates() || (kb->isVirtual() && g_pInputManager->shouldIgnoreVirtualKeyboard(kb)))
                    continue;
                depressed |= kb->m_modifiersState.depressed;
                latched |= kb->m_modifiersState.latched;
                locked |= kb->m_modifiersState.locked;
            }
            k->sendMods(depressed, latched, locked, 0);
        }
    }

    seat->m_kbFocusDestroyListener = surf->m_events.destroy.listen([this, seat] { setKeyboardFocus(seat, nullptr); });

    m_events.keyboardFocusChange.emit();
}

void CSeatManager::setKeyboardFocus(SP<CWLSurfaceResource> surf) {
    setKeyboardFocus(Input::ambientSeat(), surf);
}

static PHLWINDOW windowFromFocusSurface(SP<CWLSurfaceResource> surf) {
    if (!surf)
        return nullptr;

    const auto HLSURF = Desktop::View::CWLSurface::fromResource(surf);
    if (!HLSURF)
        return nullptr;

    return Desktop::View::CWindow::fromView(HLSURF->view());
}

bool CSeatManager::isWindowKeyboardFocusedAnywhere(PHLWINDOW window) {
    if (!window)
        return false;

    if (windowFromFocusSurface(m_state.keyboardFocus.lock()) == window)
        return true;

    for (auto const& s : m_seats) {
        if (!s || s->isDefault())
            continue;

        if (windowFromFocusSurface(s->m_keyboardFocus.lock()) == window)
            return true;
    }

    return false;
}

void CSeatManager::setKeyboardFocusDefault(SP<CWLSurfaceResource> surf) {
    if (m_state.keyboardFocus == surf)
        return;

    if (!g_pInputManager->anyHidHasCap(HID_INPUT_CAPABILITY_KEYBOARD)) {
        Log::logger->log(Log::ERR, "BUG THIS: setKeyboardFocus without a valid keyboard capability");
        return;
    }

    m_listeners.keyboardSurfaceDestroy.reset();

    // Don't gate leave on m_state.keyboardFocusResource — the WP can
    // be stale. sendLeave no-ops on keyboards without m_currentSurface.
    // Delivery fallback: also leave resources we may have entered through
    // another seat when the client had no default-owned resources.
    wl_client* OLDC = m_state.keyboardFocus ? m_state.keyboardFocus->client() : nullptr;
    for (auto const& k : PROTO::seat->m_keyboards) {
        if (!k)
            continue;

        const bool OWNED    = resourceOwnedBy(k->m_owner, defaultSeat());
        const auto OWNERRES = k->m_owner.lock();
        const bool ONOLDCLI = OLDC && OWNERRES && OWNERRES->client() == OLDC;
        if (!OWNED && !ONOLDCLI)
            continue;
        if (!OWNED && resourceHeldByOtherSeat(this, OWNERRES, defaultSeat()))
            continue;

        k->sendMods(0, m_keyboard->m_modifiersState.latched, m_keyboard->m_modifiersState.locked, m_keyboard->m_modifiersState.group);
        k->sendLeave();
    }

    m_state.keyboardFocusResource.reset();
    m_state.keyboardFocus = surf;

    if (!surf) {
        m_events.keyboardFocusChange.emit();
        return;
    }

    wl_array keys;
    wl_array_init(&keys);
    CScopeGuard x([&keys] { wl_array_release(&keys); });

    const auto& PRESSED = g_pInputManager->getKeysFromAllKBs();
    static_assert(std::is_same_v<std::decay_t<decltype(PRESSED)>::value_type, uint32_t>, "Element type different from keycode type uint32_t");

    const auto PRESSEDARRSIZE = PRESSED.size() * sizeof(uint32_t);
    if (PRESSEDARRSIZE > 0) {
        const auto PKEYS = wl_array_add(&keys, PRESSEDARRSIZE);
        if (PKEYS)
            std::ranges::copy(PRESSED, sc<uint32_t*>(PKEYS));
    }

    auto client = surf->client();

    // delivery fallback: enter through default-owned resources when the
    // client has them, else through whatever resources it does have
    // (e.g. a terminal spawned on seat1 is still usable from the default
    // seat and vice versa)
    bool hasOwned = false;
    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() == client && resourceOwnedBy(r->resource, defaultSeat())) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() != client)
            continue;
        if (hasOwned && !resourceOwnedBy(r->resource, defaultSeat()))
            continue;

        m_state.keyboardFocusResource = r->resource;
        for (auto const& k : r->resource->m_keyboards) {
            if (!k)
                continue;

            k->sendEnter(surf, &keys);
            uint32_t depressed = m_keyboard->m_modifiersState.depressed;
            uint32_t latched   = m_keyboard->m_modifiersState.latched;
            uint32_t locked    = m_keyboard->m_modifiersState.locked;
            for (auto const& kb : defaultSeat()->m_keyboards) {
                if (!kb->m_enabled || !kb->shareStates() || (kb->isVirtual() && g_pInputManager->shouldIgnoreVirtualKeyboard(kb)))
                    continue;
                depressed |= kb->m_modifiersState.depressed;
                latched |= kb->m_modifiersState.latched;
                locked |= kb->m_modifiersState.locked;
            }
            k->sendMods(depressed, latched, locked, m_keyboard->m_modifiersState.group);
        }
    }

    m_listeners.keyboardSurfaceDestroy = surf->m_events.destroy.listen([this] { setKeyboardFocus(nullptr); });

    m_events.keyboardFocusChange.emit();
}

void CSeatManager::sendKeyboardKey(SP<CSeat> seat, uint32_t timeMs, uint32_t key, wl_keyboard_key_state state_) {
    const auto FOCUS = (!seat || seat->isDefault()) ? m_state.keyboardFocusResource.lock() : seat->m_keyboardFocusResource.lock();
    if (!FOCUS)
        return;

    // P3-lite delivery fallback: prefer this seat's own resources; a client
    // with none of them (e.g. a bar spawned on another seat) is still served
    // through whatever resources it does have
    bool hasOwned = false;
    for (auto const& s : m_seatResources) {
        if (s->resource->client() == FOCUS->client() && s->resource->m_owner.lock() == seat) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != FOCUS->client())
            continue;
        if (hasOwned && s->resource->m_owner.lock() != seat)
            continue;

        for (auto const& k : s->resource->m_keyboards) {
            if (!k)
                continue;

            k->sendKey(timeMs, key, state_);
        }
    }
}

void CSeatManager::sendKeyboardKey(uint32_t timeMs, uint32_t key, wl_keyboard_key_state state) {
    sendKeyboardKey(Input::ambientSeat(), timeMs, key, state);
}

void CSeatManager::sendKeyboardMods(SP<CSeat> seat, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    const auto FOCUS = (!seat || seat->isDefault()) ? m_state.keyboardFocusResource.lock() : seat->m_keyboardFocusResource.lock();
    if (!FOCUS)
        return;

    // P3-lite delivery fallback: see sendKeyboardKey
    bool hasOwned = false;
    for (auto const& s : m_seatResources) {
        if (s->resource->client() == FOCUS->client() && s->resource->m_owner.lock() == seat) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != FOCUS->client())
            continue;
        if (hasOwned && s->resource->m_owner.lock() != seat)
            continue;

        for (auto const& k : s->resource->m_keyboards) {
            if (!k)
                continue;

            k->sendMods(depressed, latched, locked, group);
        }
    }
}

void CSeatManager::sendKeyboardMods(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    sendKeyboardMods(Input::ambientSeat(), depressed, latched, locked, group);
}

void CSeatManager::setPointerFocus(SP<CSeat> seat, SP<CWLSurfaceResource> surf, const Vector2D& local) {
    if (!seat || seat->isDefault()) {
        setPointerFocusDefault(surf, local);
        return;
    }

    const bool dndActive = PROTO::data && PROTO::data->dndActive();
    if (dndActive)
        return; // foreign seats don't participate in the global dnd focus (P3-lite)

    if (seat->m_pointerFocus == surf)
        return;

    if (surf && seat->m_pointers.empty()) {
        Log::logger->log(Log::ERR, "setPointerFocus for seat {} without a pointer", seat->name());
        return;
    }

    seat->m_ptrFocusDestroyListener.reset();

    // P3-lite delivery fallback: see setKeyboardFocus
    wl_client* OLDC = seat->m_pointerFocus ? seat->m_pointerFocus->client() : nullptr;
    for (auto const& r : m_seatResources) {
        const bool OWNED = r->resource->m_owner.lock() == seat;
        if (!OWNED && (!OLDC || r->resource->client() != OLDC))
            continue;
        if (!OWNED && r->resource->m_owner.lock() && r->resource->m_owner.lock()->isDefault() && resourceHeldByOtherSeat(this, r->resource, seat))
            continue;

        for (auto const& p : r->resource->m_pointers) {
            if (!p)
                continue;

            p->sendLeave();
        }
    }

    auto lastPointerFocusResource = seat->m_pointerFocusResource.lock();

    seat->m_pointerFocusResource.reset();
    seat->m_pointerFocus = surf;

    if (!surf) {
        sendPointerFrame(lastPointerFocusResource);
        return;
    }

    auto client = surf->client();

    bool hasOwned = false;
    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() == client && r->resource->m_owner.lock() == seat) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() != client)
            continue;
        if (hasOwned && r->resource->m_owner.lock() != seat)
            continue;

        seat->m_pointerFocusResource = r->resource;
        for (auto const& p : r->resource->m_pointers) {
            if (!p)
                continue;

            p->sendEnter(surf, local);
        }
    }

    if (seat->m_pointerFocusResource != lastPointerFocusResource)
        sendPointerFrame(lastPointerFocusResource);

    sendPointerFrame(seat);

    seat->m_ptrFocusDestroyListener = surf->m_events.destroy.listen([this, seat] { setPointerFocus(seat, nullptr, {}); });
}

void CSeatManager::setPointerFocus(SP<CWLSurfaceResource> surf, const Vector2D& local) {
    setPointerFocus(Input::ambientSeat(), surf, local);
}

void CSeatManager::setPointerFocusDefault(SP<CWLSurfaceResource> surf, const Vector2D& local) {
    const bool dndActive = PROTO::data && PROTO::data->dndActive();

    if (m_state.pointerFocus == surf)
        return;

    if (dndActive && surf) {
        if (m_state.dndPointerFocus == surf)
            return;
        Log::logger->log(Log::DEBUG, "[seatmgr] Refusing pointer focus during an active dnd, but setting dndPointerFocus");
        m_state.dndPointerFocus = surf;
        m_events.dndPointerFocusChange.emit();
        return;
    }

    if (!g_pInputManager->anyHidHasCap(HID_INPUT_CAPABILITY_POINTER)) {
        Log::logger->log(Log::ERR, "BUG THIS: setPointerFocus without a valid pointer input");
        return;
    }

    m_listeners.pointerSurfaceDestroy.reset();

    // delivery fallback: also leave resources we may have entered through
    // another seat when the client had no default-owned resources
    wl_client* OLDC = m_state.pointerFocus ? m_state.pointerFocus->client() : nullptr;
    for (auto const& p : PROTO::seat->m_pointers) {
        if (!p)
            continue;

        const bool OWNED    = resourceOwnedBy(p->m_owner, defaultSeat());
        const auto OWNERRES = p->m_owner.lock();
        const bool ONOLDCLI = OLDC && OWNERRES && OWNERRES->client() == OLDC;
        if (!OWNED && !ONOLDCLI)
            continue;
        if (!OWNED && resourceHeldByOtherSeat(this, OWNERRES, defaultSeat()))
            continue;

        p->sendLeave();
    }

    auto lastPointerFocusResource = m_state.pointerFocusResource;

    m_state.dndPointerFocus.reset();
    m_state.pointerFocusResource.reset();
    m_state.pointerFocus = surf;

    if (!surf) {
        sendPointerFrame(lastPointerFocusResource);
        m_events.pointerFocusChange.emit();
        return;
    }

    m_state.dndPointerFocus = surf;

    auto client = surf->client();

    // delivery fallback: prefer default-owned resources, else use whatever
    // the client has (e.g. a terminal spawned on seat1 is still usable from
    // the default seat and vice versa)
    bool hasOwned = false;
    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() == client && resourceOwnedBy(r->resource, defaultSeat())) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() != client)
            continue;
        if (hasOwned && !resourceOwnedBy(r->resource, defaultSeat()))
            continue;

        m_state.pointerFocusResource = r->resource;
        for (auto const& p : r->resource->m_pointers) {
            if (!p)
                continue;

            p->sendEnter(surf, local);
        }
    }

    if (m_state.pointerFocusResource != lastPointerFocusResource)
        sendPointerFrame(lastPointerFocusResource);

    sendPointerFrame();

    m_listeners.pointerSurfaceDestroy = surf->m_events.destroy.listen([this] { setPointerFocus(nullptr, {}); });

    m_events.pointerFocusChange.emit();
    m_events.dndPointerFocusChange.emit();
}

void CSeatManager::sendPointerMotion(SP<CSeat> seat, uint32_t timeMs, const Vector2D& local) {
    const auto FOCUS = (!seat || seat->isDefault()) ? m_state.pointerFocusResource.lock() : seat->m_pointerFocusResource.lock();
    if (!FOCUS)
        return;

    // P3-lite delivery fallback: see sendKeyboardKey
    bool hasOwned = false;
    for (auto const& s : m_seatResources) {
        if (s->resource->client() == FOCUS->client() && s->resource->m_owner.lock() == seat) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != FOCUS->client())
            continue;
        if (hasOwned && s->resource->m_owner.lock() != seat)
            continue;

        for (auto const& p : s->resource->m_pointers) {
            if (!p)
                continue;

            p->sendMotion(timeMs, local);
        }
    }

    if (!seat || seat->isDefault())
        m_lastLocalCoords = local;
}

void CSeatManager::sendPointerMotion(uint32_t timeMs, const Vector2D& local) {
    sendPointerMotion(Input::ambientSeat(), timeMs, local);
}

void CSeatManager::sendPointerButton(SP<CSeat> seat, uint32_t timeMs, uint32_t key, wl_pointer_button_state state_) {
    if (PROTO::data && PROTO::data->dndActive())
        return;

    const auto FOCUS = (!seat || seat->isDefault()) ? m_state.pointerFocusResource.lock() : seat->m_pointerFocusResource.lock();
    if (!FOCUS)
        return;

    // P3-lite delivery fallback: see sendKeyboardKey
    bool hasOwned = false;
    for (auto const& s : m_seatResources) {
        if (s->resource->client() == FOCUS->client() && s->resource->m_owner.lock() == seat) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != FOCUS->client())
            continue;
        if (hasOwned && s->resource->m_owner.lock() != seat)
            continue;

        for (auto const& p : s->resource->m_pointers) {
            if (!p)
                continue;

            p->sendButton(timeMs, key, state_);
        }
    }
}

void CSeatManager::sendPointerButton(uint32_t timeMs, uint32_t key, wl_pointer_button_state state) {
    sendPointerButton(Input::ambientSeat(), timeMs, key, state);
}

void CSeatManager::sendPointerFrame() {
    sendPointerFrame(Input::ambientSeat());
}

void CSeatManager::sendPointerFrame(SP<CSeat> seat) {
    const auto FOCUS = (!seat || seat->isDefault()) ? m_state.pointerFocusResource.lock() : seat->m_pointerFocusResource.lock();
    sendPointerFrame(FOCUS);
}

void CSeatManager::sendPointerFrame(WP<CWLSeatResource> pResource) {
    if (!pResource)
        return;

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != pResource->client())
            continue;

        for (auto const& p : s->resource->m_pointers) {
            if (!p)
                continue;

            p->sendFrame();
        }
    }
}

void CSeatManager::sendPointerAxis(SP<CSeat> seat, uint32_t timeMs, wl_pointer_axis axis, double value, int32_t discrete, int32_t value120, wl_pointer_axis_source source,
                                   wl_pointer_axis_relative_direction relative) {
    const auto FOCUS = (!seat || seat->isDefault()) ? m_state.pointerFocusResource.lock() : seat->m_pointerFocusResource.lock();
    if (!FOCUS)
        return;

    // P3-lite delivery fallback: see sendKeyboardKey
    bool hasOwned = false;
    for (auto const& s : m_seatResources) {
        if (s->resource->client() == FOCUS->client() && s->resource->m_owner.lock() == seat) {
            hasOwned = true;
            break;
        }
    }

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != FOCUS->client())
            continue;
        if (hasOwned && s->resource->m_owner.lock() != seat)
            continue;

        for (auto const& p : s->resource->m_pointers) {
            if (!p)
                continue;

            p->sendAxis(timeMs, axis, value);
            p->sendAxisSource(source);
            p->sendAxisRelativeDirection(axis, relative);

            if (source == 0) {
                if (p->version() >= 8)
                    p->sendAxisValue120(axis, value120);
                else
                    p->sendAxisDiscrete(axis, discrete);
            } else if (value == 0)
                p->sendAxisStop(timeMs, axis);
        }
    }
}

void CSeatManager::sendPointerAxis(uint32_t timeMs, wl_pointer_axis axis, double value, int32_t discrete, int32_t value120, wl_pointer_axis_source source,
                                   wl_pointer_axis_relative_direction relative) {
    sendPointerAxis(Input::ambientSeat(), timeMs, axis, value, discrete, value120, source, relative);
}

void CSeatManager::sendTouchDown(SP<CWLSurfaceResource> surf, uint32_t timeMs, int32_t id, const Vector2D& local) {
    m_listeners.touchSurfaceDestroy.reset();

    m_state.touchFocusResource.reset();
    m_state.touchFocus = surf;

    auto client = surf->client();
    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() != client)
            continue;

        m_state.touchFocusResource = r->resource;
        for (auto const& t : r->resource->m_touches) {
            if (!t)
                continue;

            t->sendDown(surf, timeMs, id, local);
        }
    }

    m_listeners.touchSurfaceDestroy = surf->m_events.destroy.listen([this, timeMs, id] { sendTouchUp(timeMs + 10, id); });

    m_touchLocks++;

    if (m_touchLocks <= 1)
        m_events.touchFocusChange.emit();
}

void CSeatManager::sendTouchUp(uint32_t timeMs, int32_t id) {
    if (!m_state.touchFocusResource || m_touchLocks <= 0)
        return;

    auto client = m_state.touchFocusResource->client();
    for (auto const& r : m_seatResources | std::views::reverse) {
        if (r->resource->client() != client)
            continue;

        m_state.touchFocusResource = r->resource;
        for (auto const& t : r->resource->m_touches) {
            if (!t)
                continue;

            t->sendUp(timeMs, id);
        }
    }

    m_touchLocks--;

    if (m_touchLocks <= 0)
        m_events.touchFocusChange.emit();
}

void CSeatManager::sendTouchMotion(uint32_t timeMs, int32_t id, const Vector2D& local) {
    if (!m_state.touchFocusResource)
        return;

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != m_state.touchFocusResource->client())
            continue;

        for (auto const& t : s->resource->m_touches) {
            if (!t)
                continue;

            t->sendMotion(timeMs, id, local);
        }
    }
}

void CSeatManager::sendTouchFrame() {
    if (!m_state.touchFocusResource)
        return;

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != m_state.touchFocusResource->client())
            continue;

        for (auto const& t : s->resource->m_touches) {
            if (!t)
                continue;

            t->sendFrame();
        }
    }
}

void CSeatManager::sendTouchCancel() {
    if (!m_state.touchFocusResource)
        return;

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != m_state.touchFocusResource->client())
            continue;

        for (auto const& t : s->resource->m_touches) {
            if (!t)
                continue;

            t->sendCancel();
        }
    }
}

void CSeatManager::sendTouchShape(int32_t id, const Vector2D& shape) {
    if (!m_state.touchFocusResource)
        return;

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != m_state.touchFocusResource->client())
            continue;

        for (auto const& t : s->resource->m_touches) {
            if (!t)
                continue;

            t->sendShape(id, shape);
        }
    }
}

void CSeatManager::sendTouchOrientation(int32_t id, double angle) {
    if (!m_state.touchFocusResource)
        return;

    for (auto const& s : m_seatResources) {
        if (s->resource->client() != m_state.touchFocusResource->client())
            continue;

        for (auto const& t : s->resource->m_touches) {
            if (!t)
                continue;

            t->sendOrientation(id, angle);
        }
    }
}

void CSeatManager::refocusGrab() {
    if (!m_seatGrab)
        return;

    if (!m_seatGrab->m_surfs.empty()) {
        // try to find a surf in focus first
        const auto MOUSE = g_pInputManager->getMouseCoordsInternal();
        for (auto const& s : m_seatGrab->m_surfs) {
            auto hlSurf = Desktop::View::CWLSurface::fromResource(s.lock());
            if (!hlSurf)
                continue;

            auto b = hlSurf->getSurfaceBoxGlobal();
            if (!b.has_value())
                continue;

            if (!b->containsPoint(MOUSE))
                continue;

            if (m_seatGrab->m_keyboard)
                setKeyboardFocus(s.lock());
            if (m_seatGrab->m_pointer)
                setPointerFocus(s.lock(), MOUSE - b->pos());
            return;
        }

        SP<CWLSurfaceResource> surf = m_seatGrab->m_surfs.at(0).lock();
        if (m_seatGrab->m_keyboard)
            setKeyboardFocus(surf);
        if (m_seatGrab->m_pointer)
            setPointerFocus(surf, {});
    }
}

void CSeatManager::onSetCursor(SP<CWLSeatResource> seatResource, uint32_t serial, SP<CWLSurfaceResource> surf, const Vector2D& hotspot) {
    if (!m_state.pointerFocusResource || !seatResource || seatResource->client() != m_state.pointerFocusResource->client()) {
        Log::logger->log(Log::DEBUG, "[seatmgr] Rejecting a setCursor because the client ain't in focus");
        return;
    }

    // TODO: fix this. Probably should be done in the CWlPointer as the serial could be lost by us.
    // if (!serialValid(seatResource, serial)) {
    //     Log::logger->log(Log::DEBUG, "[seatmgr] Rejecting a setCursor because the serial is invalid");
    //     return;
    // }

    m_events.setCursor.emit(SSetCursorEvent{surf, hotspot});
}

SP<CWLSeatResource> CSeatManager::seatResourceForClient(wl_client* client) {
    return PROTO::seat->seatResourceForClient(client);
}

void CSeatManager::setCurrentSelection(SP<IDataSource> source) {
    if (source == m_selection.currentSelection) {
        Log::logger->log(Log::WARN, "[seat] duplicated setCurrentSelection?");
        return;
    }

    m_selection.destroySelection.reset();

    if (m_selection.currentSelection)
        m_selection.currentSelection->cancelled();

    if (!source)
        PROTO::data->setSelection(nullptr);

    m_selection.currentSelection = source;

    if (source) {
        m_selection.destroySelection = source->m_events.destroy.listen([this] { setCurrentSelection(nullptr); });
        PROTO::data->setSelection(source);
        PROTO::dataWlr->setSelection(source, false);
        PROTO::extDataDevice->setSelection(source, false);
    }

    m_events.setSelection.emit();
}

void CSeatManager::setCurrentPrimarySelection(SP<IDataSource> source) {
    if (source == m_selection.currentPrimarySelection) {
        Log::logger->log(Log::WARN, "[seat] duplicated setCurrentPrimarySelection?");
        return;
    }

    m_selection.destroyPrimarySelection.reset();

    if (m_selection.currentPrimarySelection)
        m_selection.currentPrimarySelection->cancelled();

    if (!source)
        PROTO::primarySelection->setSelection(nullptr);

    m_selection.currentPrimarySelection = source;

    if (source) {
        m_selection.destroyPrimarySelection = source->m_events.destroy.listen([this] { setCurrentPrimarySelection(nullptr); });
        PROTO::primarySelection->setSelection(source);
        PROTO::dataWlr->setSelection(source, true);
        PROTO::extDataDevice->setSelection(source, true);
    }

    m_events.setPrimarySelection.emit();
}

void CSeatManager::setGrab(SP<CSeatGrab> grab) {
    if (m_seatGrab) {
        auto oldGrab = m_seatGrab;

        // Try to find the parent window or layer surface from the grab
        PHLWINDOW parentWindow;
        PHLLS     parentLayer;
        if (oldGrab && oldGrab->m_surfs.size()) {
            // Try to find the surface that had focus when the grab ended
            SP<CWLSurfaceResource> focusedSurf;
            auto                   keyboardFocus = m_state.keyboardFocus.lock();
            auto                   pointerFocus  = m_state.pointerFocus.lock();

            // Check if keyboard or pointer focus is in the grab
            for (auto const& s : oldGrab->m_surfs) {
                auto surf = s.lock();
                if (surf && (surf == keyboardFocus || surf == pointerFocus)) {
                    focusedSurf = surf;
                    break;
                }
            }

            // Fall back to first surface if no focused surface found
            if (!focusedSurf)
                focusedSurf = oldGrab->m_surfs.front().lock();

            if (focusedSurf) {
                auto hlSurface = Desktop::View::CWLSurface::fromResource(focusedSurf);
                if (hlSurface) {
                    auto popup = Desktop::View::CPopup::fromView(hlSurface->view());
                    if (popup) {
                        auto t1Owner = popup->getT1Owner();
                        if (t1Owner) {
                            parentWindow = Desktop::View::CWindow::fromView(t1Owner->view());
                            if (!parentWindow)
                                parentLayer = Desktop::View::CLayerSurface::fromView(t1Owner->view());
                        }
                    }
                }
            }
        }

        m_seatGrab.reset();

        if (parentLayer && parentLayer->m_layerSurface->m_current.keyboardInteractivity != ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
            Desktop::focusState()->rawSurfaceFocus(parentLayer->wlSurface()->resource());
        } else {
            static auto PFOLLOWMOUSE = CConfigValue<Config::INTEGER>("input:follow_mouse");
            if (*PFOLLOWMOUSE == 0 || *PFOLLOWMOUSE == 2 || *PFOLLOWMOUSE == 3) {
                const auto PMONITOR = State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();

                // If this was a popup grab, focus its parent window to maintain context
                if (validMapped(parentWindow)) {
                    Desktop::focusState()->rawWindowFocus(parentWindow, Desktop::FOCUS_REASON_FFM);
                    Log::logger->log(Log::DEBUG, "[seatmgr] Refocused popup parent window {} (follow_mouse={})", parentWindow->metadata().title(), *PFOLLOWMOUSE);
                } else
                    g_pInputManager->refocusLastWindow(PMONITOR);
            } else
                g_pInputManager->refocus();
        }

        auto                          currentFocus = m_state.keyboardFocus.lock();
        auto                          refocus      = !currentFocus;

        SP<Desktop::View::CWLSurface> surf;
        PHLLS                         layer;

        if (!refocus) {
            surf  = Desktop::View::CWLSurface::fromResource(currentFocus);
            layer = surf ? Desktop::View::CLayerSurface::fromView(surf->view()) : nullptr;
        }

        if (!refocus && !layer) {
            auto popup = surf ? Desktop::View::CPopup::fromView(surf->view()) : nullptr;
            if (popup) {
                auto parent = popup->getT1Owner();
                layer       = Desktop::View::CLayerSurface::fromView(parent->view());
            }
        }

        if (!refocus && layer)
            refocus = layer->m_keyboardInteractivity == ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE;

        if (refocus) {
            auto candidate = Desktop::focusState()->window();

            if (candidate && candidate->m_workspace && candidate->m_workspace->isVisibleNotCovered())
                Desktop::focusState()->rawWindowFocus(candidate, Desktop::FOCUS_REASON_FFM);
            else {
                const auto PMONITOR = State::monitorState()->query().vec(g_pInputManager->getMouseCoordsInternal()).run();
                g_pInputManager->refocusLastWindow(PMONITOR);
            }
        }

        if (oldGrab->m_onEnd)
            oldGrab->m_onEnd();
    }

    if (!grab)
        return;

    m_seatGrab = grab;

    refocusGrab();
}

void CSeatManager::resendEnterEvents() {
    SP<CWLSurfaceResource> kb = m_state.keyboardFocus.lock();
    SP<CWLSurfaceResource> pt = m_state.pointerFocus.lock();

    auto                   last = m_lastLocalCoords;

    setKeyboardFocus(nullptr);
    setPointerFocus(nullptr, {});

    setKeyboardFocus(kb);
    setPointerFocus(pt, last);
}

bool CSeatGrab::accepts(SP<CWLSurfaceResource> surf) {
    return std::ranges::find(m_surfs, surf) != m_surfs.end();
}

void CSeatGrab::add(SP<CWLSurfaceResource> surf) {
    m_surfs.emplace_back(surf);
}

void CSeatGrab::remove(SP<CWLSurfaceResource> surf) {
    std::erase(m_surfs, surf);
    if ((m_keyboard && g_pSeatManager->m_state.keyboardFocus == surf) || (m_pointer && g_pSeatManager->m_state.pointerFocus == surf))
        g_pSeatManager->refocusGrab();
}

void CSeatGrab::setCallback(std::function<void()> onEnd_) {
    m_onEnd = onEnd_;
}

void CSeatGrab::clear() {
    m_surfs.clear();
}
