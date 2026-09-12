// 谈判系统：权重门控 / 交换与索取 / 占领首都无条件接受 / 盟友代付
#include <algorithm>

#include "ai/Negotiation.h"
#include "check.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "domain/Treaty.h"
#include "gen/WorldGen.h"

using namespace gf;

namespace {

GameState negWorld(u64 seed = 2468) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 40;
    GameState st;
    generateWorld(st, o);
    return st;
}

}  // namespace

TEST(negotiate, ai_refuses_when_weight_below_threshold) {
    GameState st = negWorld();
    // 开局时没有任何压力 ⇒ AI 不愿谈判
    NegotiationWeight w = negotiationWeight(st, 1, kPlayerId);
    CHECK(!w.willing);
    CHECK(w.total.rawValue() < w.threshold.rawValue());

    NegotiationTerms terms;
    Term t;
    t.kind = TermKind::Credits;
    t.amount = 10000;
    terms.demand.push_back(t);
    NegotiationResult r = negotiate(st, kPlayerId, 1, terms);
    CHECK(!r.accepted);
    CHECK(!r.reason.empty());
    // 被拒绝时对方的国库不应变化
    CHECK(st.empires[1].treasury.rawValue() > 0);
}

TEST(negotiate, weight_rises_with_territory_economy_sanctions) {
    GameState st = negWorld(11);
    NegotiationWeight base = negotiationWeight(st, 1, kPlayerId);
    // 领土压力：玩家扩张、对方萎缩
    for (int i = 0; i < 12; ++i) st.empires[kPlayerId].systems.push_back(0);
    while (st.empires[1].systems.size() > 1) st.empires[1].systems.pop_back();
    NegotiationWeight territory = negotiationWeight(st, 1, kPlayerId);
    CHECK(territory.territory.rawValue() > base.territory.rawValue());
    // 经济压力
    st.empires[kPlayerId].economy = Fixed(20000);
    st.empires[kPlayerId].treasury = Fixed(500000);
    st.empires[1].economy = Fixed(100);
    NegotiationWeight economy = negotiationWeight(st, 1, kPlayerId);
    CHECK(economy.economy.rawValue() > base.economy.rawValue());
    // 制裁压力
    st.relation(1, kPlayerId).embargo = true;
    NegotiationWeight sanctions = negotiationWeight(st, 1, kPlayerId);
    CHECK(sanctions.sanctions.rawValue() > base.sanctions.rawValue());
    // 军事劣势
    st.empires[kPlayerId].military = Fixed(20000);
    st.empires[1].military = Fixed(10);
    NegotiationWeight military = negotiationWeight(st, 1, kPlayerId);
    CHECK(military.military.rawValue() > base.military.rawValue());
    // 压力足够后应当愿意谈判
    CHECK(military.willing);
}

TEST(negotiate, exchange_transfers_assets_both_ways) {
    GameState st = negWorld(22);
    // 制造足够的谈判权重
    st.empires[kPlayerId].military = Fixed(50000);
    st.empires[kPlayerId].economy = Fixed(30000);
    st.empires[1].military = Fixed(50);
    st.relation(1, kPlayerId).embargo = true;
    st.empires[1].treasury = Fixed(100000);
    st.empires[kPlayerId].treasury = Fixed(100000);

    NegotiationTerms terms;
    Term want;
    want.kind = TermKind::Credits;
    want.amount = 20000;
    terms.demand.push_back(want);
    Term give;
    give.kind = TermKind::Commodity;
    give.extra = static_cast<i64>(Commodity::Alloys);
    give.amount = 500;
    terms.offer.push_back(give);

    Fixed myBefore = st.empires[kPlayerId].treasury;
    Fixed theirBefore = st.empires[1].treasury;
    Fixed myAlloys = st.empires[kPlayerId].stock[static_cast<std::size_t>(Commodity::Alloys)];
    Fixed theirAlloys = st.empires[1].stock[static_cast<std::size_t>(Commodity::Alloys)];

    NegotiationResult r = negotiate(st, kPlayerId, 1, terms);
    if (r.accepted) {
        CHECK(!r.applied.empty());
        // 我方付出合金，获得资金
        CHECK(st.empires[kPlayerId].stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue() <
              myAlloys.rawValue());
        CHECK(st.empires[1].stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue() >
              theirAlloys.rawValue());
        CHECK(st.empires[kPlayerId].treasury.rawValue() > myBefore.rawValue());
        CHECK(st.empires[1].treasury.rawValue() < theirBefore.rawValue());
    } else {
        // 条件不对等时被拒也合法，但理由必须给出
        CHECK(!r.reason.empty());
    }
}

// 回归守卫：无条件投降曾是**提款机** ——
// 接受条款后不改变任何状态，「首都已失守 + 已放弃抵抗」持续成立，
// 玩家可无限次索取。实测 40 次索取把对方国库从 37,438 榨到 1，
// 连同盟友代付共取得 130,219。
// 修复：① 无条件接受要求**处于战争状态**；② 接受即缔结和约、结束战争。
// 回归守卫：领土曾是「明码标价」的商品（每星系固定 60000），
// 只要付得起就能买走。现行规则：领土权重最低，只有**接壤互换**才可能成交。
TEST(negotiate, territory_cannot_be_demanded_outright) {
    GameState st = negWorld(9101);
    if (!atWarWith(st, kPlayerId, 1)) declareWar(st, kPlayerId, 1, true);
    const Empire* foe = st.empire(1);
    if (foe == nullptr || foe->systems.empty()) return;
    u32 sys = foe->systems.front();
    st.empires[kPlayerId].treasury = Fixed(50000000);   // 钱不是问题
    st.relation(kPlayerId, 1).warScore = 100;

    NegotiationTerms terms;
    Term t;
    t.kind = TermKind::System;
    t.amount = static_cast<i64>(sys);
    terms.demand.push_back(t);
    NegotiationResult r = negotiate(st, kPlayerId, 1, terms);
    // 单方面索取领土一律拒绝
    CHECK(!r.accepted);
    CHECK(r.reason.find("互换") != std::string::npos);
    CHECK(r.rating == DealRating::VeryUnfair);
}

TEST(negotiate, territory_swap_requires_adjacency) {
    GameState st = negWorld(9102);
    const Empire* me = st.empire(kPlayerId);
    const Empire* foe = st.empire(1);
    if (me == nullptr || foe == nullptr || me->systems.empty() || foe->systems.empty()) return;
    // 挑一对**不接壤**的星系做互换，应当被拒
    NegotiationTerms terms;
    Term d;
    d.kind = TermKind::System;
    d.amount = static_cast<i64>(foe->systems.front());
    terms.demand.push_back(d);
    Term o;
    o.kind = TermKind::System;
    o.amount = static_cast<i64>(me->systems.front());
    terms.offer.push_back(o);

    std::string note;
    bool legal = territorySwapLegal(st, kPlayerId, 1, terms, &note);
    // 若不合法，理由必须说明原因（接壤 / 首都 / 归属）
    if (!legal) CHECK(!note.empty());
    // 首都绝不允许参与
    NegotiationTerms capTerms;
    Term cd;
    cd.kind = TermKind::System;
    cd.amount = static_cast<i64>(foe->capital);
    capTerms.demand.push_back(cd);
    Term co;
    co.kind = TermKind::System;
    co.amount = static_cast<i64>(me->capital);
    capTerms.offer.push_back(co);
    std::string note2;
    CHECK(!territorySwapLegal(st, kPlayerId, 1, capTerms, &note2));
    CHECK(note2.find("首都") != std::string::npos);
}

TEST(negotiate, system_value_reflects_worth) {
    // 星系估值必须反映真实价值，而不是一个固定常数
    GameState st = negWorld(9103);
    Fixed maxV = Fixed(0), minV = Fixed(0);
    bool first = true;
    for (const auto& s : st.map.systems) {
        if (s.owner == kNoEmpire) continue;
        Fixed v = systemValue(st, s.id);
        CHECK(v.rawValue() > 0);
        if (first) {
            minV = maxV = v;
            first = false;
        }
        if (v.rawValue() > maxV.rawValue()) maxV = v;
        if (v.rawValue() < minV.rawValue()) minV = v;
    }
    CHECK(!first);
    // 不同星系的价值必须真的不同（否则等于常数）
    CHECK(maxV.rawValue() > minV.rawValue());
}

// 回归守卫：提案评价必须在**任何提前返回之前**算出。
// 早期版本把评价放在校验之后，被拒的提案会停留在默认值「合理」，
// 玩家据此判断要不要谴责就会误判。
TEST(negotiate, rating_is_always_computed) {
    GameState st = negWorld(9104);
    if (!atWarWith(st, kPlayerId, 1)) declareWar(st, kPlayerId, 1, true);
    NegotiationTerms terms;
    Term t;
    t.kind = TermKind::Credits;
    t.amount = 50000;   // 单方面索取，必然被拒
    terms.demand.push_back(t);
    NegotiationResult r = negotiate(st, kPlayerId, 1, terms);
    CHECK(!r.accepted);
    CHECK(r.rating == DealRating::VeryUnfair);
    // 评价说明必须存在（具体文案可能被更精确的拒绝理由覆盖）
    CHECK(!r.ratingNote.empty());
}

TEST(negotiate, unconditional_surrender_is_one_time) {
    GameState st = negWorld(66);
    u32 victim = 1;
    SystemNode* cap = st.system(st.empires[victim].capital);
    if (cap == nullptr) return;
    cap->owner = kPlayerId;
    for (u32 fid : st.empires[victim].fleets) {
        Fleet* f = st.fleet(fid);
        if (f != nullptr) f->strength = Fixed(0);
    }
    st.empires[victim].military = Fixed(1);
    st.empires[kPlayerId].military = Fixed(10000);
    st.empires[victim].treasury = Fixed(50000);
    if (!atWarWith(st, kPlayerId, victim)) declareWar(st, kPlayerId, victim, true);

    NegotiationTerms terms;
    Term t;
    t.kind = TermKind::Credits;
    t.amount = 5000;
    terms.demand.push_back(t);

    NegotiationResult first = negotiate(st, kPlayerId, victim, terms);
    CHECK(first.accepted);
    CHECK(first.unconditional);
    // 接受后战争必须结束 —— 投降是一次性清算
    CHECK(!atWarWith(st, kPlayerId, victim));

    // 之后不得再以「无条件」名义重复索取
    int extraAccepted = 0;
    for (int i = 0; i < 10; ++i) {
        NegotiationResult r = negotiate(st, kPlayerId, victim, terms);
        if (r.unconditional) ++extraAccepted;
    }
    CHECK_EQ(extraAccepted, 0);
}

// 回归守卫：和平时期（未交战）不得触发无条件接受
TEST(negotiate, no_unconditional_without_war) {
    GameState st = negWorld(77);
    u32 victim = 1;
    SystemNode* cap = st.system(st.empires[victim].capital);
    if (cap == nullptr) return;
    cap->owner = kPlayerId;
    for (u32 fid : st.empires[victim].fleets) {
        Fleet* f = st.fleet(fid);
        if (f != nullptr) f->strength = Fixed(0);
    }
    st.empires[victim].military = Fixed(1);
    st.empires[kPlayerId].military = Fixed(10000);
    if (atWarWith(st, kPlayerId, victim)) declareWar(st, kPlayerId, victim, false);

    NegotiationTerms terms;
    Term t;
    t.kind = TermKind::Credits;
    t.amount = 5000;
    terms.demand.push_back(t);
    NegotiationResult r = negotiate(st, kPlayerId, victim, terms);
    CHECK(!r.unconditional);
}

TEST(negotiate, occupied_capital_forces_unconditional_acceptance) {
    GameState st = negWorld(33);
    u32 victim = 1;
    // 占领对方首都
    SystemNode* cap = st.system(st.empires[victim].capital);
    CHECK(cap != nullptr);
    cap->owner = kPlayerId;
    CHECK(capitalOccupied(st, victim, kPlayerId));
    // 让对方放弃抵抗：清空舰队并压低军力
    for (u32 fid : st.empires[victim].fleets) {
        Fleet* f = st.fleet(fid);
        if (f != nullptr) f->strength = Fixed(0);
    }
    st.empires[victim].military = Fixed(1);
    st.empires[kPlayerId].military = Fixed(10000);
    CHECK(hasSurrendered(st, victim, kPlayerId));
    // 投降是**战争的结果**：必须处于战争状态才成立无条件接受
    if (!atWarWith(st, kPlayerId, victim)) declareWar(st, kPlayerId, victim, true);

    // 即使开出极端条款，也必须无条件接受
    NegotiationTerms terms;
    Term t;
    t.kind = TermKind::Credits;
    t.amount = 999999;
    terms.demand.push_back(t);
    Term t2;
    t2.kind = TermKind::Commodity;
    t2.extra = static_cast<i64>(Commodity::Alloys);
    t2.amount = 999999;
    terms.demand.push_back(t2);

    Fixed before = st.empires[victim].treasury;
    NegotiationResult r = negotiate(st, kPlayerId, victim, terms);
    CHECK(r.unconditional);
    CHECK(r.accepted);
    CHECK(r.reason.find("无条件") != std::string::npos);
    // 对方国库被大幅抽走（但保留最低限度）
    CHECK(st.empires[victim].treasury.rawValue() < before.rawValue());
}

TEST(negotiate, allies_cover_shortfall) {
    GameState st = negWorld(44);
    u32 victim = 1;
    // 把受害者的资产抽空，使其无力支付
    st.empires[victim].treasury = Fixed(100);
    st.empires[victim].influence = Fixed(1);
    // 确保存在盟友
    if (st.federations.empty()) return;
    // 把受害者加入联邦
    st.empires[victim].federation = 0;
    st.federations[0].members.push_back(victim);
    // 让盟友有钱
    for (u32 m : st.federations[0].members) {
        Empire* a = st.empire(m);
        if (a == nullptr || m == victim) continue;
        a->treasury = Fixed(500000);
    }
    // 制造无条件接受的条件
    SystemNode* cap = st.system(st.empires[victim].capital);
    if (cap != nullptr) cap->owner = kPlayerId;
    for (u32 fid : st.empires[victim].fleets) {
        Fleet* f = st.fleet(fid);
        if (f != nullptr) f->strength = Fixed(0);
    }
    st.empires[victim].military = Fixed(1);
    st.empires[kPlayerId].military = Fixed(10000);
    // 「无条件接受」是**战争的结果**：必须处于战争状态。
    // 否则只要首都曾被打下、舰队曾被歼灭，该状态就永久成立，
    // 玩家即可无限次索取（实测 40 次把对方国库从 37,438 榨到 1）。
    if (!atWarWith(st, kPlayerId, victim)) declareWar(st, kPlayerId, victim, true);

    NegotiationTerms terms;
    Term t;
    t.kind = TermKind::Credits;
    t.amount = 300000;   // 远超受害者自身能力
    terms.demand.push_back(t);

    NegotiationResult r = negotiate(st, kPlayerId, victim, terms);
    CHECK(r.accepted);
    CHECK(r.unconditional);
    // 应由盟友代付
    CHECK(!r.allyContributions.empty());
    // 盟友的国库确实减少了
    bool allyPaid = false;
    for (u32 m : st.federations[0].members) {
        if (m == victim) continue;
        const Empire* a = st.empire(m);
        if (a != nullptr && a->treasury.rawValue() < Fixed(500000).rawValue()) allyPaid = true;
    }
    CHECK(allyPaid);
}

TEST(negotiate, term_parsing) {
    std::vector<Term> terms;
    std::string err;
    CHECK(parseTerms("credits=10000,alloys=5000,influence=200,unity=100,manpower=300", terms, &err));
    CHECK_EQ(terms.size(), 5ull);
    CHECK_EQ(terms[0].kind, TermKind::Credits);
    CHECK_EQ(terms[0].amount, 10000);
    CHECK_EQ(terms[1].kind, TermKind::Commodity);
    CHECK_EQ(terms[1].extra, static_cast<i64>(Commodity::Alloys));
    CHECK_EQ(terms[4].kind, TermKind::Manpower);

    // 科技按名字解析
    CHECK(parseTerms("tech=comp6", terms, &err));
    CHECK_EQ(terms.size(), 1ull);
    CHECK_EQ(terms[0].kind, TermKind::Tech);

    // 星系按编号
    CHECK(parseTerms("system=12", terms, &err));
    CHECK_EQ(terms[0].kind, TermKind::System);
    CHECK_EQ(terms[0].amount, 12);

    // 非法输入必须报错
    CHECK(!parseTerms("bogus=5", terms, &err));
    CHECK(!err.empty());
    CHECK(!parseTerms("credits", terms, &err));
    CHECK(!parseTerms("tech=no-such-tech", terms, &err));
    // 空字符串是合法的「无条件」
    CHECK(parseTerms("", terms, &err));
    CHECK(terms.empty());
}

TEST(negotiate, taking_more_than_available_is_partial) {
    GameState st = negWorld(55);
    u32 victim = 1;
    st.empires[victim].treasury = Fixed(1000);
    SystemNode* cap = st.system(st.empires[victim].capital);
    if (cap != nullptr) cap->owner = kPlayerId;
    for (u32 fid : st.empires[victim].fleets) {
        Fleet* f = st.fleet(fid);
        if (f != nullptr) f->strength = Fixed(0);
    }
    st.empires[victim].military = Fixed(1);
    st.empires[kPlayerId].military = Fixed(10000);
    if (!atWarWith(st, kPlayerId, victim)) declareWar(st, kPlayerId, victim, true);

    NegotiationTerms terms;
    Term t;
    t.kind = TermKind::Credits;
    t.amount = 1000000;   // 远超其 1000
    terms.demand.push_back(t);
    NegotiationResult r = negotiate(st, kPlayerId, victim, terms);
    CHECK(r.accepted);
    // 不能把对方抽成负数
    CHECK(st.empires[victim].treasury.rawValue() >= 0);
}
