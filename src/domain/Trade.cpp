#include "domain/Trade.h"

#include "core/GameState.h"
#include "domain/Empire.h"
#include "domain/Building.h"
#include "mkt/MarketState.h"

#include <algorithm>

#include "domain/Treaty.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 每季运力：由双方经济体量、商品可得量与关税率共同决定。
/// 关税率越高，运力越低（超过 35% 后急剧萎缩）。
Fixed routeCapacity(const GameState& st, const Empire& ex, const Empire& im, u8 c, Fixed tariff) {
    (void)0;
    // 流量口径：出口方的持续产出剩余、进口方的持续产出缺口
    Fixed surplus = exportableSurplus(st, ex, c);
    if (surplus.rawValue() <= 0) return Fixed(0);
    Fixed gap = importNeed(st, im, c);
    if (gap.rawValue() <= 0) return Fixed(0);
    // 还需要现货可运（库存缓冲已由 exportableSurplus 检查，这里再按现货兜底）
    Fixed spot = ex.stock[static_cast<std::size_t>(c)];
    if (spot.rawValue() <= 0) return Fixed(0);

    // 基础运力 = min(剩余, 缺口)，再乘以规模系数
    Fixed base = fxMin(surplus, gap);
    // 关税抑制：0% 时全额，35% 时约 55%，60% 时约 20%
    Fixed tariffFactor = fxClamp(Fixed(1) - tariff * Fixed::raw(1300), Fixed::pct(20), Fixed(1));
    return base * tariffFactor * Fixed::pct(50);
}

/// 结算单价：取双方参考价的中间值，再按关税调整
Fixed routeUnitPrice(const GameState& st, u8 c, Fixed tariff) {
    const CommodityInfo& ci = commodityInfo(static_cast<int>(c));
    Fixed px = ci.basePrice;
    // 用跨交易所均价作为世界价
    Fixed sum = Fixed(0);
    int n = 0;
    for (int e = 0; e < kExchangeCount; ++e) {
        Fixed m = st.market.exchanges[static_cast<std::size_t>(e)].books[c].mid;
        if (m.rawValue() > 0) {
            sum += m;
            ++n;
        }
    }
    if (n > 0) px = sum / Fixed(n);
    // 进口方要付含税价，出口方收到的是世界价
    (void)tariff;
    return px;
}

}  // namespace

// ===========================================================================
// 实体路径
// ===========================================================================

std::vector<u32> tradePathWeighted(const GameState& st, u32 from, u32 to, u32 exporter,
                                   u32 importer) {
    // Dijkstra（边权为正的整数代价）。代价计入：
    //   基础 10/跳 + 归属修正（己方/对方 0、盟友 4、中立 12、交战 60）+ 封锁 40
    // 于是路线会主动绕开战区与被封锁的星系，而不是无脑走最短路。
    std::vector<u32> empty;
    if (from == to) return empty;
    const std::size_t n = st.map.systems.size();
    if (n == 0 || from >= n || to >= n) return empty;
    constexpr i64 kInf = 1LL << 60;
    std::vector<i64> dist(n, kInf);
    std::vector<i64> parent(n, -1);
    std::vector<bool> done(n, false);
    dist[from] = 0;
    for (std::size_t iter = 0; iter < n; ++iter) {
        i64 best = kInf;
        i64 cur = -1;
        for (std::size_t i = 0; i < n; ++i) {
            if (done[i] || dist[i] >= best) continue;
            best = dist[i];
            cur = static_cast<i64>(i);
        }
        if (cur < 0) break;
        u32 cu = static_cast<u32>(cur);
        done[cu] = true;
        if (cu == to) break;
        const SystemNode* cs = st.system(cu);
        if (cs == nullptr) continue;
        for (u32 nx : cs->links) {
            if (nx >= n || done[nx]) continue;
            const SystemNode* ns = st.system(nx);
            if (ns == nullptr) continue;
            i64 w = 10;
            if (ns->owner == kNoEmpire) w += 12;                 // 无主：略贵（缺乏保护）
            else if (ns->owner == exporter || ns->owner == importer) w += 0;
            else if (atWarWith(st, exporter, ns->owner) || atWarWith(st, importer, ns->owner)) w += 60;
            else w += 12;                                        // 中立/第三方：过境成本
            if (ns->blockade.rawValue() > Fixed::pct(30).rawValue()) w += 40;
            if (ns->pirates.rawValue() > Fixed::pct(30).rawValue()) w += 15;
            i64 nd = dist[cu] + w;
            if (nd < dist[nx]) {
                dist[nx] = nd;
                parent[nx] = cur;
            }
        }
    }
    if (dist[to] >= kInf) return empty;
    std::vector<u32> path;
    for (i64 at = static_cast<i64>(to); at >= 0; at = parent[static_cast<std::size_t>(at)]) {
        path.push_back(static_cast<u32>(at));
        if (at == static_cast<i64>(from)) break;
    }
    std::reverse(path.begin(), path.end());
    if (path.empty() || path.front() != from) return empty;
    return path;
}

std::vector<u32> tradePathBetween(const GameState& st, u32 from, u32 to) {
    std::vector<u32> empty;
    if (from == to) return empty;
    if (st.system(from) == nullptr || st.system(to) == nullptr) return empty;
    const std::size_t n = st.map.systems.size();
    if (n == 0) return empty;
    // BFS：父节点表用来回溯路径
    std::vector<i64> parent(n, -1);
    std::vector<bool> seen(n, false);
    std::vector<u32> queue;
    queue.push_back(from);
    seen[from] = true;
    bool found = false;
    for (std::size_t head = 0; head < queue.size() && !found; ++head) {
        u32 cur = queue[head];
        const SystemNode* cs = st.system(cur);
        if (cs == nullptr) continue;
        for (u32 nx : cs->links) {
            if (nx >= n || seen[nx]) continue;
            seen[nx] = true;
            parent[nx] = static_cast<i64>(cur);
            if (nx == to) {
                found = true;
                break;
            }
            queue.push_back(nx);
        }
    }
    if (!found) return empty;
    std::vector<u32> path;
    for (i64 at = static_cast<i64>(to); at >= 0; at = parent[static_cast<std::size_t>(at)]) {
        path.push_back(static_cast<u32>(at));
        if (static_cast<u32>(at) == from) break;
    }
    std::reverse(path.begin(), path.end());
    return path;
}

Fixed tradePathRisk(const GameState& st, u32 exporter, u32 importer, const std::vector<u32>& path,
                    std::string* note) {
    Fixed risk = Fixed(0);
    std::string worst;
    for (u32 sysId : path) {
        const SystemNode* s = st.system(sysId);
        if (s == nullptr) continue;
        // 交战方控制的中转星系 ⇒ 路线被切断
        if (s->owner != kNoEmpire && s->owner != exporter && s->owner != importer) {
            if (atWarWith(st, exporter, s->owner) || atWarWith(st, importer, s->owner)) {
                if (note) *note = "途经敌对星系";
                return Fixed(1);
            }
            // 中立但关系恶劣 ⇒ 通行受阻
            Fixed tension = -st.relation(s->owner, exporter).opinion;
            if (tension.rawValue() > 0) risk += tension * Fixed::pct(10);
        }
        // 封锁：这是舰队「阻断」命令真正发挥作用的地方
        if (s->blockade.rawValue() > 0) risk += s->blockade * Fixed::pct(60);
        // 海盗与航行风险（贸易枢纽会降低风险）
        Fixed hubRelief = fxMin(s->tradeHub, Fixed::pct(50));
        risk += (s->pirates * Fixed::pct(30) + s->hazard * Fixed::pct(20)) *
                (Fixed(1) - hubRelief / Fixed::pct(50));
    }
    // 路径越长，暴露在风险中的机会越多
    if (!path.empty()) risk = risk / fxSqrt(Fixed(static_cast<i64>(path.size())));
    risk = fxClamp(risk, Fixed(0), Fixed(1));
    if (note != nullptr && risk.rawValue() > 0) {
        if (note->empty()) *note = "路径风险 " + fixedStrPlain(risk * Fixed(100), 0) + "%";
    }
    return risk;
}

/// 某帝国某商品的**每季产出**（与 economyPhase 使用同一套公式）。
/// 贸易必须基于流量而非存量判断：光是「库存高于需求」并不代表可以出口 ——
/// 那可能只是正在被消耗的储备。只有产出持续超过需求才是真正的可出口剩余。
Fixed empireProduction(const GameState& st, const Empire& e, u8 c) {
    Fixed prod = megaProduction(st, e, c);
    const Fixed demand = resourceDemand(st, e, c);
    for (u32 sys : e.systems) {
        const SystemNode* sn = st.system(sys);
        if (sn == nullptr) continue;
        for (u32 pid : sn->planets) {
            const Planet* p = st.planet(pid);
            if (p == nullptr || p->owner != e.id) continue;
            prod += planetNaturalProduction(*p, c, e.stock[c], demand);
            for (u32 bid : p->buildings) {
                const auto& bi = buildingInfo(static_cast<int>(bid & 0xFFu));
                int resource = -1;
                switch (bi.effect) {
                    case BuildingEffect::ProdEnergy: resource = static_cast<int>(Commodity::Energy); break;
                    case BuildingEffect::ProdMinerals: resource = static_cast<int>(Commodity::Minerals); break;
                    case BuildingEffect::ProdFood: resource = static_cast<int>(Commodity::Food); break;
                    case BuildingEffect::ProdMedicines: resource = static_cast<int>(Commodity::Medicines); break;
                    case BuildingEffect::ProdAlloys: resource = static_cast<int>(Commodity::Alloys); break;
                    case BuildingEffect::ProdComponents: resource = static_cast<int>(Commodity::Components); break;
                    default: break;
                }
                if (resource == c) prod += bi.effectValue;
            }
        }
    }
    return prod;
}

/// 可出口剩余（流量口径）：产出 − 需求，并预留安全库存
Fixed exportableSurplus(const GameState& st, const Empire& e, u8 c) {
    Fixed prod = empireProduction(st, e, c);
    Fixed need = resourceDemand(st, e, c);
    for (const auto& project : e.developmentProjects) need += Fixed(project.supplies[c]);
    Fixed flow = prod - need;
    if (flow.rawValue() <= 0) return Fixed(0);
    // 需要至少两季需求的库存作为周转缓冲，否则先补库存而不是出口
    Fixed buffer = need * Fixed(2);
    Fixed stock = e.stock[static_cast<std::size_t>(c)];
    if (stock.rawValue() < buffer.rawValue()) return Fixed(0);
    return flow;
}

/// 进口需求（流量口径）：需求 − 产出
Fixed importNeed(const GameState& st, const Empire& e, u8 c) {
    Fixed prod = empireProduction(st, e, c);
    Fixed need = resourceDemand(st, e, c);
    for (const auto& project : e.developmentProjects) need += Fixed(project.supplies[c]);
    Fixed flow = need - prod;
    return flow.rawValue() > 0 ? flow : Fixed(0);
}

const TradeNetwork& tradeNetwork(const GameState& st) { return st.market.trade; }
TradeNetwork& tradeNetworkMut(GameState& st) { return st.market.trade; }

const TradeRoute* findRoute(const GameState& st, u32 a, u32 b, u8 commodity) {
    for (const auto& r : st.market.trade.routes) {
        if (r.commodity != commodity) continue;
        if ((r.exporter == a && r.importer == b) || (r.exporter == b && r.importer == a)) return &r;
    }
    return nullptr;
}

Fixed avgTariffToward(const GameState& st, u32 empire, u32 partner) {
    Fixed sum = Fixed(0);
    int n = 0;
    for (const auto& r : st.market.trade.routes) {
        if (!r.active) continue;
        if (r.importer == empire && r.exporter == partner) {
            sum += r.tariff;
            ++n;
        }
    }
    return n > 0 ? sum / Fixed(n) : Fixed(0);
}

bool tradeOpen(GameState& st, u32 exporter, u32 importer, u8 commodity, Fixed tariff, std::string* err) {
    if (exporter == importer) {
        if (err) *err = "不能与自己通商";
        return false;
    }
    if (commodity >= kCommodityCount) {
        if (err) *err = "非法商品";
        return false;
    }
    const Empire* ex = st.empire(exporter);
    const Empire* im = st.empire(importer);
    if (ex == nullptr || im == nullptr) {
        if (err) *err = "非法主体";
        return false;
    }
    if (!ex->alive || !im->alive) {
        if (err) *err = "对方已灭亡";
        return false;
    }
    if (atWarWith(st, exporter, importer)) {
        if (err) *err = "交战中无法通商";
        return false;
    }
    if (st.relation(exporter, importer).embargo || st.relation(importer, exporter).embargo) {
        if (err) *err = "存在禁运，无法通商";
        return false;
    }
    if (findRoute(st, exporter, importer, commodity) != nullptr) {
        if (err) *err = "该商品的路线已存在";
        return false;
    }
    if (tariff.rawValue() < 0) tariff = Fixed(0);
    if (tariff.rawValue() > Fixed::pct(kMaxTariffPct).rawValue()) tariff = Fixed::pct(kMaxTariffPct);
    // 出口方必须确有剩余，否则路线无意义
    Fixed cap = routeCapacity(st, *ex, *im, commodity, tariff);
    if (cap.rawValue() <= 0) {
        if (err) *err = "出口方没有该商品的剩余，或进口方没有缺口";
        return false;
    }
    TradeRoute r;
    r.id = st.market.trade.nextRouteId++;
    r.exporter = exporter;
    r.importer = importer;
    r.commodity = commodity;
    r.tariff = tariff;
    r.capacity = cap;
    r.establishedTick = static_cast<u32>(st.tick);
    // 实体路径：从出口方首都到进口方首都
    // 用**风险加权**路径而非最短路：贸易会绕开战区与封锁
    r.path = tradePathWeighted(st, ex->capital, im->capital, exporter, importer);
    if (r.path.empty()) r.path = tradePathBetween(st, ex->capital, im->capital);
    st.market.trade.routes.push_back(std::move(r));
    // 日志不在这里写：AI 一季可能连开多条，逐条记录会把日志刷爆
    //（实测 5 季内 42 条 trade.open）。由调用方按帝国聚合成一条。
    return true;
}

bool tradeClose(GameState& st, u32 routeId, u32 requester, std::string* err) {
    auto& routes = st.market.trade.routes;
    for (auto it = routes.begin(); it != routes.end(); ++it) {
        if (it->id != routeId) continue;
        if (requester != it->exporter && requester != it->importer) {
            if (err) *err = "只有贸易双方可以关闭该路线";
            return false;
        }
        const Empire* ex = st.empire(it->exporter);
        const Empire* im = st.empire(it->importer);
        st.logEvent(LogPhase::Economy, "trade.close",
                    std::string(ex ? ex->name : "?") + " → " + std::string(im ? im->name : "?") + " 的 " +
                        std::string(commodityName(it->commodity)) + " 路线已关闭",
                    requester);
        routes.erase(it);
        return true;
    }
    if (err) *err = "找不到该贸易路线";
    return false;
}

bool tradeSetTariff(GameState& st, u32 routeId, u32 requester, Fixed tariff, std::string* err) {
    if (requester == kNoEmpire) {
        if (err) *err = "非法主体";
        return false;
    }
    for (auto& r : st.market.trade.routes) {
        if (r.id != routeId) continue;
        // 只有进口方可以调整关税率
        if (requester != r.importer) {
            if (err) *err = "只有进口方可以调整关税率";
            return false;
        }
        if (tariff.rawValue() < 0) tariff = Fixed(0);
        if (tariff.rawValue() > Fixed::pct(kMaxTariffPct).rawValue()) tariff = Fixed::pct(kMaxTariffPct);
        Fixed old = r.tariff;
        r.tariff = tariff;
        const Empire* ex = st.empire(r.exporter);
        const Empire* im = st.empire(r.importer);
        // 大幅提高关税会恶化关系 —— 这是关税战的政治成本
        if (tariff.rawValue() > old.rawValue() + Fixed::pct(5).rawValue()) {
            Fixed delta = (tariff - old) * Fixed::pct(60);
            st.relation(r.importer, r.exporter).opinion =
                fxClamp(st.relation(r.importer, r.exporter).opinion - delta, Fixed(-1), Fixed(1));
            st.relation(r.exporter, r.importer).opinion =
                fxClamp(st.relation(r.exporter, r.importer).opinion - delta, Fixed(-1), Fixed(1));
        }
        st.logEvent(LogPhase::Economy, "trade.tariff",
                    std::string(im ? im->name : "?") + " 对来自 " + std::string(ex ? ex->name : "?") + " 的 " +
                        std::string(commodityName(r.commodity)) + " 关税调整为 " +
                        fixedStrPlain(tariff * Fixed(100), 0) + "%",
                    r.importer);
        return true;
    }
    if (err) *err = "找不到该贸易路线";
    return false;
}

void tradePhase(GameState& st) {
    TradeNetwork& tn = st.market.trade;

    // ---- 封锁的维持与衰减 ----
    // 早期「阻断」命令只在下达时一次性 +30%，之后舰队再无作用、封锁也永不衰减。
    // 正确模型：执行阻断的舰队**每季**维持所在星系的封锁强度，
    // 没有舰队维持时封锁自然消退。
    {
        std::vector<Fixed> want(st.map.systems.size(), Fixed(0));
        for (const auto& f : st.fleets) {
            if (f.order != FleetOrder::Blockade) continue;
            if (f.org.rawValue() <= 0) continue;
            u32 sysId = (f.targetSystem != kNoSystem && f.system == f.targetSystem) ? f.targetSystem
                                                                                   : f.system;
            if (sysId >= want.size()) continue;
            // 封锁强度由舰队战力决定，上限 80%
            want[sysId] = fxMin(want[sysId] + f.strength / Fixed(3000), Fixed::pct(80));
        }
        for (std::size_t i = 0; i < st.map.systems.size(); ++i) {
            SystemNode& s = st.map.systems[i];
            if (want[i].rawValue() > 0) {
                // 向目标值靠拢（舰队需要时间建立封锁）
                s.blockade = fxLerp(s.blockade, want[i], Fixed::pct(35));
            } else if (s.blockade.rawValue() > 0) {
                // 无舰队维持 ⇒ 每季消退 20%
                s.blockade = s.blockade * Fixed::pct(80);
                if (s.blockade.rawValue() < Fixed::raw(5).rawValue()) s.blockade = Fixed(0);
            }
        }
    }
    // 重置本季统计
    for (auto& e : st.empires) {
        e.trade.importVolume = Fixed(0);
        e.trade.exportVolume = Fixed(0);
        e.trade.tariffIncome = Fixed(0);
        e.trade.exportIncome = Fixed(0);
        e.trade.spentThisTick = Fixed(0);
    }
    tn.importVolume = Fixed(0);
    tn.exportVolume = Fixed(0);
    tn.tariffIncome = Fixed(0);
    tn.exportIncome = Fixed(0);

    for (auto& r : tn.routes) {
        r.disrupted.clear();
        Empire* ex = st.empire(r.exporter);
        Empire* im = st.empire(r.importer);
        if (ex == nullptr || im == nullptr || !ex->alive || !im->alive) {
            r.active = false;
            r.volume = Fixed(0);
            r.disrupted = "一方已灭亡";
            continue;
        }
        // 中断判定：战争 / 禁运
        if (atWarWith(st, r.exporter, r.importer)) {
            r.active = false;
            r.volume = Fixed(0);
            r.disrupted = "交战中";
            continue;
        }
        if (st.relation(r.exporter, r.importer).embargo || st.relation(r.importer, r.exporter).embargo) {
            r.active = false;
            r.volume = Fixed(0);
            r.disrupted = "禁运中";
            continue;
        }
        r.active = true;

        // 实体路径风险：封锁 / 海盗 / 敌对领土
        r.pathNote.clear();
        r.path = tradePathWeighted(st, ex->capital, im->capital, r.exporter, r.importer);
        r.pathRisk = tradePathRisk(st, r.exporter, r.importer, r.path, &r.pathNote);
        if (r.path.empty() || r.pathRisk.rawValue() >= Fixed(1).rawValue()) {
            r.active = false;
            r.volume = Fixed(0);
            r.disrupted = r.pathNote.empty() ? "路径被切断" : r.pathNote;
            continue;
        }

        // 运力与运输（路径风险按比例削减）
        Fixed cap = routeCapacity(st, *ex, *im, r.commodity, r.tariff);
        cap = cap * (Fixed(1) - r.pathRisk);
        // 贸易枢纽建筑提升本国路线的运力。
        // 早期 `Trading` 效果只写进 AI 的选型打分、对贸易毫无影响，
        // 玩家花 22,600 cr 建的贸易枢纽是个纯摆设。
        cap = cap * (Fixed(1) + fxMax(ex->tradeBonus, im->tradeBonus));
        r.capacity = cap;
        if (cap.rawValue() <= 0) {
            r.volume = Fixed(0);
            r.disrupted = "无剩余或无缺口";
            ++r.dormantTicks;
            continue;
        }
        std::size_t ci = static_cast<std::size_t>(r.commodity);
        Fixed moved = fxMin(cap, ex->stock[ci]);
        if (moved.rawValue() <= 0) {
            r.volume = Fixed(0);
            r.disrupted = "出口方库存不足";
            ++r.dormantTicks;
            continue;
        }
        // 结算：出口方按世界价收款，进口方支付含税价
        Fixed px = routeUnitPrice(st, r.commodity, r.tariff);
        // 付款能力约束：进口方付不起就按可支付额度缩减运量。
        // 没有这一步，出口方可以单方面开路线并持续向进口方收款，
        // 把对方的国库掏空（实测 AI 帝国因此跌到 -48 万）。
        // 关税是进口方政府自己的收入，因此这里只检查货款部分。
        Fixed netUnit = px * (Fixed(1) - r.tariff);
        if (netUnit.rawValue() > 0) {
            // 付款能力约束：进口方付不起就按可支付额度缩减运量。
            // 这本身就保证国库不会被贸易掏成负数 —— 无需再叠加绝对储备。
            // （实测：储备设 25k 时 AI 国库普遍低于该值，贸易量直接归零。）
            Fixed usable = fxMax(im->treasury, Fixed(0));
            Fixed affordable = usable / netUnit;
            if (affordable.rawValue() < moved.rawValue()) {
                moved = fxMax(affordable, Fixed(0));
            }
        }
        if (moved.rawValue() <= 0) {
            r.volume = Fixed(0);
            r.disrupted = "进口方无力支付";
            ++r.dormantTicks;
            continue;
        }
        // 固定点下净单价与总额的舍入顺序不同，最终账款仍不得超过余额。
        while (moved.rawValue() > 0) {
            Fixed value = px * moved;
            if ((value - value * r.tariff).rawValue() <= im->treasury.rawValue()) break;
            moved -= Fixed::raw(1);
        }
        r.unitPrice = px;
        // 按最终可支付运量交货，物权、账目与统计使用同一数量。
        ex->stock[ci] -= moved;
        im->stock[ci] += moved;
        r.volume = moved;
        r.dormantTicks = 0;
        Fixed gross = px * moved;
        Fixed tariffCut = gross * r.tariff;
        // ---- 过境费：途经第三国领土要付通行费 ----
        // 缺少这一环时，路线穿过谁的地盘都免费，中转国的地理位置毫无价值。
        // 费率取货值的 2%（低于进口关税，避免压垮贸易）。
        r.transitEmpires.clear();
        r.transitFee = Fixed(0);
        {
            Fixed perHop = gross * Fixed::pct(2);
            for (std::size_t hi = 1; hi + 1 < r.path.size(); ++hi) {
                const SystemNode* hop = st.system(r.path[hi]);
                if (hop == nullptr) continue;
                u32 owner = hop->owner;
                if (owner == kNoEmpire || owner == r.exporter || owner == r.importer) continue;
                Empire* mid = st.empire(owner);
                if (mid == nullptr || !mid->alive) continue;
                if (std::find(r.transitEmpires.begin(), r.transitEmpires.end(), owner) ==
                    r.transitEmpires.end())
                    r.transitEmpires.push_back(owner);
                r.transitFee += perHop;
            }
            r.transitFee = fxMin(r.transitFee, gross * Fixed::pct(12));   // 上限，避免累进过高
        }
        Fixed netToExporter = gross - tariffCut - r.transitFee;
        if (netToExporter.rawValue() < 0) netToExporter = Fixed(0);
        ex->treasury += netToExporter;
        // 过境费按「每个途经国各得一份」分配（同一国只计一次）
        if (r.transitFee.rawValue() > 0 && !r.transitEmpires.empty()) {
            Fixed share = r.transitFee / Fixed(static_cast<i64>(r.transitEmpires.size()));
            for (u32 mid : r.transitEmpires) {
                Empire* me2 = st.empire(mid);
                if (me2 == nullptr || !me2->alive) continue;
                me2->treasury += share;
                me2->trade.tariffIncome += share;
                if (me2->isPlayer) st.market.margin.cash = me2->treasury;
            }
        }
        im->treasury -= gross;    // 进口方付出全额（货款 + 关税）
        im->treasury += tariffCut;   // 关税收入归进口方政府
        // 净效果：进口方付出 netToExporter（货款），政府收到 tariffCut
        r.exportRevenue += netToExporter;
        r.tariffRevenue += tariffCut;

        if (ex->isPlayer) st.market.margin.cash = ex->treasury;
        if (im->isPlayer) st.market.margin.cash = im->treasury;

        // 统计
        ex->trade.exportVolume += moved;
        ex->trade.exportIncome += netToExporter;
        im->trade.importVolume += moved;
        im->trade.tariffIncome += tariffCut;
        im->trade.spentThisTick += netToExporter;
        tn.exportVolume += moved;
        tn.importVolume += moved;
        tn.exportIncome += netToExporter;
        tn.tariffIncome += tariffCut;

        // 长期贸易会改善双方关系（互惠）
        Relation& rab = st.relation(r.exporter, r.importer);
        Relation& rba = st.relation(r.importer, r.exporter);
        Fixed warm = Fixed::bp(5);
        rab.opinion = fxClamp(rab.opinion + warm, Fixed(-1), Fixed(1));
        rba.opinion = fxClamp(rba.opinion + warm, Fixed(-1), Fixed(1));
    }

    // 清理长期休眠的路线：贸易格局会随生产与库存变化，
    // 失效路线若长期保留会堆积成噪声（实测曾积到 52 条、多数无货可运）。
    tn.routes.erase(std::remove_if(tn.routes.begin(), tn.routes.end(),
                                   [](const TradeRoute& r) { return r.dormantTicks > 12; }),
                    tn.routes.end());

    // 清理已灭亡方的路线
    tn.routes.erase(std::remove_if(tn.routes.begin(), tn.routes.end(),
                                   [&](const TradeRoute& r) {
                                       const Empire* a = st.empire(r.exporter);
                                       const Empire* b = st.empire(r.importer);
                                       return a == nullptr || b == nullptr || !a->alive || !b->alive;
                                   }),
                    tn.routes.end());

    // 记录每个帝国对各伙伴的平均关税（供关税战判定）
    for (auto& e : st.empires) {
        e.trade.avgTariffByPartner.clear();
        for (const auto& o : st.empires) {
            if (o.id == e.id || !o.alive) continue;
            Fixed t = avgTariffToward(st, e.id, o.id);
            if (t.rawValue() > 0) e.trade.avgTariffByPartner.emplace_back(o.id, t);
        }
    }
}

void tradeAiPhase(GameState& st) {
    // AI 每隔数季评估一次贸易机会，避免每季都做重决策
    if (st.tick % 4 != 0) return;
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        // 1) 对高关税伙伴实施报复性关税（关税战）
        for (auto& [partner, theirTariff] : e.trade.avgTariffByPartner) {
            if (theirTariff.rawValue() < Fixed::pct(30).rawValue()) continue;
            // 报复：把我方对其的关税提到相近水平
            for (auto& r : st.market.trade.routes) {
                if (r.importer != e.id || r.exporter != partner) continue;
                if (r.tariff.rawValue() >= theirTariff.rawValue() - Fixed::pct(5).rawValue()) continue;
                r.tariff = fxMin(theirTariff, Fixed::pct(kMaxTariffPct));
                st.logEvent(LogPhase::Economy, "trade.retaliate",
                            e.name + " 对 " + std::string(st.empire(partner) ? st.empire(partner)->name : "?") +
                                " 实施报复性关税 " + fixedStrPlain(r.tariff * Fixed(100), 0) + "%",
                            e.id);
            }
        }
        // 2) 开设新路线：找自己有剩余、对方有缺口的商品
        std::string opened;   // 本季为该国开通的路线摘要（聚合为一条日志）
        int openedCount = 0;
        u32 myRoutes = 0;
        for (const auto& rr : st.market.trade.routes)
            if (rr.exporter == e.id || rr.importer == e.id) ++myRoutes;
        if (myRoutes >= 6) continue;
        for (int c = 0; c < kCommodityCount; ++c) {
            std::size_t ci = static_cast<std::size_t>(c);
            if (exportableSurplus(st, e, static_cast<u8>(c)).rawValue() <= 0) continue;
            // 找一个缺该商品的和平伙伴
            for (const auto& o : st.empires) {
                if (o.id == e.id || !o.alive) continue;
                // AI 不得单方面对**玩家**开通进口路线。
                // 路线会让进口方持续付款；若 AI 能替玩家决定进口，
                // 玩家的国库会被未经同意的贸易掏空（实测持续失血至 -50 万）。
                // 玩家用 `greyfall trade --open` 自行决定进口什么。
                if (o.isPlayer) continue;
                if (atWarWith(st, e.id, o.id)) continue;
                if (st.relation(e.id, o.id).embargo || st.relation(o.id, e.id).embargo) continue;
                if (findRoute(st, e.id, o.id, static_cast<u8>(c)) != nullptr) continue;
                if (importNeed(st, o, static_cast<u8>(c)).rawValue() <= 0) continue;
                (void)ci;
                // 用较低关税开局以吸引贸易
                if (tradeOpen(st, e.id, o.id, static_cast<u8>(c), Fixed::pct(8), nullptr)) {
                    if (!opened.empty()) opened += "、";
                    opened += std::string(commodityName(c)) + "→" + o.name;
                    ++openedCount;
                    break;
                }
            }
        }
        if (!opened.empty()) {
            st.logEvent(LogPhase::Economy, "trade.open",
                        e.name + " 开通 " + std::to_string(openedCount) + " 条贸易路线：" + opened,
                        e.id);
        }
    }
}

std::string tradeRouteText(const GameState& st, const TradeRoute& r) {
    const Empire* ex = st.empire(r.exporter);
    const Empire* im = st.empire(r.importer);
    std::string out;
    out += "  #" + padRight(std::to_string(r.id), 4) + padRight(ex ? ex->name : "?", 14) + "→ " +
           padRight(im ? im->name : "?", 14) + padRight(std::string(commodityName(r.commodity)), 10);
    out += " 关税 " + padLeft(fixedStrPlain(r.tariff * Fixed(100), 0) + "%", 4);
    out += "  运力 " + padLeft(fixedStr(r.capacity, 0), 7);
    out += "  本季 " + padLeft(fixedStr(r.volume, 0), 7);
    out += "  单价 " + padLeft(fixedStr(r.unitPrice, 1), 7);
    if (r.pathRisk.rawValue() > 0)
        out += "  路径风险 " + padLeft(fixedStrPlain(r.pathRisk * Fixed(100), 0) + "%", 4);
    if (!r.disrupted.empty()) out += "  " + style("[" + r.disrupted + "]", Style::Warn);
    else if (!r.active) out += "  " + style("[停运]", Style::Dim);
    return out;
}

std::string tradeText(const GameState& st, u32 empireId) {
    const Empire* e = st.empire(empireId);
    if (e == nullptr) return "非法主体\n";
    const TradeNetwork& tn = st.market.trade;
    std::string out;
    out += "  世界贸易总量 " + fixedStr(tn.exportVolume, 0) + "   关税总额 " + fixedStr(tn.tariffIncome, 0) +
           "\n\n";
    out += style("我方贸易概况", Style::Sub) + "\n";
    out += "  进口 " + fixedStr(e->trade.importVolume, 0) + "   出口 " + fixedStr(e->trade.exportVolume, 0) +
           "\n";
    out += "  关税收入 " + fixedStr(e->trade.tariffIncome, 0) + "   出口收入 " +
           fixedStr(e->trade.exportIncome, 0) + "\n";

    // 进口路线
    out += "\n" + style("进口路线（我方征税）", Style::Sub) + "\n";
    int n = 0;
    for (const auto& r : tn.routes) {
        if (r.importer != empireId) continue;
        out += tradeRouteText(st, r) + "\n";
        ++n;
    }
    if (n == 0) out += "  （无）\n";

    // 出口路线
    out += "\n" + style("出口路线", Style::Sub) + "\n";
    n = 0;
    for (const auto& r : tn.routes) {
        if (r.exporter != empireId) continue;
        out += tradeRouteText(st, r) + "\n";
        ++n;
    }
    if (n == 0) out += "  （无）\n";

    // 路径与封锁态势
    {
        int shown = 0;
        for (const auto& r : tn.routes) {
            if (r.importer != empireId && r.exporter != empireId) continue;
            if (r.path.size() < 2 || r.pathRisk.rawValue() <= 0) continue;
            if (shown == 0) out += "\n" + style("路径风险", Style::Sub) + "\n";
            if (shown >= 6) break;
            out += "  #" + padLeft(std::to_string(r.id), 3) + "  " + std::to_string(r.path.size()) +
                   " 跳  ";
            // 列出路径上风险最高的星系
            u32 worstSys = r.path[0];
            Fixed worstVal = Fixed(0);
            for (u32 sid : r.path) {
                const SystemNode* sn = st.system(sid);
                if (sn == nullptr) continue;
                Fixed v = sn->blockade + sn->pirates + sn->hazard;
                if (v.rawValue() > worstVal.rawValue()) {
                    worstVal = v;
                    worstSys = sid;
                }
            }
            const SystemNode* ws = st.system(worstSys);
            out += "风险 " + padLeft(fixedStrPlain(r.pathRisk * Fixed(100), 0) + "%", 4);
            if (worstVal.rawValue() > Fixed::raw(50).rawValue() && ws != nullptr)
                out += "  最险节点：" + ws->name + "（封锁 " +
                       fixedStrPlain(ws->blockade * Fixed(100), 0) + "%）";
            out += "\n";
            ++shown;
        }
    }

    // 关税战态势
    if (!e->trade.avgTariffByPartner.empty()) {
        out += "\n" + style("关税态势（我方被征收的平均税率）", Style::Sub) + "\n";
        for (const auto& [partner, t] : e->trade.avgTariffByPartner) {
            const Empire* p = st.empire(partner);
            out += "  " + padRight(p ? p->name : "?", 16) + fixedStrPlain(t * Fixed(100), 0) + "%";
            if (t.rawValue() >= Fixed::pct(30).rawValue()) out += style("  ⚠ 高关税（可考虑报复）", Style::Warn);
            out += "\n";
        }
    }
    return out;
}

}  // namespace gf
