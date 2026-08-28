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

        // no seat manager yet (e.g. during early config parse/reload) or no
        // seats created: report no ambient seat, callers must fall back
        if (!g_pSeatManager || g_pSeatManager->seats().empty())
            return nullptr;

        return g_pSeatManager->defaultSeat();
    }

    SScopedAmbientSeat::SScopedAmbientSeat(SP<CSeat> seat) {
        if (seat)
            Log::logger->log(Log::DEBUG, "[seatmgr] ambient scope enter seat '{}' (depth {})", seat->name(), seatStack().size() + 1);
        seatStack().emplace_back(std::move(seat));
    }

    SScopedAmbientSeat::~SScopedAmbientSeat() {
        auto& stack = seatStack();
        if (!stack.empty())
            stack.pop_back();
    };

    SP<CSeat> seatForPid(pid_t pid) {
        const auto NAME = pidSeatRegistry()->peekFor(pid);
        if (!NAME) {
            Log::logger->log(Log::DEBUG, "[seatmgr] seatForPid pid {} -> no registry entry, default", pid);
            return g_pSeatManager->defaultSeat();
        }

        for (auto const& s : g_pSeatManager->seats()) {
            if (s->name() == *NAME)
                return s;
        }

        Log::logger->log(Log::WARN, "[seatmgr] seatForPid pid {} -> registry seat '{}' not found, default", pid, *NAME);
        return g_pSeatManager->defaultSeat();
    }

    SP<CSeat> seatForClient(wl_client* client) {
        pid_t pid = 0;
        wl_client_get_credentials(client, &pid, nullptr, nullptr);

        return seatForPid(pid);
    }
}
