#include "ai/Negotiation.h"

#include <algorithm>

#include "ai/BetrayalCalculus.h"
#include "ai/Reputation.h"
#include "domain/Treaty.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 把某帝国的「可支付能力」按条款类型求出上限
i64 capacityFor(const GameState& st, const Empire& e, const Term& t) {
    switch (t.kind) {
        case TermKind::Credits:
            return std::max<i64>(0, e.treasury.rawValue() / FIX);
        case TermKind::Influence:
            return std::max<i64>(0, e.influence.rawValue() / FIX);
        case TermKind::Unity:
            return std::max<i64>(0, e.unity.rawValue() / FIX);
        case TermKind::Commodity: {
            if (t.extra < 0 || t.extra >= kCommodityCount) return 0;
            return std::max<i64>(0, e.stock[static_cast<std::size_t>(t.extra)].rawValue() / FIX);
        }
        case TermKind::Tech:
            return static_cast<i64>(e.tech.completed.size());
        case TermKind::System:
            return static_cast<i64>(e.systems.size());
        case TermKind::Manpower:
            return std::max<i64>(0, e.popTotal);
        default:
            return 0;
    }
    (void)st;
}

/// 实际可以从该帝国拿走多少（考虑保护性下限：不能拿走全部）
i64 extractable(const GameState& st, const Empire& e, const Term& t) {
    i64 cap = capacityFor(st, e, t);
    switch (t.kind) {
        case TermKind::Credits:
            return cap * 90 / 100;      // 最多拿走 90% 国库
        case TermKind::Influence:
        case TermKind::Unity:
            return cap * 90 / 100;
        case TermKind::Commodity:
            return cap * 90 / 100;
        case TermKind::Manpower:
            return cap * 60 / 100;      // 最多迁走 60% 人口
        case TermKind::Tech:
            return cap;                  // 科技可全部转让
        case TermKind::System:
            return cap > 1 ? cap - 1 : 0;   // 至少保留一个星系
        default:
            return 0;
    }
}

/// 找到能代付的盟友：同联邦成员优先，其次防御同盟
std::vector<u32> alliesOf(const GameState& st, u32 empire) {
    std::vector<u32> out;
    const Empire* e = st.empire(empire);
    if (e == nullptr) return out;
    if (e->federation != 0xFFFFFFFFu && e->federation < st.federations.size()) {
        for (u32 m : st.federations[e->federation].members) {
            if (m != empire && st.empire(m) != nullptr && st.empire(m)->alive) out.push_back(m);
        }
    }
    for (const auto& t : st.treaties) {
        if (t.kind != TreatyKind::DefensivePact && t.kind != TreatyKind::Federation) continue;
        u32 other = 0xFFFFFFFFu;
        if (t.a == empire) other = t.b;
        else if (t.b == empire) other = t.a;
        if (other == 0xFFFFFFFFu) continue;
        const Empire* o = st.empire(other);
        if (o == nullptr || !o->alive) continue;
        if (std::find(out.begin(), out.end(), other) == out.end()) out.push_back(other);
    }
    return out;
}

}  // namespace

std::string_view dealRatingName(DealRating r) {
    switch (r) {
        case DealRating::Reasonable: return "合理";
        case DealRating::SlightlyUnfair: return "稍微不合理";
        case DealRating::VeryUnfair: return "很不合理";
        case DealRating::Count: break;
    }
    return "?";
}

Fixed systemValue(const GameState& st, u32 system) {
    const SystemNode* s = st.system(system);
    if (s == nullptr) return Fixed(0);
    // 基础价值：行星数量 × 单位价值 + 人口与开发度
    Fixed v = Fixed(20000);
    for (u32 pid : s->planets) {
        const Planet* p = st.planet(pid);
        if (p == nullptr) continue;
        v += Fixed(9000);                                   // 每颗行星
        v += Fixed(static_cast<i64>(p->pops)) * Fixed(6);    // 人口（千人）
        v += p->development * Fixed(2500);                   // 开发度
    }
    if (s->megastructure) v = v * Fixed::raw(1500);
    // 首都另有溢价，但它在交易中本来就被禁止（见 territorySwapLegal）
    if (s->capital) v = v * Fixed(3);
    return v;
}

bool territorySwapLegal(const GameState& st, u32 proposer, u32 target, const NegotiationTerms& terms,
                        std::string* note) {
    const Empire* p = st.empire(proposer);
    const Empire* t = st.empire(target);
    if (p == nullptr || t == nullptr) {
        if (note) *note = "非法主体";
        return false;
    }
    std::vector<u32> given;    // 提议方给出的星系
    std::vector<u32> taken;    // 提议方索取的星系
    for (const auto& x : terms.offer)
        if (x.kind == TermKind::System) given.push_back(static_cast<u32>(x.amount));
    for (const auto& x : terms.demand)
        if (x.kind == TermKind::System) taken.push_back(static_cast<u32>(x.amount));

    if (given.empty() && taken.empty()) return true;   // 不涉及领土

    // 规则 1：必须是互换，不能单方面索取领土
    if (given.empty() || taken.empty()) {
        if (note)
            *note = "领土只能**互换**：单方面索取领土一律不接受"
                    "（如确有优势，请用和平会议索取割让）";
        return false;
    }
    // 规则 2：首都不参与
    for (u32 s : given) {
        const SystemNode* n = st.system(s);
        if (n != nullptr && n->capital) {
            if (note) *note = "首都不参与任何领土交易";
            return false;
        }
    }
    for (u32 s : taken) {
        const SystemNode* n = st.system(s);
        if (n != nullptr && n->capital) {
            if (note) *note = "首都不参与任何领土交易";
            return false;
        }
    }
    // 规则 3：接壤。给出的星系必须与对方领土相邻；索取的星系必须与我方领土相邻。
    auto borders = [&](u32 sys, const Empire& who) {
        const SystemNode* n = st.system(sys);
        if (n == nullptr) return false;
        for (u32 nx : n->links) {
            const SystemNode* m = st.system(nx);
            if (m != nullptr && m->owner == who.id) return true;
        }
        return false;
    };
    for (u32 s : given) {
        if (s >= st.map.systems.size()) {
            if (note) *note = "给出的星系不存在";
            return false;
        }
        const SystemNode* n = st.system(s);
        if (n == nullptr || n->owner != proposer) {
            if (note) *note = "你并不拥有要给出的星系";
            return false;
        }
        if (!borders(s, *t)) {
            if (note)
                *note = "领土互换要求**接壤**：要给出的星系必须与对方的领土相邻"
                        "（飞地交换不受理）";
            return false;
        }
    }
    for (u32 s : taken) {
        if (s >= st.map.systems.size()) {
            if (note) *note = "索取的星系不存在";
            return false;
        }
        const SystemNode* n = st.system(s);
        if (n == nullptr || n->owner != target) {
            if (note) *note = "对方并不拥有该星系";
            return false;
        }
        if (!borders(s, *p)) {
            if (note)
                *note = "领土互换要求**接壤**：索取的星系必须与你自己的领土相邻"
                        "（飞地交换不受理）";
            return false;
        }
    }
    return true;
}

Fixed termValue(const GameState& st, u32 empireId, const Term& t) {
    const Empire* e = st.empire(empireId);
    (void)e;
    switch (t.kind) {
        case TermKind::Credits:
            return Fixed(t.amount);
        case TermKind::Influence:
            return Fixed(t.amount * 200);
        case TermKind::Unity:
            return Fixed(t.amount * 300);
        case TermKind::Commodity: {
            Fixed px = (t.extra >= 0 && t.extra < kCommodityCount)
                           ? st.market.spotIndex[static_cast<std::size_t>(t.extra)]
                           : Fixed(0);
            if (px.rawValue() <= 0 && t.extra >= 0 && t.extra < kCommodityCount)
                px = commodityInfo(static_cast<int>(t.extra)).basePrice;
            return Fixed::raw(mulDivSat(px.rawValue(), t.amount, 1));
        }
        case TermKind::Tech:
            return Fixed(35000);            // 每项科技估值
        case TermKind::System:
            // 用**真实估值**而非拍脑袋常数：行星、人口、开发度、巨构与首都溢价。
            // 领土交易因此能比较「这块地值不值」。
            return systemValue(st, static_cast<u32>(t.amount));
        case TermKind::Manpower:
            return Fixed(t.amount * 40);    // 每千人 40 cr
        default:
            return Fixed(0);
    }
}

Fixed termsValue(const GameState& st, u32 empire, const std::vector<Term>& terms) {
    Fixed acc = Fixed(0);
    for (const auto& t : terms) acc += termValue(st, empire, t);
    return acc;
}

bool capitalOccupied(const GameState& st, u32 owner, u32 attacker) {
    const Empire* e = st.empire(owner);
    if (e == nullptr) return false;
    const SystemNode* cap = st.system(e->capital);
    if (cap == nullptr) return false;
    return cap->owner == attacker;
}

bool hasSurrendered(const GameState& st, u32 empire, u32 against) {
    const Empire* e = st.empire(empire);
    const Empire* a = st.empire(against);
    if (e == nullptr) return true;
    if (a == nullptr) return false;
    // 无舰队 ⇒ 已放弃抵抗
    bool anyFleet = false;
    for (u32 fid : const_cast<Empire*>(e)->fleets) {
        const Fleet* f = st.fleet(fid);
        if (f != nullptr && f->strength.rawValue() > Fixed(5).rawValue()) anyFleet = true;
    }
    if (!anyFleet) return true;
    // 军力不足对方 25% ⇒ 视为放弃抵抗
    return e->military.rawValue() * 4 < a->military.rawValue();
}

NegotiationWeight negotiationWeight(const GameState& st, u32 target, u32 proposer) {
    NegotiationWeight w;
    const Empire* t = st.empire(target);
    const Empire* p = st.empire(proposer);
    if (t == nullptr || p == nullptr) return w;

    // ---- 领土压力：对方星系越多、自己越少，越需要谈 ----
    i64 mySys = static_cast<i64>(t->systems.size());
    i64 theirSys = static_cast<i64>(p->systems.size());
    i64 total = mySys + theirSys;
    if (total > 0) {
        Fixed ratio = Fixed::raw(mulDivSat(theirSys - mySys, FIX, total));
        if (ratio.rawValue() > 0) w.territory = ratio * Fixed::pct(40);
    }

    // ---- 经济压力：对方经济体量远超自己 ----
    Fixed myEco = t->economy + t->treasury / Fixed(2000);
    Fixed theirEco = p->economy + p->treasury / Fixed(2000);
    if (theirEco.rawValue() > myEco.rawValue() && theirEco.rawValue() > 0) {
        Fixed gap = Fixed(1) - myEco / theirEco;
        w.economy = fxClamp(gap, Fixed(0), Fixed(1)) * Fixed::pct(25);
    }

    // ---- 制裁/封锁压力 ----
    int sanctions = 0;
    for (const auto& r : st.relations) {
        std::size_t idx = static_cast<std::size_t>(&r - st.relations.data());
        if (idx / kMaxEmpires != target) continue;
        if (r.embargo) ++sanctions;
    }
    if (sanctions > 0) w.sanctions = Fixed(sanctions) * Fixed::pct(8);

    // ---- 战争压力：战果落后 ----
    const Relation& rel = st.relation(target, proposer);
    if (rel.atWar) {
        w.war = Fixed::pct(15) + Fixed(static_cast<i64>(rel.warScore)) * Fixed::pct(12);
    }

    // ---- 军事劣势 ----
    if (p->military.rawValue() > 0) {
        Fixed ratio = t->military / p->military;
        if (ratio.rawValue() < FIX) {
            w.military = (Fixed(1) - fxClamp(ratio, Fixed(0), Fixed(1))) * Fixed::pct(30);
        }
    }

    Fixed sum = w.territory + w.economy + w.sanctions + w.war + w.military;

    // 首都沦陷 = 极端压力，直接突破阈值
    if (capitalOccupied(st, target, proposer)) sum += Fixed::pct(60);
    // 已放弃抵抗
    if (hasSurrendered(st, target, proposer)) sum += Fixed::pct(25);

    // 信誉与观感修正：对背约者、对读档者更不愿谈
    Fixed rep = reputationOf(st, target, proposer);
    sum += (rep - Fixed::pct(50)) * Fixed::pct(20);
    sum -= Fixed(static_cast<i64>(st.rollbackCount)) * Fixed::pct(3);

    w.total = fxClamp(sum, Fixed(0), Fixed(2));

    // 阈值：AI 默认不愿谈判，只有压力足够大才肯谈
    w.threshold = Fixed::pct(35);
    if (t->mind.playerModel.typeBelief[static_cast<std::size_t>(ActorType::Exploit)].rawValue() >
        Fixed::pct(45).rawValue())
        w.threshold += Fixed::pct(15);   // 认定你是剥削型 ⇒ 更不愿谈
    if (st.empire(proposer) != nullptr && st.empire(proposer)->isPlayer)
        w.threshold += Fixed::pct(5);    // 玩家主动上门，对方会抬价

    w.willing = w.total.rawValue() >= w.threshold.rawValue();
    w.reason = "领土 " + fixedStrPlain(w.territory * Fixed(100), 0) + "% + 经济 " +
               fixedStrPlain(w.economy * Fixed(100), 0) + "% + 制裁 " +
               fixedStrPlain(w.sanctions * Fixed(100), 0) + "% + 战争 " +
               fixedStrPlain(w.war * Fixed(100), 0) + "% + 军事 " +
               fixedStrPlain(w.military * Fixed(100), 0) + "% = " +
               fixedStrPlain(w.total * Fixed(100), 0) + "%（阈值 " +
               fixedStrPlain(w.threshold * Fixed(100), 0) + "%）";
    if (capitalOccupied(st, target, proposer)) w.reason += "；首都已被占领";
    if (hasSurrendered(st, target, proposer)) w.reason += "；已放弃抵抗";
    return w;
}

std::string termText(const GameState& st, const Term& t) {
    (void)st;
    switch (t.kind) {
        case TermKind::Credits:
            return "信用点 " + groupDigits(t.amount);
        case TermKind::Influence:
            return "影响力 " + groupDigits(t.amount);
        case TermKind::Unity:
            return "凝聚力 " + groupDigits(t.amount);
        case TermKind::Commodity:
            return std::string(commodityName(static_cast<int>(t.extra))) + " " + groupDigits(t.amount);
        case TermKind::Tech:
            return "科技【" + std::string(techInfo(static_cast<int>(t.amount)).nameZh) + "】";
        case TermKind::System:
            return "星系【" + std::string(st.system(static_cast<u32>(t.amount)) != nullptr
                                              ? st.system(static_cast<u32>(t.amount))->name
                                              : std::string("?")) +
                   "】";
        case TermKind::Manpower:
            return "人力 " + groupDigits(t.amount) + " 千人";
        default:
            return "?";
    }
    return "?";
}

bool parseTerms(std::string_view text, std::vector<Term>& out, std::string* err) {
    out.clear();
    if (text.empty()) return true;
    for (auto partRaw : split(text, ',')) {
        std::string_view part = trim(partRaw);
        if (part.empty()) continue;
        std::size_t eq = part.find('=');
        if (eq == std::string_view::npos) {
            if (err) *err = "条款格式应为 key=value：" + std::string(part);
            return false;
        }
        std::string key = toLower(trim(part.substr(0, eq)));
        std::string val = std::string(trim(part.substr(eq + 1)));
        Term t;
        if (key == "credits" || key == "cr") {
            t.kind = TermKind::Credits;
            t.amount = parseInt(val, -1);
        } else if (key == "influence") {
            t.kind = TermKind::Influence;
            t.amount = parseInt(val, -1);
        } else if (key == "unity") {
            t.kind = TermKind::Unity;
            t.amount = parseInt(val, -1);
        } else if (key == "manpower" || key == "pop") {
            t.kind = TermKind::Manpower;
            t.amount = parseInt(val, -1);
        } else if (key == "tech") {
            t.kind = TermKind::Tech;
            int idx = techIndexByName(val);
            if (idx < 0) {
                if (err) *err = "未知科技：" + val;
                return false;
            }
            t.amount = idx;
        } else if (key == "system" || key == "sys") {
            t.kind = TermKind::System;
            t.amount = parseInt(val, -1);
        } else {
            int ci = commodityIndexByName(key);
            if (ci < 0) {
                if (err) *err = "未知条款类型或资源：" + key;
                return false;
            }
            t.kind = TermKind::Commodity;
            t.extra = ci;
            t.amount = parseInt(val, -1);
        }
        if (t.kind != TermKind::Tech && t.amount < 0) {
            if (err) *err = "条款数量必须为非负整数：" + std::string(part);
            return false;
        }
        out.push_back(t);
    }
    return true;
}

NegotiationResult negotiate(GameState& st, u32 proposer, u32 target, const NegotiationTerms& terms) {
    NegotiationResult res;
    Empire* p = st.empire(proposer);
    Empire* t = st.empire(target);
    if (p == nullptr || t == nullptr || proposer == target) {
        res.reason = "非法谈判双方";
        return res;
    }

    NegotiationWeight w = negotiationWeight(st, target, proposer);
    res.weight = w.total;
    res.demandValue = termsValue(st, target, terms.demand);
    res.offerValue = termsValue(st, proposer, terms.offer);

    // 首都沦陷 + 放弃抵抗 + **仍处于战争状态** ⇒ 无条件接受。
    // 战争状态这一条不可省：投降是战争的结果，不是和平时期的提款机。
    // 缺了它，只要首都曾被打下、舰队曾被歼灭，该状态就永久成立，
    // 玩家可以无限次索取（实测 40 次把对方国库从 37,438 榨到 1，
    // 连同盟友代付共取得 130,219）。
    bool capLost = capitalOccupied(st, target, proposer);
    bool surrendered = hasSurrendered(st, target, proposer);
    res.unconditional = capLost && surrendered && atWarWith(st, proposer, target);

    // 索取前提校验：要求割让的星系必须**确实属于对方**。
    // 缺了这一步，索取一个已属于自己（或第三方）的星系会被判为「接受」，
    // 但星系转移环节会静默跳过 —— 玩家付出声望与好感代价却一无所获，
    // 而界面显示谈判成功。实测「重复索取同一星系」被接受（异常）。
    for (const auto& term : terms.demand) {
        if (term.kind != TermKind::System) continue;
        const SystemNode* sys = st.system(static_cast<u32>(term.amount));
        if (sys == nullptr) {
            res.accepted = false;
            res.reason = "要求割让的星系不存在";
            return res;
        }
        if (sys->owner != target) {
            res.accepted = false;
            res.reason = "对方并不拥有该星系，无从割让";
            return res;
        }
    }
    // 同理：要求的技术必须是对方已掌握、而我方尚未掌握的
    for (const auto& term : terms.demand) {
        if (term.kind != TermKind::Tech) continue;
        int ti = static_cast<int>(term.amount);
        if (ti < 0 || ti >= kTechCount) {
            res.accepted = false;
            res.reason = "要求转移的科技不存在";
            return res;
        }
        if (!techCompleted(st.empires[target].tech, ti)) {
            res.accepted = false;
            res.reason = "对方尚未掌握该项科技";
            return res;
        }
    }

    // ---- 三档评价：必须在任何提前返回**之前**算出 ----
    // 否则被拒绝的提案会停留在默认值「合理」，玩家据此判断要不要谴责就会误判。
    {
        Fixed gap = res.demandValue - res.offerValue;
        Fixed base = fxMax(res.demandValue, Fixed(1));
        Fixed ratio = gap / base;
        if (gap.rawValue() <= 0) {
            res.rating = DealRating::Reasonable;
            res.ratingNote = "对对方不亏，属于合理提案";
        } else if (ratio.rawValue() <= Fixed::pct(25).rawValue()) {
            res.rating = DealRating::SlightlyUnfair;
            res.ratingNote = "对方小亏，属于稍微不合理的提案";
        } else {
            res.rating = DealRating::VeryUnfair;
            res.ratingNote = "对方明显吃亏，属于很不合理的提案";
        }
    }

    // ---- 领土交易规则 ----
    // 领土在 AI 的偏好里权重**最低**：割地是不可逆的损失，谈判桌上极难换到。
    // 唯一可能成交的情形是「接壤互换」—— 双方各让出一块与对方接壤的地，
    // 且都不涉及首都。除此之外一律拒绝，并说明原因。
    {
        std::string note;
        if (!territorySwapLegal(st, proposer, target, terms, &note)) {
            res.accepted = false;
            res.reason = note;
            res.rating = DealRating::VeryUnfair;
            res.ratingNote = note;
            return res;
        }
    }
    // 主权溢价：即使合法互换，AI 也会索取额外的对价补偿（割地的政治代价）。
    // 系数 2.2 意味着「换一块同等价值的地」实际要付出约 2.2 倍价值。
    Fixed territorySurcharge = Fixed(0);
    for (const auto& x : terms.demand)
        if (x.kind == TermKind::System) territorySurcharge += termValue(st, target, x);

    if (!res.unconditional && !w.willing) {
        res.accepted = false;
        res.reason = "对方拒绝谈判：" + w.reason;
        res.rating = DealRating::VeryUnfair;
        res.ratingNote = "对方根本不愿谈判";
        return res;
    }

    if (!res.unconditional) {
        // 只有当「我方付出 + 威慑溢价」足以覆盖对方损失时才接受
        Fixed premium = Fixed::pct(15) + Fixed::pct(20) * (Fixed(1) - fxClamp(w.total, Fixed(0), Fixed(1)));
        Fixed required = res.demandValue * (Fixed(1) + premium) + territorySurcharge;

        if (res.offerValue.rawValue() < required.rawValue()) {
            res.accepted = false;
            res.reason = "对方认为条件不对等：要求价值 " + fixedStr(res.demandValue, 0) +
                         (territorySurcharge.rawValue() > 0
                              ? ("（含领土主权溢价 " + fixedStr(territorySurcharge, 0) + "）")
                              : std::string()) +
                         "，你只给 " + fixedStr(res.offerValue, 0) + "（需 ≥ " + fixedStr(required, 0) + "）";
            return res;
        }
    }

    // ---- 执行条款 ----
    // 1) 我方先支付
    for (const auto& term : terms.offer) {
        i64 take = std::min<i64>(term.amount, extractable(st, *p, term));
        if (take <= 0) continue;
        switch (term.kind) {
            case TermKind::Credits:
                p->treasury -= Fixed(take);
                t->treasury += Fixed(take);
                break;
            case TermKind::Influence:
                p->influence -= Fixed(take);
                t->influence += Fixed(take);
                break;
            case TermKind::Unity:
                p->unity -= Fixed(take);
                t->unity += Fixed(take);
                break;
            case TermKind::Commodity:
                p->stock[static_cast<std::size_t>(term.extra)] -= Fixed(take);
                t->stock[static_cast<std::size_t>(term.extra)] += Fixed(take);
                break;
            case TermKind::Tech: {
                auto it = std::find(p->tech.completed.begin(), p->tech.completed.end(),
                                    static_cast<u8>(term.amount));
                if (it != p->tech.completed.end() && !techCompleted(t->tech, static_cast<int>(term.amount))) {
                    p->tech.completed.erase(it);
                    t->tech.completed.push_back(static_cast<u8>(term.amount));
                }
                break;
            }
            case TermKind::System:
                break;   // 星系转移单独处理（需要归属校验）
            case TermKind::Manpower: {
                i64 moved = std::min<i64>(take, p->popTotal);
                p->popTotal -= moved;
                t->popTotal += moved;
                break;
            }
            default:
                break;
        }
        res.applied.push_back("我方付出：" + termText(st, term));
    }
    // 星系转移（我方 → 对方）
    for (const auto& term : terms.offer) {
        if (term.kind != TermKind::System) continue;
        u32 sysId = static_cast<u32>(term.amount);
        SystemNode* sys = st.system(sysId);
        if (sys == nullptr || sys->owner != proposer) continue;
        sys->owner = target;
        p->systems.erase(std::remove(p->systems.begin(), p->systems.end(), sysId), p->systems.end());
        t->systems.push_back(sysId);
        for (u32 pid : sys->planets) {
            Planet* pl = st.planet(pid);
            if (pl != nullptr && pl->owner == proposer) pl->owner = target;
        }
        res.applied.push_back("我方割让：" + termText(st, term));
    }

    // 2) 对方支付；不足部分由盟友代付
    std::vector<u32> allies = alliesOf(st, target);
    for (const auto& term : terms.demand) {
        i64 remaining = term.amount;
        // 先由对方自己付
        i64 take = std::min<i64>(remaining, extractable(st, *t, term));
        auto pay = [&](Empire& payer, i64 n, bool isAlly, u32 allyId) {
            if (n <= 0) return;
            switch (term.kind) {
                case TermKind::Credits:
                    payer.treasury -= Fixed(n);
                    p->treasury += Fixed(n);
                    break;
                case TermKind::Influence:
                    payer.influence -= Fixed(n);
                    p->influence += Fixed(n);
                    break;
                case TermKind::Unity:
                    payer.unity -= Fixed(n);
                    p->unity += Fixed(n);
                    break;
                case TermKind::Commodity:
                    payer.stock[static_cast<std::size_t>(term.extra)] -= Fixed(n);
                    p->stock[static_cast<std::size_t>(term.extra)] += Fixed(n);
                    break;
                case TermKind::Tech: {
                    auto it = std::find(payer.tech.completed.begin(), payer.tech.completed.end(),
                                        static_cast<u8>(term.amount));
                    if (it != payer.tech.completed.end() &&
                        !techCompleted(p->tech, static_cast<int>(term.amount))) {
                        payer.tech.completed.erase(it);
                        p->tech.completed.push_back(static_cast<u8>(term.amount));
                    }
                    break;
                }
                case TermKind::Manpower: {
                    i64 moved = std::min<i64>(n, payer.popTotal);
                    payer.popTotal -= moved;
                    p->popTotal += moved;
                    break;
                }
                default:
                    break;
            }
            if (isAlly) {
                res.allyContributions.push_back("盟友 " + std::string(st.empire(allyId)->name) + " 代付 " +
                                                termText(st, term) + " 中的 " + groupDigits(n));
            }
        };
        if (take > 0) {
            pay(*t, take, false, 0);
            remaining -= take;
        }
        // 剩余由盟友按其可支付额度代付
        for (u32 allyId : allies) {
            if (remaining <= 0) break;
            Empire* ally = st.empire(allyId);
            if (ally == nullptr || !ally->alive) continue;
            i64 canPay = std::min<i64>(remaining, extractable(st, *ally, term));
            if (canPay <= 0) continue;
            pay(*ally, canPay, true, allyId);
            remaining -= canPay;
        }
        res.applied.push_back("对方付出：" + termText(st, term) +
                              (remaining > 0 ? ("（尚缺 " + groupDigits(remaining) + "，无力支付）") : ""));
    }
    // 星系割让（对方 → 我方）
    for (const auto& term : terms.demand) {
        if (term.kind != TermKind::System) continue;
        u32 sysId = static_cast<u32>(term.amount);
        SystemNode* sys = st.system(sysId);
        if (sys == nullptr || sys->owner != target) continue;
        sys->owner = proposer;
        t->systems.erase(std::remove(t->systems.begin(), t->systems.end(), sysId), t->systems.end());
        p->systems.push_back(sysId);
        for (u32 pid : sys->planets) {
            Planet* pl = st.planet(pid);
            if (pl != nullptr && pl->owner == target) pl->owner = proposer;
        }
        res.applied.push_back("对方割让：" + termText(st, term));
    }

    if (proposer == kPlayerId) st.market.margin.cash = p->treasury;
    if (target == kPlayerId) st.market.margin.cash = t->treasury;

    res.accepted = true;
    res.reason = res.unconditional
                     ? "首都已被占领且对方已放弃抵抗 —— 无条件接受全部条款"
                     : ("对方接受条款（谈判权重 " + fixedStrPlain(w.total * Fixed(100), 0) + "% ≥ 阈值 " +
                        fixedStrPlain(w.threshold * Fixed(100), 0) + "%）");

    // 无条件投降是**一次性清算**，不是提款机。
    // 早期接受条款后不改变任何状态，于是「首都已失守 + 已放弃抵抗」持续成立，
    // 玩家可以无限次索取：实测 40 次索取把对方国库从 37,438 榨到 1，
    // 连同盟友代付共取得 130,219。投降必须终结敌对状态 ——
    // 接受条款即缔结和约，之后的索取要重新满足谈判权重门槛。
    if (res.unconditional && atWarWith(st, proposer, target)) {
        declareWar(st, proposer, target, false);
        res.applied.push_back("缔结和约：战争结束");
    }
    if (!res.allyContributions.empty()) {
        res.reason += "；" + std::to_string(res.allyContributions.size()) + " 项由盟友代付";
    }

    // 谈判影响关系
    Relation& rel = st.relation(target, proposer);
    rel.opinion = fxClamp(rel.opinion - Fixed::pct(10), Fixed(-1), Fixed(1));
    if (res.demandValue.rawValue() > 0) {
        // 单方面索取会损害信誉，并让对方记住
        reputationUpdate(st, target, proposer, -Fixed::pct(12));
    }
    st.logEvent(LogPhase::Model, kLogEnvoy,
                "谈判：" + p->name + " → " + t->name + "：" + res.reason, proposer, res.demandValue);
    return res;
}

std::string negotiationReport(const GameState& st, u32 target, u32 proposer) {
    const Empire* t = st.empire(target);
    const Empire* p = st.empire(proposer);
    if (t == nullptr || p == nullptr) return "非法主体";
    NegotiationWeight w = negotiationWeight(st, target, proposer);
    std::string out;
    out += "═══ 谈判态势：" + t->name + " 面对 " + p->name + " ═══\n";
    out += "  谈判权重分解：\n";
    out += "    领土压力   " + fixedStrPlain(w.territory * Fixed(100), 0) + "%\n";
    out += "    经济压力   " + fixedStrPlain(w.economy * Fixed(100), 0) + "%\n";
    out += "    制裁压力   " + fixedStrPlain(w.sanctions * Fixed(100), 0) + "%\n";
    out += "    战争压力   " + fixedStrPlain(w.war * Fixed(100), 0) + "%\n";
    out += "    军事劣势   " + fixedStrPlain(w.military * Fixed(100), 0) + "%\n";
    out += "    ────────────────\n";
    out += "    合计       " + fixedStrPlain(w.total * Fixed(100), 0) + "%    阈值 " +
           fixedStrPlain(w.threshold * Fixed(100), 0) + "%\n";
    out += std::string("  是否愿意谈判：") +
           (w.willing ? style("愿意", Style::Good) : style("不愿意（AI 不主动倾向于谈判）", Style::Bad)) + "\n";
    out += "  依据：" + w.reason + "\n";
    bool capLost = capitalOccupied(st, target, proposer);
    bool surr = hasSurrendered(st, target, proposer);
    out += std::string("  首都状态：") + (capLost ? style("已被你占领", Style::Good) : "仍在其手中") + "\n";
    out += std::string("  抵抗意志：") + (surr ? style("已放弃抵抗", Style::Good) : "仍在抵抗") + "\n";
    if (capLost && surr) {
        out += style("  ⇒ 对方必须无条件接受你的任何条款", Style::Accent) + "\n";
    }
    std::vector<u32> allies = alliesOf(st, target);
    out += "  盟友：" + (allies.empty() ? std::string("无") : std::to_string(allies.size()) + " 个（可代付）") + "\n";
    for (u32 a : allies) {
        const Empire* al = st.empire(a);
        if (al == nullptr) continue;
        out += "    · " + al->name + "  国库 " + fixedStr(al->treasury, 0) + "  影响力 " +
               fixedStr(al->influence, 0) + "  星系 " + std::to_string(al->systems.size()) + "\n";
    }
    out += "\n  对方可支付能力：\n";
    out += "    信用点 " + groupDigits(std::max<i64>(0, t->treasury.rawValue() / FIX)) + "  影响力 " +
           groupDigits(std::max<i64>(0, t->influence.rawValue() / FIX)) + "  凝聚力 " +
           groupDigits(std::max<i64>(0, t->unity.rawValue() / FIX)) + "\n";
    out += "    科技 " + std::to_string(t->tech.completed.size()) + " 项  星系 " +
           std::to_string(t->systems.size()) + " 个  人力 " + groupDigits(t->popTotal) + " 千人\n";
    return out;
}

}  // namespace gf
