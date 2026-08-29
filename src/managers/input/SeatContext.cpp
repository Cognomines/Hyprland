#include "SeatContext.hpp"
#include "../SeatManager.hpp"
#include "PidSeatRegistry.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <wayland-server-core.h>

namespace Input {
    namespace {
        std::vector<SP<CSeat>>& seatStack() {
            static std::vector<SP<CSeat>> stack;
            return stack;
        }

        // how far up the process tree seatForPid walks when the exact pid
        // isn't registered (e.g. `sh -c "pkill rofi && rofi ..."` spawns rofi
        // as a grandchild). Plenty for realistic wrapper depth; a full tree
        // is unnecessary since over-forking beyond this is degenerate.
        constexpr int PidAncestorWalkLimit = 32;

        pid_t ppidOf(pid_t pid) {
            if (pid <= 1)
                return 0;

            std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
            std::string   buf;
            if (!std::getline(f, buf))
                return 0;

            // skip "pid (comm) " — comm may itself contain spaces and
            // parentheses, so anchor on the LAST `)` instead of index math
            const auto RP = buf.rfind(')');
            if (RP == std::string::npos)
                return 0;

            pid_t ppid = 0;
            if (std::sscanf(buf.c_str() + RP + 1, " %*c %d", &ppid) != 1)
                return 0;

            return ppid;
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
        // exact pid first, then walk ancestors: a bind like
        // exec_cmd("pkill rofi || true && rofi ...") registers only the
        // `/bin/sh -c` child with the triggering seat, while rofi itself
        // runs as a grandchild with its own pid. Resolving through the
        // parent chain attributes such spawns to the right seat.
        int examined = 0;
        for (pid_t cur = pid; cur > 0 && examined < PidAncestorWalkLimit; ++examined, cur = ppidOf(cur)) {
            const auto NAME = pidSeatRegistry()->peekFor(cur);
            if (!NAME)
                continue;

            for (auto const& s : g_pSeatManager->seats()) {
                if (s->name() == *NAME)
                    return s;
            }

            Log::logger->log(Log::WARN, "[seatmgr] seatForPid pid {} -> registry seat '{}' not found, default", cur, *NAME);
            return g_pSeatManager->defaultSeat();
        }

        Log::logger->log(Log::DEBUG, "[seatmgr] seatForPid pid {} -> no registry entry in {} process(es), default", pid, examined);
        return g_pSeatManager->defaultSeat();
    }

    SP<CSeat> seatForClient(wl_client* client) {
        pid_t pid = 0;
        wl_client_get_credentials(client, &pid, nullptr, nullptr);

        return seatForPid(pid);
    }

    SP<CSeat> seatForSurfacePlacement(SP<CSeat> spawnSeat, wl_client* client) {
        if (spawnSeat && !spawnSeat->isDefault())
            return spawnSeat;

        if (!client || !g_pSeatManager)
            return spawnSeat;

        return g_pSeatManager->lastInteractingSeat(client);
    }
}
