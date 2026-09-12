#include "combat/Military.h"

#include <algorithm>
#include <string>

#include "cli/TextTable.h"
#include "core/GameState.h"
#include "domain/Building.h"
#include "domain/Empire.h"
#include "domain/SpeciesAdv.h"
#include "domain/Fleet.h"
#include "domain/Planet.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 每季军力增长上限 = 目标的这个比例。
/// 这是「造舰需要时间」的核心参数：5% 意味着从零补到满编约需 40 季，
/// 被歼灭后重建同样要这么久 —— 战争因此有真正的代价与恢复期。
constexpr i64 kBuildUpRatePct = 5;

/// 战损后的军力衰减速率（更快：舰队被歼灭是即时损失）
constexpr i64 kDecayRatePct = 25;

}  // namespace

Fixed difficultyMilitaryBonus(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr || e->isPlayer) return Fixed(1);
    int d = st.difficulty;
    if (d < 1) d = 1;
    if (d > 5) d = 5;
    // 难度 1 无加成；每级 +18%，难度 5 为 1 + 4×18% = 1.72 倍。
    // 早期版本难度只提升 AI 的前瞻深度与算力预算，不碰经济与军力，
    // 因此「最高难度」在体量上并不比玩家强 —— 这是「太简单」的根源之一。
    return Fixed(1) + Fixed::pct(18) * Fixed(static_cast<i64>(d - 1));
}

Fixed militaryTargetOf(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr || !e->alive) return Fixed(0);

    // 基础：经济产出（系数刻意压低 —— 军力上限主要靠**建设**堆出来，
    // 而不是随经济自动水涨船高。否则开局即可拥有数千军力，
    // 「军事发展需要时间」就无从谈起。）
    Fixed target = e->economy * Fixed::raw(350);

    // 行星人口（权重较低：人口是国力基础，不是直接的军力）
    for (const auto& p : st.planets) {
        if (p.owner != empire) continue;
        target += Fixed(static_cast<i64>(p.pops)) * Fixed::raw(120);
        target += p.development * Fixed(20);
    }

    // 建筑才是军力上限的主引擎：船坞直接决定你能维持多大规模的舰队。
    // 想扩军就必须先花时间与资源造舰坞 —— 这是「延长军事发展」的关键。
    for (const auto& p : st.planets) {
        if (p.owner != empire) continue;
        for (u32 bid : p.buildings) {
            const BuildingInfo& bi = buildingInfo(static_cast<int>(bid & 0xFFu));
            if (bi.effect == BuildingEffect::Shipyard) target += bi.effectValue * Fixed(160);
            else if (bi.effect == BuildingEffect::Defense) target += bi.effectValue * Fixed(40);
        }
    }

    // 科技与政策加成
    target = target * (Fixed(1) + empireModifier(*e, ModKind::MilitaryPower));

    // 难度加成（仅对 AI）：让高难度的 AI 在体量上真正压过玩家
    // 游牧政体：舰队即国土 —— 群势越高，可维持的军力越强
    target = target * (Fixed(1) + nomadicMilitaryBonus(st, empire));
    target = target * difficultyMilitaryBonus(st, empire);

    // 下限：任何帝国都保有最基本的自卫力量
    return fxMax(target, Fixed(120));
}

void militaryPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        const Fixed target = militaryTargetOf(st, e.id);
        if (target.rawValue() <= 0) continue;

        // 向目标靠拢：增长慢（造舰周期），衰减快（战损即时）
        const bool growing = e.military.rawValue() < target.rawValue();
        Fixed rate = target * Fixed::pct(growing ? kBuildUpRatePct : kDecayRatePct);
        if (rate.rawValue() < Fixed(1).rawValue()) rate = Fixed(1);

        if (growing) {
            e.military += rate;
            if (e.military.rawValue() > target.rawValue()) e.military = target;
        } else {
            e.military -= rate;
            if (e.military.rawValue() < target.rawValue()) e.military = target;
        }

        // 把军力分配到舰队：舰队强度之和应当与军力一致。
        // 没有这一步，`military` 只是一个展示数字，战斗仍用开局写死的 strength。
        if (e.fleets.empty()) continue;
        Fixed total = Fixed(0);
        for (u32 fid : e.fleets) {
            const Fleet* f = st.fleet(fid);
            if (f != nullptr) total += f->strength;
        }
        // 建造中的舰队（strength 为 0 的新舰）按缺口分摊
        Fixed want = e.military;
        if (total.rawValue() <= 0) {
            // 全军覆没：从零重建，每支舰队分到目标的一部分
            Fixed each = want / Fixed(static_cast<i64>(e.fleets.size()));
            for (u32 fid : e.fleets) {
                Fleet* f = st.fleet(fid);
                if (f == nullptr) continue;
                f->strength = fxMax(f->strength, each);
            }
            continue;
        }
        // 按现有比例缩放，向目标靠拢（每季最多调整 5%，避免瞬间满编）
        Fixed diff = want - total;
        Fixed step = want * Fixed::pct(kBuildUpRatePct);
        if (diff.rawValue() > step.rawValue()) diff = step;
        if (diff.rawValue() < -step.rawValue()) diff = -step;
        for (u32 fid : e.fleets) {
            Fleet* f = st.fleet(fid);
            if (f == nullptr || total.rawValue() <= 0) continue;
            Fixed share = f->strength / total;
            f->strength += diff * share;
            if (f->strength.rawValue() < 0) f->strength = Fixed(0);
        }
    }
}

std::string militaryReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    std::string out;
    TextTable t;
    t.header({"项目", "值"});
    t.row({"当前军力", fixedStr(e->military, 0)});
    t.row({"军力上限", fixedStr(militaryTargetOf(st, empire), 0)});
    Fixed bonus = difficultyMilitaryBonus(st, empire);
    t.row({"难度加成", bonus.rawValue() == Fixed(1).rawValue()
                          ? std::string("无（玩家或难度 1）")
                          : ("×" + fixedStrPlain(bonus, 2))});
    Fixed total = Fixed(0);
    int n = 0;
    for (u32 fid : e->fleets) {
        const Fleet* f = st.fleet(fid);
        if (f == nullptr) continue;
        total += f->strength;
        ++n;
    }
    t.row({"舰队", std::to_string(n) + " 支，合计强度 " + fixedStr(total, 0)});
    // 补满还需多久
    if (e->military.rawValue() < militaryTargetOf(st, empire).rawValue()) {
        Fixed gap = militaryTargetOf(st, empire) - e->military;
        Fixed rate = militaryTargetOf(st, empire) * Fixed::pct(5);
        if (rate.rawValue() > 0) {
            i64 ticks = (gap.rawValue() + rate.rawValue() - 1) / rate.rawValue();
            t.row({"补满尚需", std::to_string(ticks) + " 季"});
        }
    } else {
        t.row({"状态", "已满编"});
    }
    out += t.render();
    return out;
}

}  // namespace gf
