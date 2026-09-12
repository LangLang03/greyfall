#include "domain/Fleet.h"

#include "util/Str.h"

namespace gf {

std::string_view fleetOrderName(FleetOrder o) {
    switch (o) {
        case FleetOrder::Idle: return "待命";
        case FleetOrder::Move: return "移动";
        case FleetOrder::Patrol: return "巡逻";
        case FleetOrder::Embargo: return "封锁";
        case FleetOrder::Engage: return "交战";
        case FleetOrder::Escort: return "护航";
        case FleetOrder::Blockade: return "阻断";
        case FleetOrder::Retreat: return "撤退";
        case FleetOrder::Count: break;
    }
    return "?";
}

FleetOrder fleetOrderFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(FleetOrder::Count); ++i)
        if (fleetOrderName(static_cast<FleetOrder>(i)) == s) return static_cast<FleetOrder>(i);
    if (iequals(s, "move")) return FleetOrder::Move;
    if (iequals(s, "patrol")) return FleetOrder::Patrol;
    if (iequals(s, "embargo")) return FleetOrder::Embargo;
    if (iequals(s, "engage")) return FleetOrder::Engage;
    if (iequals(s, "escort")) return FleetOrder::Escort;
    if (iequals(s, "blockade")) return FleetOrder::Blockade;
    if (iequals(s, "retreat")) return FleetOrder::Retreat;
    return FleetOrder::Idle;
}

}  // namespace gf
