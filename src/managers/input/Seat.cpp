#include "Seat.hpp"

#include "../../devices/IKeyboard.hpp"
#include "../../devices/IPointer.hpp"
#include "../../devices/ITouch.hpp"
#include "../../devices/Tablet.hpp"

CSeat::CSeat(const std::string& name, bool isDefault) : m_name(name), m_isDefault(isDefault) {
    ;
}

CSeat::~CSeat() {
    ;
}

const std::string& CSeat::name() const {
    return m_name;
}

bool CSeat::isDefault() const {
    return m_isDefault;
}
