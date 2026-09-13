#include "save/Serde.h"

#include <string>
#include <type_traits>

#include "core/Errors.h"
#include "core/GameState.h"
#include "crypto/Sha256.h"
#include "mkt/OrderBook.h"
#include "save/ByteReader.h"
#include "save/ByteWriter.h"
#include "util/Str.h"

namespace gf {
namespace {

// 单容器元素上限：防御损坏/恶意存档导致的巨量分配
constexpr u64 kMaxVecElements = 200000;

// ---------------------------------------------------------------------------
// 写入归档
// ---------------------------------------------------------------------------
class WrArchive {
public:
    u64 schema = kSchemaVersion;
    ByteWriter w;
    StringPool pool;
    bool collect = true;
    /// finalize 之后的只读池（第二遍写入时引用排序后的索引）
    const StringPool* finalized = nullptr;

    [[nodiscard]] bool writing() const { return true; }
    [[nodiscard]] bool reading() const { return false; }

    void operator()(bool& v) {
        if (!collect) w.boolv(v);
    }
    void operator()(u8& v) {
        if (!collect) w.u8v(v);
    }
    void operator()(i8& v) {
        if (!collect) w.u8v(static_cast<u8>(v));
    }
    void operator()(u16& v) {
        if (!collect) w.u16v(v);
    }
    void operator()(i16& v) {
        if (!collect) w.u16v(static_cast<u16>(v));
    }
    void operator()(u32& v) {
        if (!collect) w.u32v(v);
    }
    void operator()(i32& v) {
        if (!collect) w.u32v(static_cast<u32>(v));
    }
    void operator()(u64& v) {
        if (!collect) w.varint(v);
    }
    void operator()(i64& v) {
        if (!collect) w.svarint(v);
    }
    void operator()(Fixed& v) {
        if (!collect) w.fx(v);
    }
    /// 枚举统一按底层类型写
    template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
    void operator()(E& e) {
        using U = std::underlying_type_t<E>;
        U u = static_cast<U>(e);
        (*this)(u);
        e = static_cast<E>(u);
    }
    void operator()(std::string& s) { str(s); }
    void str(std::string& s) {
        if (collect) {
            pool.intern(s);
        } else {
            const StringPool& p = finalized ? *finalized : pool;
            u32 idx = p.indexOf(s);
            w.varint(idx == 0xFFFFFFFFu ? 0 : idx + 1);
        }
    }
    /// 已知的编译期常量字符串：不入池
    void constStr(std::string_view) {}
    [[nodiscard]] u64 count(u64 n) {
        if (!collect) w.varint(n);
        return n;
    }
    /// 下标（读档时解析到对象引用，写档时无操作）
    void index(u32&, const std::vector<u32>&) {}
};

// ---------------------------------------------------------------------------
// 读取归档
// ---------------------------------------------------------------------------
class RdArchive {
public:
    u64 schema = kSchemaVersion;
    ByteReader r;
    const std::vector<std::string>* pool = nullptr;

    explicit RdArchive(const u8* data, std::size_t len) : r(data, len) {}

    [[nodiscard]] bool writing() const { return false; }
    [[nodiscard]] bool reading() const { return true; }

    void operator()(bool& v) { v = r.boolv(); }
    void operator()(u8& v) { v = r.u8v(); }
    void operator()(i8& v) { v = static_cast<i8>(r.u8v()); }
    void operator()(u16& v) { v = r.u16v(); }
    void operator()(i16& v) { v = static_cast<i16>(r.u16v()); }
    void operator()(u32& v) { v = r.u32v(); }
    void operator()(i32& v) { v = static_cast<i32>(r.u32v()); }
    void operator()(u64& v) { v = r.varint(); }
    void operator()(i64& v) { v = r.svarint(); }
    void operator()(Fixed& v) { v = r.fx(); }
    template <typename E, std::enable_if_t<std::is_enum_v<E>, int> = 0>
    void operator()(E& e) {
        using U = std::underlying_type_t<E>;
        U u{};
        (*this)(u);
        e = static_cast<E>(u);
    }
    void operator()(std::string& s) { str(s); }
    void str(std::string& s) {
        u64 code = r.varint();
        if (code == 0) {
            s.clear();
            return;
        }
        u64 idx = code - 1;
        if (pool == nullptr || idx >= pool->size()) {
            r.fail();
            s.clear();
            return;
        }
        s = (*pool)[static_cast<std::size_t>(idx)];
    }
    void constStr(std::string_view) {}
    [[nodiscard]] u64 count(u64) {
        u64 n = r.varint();
        if (n > kMaxVecElements) {
            r.fail();
            return 0;
        }
        return n;
    }
    void index(u32&, const std::vector<u32>&) {}
};

// ---------------------------------------------------------------------------
// 通用容器辅助
// ---------------------------------------------------------------------------
template <typename Ar, typename T>
void visitPodVec(Ar& a, std::vector<T>& v) {
    u64 n = a.writing() ? static_cast<u64>(v.size()) : 0;
    n = a.count(n);
    if (a.reading()) v.resize(static_cast<std::size_t>(n));
    for (auto& x : v) a(x);
}

template <typename Ar, typename T, std::size_t N>
void visitArr(Ar& a, std::array<T, N>& arr) {
    for (auto& x : arr) a(x);
}

/// 对象数组：逐元素委托给访问函数
template <typename Ar, typename T, std::size_t N, typename Fn>
void visitObjArr(Ar& a, std::array<T, N>& arr, Fn&& fn) {
    for (auto& x : arr) fn(a, x);
}

template <typename Ar, typename T, typename Fn>
void visitObjVec(Ar& a, std::vector<T>& v, Fn&& fn) {
    u64 n = a.writing() ? static_cast<u64>(v.size()) : 0;
    n = a.count(n);
    if (a.reading()) v.resize(static_cast<std::size_t>(n));
    for (auto& x : v) fn(a, x);
}

// ---------------------------------------------------------------------------
// 各结构访问
// ---------------------------------------------------------------------------
template <typename Ar>
void visitProvenance(Ar& a, Provenance& p) {
    a(p.channel);
    a(p.source);
    a(p.signalCost);
    a(p.credibility);
    a(p.tick);
    a(p.rollbackAt);
    a(p.forged);
    a(p.forger);
    a.str(p.note);
}

template <typename Ar>
void visitToModel(Ar& a, ToModel& m) {
    visitArr(a, m.wGoal);
    a(m.riskAversion);
    a(m.discount);
    visitArr(a, m.typeBelief);
    visitArr(a, m.habits);
    a(m.modelConfidence);
    a(m.observations);
    a(m.deceptions);
    a(m.lastGradientNoise);
    a(m.contamination);
}

template <typename Ar>
void visitMind(Ar& a, EmpireMind& m) {
    visitToModel(a, m.playerModel);
    visitArr(a, m.reputation);
    visitArr(a, m.grudge);
    visitArr(a, m.threat);
    a(m.lastBetrayalEV);
    a(m.lastBetrayalRepCost);
    a(m.lastBetrayalWarCost);
    a(m.lastBetrayalGain);
    a.str(m.lastBetrayalReason);
    visitArr(a, m.playerPattern);
    a(m.foresight);
    a(m.nodeBudget);
}

template <typename Ar>
void visitFaction(Ar& a, Faction& f) {
    a(f.kind);
    a.str(f.name);
    a(f.influence);
    a(f.satisfaction);
    a(f.demandPressure);
    a(f.lastDemandTick);
    if (a.schema >= 5) a(f.nextSatisfyTick);
    a.str(f.lastDemand);
    a(f.patron);
    a(f.patronFunding);
}

template <typename Ar>
void visitDomestic(Ar& a, Domestic& d) {
    visitObjVec(a, d.factions, [](Ar& ar, Faction& f) { visitFaction(ar, f); });
    a(d.unrest);
    a(d.legitimacy);
    a(d.coupRisk);
    a(d.coupCountdown);
    a(d.lastCoupTick);
    a(d.repression);
    a(d.legitimacyDecay);
}

template <typename Ar>
void visitTechState(Ar& a, TechState& t) {
    visitArr(a, t.progress);
    visitArr(a, t.current);
    visitPodVec(a, t.completed);
    a(t.rate);
    visitArr(a, t.focus);
    // 立项制研究的字段必须一并序列化。
    // 漏存会让「存档 → 续跑」与「连续跑」产生分歧：
    // 载入后立项丢失、每季投入归零，AI 与玩家的研究节奏都会改变。
    a(t.project);
    a(t.projectProgress);
    a(t.projectTicks);
    a(t.fundingPerTick);
    a(t.totalCompleted);
}

template <typename Ar>
void visitDesign(Ar& a, FleetDesign& d) {
    a(d.id);
    a.str(d.name);
    a(d.hull);
    visitPodVec(a, d.modules);
    a(d.firepower);
    a(d.defense);
    a(d.speed);
    a(d.supplyUse);
    for (auto& c : d.buildCost) a(c);
    a(d.creditCost);
    a(d.custom);
}

template <typename Ar>
void visitMegaBuild(Ar& a, MegastructureBuild& m) {
    a(m.id);
    a(m.defId);
    a(m.system);
    a(m.owner);
    a(m.stage);
    a(m.progress);
    a(m.complete);
    a(m.startedTick);
}

template <typename Ar>
void visitActiveEffect(Ar& a, ActiveEffect& x) {
    a(x.defId);
    a(x.target);
    a(x.value);
    a(x.ticksLeft);
    a(x.positive);
    a.str(x.source);
}

template <typename Ar>
void visitCountdown(Ar& a, CountdownState& x) {
    a(x.defId);
    a(x.ticksLeft);
    a(x.target);
    a(x.goal);
    a(x.completed);
    a(x.failed);
}

template <typename Ar>
void visitResolutions(Ar& a, EmpireResolutions& r) {
    visitObjVec(a, r.active, [](Ar& ar, ActiveEffect& x) { visitActiveEffect(ar, x); });
    visitPodVec(a, r.triggered);
    visitPodVec(a, r.completed);
    visitPodVec(a, r.failed);
    visitObjVec(a, r.countdowns, [](Ar& ar, CountdownState& x) { visitCountdown(ar, x); });
}

template <typename Ar>
void visitPolicyState(Ar& a, PolicyState& p) {
    visitArr(a, p.active);
    visitArr(a, p.pending);
    visitArr(a, p.transitionLeft);
    a(p.enactCount);
}

template <typename Ar>
void visitSeat(Ar& a, Seat& x) {
    a(x.faction);
    a(x.seats);
    a(x.influence);
    a(x.satisfaction);
    a(x.stance);
    a.str(x.persuasion);
    a(x.promised);
}

template <typename Ar>
void visitParliament(Ar& a, Parliament& p) {
    visitArr(a, p.seats);
    a(p.totalSeats);
    a(p.session.active);
    a(p.session.billId);
    a(p.session.startTick);
    a(p.session.rounds);
    visitObjVec(a, p.session.seats, [](Ar& ar, Seat& x) { visitSeat(ar, x); });
    a(p.session.yesSeats);
    a(p.session.noSeats);
    a(p.session.undecidedSeats);
    a(p.session.threshold);
    a(p.session.thresholdKind);
    a(p.session.resolved);
    a(p.session.passed);
    a.str(p.session.lastResult);
    visitPodVec(a, p.passed);
    visitPodVec(a, p.rejected);
    a(p.capital);
    a(p.threshold);
    a(p.legislationCount);
}

template <typename Ar>
void visitTradeRoute(Ar& a, TradeRoute& r) {
    a(r.id);
    a(r.exporter);
    a(r.importer);
    a(r.commodity);
    a(r.volume);
    a(r.capacity);
    a(r.tariff);
    a(r.unitPrice);
    a(r.active);
    a(r.establishedTick);
    a(r.tariffRevenue);
    a(r.exportRevenue);
    a.str(r.disrupted);
    a(r.dormantTicks);
    visitPodVec(a, r.path);
    a(r.pathRisk);
    a.str(r.pathNote);
}

template <typename Ar>
void visitTradeNetwork(Ar& a, TradeNetwork& t) {
    visitObjVec(a, t.routes, [](Ar& ar, TradeRoute& r) { visitTradeRoute(ar, r); });
    a(t.nextRouteId);
    a(t.importVolume);
    a(t.exportVolume);
    a(t.tariffIncome);
    a(t.exportIncome);
}

template <typename Ar>
void visitTradeStats(Ar& a, EmpireTradeStats& t) {
    a(t.importVolume);
    a(t.exportVolume);
    a(t.tariffIncome);
    a(t.exportIncome);
    // 伙伴关税率：手动序列化，避免依赖不存在的通用辅助
    {
        u32 n = static_cast<u32>(t.avgTariffByPartner.size());
        a(n);
        if (a.reading()) t.avgTariffByPartner.resize(n);
        for (u32 i = 0; i < n; ++i) {
            a(t.avgTariffByPartner[i].first);
            a(t.avgTariffByPartner[i].second);
        }
    }
    a(t.routeCount);
    a(t.spentThisTick);
}

template <typename Ar>
void visitSpyNetwork(Ar& a, SpyNetwork& n) {
    a(n.target);
    a(n.infiltration);
    a(n.exposure);
    a(n.agents);
    a(n.establishedTick);
    a(n.burned);
    a(n.missionsRun);
    a(n.timesBurned);
    a(n.burnedTick);
    a(n.intelValue);
    a(n.lastMissionTick);
}

template <typename Ar>
void visitSpyAgency(Ar& a, SpyAgency& s) {
    visitObjVec(a, s.networks, [](Ar& ar, SpyNetwork& n) { visitSpyNetwork(ar, n); });
    a(s.agentPool);
    a(s.totalAgents);
    a(s.funding);
    a(s.totalMissions);
    a(s.totalBurned);
}

template <typename Ar>
void visitPeaceDemand(Ar& a, PeaceDemand& d) {
    a(d.kind);
    a(d.target);
    a(d.amount);
    a(d.cost);
    a.str(d.text);
}

template <typename Ar>
void visitPeace(Ar& a, PeaceConference& c) {
    a(c.active);
    a(c.winner);
    a(c.loser);
    a(c.warScore);
    a(c.spent);
    a(c.unconditional);
    a(c.startTick);
    visitObjVec(a, c.demands, [](Ar& ar, PeaceDemand& d) { visitPeaceDemand(ar, d); });
    a.str(c.lastResult);
}

template <typename Ar>
void visitProposal(Ar& a, Proposal& p) {
    a(p.id);
    a(p.kind);
    a(p.from);
    a(p.to);
    a(p.createdTick);
    a(p.expiresTick);
    a.str(p.title);
    a.str(p.body);
    a.str(p.ifAccept);
    a.str(p.ifReject);
    a(p.fairnessPct);
}

template <typename Ar>
void visitProposalBox(Ar& a, ProposalBox& b) {
    visitObjVec(a, b.items, [](Ar& ar, Proposal& p) { visitProposal(ar, p); });
    a(b.nextId);
}

template <typename Ar>
void visitCasusBelli(Ar& a, CasusBelli& c) {
    a(c.kind);
    a(c.target);
    a(c.gainedTick);
    a(c.expireTick);
    a.str(c.note);
}

template <typename Ar>
void visitWeariness(Ar& a, WarWeariness& w) {
    a(w.enemy);
    a(w.value);
    a(w.startTick);
    a(w.peaceDemanded);
    a(w.lastEventTick);
}

template <typename Ar>
void visitRuler(Ar& a, Ruler& r) {
    a.str(r.name);
    a(r.trait);
    a(r.skill);
    a(r.reignStart);
    a(r.age);
    a(r.elected);
    a(r.termEnd);
    a(r.electionsWon);
}

template <typename Ar>
void visitScientist(Ar& a, Scientist& s) {
    a(s.id);
    a.str(s.name);
    a(s.owner);
    a(s.field);
    a(s.skill);
    a(s.assignedBranch);
    a(s.projectsCompleted);
    a(s.recruitedTick);
}

template <typename Ar>
void visitFormation(Ar& a, Formation& f) {
    a(f.id);
    a.str(f.name);
    a(f.owner);
    visitPodVec(a, f.fleets);
    a(f.commander);
    a(f.coordination);
    a(f.createdTick);
    a(f.battlesWon);
}

template <typename Ar>
void visitCandidate(Ar& a, Candidate& c) {
    a.str(c.name);
    a(c.trait);
    a(c.skill);
    a(c.support);
    a(c.incumbent);
}

template <typename Ar>
void visitElection(Ar& a, Election& el) {
    a(el.active);
    a(el.startTick);
    a(el.endTick);
    visitObjVec(a, el.candidates, [](Ar& ar, Candidate& c) { visitCandidate(ar, c); });
    a(el.endorsed);
    a(el.campaignSpent);
    a(el.electionsHeld);
}

template <typename Ar>
void visitGovernmentState(Ar& a, GovernmentState& g) {
    visitElection(a, g.election);
    a(g.repression);
    a(g.resentment);
    a(g.successionCrisis);
}

template <typename Ar>
void visitStarbase(Ar& a, Starbase& b) {
    a(b.system);
    a(b.owner);
    a(b.tier);
    a(b.builtTick);
    a(b.siegesSurvived);
}

template <typename Ar>
void visitRevolt(Ar& a, Revolt& r) {
    a(r.system);
    a(r.owner);
    a(r.stage);
    a(r.severity);
    a(r.sinceTick);
    a(r.lastEventTick);
    a(r.leader);
    a(r.suppressed);
}

template <typename Ar>
void visitFauna(Ar& a, FaunaHerd& f) {
    a(f.system);
    a(f.kind);
    a(f.population);
    a(f.lastHunted);
}

template <typename Ar>
void visitBuildOrder(Ar& a, Planet::BuildOrder& o) {
    a(o.building);
    a(o.ticksLeft);
    a(o.totalTicks);
    a(o.paidCredits);
    a.str(o.note);
}

template <typename Ar>
void visitShipOrder(Ar& a, Empire::ShipOrder& o) {
    a(o.system);
    a(o.design);
    a(o.ticksLeft);
    a(o.totalTicks);
    a(o.paidCredits);
}

template <typename Ar>
void visitDevelopment(Ar& a, DevelopmentProject& p) {
    a(p.kind); a(p.target); a(p.definition); a(p.auxiliary);
    a(p.ticksLeft); a(p.totalTicks); a(p.startedTick); a(p.lastTick); a(p.hasAdvanced);
    a(p.stalledTicks); a(p.paidCredits); a(p.upkeep); visitArr(a, p.supplies);
    visitPodVec(a, p.fleets); a.str(p.pauseReason);
}

template <typename Ar>
void visitNationalEdict(Ar& a, NationalEdict& e) { a(e.kind); a(e.expiresTick); }

template <typename Ar>
void visitEmpire(Ar& a, Empire& e) {
    a(e.id);
    a.str(e.name);
    a.str(e.adjective);
    a.str(e.rulerName);
    a(e.species);
    visitArr(a, e.ethics);
    visitArr(a, e.civics);
    a(e.government);
    a(e.stance);
    a(e.isPlayer);
    a(e.alive);
    a(e.aiPersonaAggressive);
    a(e.capital);

    a(e.treasury);
    visitArr(a, e.stock);
    visitArr(a, e.capacity);
    visitArr(a, e.demand);
    a(e.influence);
    a(e.unity);
    a(e.creditRating);
    a(e.debt);

    a(e.military);
    a(e.economy);
    // lastIncome 与 vassalOf 必须序列化：
    // 它们参与 AI 的预算/建造/政变判定，漏存会让「存档 → 续跑」与
    // 「连续跑」产生分歧（实测差异体现为一次建造决策）。
    a(e.lastIncome);
    a(e.vassalOf);
    // 正当战争理由与战争疲劳：漏存会让存档续跑与连续跑产生分歧，
    // 且载入后疲劳归零、理由消失 —— 开战门槛与反战减益全部失效。
    visitObjVec(a, e.casusBelli, [](Ar& ar, CasusBelli& c) { visitCasusBelli(ar, c); });
    visitObjVec(a, e.weariness, [](Ar& ar, WarWeariness& w) { visitWeariness(ar, w); });
    // 人事：漏存会让领袖、科学家与集团军在读档后全部消失
    visitRuler(a, e.ruler);
    visitObjVec(a, e.scientists, [](Ar& ar, Scientist& s) { visitScientist(ar, s); });
    visitObjVec(a, e.formations, [](Ar& ar, Formation& f) { visitFormation(ar, f); });
    // 政体状态：漏存会让选举进程、镇压与怨恨在读档后全部归零
    visitGovernmentState(a, e.gov);
    // 建造与造舰队列：漏存会让读档后所有在建项目凭空完工或消失
    visitObjVec(a, e.shipQueue, [](Ar& ar, Empire::ShipOrder& o) { visitShipOrder(ar, o); });
    a(e.nextDesignId);
    a(e.corruption);
    a(e.labor);
    visitArr(a, e.pressure);
    for (std::size_t i = 0; i < e.geneMods.size(); ++i) a(e.geneMods[i]);
    // 意识形态压力：漏存会让长期渗透功亏一篑（读档后压力归零）
    visitArr(a, e.ideologyPressure);
    a(e.stability);
    a(e.legitimacy);
    a(e.score);
    a(e.intelDefense);
    a(e.counterIntel);
    a(e.propaganda);

    visitTechState(a, e.tech);

    visitPodVec(a, e.systems);
    visitPodVec(a, e.fleets);
    visitObjVec(a, e.designs, [](Ar& ar, FleetDesign& d) { visitDesign(ar, d); });
    visitObjVec(a, e.megas, [](Ar& ar, MegastructureBuild& m) { visitMegaBuild(ar, m); });
    visitPodVec(a, e.colonizing);
    if (a.schema >= 5) {
        visitObjVec(a, e.developmentProjects, [](Ar& ar, DevelopmentProject& p) { visitDevelopment(ar, p); });
        visitObjVec(a, e.nationalEdicts, [](Ar& ar, NationalEdict& n) { visitNationalEdict(ar, n); });
        a(e.ascensions);
    }
    visitPodVec(a, e.buildQueue);

    visitDomestic(a, e.domestic);
    a(e.federation);
    visitArr(a, e.opinion);
    a(e.lastWarTick);
    a(e.lastInvadeTick);

    visitMind(a, e.mind);

    a(e.apMax);
    a(e.apLeft);

    a(e.popTotal);
    a(e.gdp);
    a(e.coloniesFounded);
    a(e.warsWon);
    a(e.betrayalsCommitted);
    a(e.betrayalsSuffered);
    visitResolutions(a, e.resolutions);
    visitPolicyState(a, e.policies);
    visitParliament(a, e.parliament);
    visitTradeStats(a, e.trade);
    visitSpyAgency(a, e.spy);
}

template <typename Ar>
void visitPlanet(Ar& a, Planet& p) {
    a(p.id);
    a(p.system);
    a.str(p.name);
    a(p.type);
    a(p.size);
    a(p.pops);
    a(p.habitability);
    a(p.stability);
    a(p.development);
    a(p.unrest);
    visitArr(a, p.yield);
    visitPodVec(a, p.buildings);
    visitPodVec(a, p.districts);
    a(p.capital);
    a(p.colonized);
    if (a.schema >= 5) { a(p.settlementTicksLeft); a(p.settlementStartedTick); }
    a(p.owner);
    a(p.devastation);
    a(p.anomaly);
    a(p.garrison);
    visitObjVec(a, p.buildQueue, [](Ar& ar, Planet::BuildOrder& o) { visitBuildOrder(ar, o); });
}

template <typename Ar>
void visitSystem(Ar& a, SystemNode& s) {
    a(s.id);
    a.str(s.name);
    a(s.x);
    a(s.y);
    a(s.owner);
    a(s.sector);
    a(s.capital);
    a(s.colonized);
    a(s.megastructure);
    a(s.megastructureId);
    a(s.anomaly);
    a(s.hazard);
    a(s.blockade);
    a(s.tradeHub);
    visitPodVec(a, s.links);
    visitPodVec(a, s.planets);
    a(s.pirates);
}

template <typename Ar>
void visitSector(Ar& a, Sector& s) {
    a(s.id);
    a.str(s.name);
    visitPodVec(a, s.systems);
    a(s.development);
}

template <typename Ar>
void visitCommander(Ar& a, Commander& c) {
    a(c.id);
    a.str(c.name);
    a(c.owner);
    a(c.trait);
    a(c.rank);
    a(c.merit);
    visitPodVec(a, c.traits);
    a(c.attack);
    a(c.defense);
    a(c.logistics);
    a(c.planning);
    a(c.fleet);
    a(c.battlesWon);
    a(c.battlesLost);
    a(c.experience);
}

template <typename Ar>
void visitBattle(Ar& a, Battle& b) {
    a(b.id);
    a(b.system);
    a(b.attacker);
    a(b.defender);
    a(b.startTick);
    a(b.ticks);
    visitPodVec(a, b.attackerFleets);
    visitPodVec(a, b.defenderFleets);
    a(b.attackerCommander);
    a(b.defenderCommander);
    a(b.attackerWidth);
    a(b.defenderWidth);
    a(b.widthCap);
    a(b.attackerPenalty);
    a(b.defenderPenalty);
    a(b.attackerLoss);
    a(b.defenderLoss);
    a(b.attackerOrgLoss);
    a(b.defenderOrgLoss);
    a(b.progress);
    a(b.terrainDefense);
    a.str(b.terrainName);
    a(b.resolved);
    a(b.attackerWon);
    a.str(b.outcome);
}

template <typename Ar>
void visitFleet(Ar& a, Fleet& f) {
    a(f.id);
    a.str(f.name);
    a(f.owner);
    a(f.design);
    a(f.system);
    a(f.targetSystem);
    a(f.order);
    a(f.strength);
    a(f.morale);
    a(f.supply);
    a(f.stealth);
    a(f.upkeep);
    a(f.veteran);
    a(f.org);
    a(f.maxOrg);
    a(f.experience);
    a(f.commander);
    a(f.battle);
    a(f.planning);
}

template <typename Ar>
void visitTreaty(Ar& a, Treaty& t) {
    a(t.kind);
    a(t.a);
    a(t.b);
    a(t.signedTick);
    a(t.expireTick);
    a(t.terms);
    a(t.compliance);
    a.str(t.note);
}

template <typename Ar>
void visitMotion(Ar& a, FederalMotion& m) {
    a(m.id);
    a(m.subject);
    a(m.target);
    a(m.proposer);
    a(m.proposedTick);
    a(m.threshold);
    visitPodVec(a, m.yes);
    visitPodVec(a, m.no);
    visitPodVec(a, m.abstain);
    a(m.resolved);
    a(m.passed);
    a(m.yesWeight);
    a(m.noWeight);
}

template <typename Ar>
void visitFederation(Ar& a, Federation& f) {
    a(f.id);
    a.str(f.name);
    a(f.founder);
    visitPodVec(a, f.members);
    a(f.cohesion);
    a(f.treasury);
    a(f.commonFleet);
    visitObjVec(a, f.motions, [](Ar& ar, FederalMotion& m) { visitMotion(ar, m); });
    a(f.concession);
}

template <typename Ar>
void visitRelation(Ar& a, Relation& r) {
    a(r.opinion);
    a(r.trust);
    a(r.fear);
    a(r.debt);
    a(r.border);
    a(r.lastWar);
    a(r.warScore);
    a(r.atWar);
    a(r.embargo);
    a(r.warStartTick);
}

template <typename Ar>
void visitCrisis(Ar& a, CrisisState& c) {
    a(c.id);
    a(c.defId);
    a(c.name);
    a(c.active);
    a(c.startTick);
    a(c.severity);
    a(c.target);
    a(c.progress);
    visitPodVec(a, c.participants);
}

template <typename Ar>
void visitPending(Ar& a, PendingChoice& p) {
    a(p.id);
    a(p.eventId);
    a(p.scopeTarget);
    a(p.createdTick);
    a(p.deferCount);
    visitPodVec(a, p.options);  // std::string 由 a() 的重载处理
    visitPodVec(a, p.hints);
    if (a.schema >= 3) { a(p.kind); a(p.subject); a(p.deferredUntil); }
}

template <typename Ar>
void visitPlanned(Ar& a, PlannedAction& p) {
    a(p.kind);
    a.str(p.command);
    a.str(p.detail);
    a(p.queuedTick);
    a(p.apCost);
    a(p.res);
    a(p.qty);
    a(p.px);
    a(p.target);
    a(p.exch);
}

// ---- 市场 ----
template <typename Ar>
void visitOrder(Ar& a, Order& o) {
    a(o.id);
    a(o.owner);
    a(o.seq);
    a(o.px);
    a(o.qty);
    a(o.shown);
    a(o.filled);
    a(o.avgPx);
    a(o.exch);
    a(o.res);
    a(o.buy);
    a(o.kind);
    a(o.tif);
    a(o.placedTick);
    a(o.controller);
    a(o.synthetic);
}

template <typename Ar>
void visitBook(Ar& a, Book& b) {
    visitObjVec(a, b.orders, [](Ar& ar, Order& o) { visitOrder(ar, o); });
    // bids/asks 为派生聚合，读档后由 rebuildDerived 重建 ⇒ 不入档
    a(b.last);
    a(b.mid);
    a(b.spread);
    a(b.open);
    a(b.high);
    a(b.low);
    a(b.volume);
    a(b.vwap);
    a(b.sigma);
    a(b.var20);
    a(b.impactPerm);
    a(b.impactTemp);
    a(b.openInterest);
    a(b.halted);
    a(b.netFlow);
    a(b.liquidityDrained);
}

template <typename Ar>
void visitVolState(Ar& a, VolState& v) {
    a(v.omega);
    a(v.alpha);
    a(v.beta);
    a(v.lastReturn);
    a(v.lastSigma);
    a(v.jumpBias);
}

template <typename Ar>
void visitFutures(Ar& a, FuturesQuote& f) {
    a(f.price);
    a(f.basis);
    a(f.openInterest);
    a(f.convenience);
    a(f.carry);
}

template <typename Ar>
void visitMM(Ar& a, MarketMakerState& m) {
    a(m.inventory);
    a(m.gamma);
    a(m.baseSpread);
    a(m.skew);
    a(m.halted);
    a.str(m.haltReason);
    a(m.quotesPlaced);
    a(m.inventoryLimit);
    a(m.absorbed);
}

template <typename Ar>
void visitPosition(Ar& a, Position& p) {
    a(p.res);
    a(p.qty);
    a(p.avgCost);
    a(p.realized);
}

template <typename Ar>
void visitFutPos(Ar& a, FuturesPosition& p) {
    a(p.res);
    a(p.term);
    a(p.qty);
    a(p.entry);
    a(p.margin);
    a(p.leverage);
    a(p.isShort);
    a(p.openedTick);
    if (a.schema >= 3) { a(p.owner); a(p.expiryTick); }
}

template <typename Ar>
void visitMargin(Ar& a, MarginState& m) {
    a(m.initMargin);
    a(m.maintMargin);
    a(m.cash);
    a(m.equity);
    a(m.callActive);
    a(m.cascadeDepth);
    a(m.forcedLiquidations);
}

template <typename Ar>
void visitEscrow(Ar& a, EscrowRecord& e) {
    a(e.id);
    a(e.payer);
    a(e.payee);
    a(e.amount);
    a(e.res);
    a(e.qty);
    a(e.openedTick);
    a(e.termTicks);
    a(e.released);
    a(e.breached);
}

template <typename Ar>
void visitDebt(Ar& a, DebtRecord& d) {
    a(d.id);
    a(d.borrower);
    a(d.lender);
    a(d.principal);
    a(d.rate);
    a(d.termTicks);
    a(d.issuedTick);
    a(d.defaulted);
}

template <typename Ar>
void visitInsider(Ar& a, InsiderSignal& s) {
    a(s.res);
    a(s.magnitude);
    a(s.fireTick);
    a(s.knower);
    a(s.consumed);
}

template <typename Ar>
void visitManip(Ar& a, ManipulationRecord& m) {
    a(m.actor);
    a(m.kind);
    a(m.tick);
    a(m.score);
    a(m.penalized);
    a(m.fine);
    a.str(m.detail);
}

template <typename Ar>
void visitShock(Ar& a, ShockRecord& s) {
    a(s.tick);
    a(s.res);
    a(s.magnitude);
    a.str(s.cause);
    a(s.sigmaBefore);
    a(s.sigmaAfter);
}

template <typename Ar>
void visitActorStats(Ar& a, ActorMarketStats& s) {
    a(s.actor);
    a(s.ordersPlaced);
    a(s.ordersCancelled);
    a(s.qtyFilled);
    a(s.qtyCancelled);
    a(s.buyVolume);
    a(s.sellVolume);
    a(s.priceRunUp);
    a(s.spentThisTick);
    a(s.inventoryPeak);
    a(s.spoofScore);
    a(s.lastOrderTick);
}

template <typename Ar>
void visitExchange(Ar& a, ExchangeMarket& x) {
    a(x.kind);
    for (auto& b : x.books) visitBook(a, b);
    for (auto& row : x.futures)
        for (auto& q : row) visitFutures(a, q);
    for (auto& m : x.mm) visitMM(a, m);
    a(x.clearingReserve);
    a(x.volumeTick);
    a(x.embargo);
    a(x.tariff);
}

template <typename Ar>
void visitMarket(Ar& a, MarketState& m) {
    visitTradeNetwork(a, m.trade);
    for (auto& x : m.exchanges) visitExchange(a, x);
    visitObjArr(a, m.vol, [](Ar& ar, VolState& v) { visitVolState(ar, v); });
    visitArr(a, m.spotIndex);

    visitObjVec(a, m.positions, [](Ar& ar, Position& p) { visitPosition(ar, p); });
    visitObjVec(a, m.futuresPositions, [](Ar& ar, FuturesPosition& p) { visitFutPos(ar, p); });
    if (a.schema >= 3) {
        visitObjVec(a, m.insurancePolicies, [](Ar& ar, InsurancePolicy& p) {
            ar(p.res); ar(p.qty); ar(p.entry); ar(p.remaining); ar(p.paid); ar(p.expiresTick);
        });
    }
    visitMargin(a, m.margin);

    visitArr(a, m.arbGap);
    visitArr(a, m.fx);
    a(m.blackMarketPremium);
    a(m.blackMarketStructural);
    a(m.rationing);
    visitArr(a, m.arbPressure);
    visitArr(a, m.blackMarketPrice);

    visitObjVec(a, m.debts, [](Ar& ar, DebtRecord& d) { visitDebt(ar, d); });
    visitArr(a, m.creditRating);
    visitObjVec(a, m.escrows, [](Ar& ar, EscrowRecord& e) { visitEscrow(ar, e); });
    visitObjVec(a, m.insiderSignals, [](Ar& ar, InsiderSignal& s) { visitInsider(ar, s); });
    visitObjVec(a, m.manipulations, [](Ar& ar, ManipulationRecord& r) { visitManip(ar, r); });
    a(m.nextEscrowId);
    a(m.nextDebtId);
    visitObjVec(a, m.shocks, [](Ar& ar, ShockRecord& s) { visitShock(ar, s); });
    visitObjVec(a, m.actorStats, [](Ar& ar, ActorMarketStats& s) { visitActorStats(ar, s); });
    a(m.nextOrderId);
    a(m.nextSeq);
    a(m.tickNotional);
    a(m.tickFills);
}

// ---- 玩家资产 ----
template <typename Ar>
void visitItem(Ar& a, ItemInstance& it) {
    a(it.def);
    a(it.count);
    visitProvenance(a, it.prov);
    a(it.forged);
    a(it.contamination);
    a(it.acquiredTick);
}

template <typename Ar>
void visitClueNode(Ar& a, ClueNode& c) {
    a(c.def);
    a(c.known);
    a(c.credibility);
    visitProvenance(a, c.prov);
    a(c.act);
    a(c.discoveredTick);
    a(c.archived);
}

template <typename Ar>
void visitClueEdge(Ar& a, ClueEdge& e) {
    a(e.a);
    a(e.b);
    a(e.kind);
    a(e.weight);
    a(e.adjudicated);
    a(e.winner);
    a(e.createdTick);
    a(e.playerMade);
}

template <typename Ar>
void visitPlot(Ar& a, PlotState& p) {
    a(p.act);
    visitPodVec(a, p.knownClues);
    visitPodVec(a, p.conclusionsReached);
    visitPodVec(a, p.committedConclusions);
    visitPodVec(a, p.falseConclusions);
    visitArr(a, p.endingVector);
    visitPodVec(a, p.pendingBeats);
    a(p.hiddenActUnlocked);
    a(p.storyFlags);
}

template <typename Ar>
void visitLogEntry(Ar& a, LogEntry& l) {
    a(l.tick);
    a(l.phase);
    a(l.actor);
    a.str(l.code);
    a.str(l.text);
    a(l.value);
}

template <typename Ar>
void visitHistory(Ar& a, HistoryPoint& h) {
    a(h.tick);
    a(h.stateHash);
    visitArr(a, h.score);
    visitArr(a, h.treasury);
    visitArr(a, h.index);
}

template <typename Ar>
void visitGameState(Ar& a, GameState& st) {
    a(st.schemaVersion);
    a(st.seed);
    a(st.tick);
    a(st.difficulty);
    a(st.epochIndex);
    a.str(st.epochName);
    a(st.legacyMask);
    a(st.aiForesight);
    a(st.aiNodeBudget);
    a(st.endless);
    a(st.ended);
    a(st.endingId);
    a(st.act);
    if (a.schema >= 4) {
        a(st.victory.consecutiveQuarters);
        a(st.victory.lastEvaluatedTick);
        a(st.victory.achieved);
    }
    a(st.modifierBits);
    a.str(st.modifierName);

    // 星图
    visitObjVec(a, st.map.systems, [](Ar& ar, SystemNode& s) { visitSystem(ar, s); });
    visitObjVec(a, st.map.sectors, [](Ar& ar, Sector& s) { visitSector(ar, s); });
    visitObjVec(a, st.planets, [](Ar& ar, Planet& p) { visitPlanet(ar, p); });
    visitObjVec(a, st.empires, [](Ar& ar, Empire& e) { visitEmpire(ar, e); });
    visitObjVec(a, st.fleets, [](Ar& ar, Fleet& f) { visitFleet(ar, f); });
    visitObjVec(a, st.commanders, [](Ar& ar, Commander& c) { visitCommander(ar, c); });
    visitObjVec(a, st.battles, [](Ar& ar, Battle& b) { visitBattle(ar, b); });
    a(st.nextBattleId);
    visitObjVec(a, st.treaties, [](Ar& ar, Treaty& t) { visitTreaty(ar, t); });
    visitObjVec(a, st.federations, [](Ar& ar, Federation& f) { visitFederation(ar, f); });
    visitObjVec(a, st.relations, [](Ar& ar, Relation& r) { visitRelation(ar, r); });
    visitObjVec(a, st.crises, [](Ar& ar, CrisisState& c) { visitCrisis(ar, c); });
    visitObjVec(a, st.pending.items, [](Ar& ar, PendingChoice& p) { visitPending(ar, p); });
    a(st.pending.nextId);
    visitObjVec(a, st.pendingActions, [](Ar& ar, PlannedAction& p) { visitPlanned(ar, p); });

    visitObjVec(a, st.peace, [](Ar& ar, PeaceConference& c) { visitPeace(ar, c); });
    visitProposalBox(a, st.proposals);
    visitObjVec(a, st.starbases, [](Ar& ar, Starbase& b) { visitStarbase(ar, b); });
    visitObjVec(a, st.revolts, [](Ar& ar, Revolt& r) { visitRevolt(ar, r); });
    visitObjVec(a, st.fauna, [](Ar& ar, FaunaHerd& f) { visitFauna(ar, f); });
    a(st.nextScientistId);
    a(st.nextFormationId);
    visitMarket(a, st.market);

    visitObjVec(a, st.inventory.items, [](Ar& ar, ItemInstance& it) { visitItem(ar, it); });
    visitObjVec(a, st.clues, [](Ar& ar, ClueNode& c) { visitClueNode(ar, c); });
    visitObjVec(a, st.clueEdges, [](Ar& ar, ClueEdge& e) { visitClueEdge(ar, e); });
    visitPlot(a, st.plot);

    // RNG：状态 + 消费计数
    {
        auto states = st.rng.states();
        auto counters = st.rng.counters();
        visitArr(a, states);
        visitArr(a, counters);
        if (a.reading()) {
            st.rng.setStates(states);
            st.rng.setCounters(counters);
        }
    }

    a(st.rollbackCount);
    a(st.chronicleHead);
    a(st.chronicleBurned);
    a(st.lastCommitTick);
    a(st.saveCount);
    a(st.loadCount);

    visitObjVec(a, st.log, [](Ar& ar, LogEntry& l) { visitLogEntry(ar, l); });
    a(st.logSeq);
    visitObjVec(a, st.history, [](Ar& ar, HistoryPoint& h) { visitHistory(ar, h); });
}

}  // namespace

std::vector<u8> serializeState(const GameState& st) {
    // 两遍：第一遍收集字符串池，第二遍写 body（引用排序后的池索引）
    GameState& mut = const_cast<GameState&>(st);  // 写模式不会修改状态

    WrArchive collectPass;
    collectPass.collect = true;
    visitGameState(collectPass, mut);
    collectPass.pool.finalize();

    WrArchive writePass;
    writePass.collect = false;
    writePass.finalized = &collectPass.pool;
    visitGameState(writePass, mut);

    ByteWriter out;
    out.reserve(writePass.w.size() + 1024);
    out.varint(static_cast<u64>(kSchemaVersion));
    out.varint(static_cast<u64>(collectPass.pool.size()));
    for (const std::string& s : collectPass.pool.strings()) out.rawStr(s);
    out.varint(static_cast<u64>(writePass.w.size()));
    out.raw(writePass.w.data().data(), writePass.w.size());
    return out.data();
}

bool tryDeserializeState(const std::vector<u8>& bytes, GameState& st) {
    ByteReader head(bytes);
    u64 ver = head.varint();
    if (ver < 2 || ver > static_cast<u64>(kSchemaVersion)) return false;
    u64 poolCount = head.varint();
    if (poolCount > 1000000) return false;
    std::vector<std::string> pool;
    pool.reserve(static_cast<std::size_t>(poolCount));
    for (u64 i = 0; i < poolCount; ++i) {
        std::string s = head.rawStr();
        if (!head.ok()) return false;
        pool.push_back(std::move(s));
    }
    u64 bodyLen = head.varint();
    if (!head.ok()) return false;
    if (bodyLen > head.remaining()) return false;

    GameState fresh;
    RdArchive ar(head.cursor(), static_cast<std::size_t>(bodyLen));
    ar.pool = &pool;
    ar.schema = ver;
    visitGameState(ar, fresh);
    if (!ar.r.ok() || ar.r.remaining() != 0 || fresh.schemaVersion != ver) return false;
    if (ver == 2) {
        for (auto& c : fresh.pending.items) {
            // v2 没有类型字段；只迁移旧版生成器的完整派系选项签名。
            if (c.eventId >= 55 && c.eventId < 75 && c.options.size() == 3 &&
                c.options[0] == "满足诉求（消耗资源，满意度 +25%）" &&
                c.options[1] == "压制派系（民怨 +，满意度 -）" &&
                c.options[2] == "拖延（压力继续累积）") {
                c.kind = ChoiceKind::Faction;
                c.subject = c.eventId - 55;
            }
        }
        for (auto& p : fresh.market.futuresPositions) {
            p.owner = kPlayerId;
            p.expiryTick = (p.openedTick / 4 + p.term + 1) * 4 - 1;
        }
        // 保留旧档已记录的基础研究速率，不猜测历史累计值。
    }
    if (ver < 4) {
        fresh.victory.lastEvaluatedTick = fresh.tick;
        fresh.victory.achieved = fresh.endingId == 12;
    }
    if (ver < 5) for (auto& e : fresh.empires) e.colonizing.clear();
    if (ver >= 5) for (const auto& e : fresh.empires) {
        if (e.ascensions & ~31u) return false;
        for (const auto& edict : e.nationalEdicts) if (edict.kind >= 6) return false;
        for (const auto& p : e.developmentProjects) {
            if (p.kind >= ProjectKind::Count || p.totalTicks == 0 || p.ticksLeft > p.totalTicks ||
                p.paidCredits < 0 || p.upkeep < 0) return false;
            for (i64 supply : p.supplies) if (supply < 0) return false;
            if (p.kind == ProjectKind::Ascension && p.definition >= 5) return false;
            if (p.kind == ProjectKind::GeneMod && p.definition >= static_cast<u32>(GeneMod::Count)) return false;
            if (p.kind == ProjectKind::Mega && p.definition >= kMegastructureCount) return false;
            if (p.kind == ProjectKind::Starbase && p.definition >= static_cast<u32>(StarbaseTier::Count)) return false;
            if (p.kind == ProjectKind::Processing && (p.definition < 1 || p.definition > 10)) return false;
        }
    }
    fresh.schemaVersion = kSchemaVersion;
    fresh.relations.resize(static_cast<std::size_t>(kMaxEmpires) * kMaxEmpires);
    rebuildDerived(fresh);
    st = std::move(fresh);
    return true;
}

void deserializeState(const std::vector<u8>& bytes, GameState& st) {
    if (!tryDeserializeState(bytes, st)) {
        fail(ExitCode::Integrity, "存档内容损坏或版本不匹配（反序列化失败）");
    }
}

void rebuildDerived(GameState& st) {
    refreshEmpireBonuses(st);
    for (auto& ex : st.market.exchanges) {
        for (int c = 0; c < kCommodityCount; ++c) {
            bookRebuildLevels(ex.books[static_cast<std::size_t>(c)]);
            ex.books[static_cast<std::size_t>(c)].mid =
                bookMid(ex.books[static_cast<std::size_t>(c)]);
        }
    }
}

std::string stateDigestHex(const GameState& st) {
    std::vector<u8> bytes = serializeState(st);
    Sha256Digest d = sha256(bytes.data(), bytes.size());
    return hexEncode(d.data(), 8);
}

}  // namespace gf
