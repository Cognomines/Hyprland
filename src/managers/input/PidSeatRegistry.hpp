#pragma once

#include "../../defines.hpp"

#include <sys/types.h>

#include <string>

/*
    Associates spawned child pids with the logical seat of their triggering
    context (e.g. the seat a keybind was pressed on). Consumed by wl_seat
    bind resolution so clients land on the seat that launched them.
*/

namespace Input {
    class CPidSeatRegistry {
      public:
        void associate(pid_t pid, const std::string& seatName);
        // returns the seat name for pid and consumes the entry; "" if unknown
        std::string takeFor(pid_t pid);

      private:
        static constexpr size_t MAX_ENTRIES = 512;
    };

    UP<CPidSeatRegistry>& pidSeatRegistry();
}
