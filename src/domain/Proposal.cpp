#include "domain/Proposal.h"

#include <algorithm>
#include <string>

#include "ai/Negotiation.h"
#include "cli/TextTable.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Planet.h"
#include "domain/Treaty.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 提案有效期：超过这么多季未被回应即自动失效
constexpr u32 kProposalLifetime = 8;

/// 提案箱上限，避免堆积成山
constexpr std::size_t kMaxProposals = 8;

bool bordersEmpire(const GameState& st, u32 system, u32 empire) {
    const SystemNode* s = st.system(system);
    if (s == nullptr) return false;
    for (u32 nx : s->links) {
        const SystemNode* m = st.system(nx);
        if (m != nullptr && m->owner == empire) return true;
    }
    return false;
}

}  // namespace

std::string_view proposalKindName(ProposalKind k) {
    switch (k) {
        case ProposalKind::TradeGoods: return "资源互换";
        case ProposalKind::ResearchPact: return "研究协定";
        case ProposalKind::NonAggression: return "互不侵犯";
        case ProposalKind::JointIntel: return "联合情报";
        case ProposalKind::TerritorySwap: return "领土互换";
        case ProposalKind::Tribute: return "索贡";
        case ProposalKind::Count: break;
    }
    return "?";
}

Proposal* ProposalBox::find(u32 id) {
    for (auto& p : items)
        if (p.id == id) return &p;
    return nullptr;
}

void diplomacyPhase(GameState& st) {
    // 清理过期提案
    st.proposals.items.erase(
        std::remove_if(st.proposals.items.begin(), st.proposals.items.end(),
                       [&](const Proposal& p) { return st.tick >= p.expiresTick; }),
        st.proposals.items.end());

    if (st.proposals.items.size() >= kMaxProposals) return;
    // 节奏：每 6 季最多产生一份新提案，避免刷屏
    if (st.tick % 6 != 0) return;

    for (const auto& e : st.empires) {
        if (e.isPlayer || !e.alive) continue;
        if (st.proposals.items.size() >= kMaxProposals) break;
        // 关系太差就不会提案
        const Relation& rel = st.relation(e.id, kPlayerId);
        if (rel.atWar) continue;
        if (rel.opinion.rawValue() < Fixed::pct(-30).rawValue()) continue;

        Proposal p;
        p.from = e.id;
        p.to = kPlayerId;
        p.createdTick = st.tick;
        p.expiresTick = static_cast<u32>(st.tick) + kProposalLifetime;

        // 依关系与需求选择提案类型
        const Relation& mine = st.relation(kPlayerId, e.id);
        bool friendly = rel.opinion.rawValue() > Fixed::pct(20).rawValue();
        bool hasPact = hasTreaty(st.treaties, TreatyKind::ResearchPact, kPlayerId, e.id);
        (void)mine;

        // 领土互换：极少数情况下才会提出（关系极好 + 存在接壤飞地）
        bool offeredTerritory = false;
        if (friendly && st.tick % 24 == 0) {
            for (u32 mineSys : st.empire(kPlayerId)->systems) {
                const SystemNode* ms = st.system(mineSys);
                if (ms == nullptr || ms->capital) continue;
                if (!bordersEmpire(st, mineSys, e.id)) continue;
                for (u32 theirSys : e.systems) {
                    const SystemNode* ts = st.system(theirSys);
                    if (ts == nullptr || ts->capital) continue;
                    if (!bordersEmpire(st, theirSys, kPlayerId)) continue;
                    // 找到一对接壤飞地：提出互换
                    Fixed mv = systemValue(st, mineSys), tv = systemValue(st, theirSys);
                    if (mv.rawValue() <= 0 || tv.rawValue() <= 0) continue;
                    // 只在价值接近时才提（差 30% 以内），并补足差额
                    Fixed lo = fxMin(mv, tv), hi = fxMax(mv, tv);
                    if (hi.rawValue() > lo.rawValue() * 13 / 10) continue;
                    p.kind = ProposalKind::TerritorySwap;
                    p.title = "领土互换提议";
                    p.body = e.name + " 提议互换接壤星系：" + ms->name + " ⇄ " + ts->name +
                             "（双方价值相当，均非首都）";
                    p.ifAccept = "你的 " + ms->name + " 归对方，" + ts->name + " 归你";
                    p.ifReject = "关系小幅恶化；对方可能另寻他路";
                    int diff = static_cast<int>((tv.rawValue() - mv.rawValue()) / fxMax(mv, Fixed(1)).rawValue());
                    p.fairnessPct = static_cast<int>(diff);
                    offeredTerritory = true;
                    break;
                }
                if (offeredTerritory) break;
            }
        }

        if (!offeredTerritory) {
            if (!hasPact && friendly) {
                p.kind = ProposalKind::ResearchPact;
                p.title = "研究协定提议";
                p.body = e.name + " 提议缔结研究协定，双方研究速率小幅提升。";
                p.ifAccept = "获得研究速率加成，关系改善";
                p.ifReject = "关系小幅恶化";
                p.fairnessPct = 5;
            } else if (rel.opinion.rawValue() > Fixed::pct(60).rawValue()) {
                p.kind = ProposalKind::JointIntel;
                p.title = "联合情报提议";
                p.body = e.name + " 提议共享情报网络，双方对第三方的视野提升。";
                p.ifAccept = "获得情报视野加成";
                p.ifReject = "无直接损失，但对方会记住";
                p.fairnessPct = 8;
            } else {
                p.kind = ProposalKind::TradeGoods;
                // 用双方都有的常规商品做一笔等价交换
                int c = static_cast<int>(st.tick % kCommodityCount);
                if (!commodityInfo(c).tradable) c = static_cast<int>(Commodity::Minerals);
                i64 qty = 400 + static_cast<i64>(st.tick % 7) * 120;
                p.title = "资源互换提议";
                p.body = e.name + " 提议以 " + std::to_string(qty) + " 单位" +
                         std::string(commodityName(c)) + " 交换你方同等价值的信用点。";
                p.ifAccept = "按市价成交，关系改善";
                p.ifReject = "关系小幅恶化";
                p.fairnessPct = 0;
            }
        }

        p.id = st.proposals.nextId++;
        st.proposals.items.push_back(std::move(p));
        st.logEvent(LogPhase::Model, "diplo.proposal",
                    std::string("收到来自 ") + e.name + " 的" +
                        std::string(proposalKindName(st.proposals.items.back().kind)) + "提议",
                    e.id);
    }
}

void aiTradePhase(GameState& st) {
    // AI 之间也会交易：关系良好且互补的两个 AI 定期成交。
    // 领土在 AI 之间的交易里**权重最低** —— 只有接壤互换才可能发生，
    // 且和玩家侧走同一套合法性判定。
    if (st.tick % 4 != 0) return;
    for (std::size_t i = 0; i < st.empires.size(); ++i) {
        Empire& a = st.empires[i];
        if (a.isPlayer || !a.alive) continue;
        for (std::size_t j = i + 1; j < st.empires.size(); ++j) {
            Empire& b = st.empires[j];
            if (b.isPlayer || !b.alive) continue;
            if (atWarWith(st, a.id, b.id)) continue;
            const Relation& rel = st.relation(a.id, b.id);
            // 门槛刻意放低（只需非敌对）：实测 AI 之间的观感长期在 0~15%，
            // 若要求 25% 则永远不会成交。贸易本身会改善关系（每笔 +2%），
            // 因此低门槛反而形成「越贸易越亲近」的正反馈。
            if (rel.opinion.rawValue() < Fixed::pct(-10).rawValue()) continue;

            // 找一种「我库存充裕、你库存紧张」的商品做小额互换。
            // 注意不能按**流量**（产出−需求）判断：AI 之间的贸易路线早已把
            // 缺口填平，importNeed 长期为 0，实测 21 对 AI 全部「无互补商品」。
            // 库存差异才是真实且持续存在的交易动机。
            for (int c = 0; c < kCommodityCount; ++c) {
                if (!commodityInfo(c).tradable) continue;
                Fixed aStock = a.stock[static_cast<std::size_t>(c)];
                Fixed bStock = b.stock[static_cast<std::size_t>(c)];
                // 我至少要显著多于对方，才有出让的余地
                if (aStock.rawValue() <= bStock.rawValue() * 2) continue;
                Fixed qty = (aStock - bStock) / Fixed(4);
                qty = fxMin(qty, Fixed(300));
                if (qty.rawValue() < Fixed(20).rawValue()) continue;
                Fixed px = st.market.spotIndex[static_cast<std::size_t>(c)];
                if (px.rawValue() <= 0) px = commodityInfo(c).basePrice;
                Fixed value = px * qty;
                if (b.treasury.rawValue() < value.rawValue()) continue;
                a.stock[static_cast<std::size_t>(c)] -= qty;
                b.stock[static_cast<std::size_t>(c)] += qty;
                b.treasury -= value;
                a.treasury += value;
                st.relation(a.id, b.id).opinion =
                    fxClamp(st.relation(a.id, b.id).opinion + Fixed::pct(2), Fixed(-1), Fixed(1));
                st.relation(b.id, a.id).opinion =
                    fxClamp(st.relation(b.id, a.id).opinion + Fixed::pct(2), Fixed(-1), Fixed(1));
                st.logEvent(LogPhase::Economy, "trade.ai",
                            a.name + " 与 " + b.name + " 成交：" + fixedStr(qty, 0) + " 单位" +
                                std::string(commodityName(c)) + "（" + fixedStr(value, 0) + " cr）",
                            a.id);
                break;   // 每次只成交一笔，避免单季刷爆
            }
        }
    }
}

bool proposalAccept(GameState& st, u32 id, std::string* msg) {
    Proposal* p = st.proposals.find(id);
    if (p == nullptr) {
        if (msg) *msg = "提案不存在或已失效";
        return false;
    }
    Empire* from = st.empire(p->from);
    Empire* me = st.empire(kPlayerId);
    if (from == nullptr || me == nullptr) {
        if (msg) *msg = "非法主体";
        return false;
    }
    switch (p->kind) {
        case ProposalKind::ResearchPact: {
            Treaty t;
            t.kind = TreatyKind::ResearchPact;
            t.a = kPlayerId;
            t.b = p->from;
            t.signedTick = st.tick;
            t.expireTick = static_cast<i64>(st.tick) + 40;
            upsertTreaty(st.treaties, t);
            me->tech.rate += Fixed::raw(150);
            from->tech.rate += Fixed::raw(150);
            if (msg) *msg = "已缔结研究协定（双方研究速率 +0.15，持续 40 季）";
            break;
        }
        case ProposalKind::NonAggression: {
            Treaty t;
            t.kind = TreatyKind::NonAggression;
            t.a = kPlayerId;
            t.b = p->from;
            t.signedTick = st.tick;
            t.expireTick = static_cast<i64>(st.tick) + 60;
            upsertTreaty(st.treaties, t);
            if (msg) *msg = "已缔结互不侵犯条约（60 季）";
            break;
        }
        case ProposalKind::JointIntel: {
            Treaty t;
            t.kind = TreatyKind::JointIntel;
            t.a = kPlayerId;
            t.b = p->from;
            t.signedTick = st.tick;
            t.expireTick = static_cast<i64>(st.tick) + 40;
            upsertTreaty(st.treaties, t);
            me->counterIntel = fxClamp(me->counterIntel + Fixed::pct(15), Fixed(0), Fixed(1));
            from->counterIntel = fxClamp(from->counterIntel + Fixed::pct(15), Fixed(0), Fixed(1));
            if (msg) *msg = "已共享情报（双方反间谍 +15%）";
            break;
        }
        case ProposalKind::TradeGoods: {
            Fixed amount = Fixed(12000);
            if (me->treasury.rawValue() < amount.rawValue()) {
                if (msg) *msg = "国库不足以支付这笔交易";
                return false;
            }
            me->treasury -= amount;
            from->treasury += amount;
            st.market.margin.cash = me->treasury;
            if (msg) *msg = "已完成资源互换（支付 " + fixedStr(amount, 0) + " cr）";
            break;
        }
        case ProposalKind::TerritorySwap: {
            // 真正执行互换：找到提案里提到的那对星系
            // （简单起见：选一对符合「接壤且非首都」的）
            u32 mineSys = kNoSystem, theirSys = kNoSystem;
            for (u32 s : me->systems) {
                const SystemNode* n = st.system(s);
                if (n == nullptr || n->capital) continue;
                if (!bordersEmpire(st, s, p->from)) continue;
                mineSys = s;
                break;
            }
            for (u32 s : from->systems) {
                const SystemNode* n = st.system(s);
                if (n == nullptr || n->capital) continue;
                if (!bordersEmpire(st, s, kPlayerId)) continue;
                theirSys = s;
                break;
            }
            if (mineSys == kNoSystem || theirSys == kNoSystem) {
                if (msg) *msg = "已无可互换的接壤星系";
                return false;
            }
            SystemNode* ms = st.system(mineSys);
            SystemNode* ts = st.system(theirSys);
            ms->owner = p->from;
            ts->owner = kPlayerId;
            me->systems.erase(std::remove(me->systems.begin(), me->systems.end(), mineSys), me->systems.end());
            from->systems.push_back(mineSys);
            from->systems.erase(std::remove(from->systems.begin(), from->systems.end(), theirSys),
                                from->systems.end());
            me->systems.push_back(theirSys);
            for (u32 pid : ms->planets) {
                Planet* pl = st.planet(pid);
                if (pl != nullptr) pl->owner = p->from;
            }
            for (u32 pid : ts->planets) {
                Planet* pl = st.planet(pid);
                if (pl != nullptr) pl->owner = kPlayerId;
            }
            if (msg) *msg = "已完成领土互换：" + ms->name + " ⇄ " + ts->name;
            break;
        }
        case ProposalKind::Tribute: {
            Fixed amount = Fixed(15000);
            if (me->treasury.rawValue() < amount.rawValue()) {
                if (msg) *msg = "国库不足以支付贡金";
                return false;
            }
            me->treasury -= amount;
            from->treasury += amount;
            st.market.margin.cash = me->treasury;
            if (msg) *msg = "已支付贡金 " + fixedStr(amount, 0) + " cr，战争威胁暂时解除";
            break;
        }
        case ProposalKind::Count: break;
    }
    st.relation(kPlayerId, p->from).opinion =
        fxClamp(st.relation(kPlayerId, p->from).opinion + Fixed::pct(8), Fixed(-1), Fixed(1));
    st.relation(p->from, kPlayerId).opinion =
        fxClamp(st.relation(p->from, kPlayerId).opinion + Fixed::pct(8), Fixed(-1), Fixed(1));
    st.logEvent(LogPhase::Model, "diplo.accepted",
                std::string("接受了 ") + from->name + " 的" +
                    std::string(proposalKindName(p->kind)) + "提议",
                kPlayerId);
    st.proposals.items.erase(std::remove_if(st.proposals.items.begin(), st.proposals.items.end(),
                                            [&](const Proposal& x) { return x.id == id; }),
                             st.proposals.items.end());
    return true;
}

bool proposalReject(GameState& st, u32 id, std::string* msg) {
    Proposal* p = st.proposals.find(id);
    if (p == nullptr) {
        if (msg) *msg = "提案不存在或已失效";
        return false;
    }
    const Empire* from = st.empire(p->from);
    st.relation(p->from, kPlayerId).opinion =
        fxClamp(st.relation(p->from, kPlayerId).opinion - Fixed::pct(6), Fixed(-1), Fixed(1));
    if (msg)
        *msg = std::string("已拒绝 ") + (from ? from->name : "?") + " 的提议（对方观感 -6%）";
    st.logEvent(LogPhase::Model, "diplo.rejected",
                std::string("拒绝了 ") + (from ? from->name : "?") + " 的" +
                    std::string(proposalKindName(p->kind)) + "提议",
                kPlayerId);
    st.proposals.items.erase(std::remove_if(st.proposals.items.begin(), st.proposals.items.end(),
                                            [&](const Proposal& x) { return x.id == id; }),
                             st.proposals.items.end());
    return true;
}

std::string proposalText(const GameState& st, u32 id) {
    for (const auto& p : st.proposals.items) {
        if (p.id != id) continue;
        const Empire* from = st.empire(p.from);
        std::string out;
        TextTable t;
        t.header({"项目", "值"});
        t.row({"提案", std::to_string(p.id)});
        t.row({"提出方", from ? from->name : "?"});
        t.row({"类型", std::string(proposalKindName(p.kind))});
        t.row({"标题", p.title});
        t.row({"内容", p.body});
        t.row({"接受", p.ifAccept});
        t.row({"拒绝", p.ifReject});
        t.row({"公平度", (p.fairnessPct >= 0 ? "+" : "") + std::to_string(p.fairnessPct) + "%" +
                              (p.fairnessPct < -20 ? "（对你不利）" : "")});
        t.row({"失效于", std::to_string(p.expiresTick) + " 季"});
        out += t.render();
        out += "\n  用 `greyfall proposal --accept " + std::to_string(p.id) + "` 或 `--reject " +
               std::to_string(p.id) + "` 回应。\n";
        return out;
    }
    return "  提案不存在或已失效\n";
}

std::string proposalListText(const GameState& st) {
    if (st.proposals.items.empty()) return "  （当前没有待回应的提议）\n";
    std::string out;
    TextTable t;
    t.header({"#", "提出方", "类型", "内容", "公平度", "失效"});
    for (const auto& p : st.proposals.items) {
        const Empire* from = st.empire(p.from);
        std::string title = p.title;
        if (title.size() > 26) title = title.substr(0, 24) + "…";
        t.row({std::to_string(p.id), from ? from->name.substr(0, 6) : std::string("?"),
               std::string(proposalKindName(p.kind)), title,
               (p.fairnessPct >= 0 ? "+" : "") + std::to_string(p.fairnessPct) + "%",
               std::to_string(p.expiresTick)});
    }
    out += t.render();
    return out;
}

}  // namespace gf
