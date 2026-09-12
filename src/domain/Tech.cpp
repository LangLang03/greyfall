#include <string>

#include "domain/Tech.h"

#include <algorithm>

namespace gf {

std::string_view techBranchName(TechBranch b) {
    switch (b) {
        case TechBranch::Physics: return "物理";
        case TechBranch::Society: return "社会";
        case TechBranch::Engineering: return "工程";
        case TechBranch::Biology: return "生物";
        case TechBranch::Computing: return "计算";
        case TechBranch::Psionics: return "灵能";
        case TechBranch::Count: break;
    }
    return "?";
}

bool techCompleted(const TechState& st, int techId) {
    if (techId < 0) return false;
    for (u8 c : st.completed)
        if (c == static_cast<u8>(techId)) return true;
    return false;
}

bool techAvailable(const TechState& st, int techId) {
    if (techId < 0 || techId >= kTechCount) return false;
    if (techCompleted(st, techId)) return false;
    const TechInfo& t = techInfo(techId);
    for (u8 p : t.prereq)
        if (!techCompleted(st, static_cast<int>(p))) return false;
    return true;
}

BranchProgress branchProgress(const TechState& st, TechBranch b) {
    BranchProgress p;
    for (u8 c : st.completed) {
        const TechInfo& t = techInfo(static_cast<int>(c));
        if (t.branch == b) ++p.completed;
    }
    p.mastered = (p.completed >= 16);
    return p;
}

namespace {

/// 分支的两条主轴（与 content/Tech.cpp 的效果分配保持一致）
void branchAxes(TechBranch b, ModKind& primary, ModKind& secondary) {
    switch (b) {
        case TechBranch::Physics: primary = ModKind::ResearchRate; secondary = ModKind::Detection; break;
        case TechBranch::Society: primary = ModKind::Stability; secondary = ModKind::InfluenceGain; break;
        case TechBranch::Engineering: primary = ModKind::BuildRate; secondary = ModKind::TradeMargin; break;
        case TechBranch::Biology: primary = ModKind::Growth; secondary = ModKind::Unrest; break;
        case TechBranch::Computing: primary = ModKind::ResearchRate; secondary = ModKind::ManipulationSkill; break;
        case TechBranch::Psionics: primary = ModKind::IntelDefense; secondary = ModKind::DiploWeight; break;
        default: primary = ModKind::Count; secondary = ModKind::Count; break;
    }
}

/// 满级奖励的主轴（与主轴略有不同，代表「贯通」）
ModKind masteryAxis(TechBranch b) {
    switch (b) {
        case TechBranch::Physics: return ModKind::ResearchRate;
        case TechBranch::Society: return ModKind::DiploWeight;
        case TechBranch::Engineering: return ModKind::BuildRate;
        case TechBranch::Biology: return ModKind::Growth;
        case TechBranch::Computing: return ModKind::ResearchRate;
        case TechBranch::Psionics: return ModKind::IntelDefense;
        default: return ModKind::Count;
    }
}

}  // namespace

Fixed techModifier(const TechState& st, ModKind kind) {
    if (kind == ModKind::Count) return Fixed(0);
    Fixed acc = Fixed(0);

    // 1) 已完成科技的直接效果
    for (u8 c : st.completed) {
        const TechInfo& t = techInfo(static_cast<int>(c));
        for (u8 i = 0; i < t.effectCount && i < t.effects.size(); ++i) {
            if (t.effects[i].kind == kind) acc += t.effects[i].value;
        }
    }

    // 2) 分支专精：某分支每完成 4 项，该分支主轴额外 +3%
    for (int b = 0; b < kTechBranchCount; ++b) {
        auto br = static_cast<TechBranch>(b);
        BranchProgress bp = branchProgress(st, br);
        int steps = bp.completed / 4;   // 4 / 8 / 12 / 16 各给一档
        if (steps <= 0) continue;
        Fixed bonus = Fixed::pct(3) * Fixed(steps);
        ModKind primary = ModKind::Count, secondary = ModKind::Count;
        branchAxes(br, primary, secondary);
        if (primary == kind) acc += bonus;
        if (secondary == kind) acc += (secondary == ModKind::Unrest) ? -bonus : bonus;
    }

    // 3) 满级（16/16）额外奖励
    for (int b = 0; b < kTechBranchCount; ++b) {
        auto br = static_cast<TechBranch>(b);
        if (!branchProgress(st, br).mastered) continue;
        if (masteryAxis(br) == kind) acc += Fixed::pct(10);
    }
    return acc;
}

std::string techEffectText(const TechInfo& t) {
    if (t.effectCount == 0) return "无属性修正";
    std::string out;
    for (u8 i = 0; i < t.effectCount && i < t.effects.size(); ++i) {
        if (t.effects[i].kind == ModKind::Count) continue;
        if (!out.empty()) out += "，";
        out += std::string(modKindName(t.effects[i].kind)) + " " +
               fixedStrSigned(t.effects[i].value * Fixed(100), 1) + "%";
    }
    return out.empty() ? "无属性修正" : out;
}

std::vector<u8> techAdvance(TechState& st, Fixed points) {
    std::vector<u8> done;
    // 研究点按分支侧重分配。未设侧重时退化为均分。
    Fixed totalFocus = Fixed(0);
    for (int b = 0; b < kTechBranchCount; ++b) totalFocus += st.focus[static_cast<std::size_t>(b)];
    if (totalFocus.rawValue() <= 0) {
        for (int b = 0; b < kTechBranchCount; ++b) st.focus[static_cast<std::size_t>(b)] = Fixed(1);
        totalFocus = Fixed(static_cast<i64>(kTechBranchCount));
    }
    // 预算系数决定「研究完整棵科技树需要多少季」。
    // 过高会让科技在中期被穷尽，失去长线取舍。
    const Fixed budget = points * st.rate * Fixed::pct(16);
    for (int b = 0; b < kTechBranchCount; ++b) {
        std::size_t bi = static_cast<std::size_t>(b);
        st.progress[bi] += budget * st.focus[bi] / totalFocus;
    }
    // 每分支尝试推进当前节点；若未选则自动选择第一个可用项
    for (int b = 0; b < kTechBranchCount; ++b) {
        std::size_t bi = static_cast<std::size_t>(b);
        for (int guard = 0; guard < 4; ++guard) {
            u32 cur = st.current[bi];
            if (cur == 0 || techCompleted(st, static_cast<int>(cur)) ||
                !techAvailable(st, static_cast<int>(cur))) {
                int pick = -1;
                for (int i = 0; i < kTechCount; ++i) {
                    const TechInfo& t = techInfo(i);
                    if (t.branch != static_cast<TechBranch>(b)) continue;
                    if (!techAvailable(st, i)) continue;
                    if (pick < 0 || t.tier < techInfo(pick).tier) pick = i;
                }
                if (pick < 0) break;
                cur = static_cast<u32>(pick);
                st.current[bi] = cur;
            }
            const TechInfo& t = techInfo(static_cast<int>(cur));
            // 量纲必须一致：progress 是定点数（raw = 点数×1000），
            // t.cost 是「点数」整数。直接比较 rawValue 与 t.cost 会让
            // 实际成本只有标称值的 1/1000，科技树在 20 季内被穷尽。
            const Fixed costFixed = Fixed(static_cast<i64>(t.cost));
            if (st.progress[bi].rawValue() < costFixed.rawValue()) break;
            st.progress[bi] = Fixed::raw(st.progress[bi].rawValue() - costFixed.rawValue());
            st.completed.push_back(static_cast<u8>(cur));
            done.push_back(static_cast<u8>(cur));
            st.current[bi] = 0;
        }
    }
    return done;
}

// ---------------------------------------------------------------------------
// 立项制研究：研究需要时间
// ---------------------------------------------------------------------------

int techMinTicks(int tier) {
    // 最短工期随层级上升：1 级 6 季，6 级 16 季。
    // 这是「钱买不到时间」的保证 —— 任何科技都不可能一季完成。
    int t = tier;
    if (t < 1) t = 1;
    if (t > 6) t = 6;
    return 4 + t * 2;
}

bool techStartProject(TechState& st, int techIdx, std::string* err) {
    if (techIdx < 0 || techIdx >= kTechCount) {
        if (err) *err = "非法科技编号";
        return false;
    }
    if (techCompleted(st, techIdx)) {
        if (err) *err = "【" + std::string(techInfo(techIdx).nameZh) + "】已完成";
        return false;
    }
    if (!techAvailable(st, techIdx)) {
        if (err) *err = "【" + std::string(techInfo(techIdx).nameZh) + "】的前置科技尚未完成";
        return false;
    }
    if (st.project == static_cast<u32>(techIdx)) {
        if (err) *err = "【" + std::string(techInfo(techIdx).nameZh) + "】已是当前立项";
        return false;
    }
    // 同时只能攻关一项：切换立项会丢弃既有进度（这是取舍，不是惩罚）
    st.project = static_cast<u32>(techIdx);
    st.projectProgress = Fixed(0);
    st.projectTicks = 0;
    st.current[static_cast<std::size_t>(techInfo(techIdx).branch)] = static_cast<u32>(techIdx);
    return true;
}

std::vector<u8> techTickProject(TechState& st, Fixed funding, const std::vector<u8>& completedList) {
    return techTickProject(st, funding, completedList, Fixed(0));
}

std::vector<u8> techTickProject(TechState& st, Fixed funding, const std::vector<u8>& completedList,
                                Fixed scientistBonusRaw) {
    std::vector<u8> done;
    if (st.project == TechState::kNoTech) return done;
    int idx = static_cast<int>(st.project);
    if (idx >= kTechCount) {
        st.project = TechState::kNoTech;
        return done;
    }
    const TechInfo& t = techInfo(idx);
    (void)completedList;
    const Fixed costFixed = Fixed(static_cast<i64>(t.cost));

    // 资金 → 研究点：40 cr 换 1 点。
    // 该换算率是刻意的「弱兑换」：高阶层科技的点数需求很大
    //（6 级 9600 点 ⇒ 满速需 24,000 cr/季），因此后期研究是真金白银的长期投入。
    Fixed fromMoney = Fixed(0);
    if (funding.rawValue() > 0) fromMoney = funding / Fixed(40);
    Fixed gain = st.rate + st.rateBonus + fromMoney;

    // 速度上限 = 成本 / 最短工期。这是「研究需要时间」的核心保证：
    // 无论投入多少资金，本季推进都不会超过这个值。
    const int minTicks = techMinTicks(t.tier);
    Fixed cap = costFixed / Fixed(static_cast<i64>(minTicks));
    // 科学家的加速：在**上限之内**提升推进速度（不突破最短工期）
    if (scientistBonusRaw.rawValue() > 0) gain += gain * scientistBonusRaw;
    if (gain.rawValue() > cap.rawValue()) gain = cap;
    if (gain.rawValue() < 0) gain = Fixed(0);

    // 窃取、事件和道具取得的资料沿用分支资料库，在季度上限内兑现。
    for (auto& bank : st.progress) {
        Fixed used = fxMin(fxMax(bank, Fixed(0)), fxMax(cap - gain, Fixed(0)));
        bank -= used;
        gain += used;
    }

    st.projectProgress += gain;
    ++st.projectTicks;

    if (st.projectProgress.rawValue() >= costFixed.rawValue() && st.projectTicks >= static_cast<u32>(minTicks)) {
        st.completed.push_back(static_cast<u8>(idx));
        st.projectProgress = Fixed(0);
        st.projectTicks = 0;
        st.project = TechState::kNoTech;
        st.current[static_cast<std::size_t>(t.branch)] = 0;
        st.totalCompleted += 1;
        done.push_back(static_cast<u8>(idx));
    }
    return done;
}

int techEtaTicks(const TechState& st) {
    if (st.project == TechState::kNoTech || st.project >= kTechCount) return -1;
    const TechInfo& t = techInfo(static_cast<int>(st.project));
    const Fixed costFixed = Fixed(static_cast<i64>(t.cost));
    Fixed remain = costFixed - st.projectProgress;
    if (remain.rawValue() <= 0) return 0;
    const int minTicks = techMinTicks(t.tier);
    Fixed cap = costFixed / Fixed(static_cast<i64>(minTicks));
    if (cap.rawValue() <= 0) return -1;
    // 按当前速率估算（含资金），并保证不低于剩余最短工期
    Fixed gain = st.rate + st.rateBonus;
    if (st.fundingPerTick.rawValue() > 0) gain += st.fundingPerTick / Fixed(40);
    if (gain.rawValue() > cap.rawValue()) gain = cap;
    if (gain.rawValue() <= 0) return -1;
    int eta = static_cast<int>((remain.rawValue() + gain.rawValue() - 1) / gain.rawValue());
    int floorTicks = minTicks - static_cast<int>(st.projectTicks);
    if (floorTicks < 0) floorTicks = 0;
    return eta > floorTicks ? eta : floorTicks;
}

}  // namespace gf
