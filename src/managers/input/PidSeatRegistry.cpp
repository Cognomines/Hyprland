#include "PidSeatRegistry.hpp"
#include "../../defines.hpp"

#include <unordered_map>

namespace Input {
    namespace {
        std::unordered_map<pid_t, std::string>& map() {
            static std::unordered_map<pid_t, std::string> m;
            return m;
        }
    }

    void CPidSeatRegistry::associate(pid_t pid, const std::string& seatName) {
        auto& m = map();
        if (m.size() >= MAX_ENTRIES)
            m.erase(m.begin()); // drop oldest; entries are consumed on bind anyway
        m.insert_or_assign(pid, seatName);
    }

    std::string CPidSeatRegistry::takeFor(pid_t pid) {
        auto& m  = map();
        auto  it = m.find(pid);
        if (it == m.end())
            return "";
        auto out = std::move(it->second);
        m.erase(it);
        return out;
    }

    std::optional<std::string> CPidSeatRegistry::peekFor(pid_t pid) {
        auto& m  = map();
        auto  it = m.find(pid);
        if (it == m.end())
            return std::nullopt;
        return it->second;
    }

    UP<CPidSeatRegistry>& pidSeatRegistry() {
        static UP<CPidSeatRegistry> p = makeUnique<CPidSeatRegistry>();
        return p;
    }
}
