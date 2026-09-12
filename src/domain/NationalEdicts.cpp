#include "domain/Development.h"

#include <algorithm>
#include "core/GameState.h"

namespace gf {
namespace {
constexpr const char* names[] = {"战时配给", "言论管制", "军费扩张", "研究总动员", "边境戒严", "市场自由化"};
constexpr i64 maintenance[] = {500, 600, 1500, 1000, 800, 400};
}

i64 nationalEdictUpkeep(int kind) { return kind >= 0 && kind < 6 ? maintenance[kind] : 0; }

Fixed developmentModifier(const Empire& e, ModKind kind) {
    Fixed value;
    auto add = [&](ModKind target, int percent) { if (kind == target) value += Fixed::pct(percent); };
    if (e.ascensions & 1u) add(ModKind::IntelDefense, 15);
    if (e.ascensions & 2u) { add(ModKind::BuildRate, 25); add(ModKind::Growth, -10); }
    if (e.ascensions & 4u) { add(ModKind::Growth, 30); add(ModKind::Stability, 10); }
    if (e.ascensions & 8u) { add(ModKind::ColonyCost, -40); add(ModKind::Stability, -15); }
    for (const auto& edict : e.nationalEdicts) switch (edict.kind) {
        case 0: add(ModKind::Unrest, 5); break;
        case 1: add(ModKind::IntelDefense, 10); add(ModKind::Unrest, 3); add(ModKind::Stability, 5); break;
        case 2: add(ModKind::MilitaryPower, 15); add(ModKind::TradeMargin, -5); break;
        case 3: add(ModKind::ResearchRate, 20); add(ModKind::Unrest, 4); break;
        case 4: add(ModKind::Stability, 8); add(ModKind::Unrest, -5); add(ModKind::TradeMargin, -10); break;
        case 5: add(ModKind::TradeMargin, 12); add(ModKind::Unrest, 4); break;
        default: break;
    }
    return value;
}

bool activateNationalEdict(GameState& st, u32 empire, int kind, std::string* message) {
    auto reject = [&](const std::string& reason) { if (message) *message = reason; return false; };
    auto* e = st.empire(empire);
    if (!e || kind < 0 || kind >= 6) return reject("未知法令");
    int active = 0;
    for (const auto& edict : e->nationalEdicts) if (st.tick < edict.expiresTick) {
        if (edict.kind == kind) return reject("该法令仍在生效，不能重复叠加或提前续期");
        ++active;
    }
    if (active >= 3) return reject("最多同时维持三项国家法令");
    ProjectCost cost;
    cost.credits = kind == 2 ? 15000 : 5000;
    if (!payProject(st, *e, cost, message)) return false;
    std::erase_if(e->nationalEdicts, [&](const auto& edict) { return st.tick >= edict.expiresTick; });
    e->nationalEdicts.push_back({static_cast<u8>(kind), st.tick + 8});
    refreshEmpireBonuses(st);
    if (message) *message = std::string(names[kind]) + "生效 8 季，启动 " + std::to_string(cost.credits) +
        " cr，维护 " + std::to_string(maintenance[kind]) + " cr/季；效果到期撤销。";
    st.logEvent(LogPhase::Domestic, "domestic.edict", e->name + "颁布" + names[kind], empire);
    return true;
}

void nationalEdictPhase(GameState& st) {
    for (auto& e : st.empires) {
        Fixed available = fxMax(Fixed(0), e.treasury);
        std::erase_if(e.nationalEdicts, [&](const auto& edict) {
            if (!e.alive || st.tick >= edict.expiresTick) return true;
            const Fixed cost(nationalEdictUpkeep(edict.kind));
            if (available < cost) {
                st.logEvent(LogPhase::Domestic, "edict.expired", e.name + "因无力承担维护而撤销法令", e.id);
                return true;
            }
            available -= cost;
            return false;
        });
    }
}

std::string nationalEdictReport(const GameState& st, u32 empire) {
    const auto* e = st.empire(empire);
    std::string out = "国家法令持续 8 季；同时最多 3 项；启动 5000 cr（军费扩张 15000 cr）。\n";
    for (int i = 0; i < 6; ++i) {
        out += "  " + std::string(names[i]) + "：维护 " + std::to_string(maintenance[i]) + " cr/季";
        if (e) for (const auto& edict : e->nationalEdicts)
            if (edict.kind == i && st.tick < edict.expiresTick) out += "，剩余 " + std::to_string(edict.expiresTick - st.tick) + " 季";
        out += "\n";
    }
    return out;
}
}  // namespace gf
