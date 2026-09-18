#include "domain/Government.h"

#include <algorithm>
#include <string>

#include "util/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Personnel.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 选举周期（季）
constexpr u32 kElectionCycle = 40;
/// 竞选期长度（季）
constexpr u32 kCampaignLength = 6;
/// 公开支持一位候选人的影响力成本
constexpr i64 kEndorseCost = 200;
/// 镇压的影响力成本与合法性收益
constexpr i64 kSuppressCost = 120;

std::string makeName(GameState& st) {
    static const char* kFirst[] = {"阿", "贝", "柯", "德", "恩", "法", "格", "海",
                                   "伊", "杰", "卡", "洛", "米", "诺", "奥", "佩"};
    static const char* kLast[] = {"恩", "斯", "尔", "顿", "森", "华", "理", "文",
                                  "德", "拉", "克", "姆", "诺", "维", "奇", "亚"};
    std::string s;
    s += kFirst[st.rng.pick(RngStream::Empire, sizeof(kFirst) / sizeof(kFirst[0]))];
    s += kLast[st.rng.pick(RngStream::Empire, sizeof(kLast) / sizeof(kLast[0]))];
    return s;
}

/// 归一化候选人支持率，使其总和为 1
void normalize(std::vector<Candidate>& c) {
    Fixed total = Fixed(0);
    for (const auto& x : c) total += x.support;
    if (total.rawValue() <= 0) {
        Fixed even = Fixed(1) / Fixed(static_cast<i64>(c.size() ? c.size() : 1));
        for (auto& x : c) x.support = even;
        return;
    }
    for (auto& x : c) x.support = x.support / total;
}

}  // namespace

std::string_view legitimacySourceName(LegitimacySource s) {
    switch (s) {
        case LegitimacySource::Election: return "选举授权";
        case LegitimacySource::Tradition: return "血统传统";
        case LegitimacySource::Fear: return "恐惧镇压";
        case LegitimacySource::Performance: return "绩效表现";
        case LegitimacySource::Faith: return "信仰教义";
        case LegitimacySource::Consensus: return "共识协同";
        case LegitimacySource::Count: break;
    }
    return "?";
}

std::string legitimacySourceDesc(LegitimacySource s) {
    switch (s) {
        case LegitimacySource::Election:
            return "合法性来自选票。定期改选：失去民心就下台。"
                   "民怨对合法性的打击最大，但改选成功会带来一次合法性提振。";
        case LegitimacySource::Tradition:
            return "合法性来自血统与传承。稳定但僵化：领袖过世可能触发**继承危机**，"
                   "期间合法性骤降、派系蠢动。";
        case LegitimacySource::Fear:
            return "合法性来自压制。可以主动**镇压**快速拉高合法性，"
                   "但会累积**怨恨**，怨恨越高民怨与政变风险越大。";
        case LegitimacySource::Performance:
            return "合法性来自成果。国库充盈、战果为正时合法性上升，"
                   "经济恶化或战败时跌得比谁都快。";
        case LegitimacySource::Faith:
            return "合法性来自教义。容忍度低（异见推高民怨），但凝聚力强、"
                   "对经济波动不敏感。";
        case LegitimacySource::Consensus:
            return "合法性来自群体意志。没有派系之争，但决策迟缓、"
                   "对外来冲击反应迟钝。";
        default: break;
    }
    return "";
}

LegitimacySource legitimacySourceOf(u8 government) {
    switch (government) {
        case 0:    // 民主制
        case 1:    // 共和制
        case 12:   // 赛博共和
        case 17:   // 议会民主主义
            return LegitimacySource::Election;
        case 4:    // 帝制
        case 10:   // 部落议会
            return LegitimacySource::Tradition;
        case 2:    // 寡头制
        case 3:    // 独裁制
        case 8:    // 军事委员会
            return LegitimacySource::Fear;
        case 6:    // 技术官僚制
        case 7:    // 企业制
            return LegitimacySource::Performance;
        case 5:    // 神权制
        case 13:   // 神谕制
            return LegitimacySource::Faith;
        case 9:    // 蜂群意识
        case 11:   // 无政府
            return LegitimacySource::Consensus;
        default:
            return LegitimacySource::Performance;
    }
}

bool isElective(u8 government) {
    return legitimacySourceOf(government) == LegitimacySource::Election;
}

bool allowsRepression(u8 government) {
    LegitimacySource s = legitimacySourceOf(government);
    return s == LegitimacySource::Fear || s == LegitimacySource::Tradition ||
           s == LegitimacySource::Faith;
}

Fixed legitimacyFromSource(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return Fixed(0);
    const GovernmentState& g = e->gov;
    const Fixed base = governmentInfo(e->government).legitimacy;
    switch (legitimacySourceOf(e->government)) {
        case LegitimacySource::Election:
            // 民怨对选举授权打击最大
            return base - e->domestic.unrest * Fixed::pct(35);
        case LegitimacySource::Tradition:
            // 继承危机期间骤降
            return base - (g.successionCrisis > 0 ? Fixed::pct(25) : Fixed(0));
        case LegitimacySource::Fear:
            // 镇压提高合法性，怨恨反噬
            return base + g.repression * Fixed::pct(25) - g.resentment * Fixed::pct(20);
        case LegitimacySource::Performance: {
            // 国库与战果直接决定
            Fixed wealth = e->lastIncome.rawValue() > 0 ? Fixed::pct(10) : Fixed::pct(-12);
            Fixed war = e->warsWon > 0 ? Fixed::pct(5) : Fixed(0);
            return base + wealth + war;
        }
        case LegitimacySource::Faith:
            // 对经济不敏感，但异见（民怨）仍会侵蚀
            return base - e->domestic.unrest * Fixed::pct(15);
        case LegitimacySource::Consensus:
            // 稳定但反应迟钝：几乎不受短期波动影响
            return base;
        default: break;
    }
    return base;
}

bool suppress(GameState& st, u32 empire, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    if (!allowsRepression(e->government)) {
        if (msg)
            *msg = std::string(governmentInfo(e->government).nameZh) +
                   " 不以恐惧为合法性来源，镇压会适得其反";
        return false;
    }
    if (e->influence.rawValue() < Fixed(kSuppressCost).rawValue()) {
        if (msg) *msg = "影响力不足：镇压需要 " + std::to_string(kSuppressCost);
        return false;
    }
    e->influence -= Fixed(kSuppressCost);
    e->gov.repression = fxClamp(e->gov.repression + Fixed::pct(20), Fixed(0), Fixed(1));
    e->gov.resentment = fxClamp(e->gov.resentment + Fixed::pct(12), Fixed(0), Fixed(1));
    // 立即见效：民怨短期下降，但怨恨会把它推回来
    e->domestic.unrest = fxClamp(e->domestic.unrest - Fixed::pct(6), Fixed(0), Fixed(1));
    if (msg)
        *msg = "已实施镇压（合法性 +5%，民怨 -6%，但**怨恨 +12%**）";
    st.logEvent(LogPhase::Domestic, "gov.repress",
                e->name + " 实施镇压（怨恨累积 " +
                    fixedStrPlain(e->gov.resentment * Fixed(100), 0) + "%）",
                empire);
    return true;
}

bool callElection(GameState& st, u32 empire, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    if (!isElective(e->government)) {
        if (msg)
            *msg = std::string(governmentInfo(e->government).nameZh) +
                   " 不通过选举产生领袖（合法性来源：" +
                   std::string(legitimacySourceName(legitimacySourceOf(e->government))) + "）";
        return false;
    }
    if (e->gov.election.active) {
        if (msg) *msg = "选举已在进程中";
        return false;
    }
    Election el;
    el.active = true;
    el.startTick = st.tick;
    el.endTick = static_cast<u32>(st.tick) + kCampaignLength;
    // 在位者 + 2 名挑战者
    Candidate inc;
    inc.name = e->ruler.name;
    inc.trait = e->ruler.trait;
    inc.skill = e->ruler.skill;
    inc.incumbent = true;
    // 在位者支持率受民怨影响：民怨高则支持率低
    inc.support = Fixed::pct(45) - e->domestic.unrest * Fixed::pct(30) +
                  (e->stability - Fixed::pct(50)) * Fixed::pct(20);
    if (inc.support.rawValue() < Fixed::pct(5).rawValue()) inc.support = Fixed::pct(5);
    el.candidates.push_back(inc);
    for (int i = 0; i < 2; ++i) {
        Candidate c;
        c.name = makeName(st);
        c.trait = static_cast<RulerTrait>(
            1 + static_cast<int>(st.rng.pick(RngStream::Empire, static_cast<std::size_t>(RulerTrait::Count) - 1)));
        c.skill = Fixed::pct(35) + Fixed::pct(static_cast<i64>(st.rng.range(RngStream::Empire, 0, 45)));
        c.support = Fixed::pct(20) + Fixed::pct(static_cast<i64>(st.rng.range(RngStream::Empire, 0, 25)));
        el.candidates.push_back(c);
    }
    normalize(el.candidates);
    e->gov.election = el;
    if (msg)
        *msg = "选举启动，为期 " + std::to_string(kCampaignLength) + " 季（共 " +
               std::to_string(el.candidates.size()) + " 位候选人）";
    st.logEvent(LogPhase::Domestic, "gov.election", e->name + " 启动选举", empire);
    return true;
}

bool endorseCandidate(GameState& st, u32 empire, int index, std::string* msg) {
    Empire* e = st.empire(empire);
    if (e == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    Election& el = e->gov.election;
    if (!el.active) {
        if (msg) *msg = "当前没有进行中的选举";
        return false;
    }
    if (index < 0 || index >= static_cast<int>(el.candidates.size())) {
        if (msg) *msg = "候选人编号越界";
        return false;
    }
    if (e->influence.rawValue() < Fixed(kEndorseCost).rawValue()) {
        if (msg) *msg = "影响力不足：公开支持需要 " + std::to_string(kEndorseCost);
        return false;
    }
    e->influence -= Fixed(kEndorseCost);
    el.candidates[static_cast<std::size_t>(index)].support += Fixed::pct(12);
    el.endorsed = index;
    el.campaignSpent += Fixed(kEndorseCost);
    normalize(el.candidates);
    if (msg)
        *msg = "已公开支持 " + el.candidates[static_cast<std::size_t>(index)].name +
               "（其支持率 +12%，影响力 -" + std::to_string(kEndorseCost) + "）";
    return true;
}

void governmentPhase(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        GovernmentState& g = e.gov;

        // ---- 选举进程 ----
        if (g.election.active) {
            // 竞选期内支持率自然漂移
            for (auto& c : g.election.candidates)
                c.support = fxClamp(c.support + Fixed::pct(static_cast<i64>(st.rng.range(RngStream::Empire, -2, 3))),
                                    Fixed::pct(1), Fixed(1));
            normalize(g.election.candidates);
            if (st.tick >= g.election.endTick) {
                // 计票
                int winner = 0;
                for (std::size_t i = 1; i < g.election.candidates.size(); ++i)
                    if (g.election.candidates[i].support.rawValue() >
                        g.election.candidates[static_cast<std::size_t>(winner)].support.rawValue())
                        winner = static_cast<int>(i);
                const Candidate& w = g.election.candidates[static_cast<std::size_t>(winner)];
                bool changed = w.name != e.ruler.name;
                e.ruler.name = w.name;
                e.ruler.trait = w.trait;
                e.ruler.skill = w.skill;
                e.ruler.reignStart = st.tick;
                e.ruler.termEnd = static_cast<u32>(st.tick) + kElectionCycle;
                e.ruler.elected = true;
                if (!changed) e.ruler.electionsWon += 1;
                // 选举结果的影响：换人会带来动荡，连任则提振合法性
                if (changed) {
                    e.stability = fxClamp(e.stability - Fixed::pct(5), Fixed(0), Fixed(1));
                } else {
                    e.domestic.legitimacy =
                        fxClamp(e.domestic.legitimacy + Fixed::pct(6), Fixed(0), Fixed(1));
                }
                g.election.electionsHeld += 1;
                st.logEvent(LogPhase::Domestic, "gov.election",
                            e.name + " 选举结束：" + w.name + " 当选（" +
                                std::string(rulerTraitName(w.trait)) + "），" +
                                (changed ? "政权更替" : "成功连任"),
                            e.id);
                g.election.active = false;
                g.election.endorsed = -1;
            }
        } else if (isElective(e.government)) {
            // 到期自动启动选举
            if (e.ruler.termEnd > 0 && st.tick >= e.ruler.termEnd) {
                (void)callElection(st, e.id, nullptr);
            }
        }

        // ---- 镇压的代价：怨恨随时间推高民怨 ----
        if (g.resentment.rawValue() > 0) {
            // 怨恨每季自然消退 2%，但持续把民怨往上推
            e.domestic.unrest = fxClamp(e.domestic.unrest + g.resentment * Fixed::pct(1), Fixed(0), Fixed(1));
            g.resentment = fxClamp(g.resentment - Fixed::pct(2), Fixed(0), Fixed(1));
            // 镇压强度也随之衰减（需要持续投入）
            g.repression = fxClamp(g.repression - Fixed::pct(1), Fixed(0), Fixed(1));
        }

        // ---- 继承危机（传统类政体）----
        if (g.successionCrisis > 0) {
            --g.successionCrisis;
            if (g.successionCrisis == 0) {
                st.logEvent(LogPhase::Domestic, "gov.succession",
                            e.name + " 的继承危机结束，新领袖坐稳位置", e.id);
            }
        }
    }
}

std::string governmentReport(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    const GovernmentInfo& gi = governmentInfo(e->government);
    std::string out;
    TextTable t;
    t.header({"项目", "值"});
    t.row({"政体", std::string(gi.nameZh)});
    t.row({"合法性来源", std::string(legitimacySourceName(legitimacySourceOf(e->government)))});
    t.row({"行动点加成", "+" + std::to_string(gi.apBonus)});
    t.row({"合法性基线", fixedStrPlain(gi.legitimacy * Fixed(100), 0) + "%"});
    t.row({"当前合法性", fixedStrPlain(e->domestic.legitimacy * Fixed(100), 0) + "%"});
    t.row({"来源提供的合法性", fixedStrPlain(legitimacyFromSource(st, empire) * Fixed(100), 0) + "%"});
    t.row({"领袖产生", isElective(e->government) ? "选举（每 40 季）" : "继承 / 任命"});
    t.row({"可否镇压", allowsRepression(e->government) ? "可以" : "不可（会适得其反）"});
    if (legitimacySourceOf(e->government) == LegitimacySource::Fear) {
        t.row({"镇压强度", fixedStrPlain(e->gov.repression * Fixed(100), 0) + "%"});
        t.row({"累积怨恨", fixedStrPlain(e->gov.resentment * Fixed(100), 0) + "%"});
    }
    out += t.render();
    out += "\n  " + legitimacySourceDesc(legitimacySourceOf(e->government)) + "\n";
    return out;
}

std::string electionText(const GameState& st, u32 empire) {
    const Empire* e = st.empire(empire);
    if (e == nullptr) return "非法主体\n";
    const Election& el = e->gov.election;
    if (!el.active) {
        if (!isElective(e->government))
            return "  本政体不举行选举（合法性来源：" +
                   std::string(legitimacySourceName(legitimacySourceOf(e->government))) + "）\n";
        return "  （当前没有进行中的选举。任期至 " + std::to_string(e->ruler.termEnd) +
               " 季；用 `greyfall gov --elect` 提前改选）\n";
    }
    std::string out;
    out += "  竞选期：第 " + std::to_string(el.startTick) + " ~ " + std::to_string(el.endTick) +
           " 季（当前 " + std::to_string(st.tick) + "）\n\n";
    TextTable t;
    t.header({"#", "候选人", "出身", "能力", "支持率", "备注"});
    for (std::size_t i = 0; i < el.candidates.size(); ++i) {
        const Candidate& c = el.candidates[i];
        std::string note;
        if (c.incumbent) note = "在位者";
        if (static_cast<int>(i) == el.endorsed) note += " ★你公开支持";
        t.row({std::to_string(i), c.name, std::string(rulerTraitName(c.trait)),
               fixedStrPlain(c.skill * Fixed(100), 0) + "%",
               fixedStrPlain(c.support * Fixed(100), 0) + "%", note});
    }
    out += t.render();
    out += "  用 `greyfall gov --support <编号>` 公开支持某位候选人（200 影响力，支持率 +12%）。\n";
    out += "  民怨越高，在位者支持率越低 —— 失去民心就下台。\n";
    return out;
}

}  // namespace gf
