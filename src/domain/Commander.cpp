#include "domain/Commander.h"

#include "core/GameState.h"
#include "domain/Empire.h"
#include "util/Fmt.h"
#include "util/Str.h"

#include "util/Str.h"

namespace gf {

std::string_view commanderTraitName(CommanderTrait t) {
    switch (t) {
        case CommanderTrait::None: return "无";
        case CommanderTrait::Offensive: return "攻势";
        case CommanderTrait::Defensive: return "防御";
        case CommanderTrait::Logistician: return "后勤";
        case CommanderTrait::Maneuver: return "机动";
        case CommanderTrait::Trickster: return "诡道";
        case CommanderTrait::Inspiring: return "鼓舞";
        case CommanderTrait::Siege: return "攻坚";
        case CommanderTrait::Count: break;
    }
    return "?";
}

CommanderTrait commanderTraitFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(CommanderTrait::Count); ++i)
        if (commanderTraitName(static_cast<CommanderTrait>(i)) == s) return static_cast<CommanderTrait>(i);
    if (iequals(s, "offensive")) return CommanderTrait::Offensive;
    if (iequals(s, "defensive")) return CommanderTrait::Defensive;
    if (iequals(s, "logistician")) return CommanderTrait::Logistician;
    if (iequals(s, "maneuver")) return CommanderTrait::Maneuver;
    if (iequals(s, "trickster")) return CommanderTrait::Trickster;
    if (iequals(s, "inspiring")) return CommanderTrait::Inspiring;
    if (iequals(s, "siege")) return CommanderTrait::Siege;
    return CommanderTrait::None;
}

Fixed traitAttackBonus(CommanderTrait t) {
    switch (t) {
        case CommanderTrait::Offensive: return Fixed::pct(15);
        case CommanderTrait::Maneuver: return Fixed::pct(8);
        case CommanderTrait::Inspiring: return Fixed::pct(5);
        case CommanderTrait::Siege: return Fixed::pct(10);
        case CommanderTrait::Trickster: return Fixed::pct(6);
        case CommanderTrait::Defensive: return Fixed::pct(-5);
        default: return Fixed(0);
    }
}

Fixed traitDefenseBonus(CommanderTrait t) {
    switch (t) {
        case CommanderTrait::Defensive: return Fixed::pct(18);
        case CommanderTrait::Inspiring: return Fixed::pct(6);
        case CommanderTrait::Logistician: return Fixed::pct(4);
        case CommanderTrait::Offensive: return Fixed::pct(-5);
        default: return Fixed(0);
    }
}

Fixed traitLogisticsBonus(CommanderTrait t) {
    switch (t) {
        case CommanderTrait::Logistician: return Fixed::pct(25);
        case CommanderTrait::Inspiring: return Fixed::pct(8);
        default: return Fixed(0);
    }
}

Fixed traitWidthFactor(CommanderTrait t) {
    // 诡道指挥官能压缩对方的展开宽度
    switch (t) {
        case CommanderTrait::Trickster: return Fixed::pct(-15);
        case CommanderTrait::Maneuver: return Fixed::pct(8);
        default: return Fixed(0);
    }
}

int veterancyLevel(Fixed experience) {
    i64 e = experience.rawValue();
    if (e >= 800) return 4;
    if (e >= 500) return 3;
    if (e >= 250) return 2;
    if (e >= 80) return 1;
    return 0;
}

std::string_view veterancyName(int level) {
    switch (level) {
        case 0: return "新兵";
        case 1: return "老兵";
        case 2: return "精锐";
        case 3: return "王牌";
        case 4: return "传奇";
        default: break;
    }
    return "?";
}

Fixed veterancyMultiplier(int level) {
    switch (level) {
        case 0: return Fixed(1);
        case 1: return Fixed::pct(108);
        case 2: return Fixed::pct(118);
        case 3: return Fixed::pct(130);
        case 4: return Fixed::pct(145);
        default: break;
    }
    return Fixed(1);
}

}  // namespace gf

// ===========================================================================
// 军衔与晋升（HOI4 式）
// ===========================================================================
namespace gf {

std::string_view commanderRankName(CommanderRank r) {
    switch (r) {
        case CommanderRank::Captain: return "上尉";
        case CommanderRank::Major: return "少校";
        case CommanderRank::Colonel: return "上校";
        case CommanderRank::General: return "少将";
        case CommanderRank::Marshal: return "元帅";
        case CommanderRank::Count: break;
    }
    return "?";
}

CommanderRank commanderRankFromName(std::string_view s) {
    for (int i = 0; i < kCommanderRankCount; ++i)
        if (commanderRankName(static_cast<CommanderRank>(i)) == s) return static_cast<CommanderRank>(i);
    return CommanderRank::Count;
}

Fixed rankMeritRequired(CommanderRank r) {
    // 逐级递增：晋升越往上越难
    switch (r) {
        case CommanderRank::Captain: return Fixed(0);
        case CommanderRank::Major: return Fixed(120);
        case CommanderRank::Colonel: return Fixed(360);
        case CommanderRank::General: return Fixed(840);
        case CommanderRank::Marshal: return Fixed(1800);
        case CommanderRank::Count: break;
    }
    return Fixed(0);
}

Fixed rankSkillCap(CommanderRank r) {
    // 军衔越高，技能上限越高（0.70 → 0.98）
    return Fixed::pct(70 + 7 * static_cast<i64>(r));
}

Fixed rankCommandBonus(CommanderRank r) {
    // 统率加成：影响战斗展开宽度
    return Fixed::pct(4 * static_cast<i64>(r));
}

int rankTraitSlots(CommanderRank r) {
    // 上尉 1 → 元帅 3
    int slots = 1 + static_cast<int>(r) / 2;
    return slots > kMaxCommanderTraits ? kMaxCommanderTraits : slots;
}

bool commanderHasTrait(const Commander& c, CommanderTrait t) {
    if (c.trait == t) return true;
    for (CommanderTrait x : c.traits)
        if (x == t) return true;
    return false;
}

Fixed commanderTraitAttack(const Commander& c) {
    Fixed acc = traitAttackBonus(c.trait);
    for (CommanderTrait t : c.traits) acc += traitAttackBonus(t);
    return acc;
}

Fixed commanderTraitDefense(const Commander& c) {
    Fixed acc = traitDefenseBonus(c.trait);
    for (CommanderTrait t : c.traits) acc += traitDefenseBonus(t);
    return acc;
}

Fixed commanderTraitLogistics(const Commander& c) {
    Fixed acc = traitLogisticsBonus(c.trait);
    for (CommanderTrait t : c.traits) acc += traitLogisticsBonus(t);
    return acc;
}

Fixed commanderTraitWidth(const Commander& c) {
    Fixed acc = traitWidthFactor(c.trait);
    for (CommanderTrait t : c.traits) acc += traitWidthFactor(t);
    return acc;
}

CommanderRank commanderAwardMerit(GameState& st, u32 commanderId, Fixed merit) {
    Commander* c = st.commander(commanderId);
    if (c == nullptr) return CommanderRank::Captain;
    c->merit += merit;
    CommanderRank rank = c->rank;
    // 连续晋升（一次授予大量战功时可跨级）
    while (static_cast<int>(rank) + 1 < kCommanderRankCount) {
        auto next = static_cast<CommanderRank>(static_cast<int>(rank) + 1);
        if (c->merit.rawValue() < rankMeritRequired(next).rawValue()) break;
        rank = next;
        c->rank = rank;
        // 晋升提升技能上限并立即获得一部分成长
        Fixed cap = rankSkillCap(rank);
        c->attack = fxClamp(c->attack + Fixed::pct(4), Fixed(0), cap);
        c->defense = fxClamp(c->defense + Fixed::pct(4), Fixed(0), cap);
        c->logistics = fxClamp(c->logistics + Fixed::pct(3), Fixed(0), cap);
        c->planning = fxClamp(c->planning + Fixed::pct(3), Fixed(0), cap);
        // 晋升到一定军衔时习得新特质
        if (static_cast<int>(c->traits.size()) < rankTraitSlots(rank)) {
            // 选取尚未拥有、且与现有特质不重复的一项
            for (int t = 1; t < static_cast<int>(CommanderTrait::Count); ++t) {
                auto cand = static_cast<CommanderTrait>(t);
                if (commanderHasTrait(*c, cand)) continue;
                c->traits.push_back(cand);
                break;
            }
        }
        st.logEvent(LogPhase::Combat, kLogWar,
                    "指挥官 " + c->name + " 晋升为【" + std::string(commanderRankName(rank)) + "】",
                    c->owner);
    }
    return rank;
}

bool commanderTrain(GameState& st, u32 commanderId, Fixed influenceCost, std::string* err) {
    Commander* c = st.commander(commanderId);
    if (c == nullptr) {
        if (err) *err = "找不到该指挥官";
        return false;
    }
    Empire* e = st.empire(c->owner);
    if (e == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (e->influence.rawValue() < influenceCost.rawValue()) {
        if (err)
            *err = "影响力不足：深造需要 " + fixedStr(influenceCost, 0) + "，当前 " +
                   fixedStr(e->influence, 0);
        return false;
    }
    e->influence -= influenceCost;
    // 深造：技能提升 + 战功
    Fixed cap = rankSkillCap(c->rank);
    c->attack = fxClamp(c->attack + Fixed::pct(3), Fixed(0), cap);
    c->defense = fxClamp(c->defense + Fixed::pct(3), Fixed(0), cap);
    c->logistics = fxClamp(c->logistics + Fixed::pct(2), Fixed(0), cap);
    c->planning = fxClamp(c->planning + Fixed::pct(2), Fixed(0), cap);
    (void)commanderAwardMerit(st, commanderId, Fixed(60));
    st.logEvent(LogPhase::Combat, kLogWar, "指挥官 " + c->name + " 完成军校深造", c->owner);
    return true;
}

std::string commanderRankText(const Commander& c) {
    std::string out = std::string(commanderRankName(c.rank));
    out += "  主特质 " + std::string(commanderTraitName(c.trait));
    for (CommanderTrait t : c.traits) out += " / " + std::string(commanderTraitName(t));
    out += "  战功 " + fixedStr(c.merit, 0);
    auto next = static_cast<CommanderRank>(static_cast<int>(c.rank) + 1);
    if (static_cast<int>(c.rank) + 1 < kCommanderRankCount) {
        out += "  → 距【" + std::string(commanderRankName(next)) + "】还需 " +
               fixedStr(rankMeritRequired(next) - c.merit, 0);
    } else {
        out += "  已达最高军衔";
    }
    return out;
}

}  // namespace gf
