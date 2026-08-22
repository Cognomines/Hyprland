#pragma once

#include "../../defines.hpp"

class CSeat;

/*
    Short-lived context for "current seat" semantics: bind execution, IPC
    dispatch (--seat) and submap lookups set the ambient seat for their scope;
    everything else resolves to the default seat.
*/

namespace Input {
    // the ambient seat, never null: top of the context stack, else the default seat
    SP<CSeat> ambientSeat();

    // pushes onto the ambient-seat stack until destroyed; nesting supported
    struct SScopedAmbientSeat {
        explicit SScopedAmbientSeat(SP<CSeat> seat);
        ~SScopedAmbientSeat();

        SScopedAmbientSeat(const SScopedAmbientSeat&)            = delete;
        SScopedAmbientSeat(SScopedAmbientSeat&&)                 = delete;
        SScopedAmbientSeat& operator=(const SScopedAmbientSeat&) = delete;
    };
}
