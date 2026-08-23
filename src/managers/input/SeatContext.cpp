#include "SeatContext.hpp"
#include "../SeatManager.hpp"
#include "PidSeatRegistry.hpp"

#include <vector>

#include <wayland-server-core.h>

namespace Input {
    namespace {
        std::vector<SP<CSeat>>& seatStack() {
            static std::vector<SP<CSeat>> stack;
            return stack;
        }
    }

    SP<CSeat> ambientSeat() {
        auto& stack = seatStack();
        if (!stack.empty())
            return stack.back();

        return g_pSeatManager->defaultSeat();
    }

    SScopedAmbientSeat::SScopedAmbientSeat(SP<CSeat> seat) {
        seatStack().emplace_back(std::move(seat));
    }

    SScopedAmbientSeat::~SScopedAmbientSeat() {
        auto& stack = seatStack();
        if (!stack.empty())
            stack.pop_back();
    };

    SP<CSeat> seatForClient(wl_client* client) {
        pid_t pid = 0;
        wl_client_get_credentials(client, &pid, nullptr, nullptr);

        const auto NAME = pidSeatRegistry()->peekFor(pid);
        if (!NAME)
            return g_pSeatManager->defaultSeat();

        for (auto const& s : g_pSeatManager->seats()) {
            if (s->name() == *NAME)
                return s;
        }

        return g_pSeatManager->defaultSeat();
    }
}
