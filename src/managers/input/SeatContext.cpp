#include "SeatContext.hpp"
#include "../SeatManager.hpp"

#include <vector>

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
}
