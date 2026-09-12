#include "util/Fmt.h"
#include "gen/EmpireGen.h"
#include "ai/OmniscientReader.h"

#include <algorithm>

#include "domain/Empire.h"
#include "mkt/OrderBook.h"
#include "util/Str.h"

namespace gf {

Fixed subjectCounterIntel(const GameState& st, u32 subject) {
    const Empire* e = st.empire(subject);
    if (e == nullptr) return Fixed(0);
    Fixed ci = e->counterIntel + empireModifier(*e, ModKind::IntelDefense);
    // 装备了心灵屏障/静默场一类的道具
    for (const auto& it : st.inventory.items) {
        if (subject != kPlayerId) break;
        const ItemDef& d = itemDef(it.def);
        if (itemHasTag(d, ItemTag::Barrier)) ci += d.effectValue;
    }
    // 静默修道院等建筑的额外防御已在 intelDefense 中体现
    ci += e->intelDefense * Fixed::pct(50);
    return fxClamp(ci, Fixed(0), Fixed::pct(85));
}

Observable omniscientSnapshot(const GameState& st, u32 subject) {
    Observable o;
    o.subject = subject;
    const Empire* e = st.empire(subject);
    if (e == nullptr) return o;

    // 反情报/屏障降低覆盖率
    Fixed ci = subjectCounterIntel(st, subject);
    o.coverage = Fixed(1) - ci / Fixed(2);

    auto addExposure = [&](std::string field, ProvChannel ch, Fixed conf, std::string path, bool forged = false,
                           Fixed cost = Fixed(0)) {
        FieldExposure f;
        f.field = std::move(field);
        f.channel = ch;
        f.confidence = fxClamp(conf, Fixed(0), Fixed(1));
        f.signalCost = cost;
        f.path = std::move(path);
        f.forged = forged;
        o.exposure.push_back(std::move(f));
    };

    // ---- 库存：直接观测（泛视网络）----
    o.resources = e->stock;
    bool forgedStock = false;
    // 玩家用 MarketIntervene 类道具布置过假库存
    for (const auto& it : st.inventory.items) {
        if (subject != kPlayerId) break;
        const ItemDef& d = itemDef(it.def);
        if (d.effect == ItemEffect::MarketFakeStock && it.forged) {
            forgedStock = true;
            o.contaminatedFields += 1;
        }
    }
    addExposure("resources", ProvChannel::Panopticon, Fixed::pct(92) * o.coverage, "泛视网络 · 舰桥遥测广播",
                forgedStock);
    if (forgedStock) {
        // 被污染的库存读数：AI 看到的不是真值
        for (int c = 0; c < kCommodityCount; ++c) {
            o.resources[static_cast<std::size_t>(c)] =
                o.resources[static_cast<std::size_t>(c)] * Fixed::raw(800);
        }
    }

    // ---- 预算 ----
    o.treasury = subject == kPlayerId ? st.market.margin.cash : e->treasury;
    o.influence = e->influence;
    o.unity = e->unity;
    addExposure("budget", ProvChannel::MarketFlow, Fixed::pct(85) * o.coverage,
                "订单流反演（每一笔支出都会留下痕迹）");

    // ---- 未公开线索 ----
    for (const auto& c : st.clues) {
        if (!c.known) continue;
        ClueView v;
        v.def = c.def;
        v.known = true;
        v.credibility = c.prov.effectiveCredibility(st.tick) * o.coverage;
        v.channel = c.prov.channel;
        v.act = c.act;
        for (const auto& eg : st.clueEdges)
            if (eg.a == c.def || eg.b == c.def) v.linked = true;
        o.clues.push_back(v);
    }
    addExposure("clues", ProvChannel::Panopticon, Fixed::pct(78) * o.coverage,
                "泛视网络 · 私人线索索引（未 link 的也可见）");

    // ---- 研发进度 ----
    o.techProgress = e->tech.progress;
    o.techCompleted = e->tech.completed;
    addExposure("tech", ProvChannel::Archive, Fixed::pct(88) * o.coverage, "档案抓取 · 论文与专利时间序列");

    // ---- 舰队坐标 ----
    for (u32 fid : e->fleets) {
        const Fleet* f = st.fleet(fid);
        if (f == nullptr) continue;
        FleetView v;
        v.id = f->id;
        v.system = f->system;
        v.target = f->targetSystem;
        v.strength = f->strength;
        v.morale = f->morale;
        v.order = static_cast<u8>(f->order);
        o.fleets.push_back(v);
    }
    addExposure("fleets", ProvChannel::Panopticon, Fixed::pct(95) * o.coverage, "泛视网络 · 舰队坐标实时广播");

    // ---- 未抉择事件与候选支 ----
    for (const auto& p : st.pending.items) {
        PendingView v;
        v.id = p.id;
        v.eventId = p.eventId;
        v.options = static_cast<int>(p.options.size());
        v.deferCount = p.deferCount;
        o.pending.push_back(v);
    }
    addExposure("pending", ProvChannel::Panopticon, Fixed::pct(80) * o.coverage, "泛视网络 · 抉择界面镜像");

    // ---- 未成交订单（front-running 的依据）----
    for (const auto& ex : st.market.exchanges) {
        for (const auto& b : ex.books) {
            for (const auto& ord : b.orders) {
                if (ord.owner != subject) continue;
                if (ord.qty - ord.filled <= 0) continue;
                OrderIntentView v;
                v.res = ord.res;
                v.buy = ord.buy;
                v.qty = ord.qty - ord.filled;
                v.px = ord.px;
                v.exch = ord.exch;
                v.queuedTick = ord.placedTick;
                o.orders.push_back(v);
            }
        }
    }
    for (const auto& pa : st.pendingActions) {
        if (pa.kind != PlannedKind::Order && pa.kind != PlannedKind::Futures) continue;
        OrderIntentView v;
        v.res = pa.res;
        v.buy = pa.qty > 0;
        v.qty = pa.qty;
        v.px = pa.px;
        v.exch = pa.exch;
        v.queuedTick = pa.queuedTick;
        v.exploited = true;
        o.orders.push_back(v);
    }
    addExposure("orders", ProvChannel::Panopticon, Fixed::pct(97) * o.coverage,
                "泛视网络 · 订单队列明文（可被前置交易）");

    // ---- 读档历史 ----
    o.rollbackCount = st.rollbackCount;
    o.chronicleHead = st.chronicleHead;
    o.chronicleBurned = st.chronicleBurned;
    addExposure("rollbackCount", ProvChannel::Archive, Fixed::pct(99),
                "档案焚毁痕迹 · 你自己无法隐藏", false, Fixed(0));

    // 已支付的启动物资不再重复算作未来需求，只统计后续施工补给。
    for (const auto& m : e->megas) if (!m.complete) o.megastructureIds.push_back(m.id);
    for (const auto& project : e->developmentProjects)
        for (int c = 0; c < kCommodityCount; ++c)
            o.futureDemand[c] += Fixed(project.supplies[c] * std::min<u32>(3, project.ticksLeft));
    o.colonizing = e->colonizing;
    addExposure("futureDemand", ProvChannel::Analysis, Fixed::pct(71) * o.coverage,
                "推演 · 从在建工程反推未来净需求", false, Fixed(0));

    o.military = e->military;
    o.economy = e->economy;
    o.stability = e->stability;
    return o;
}

Fixed observationConfidence(const GameState& st, u32 observer, u32 subject) {
    const Empire* o = st.empire(observer);
    if (o == nullptr) return Fixed(0);
    Fixed base = Fixed::pct(70) + o->mind.playerModel.modelConfidence * Fixed::pct(25);
    base -= subjectCounterIntel(st, subject) * Fixed::pct(40);
    if (st.chronicleBurned) base += Fixed::pct(10);   // 档案焚毁者反而被看得更清楚（无从辩解）
    return fxClamp(base, Fixed::pct(5), Fixed::pct(99));
}

std::string whatTheyKnowReport(const GameState& st, u32 observer, u32 subject) {
    Observable o = omniscientSnapshot(st, subject);
    const Empire* obs = st.empire(observer);
    std::string out;
    out += "═══ 泛视网络透明度报告 ═══\n";
    out += "观测者：" + std::string(obs ? obs->name : "未知") + "    被观测者：" +
           std::string(st.empire(subject) ? st.empire(subject)->name : "你") + "\n";
    out += "读取覆盖率：" + fixedStrPlain(o.coverage * Fixed(100), 1) + "%";
    if (o.contaminatedFields > 0)
        out += "    被布置的字段：" + std::to_string(o.contaminatedFields) + "（伪造数据已进入它的模型）";
    out += "\n\n";

    // 关键字段摘要
    i64 alloys = o.resources[static_cast<std::size_t>(Commodity::Alloys)].rawValue() / FIX;
    i64 energy = o.resources[static_cast<std::size_t>(Commodity::Energy)].rawValue() / FIX;
    Fixed totalFuture = Fixed(0);
    for (const auto& v : o.futureDemand) totalFuture += v;
    i64 futureAlloys = o.futureDemand[static_cast<std::size_t>(Commodity::Alloys)].rawValue() / FIX;

    out += "已知：\n";
    out += "  · 库存 alloys " + groupDigits(alloys) + " · energy " + groupDigits(energy) + "\n";
    if (totalFuture.rawValue() > 0) {
        out += "  · 在建工程未来需求合计 ≈ " + groupDigits(totalFuture.rawValue() / FIX) + " 单位";
        if (futureAlloys > 0) out += "，其中合金 +" + groupDigits(futureAlloys);
        out += "\n";
    }
    out += "  · 未抉择事件 " + std::to_string(o.pending.size()) + " · rollbackCount " +
           std::to_string(o.rollbackCount) + "\n";
    out += "  · 未成交订单 " + std::to_string(o.orders.size()) + " 笔（可被前置交易）\n";
    out += "  · 私人线索 " + std::to_string(o.clues.size()) + " 条（含未 link 的）\n";
    out += "  · 舰队 " + std::to_string(o.fleets.size()) + " 支（坐标实时可见）\n\n";

    // 判断
    out += "判断：\n";
    if (futureAlloys > static_cast<i64>(alloys)) {
        Fixed conf = observationConfidence(st, observer, subject);
        out += "  → 你 " + std::to_string(std::max(1, 4 - static_cast<int>(o.rollbackCount))) + " 季内必须买入 " +
               groupDigits(futureAlloys - alloys) + " 单位合金（置信 " + fixedStrPlain(conf, 2) + "）\n";
        out += "  → 它已据此提价，并在 F3 建立多头\n";
    } else {
        out += "  → 未检测到迫切的买入压力\n";
    }
    if (o.rollbackCount > 0) {
        out += "  → 检测到 " + std::to_string(o.rollbackCount) + " 次读档：它判定你会重试，";
        out += "已提高要价、降低让步概率，并优先采用不可逆打击\n";
    }
    if (o.chronicleBurned) {
        out += "  → chronicle 链缺失（档案焚毁）：各方将你视为背约者，援助与贸易折价消失\n";
    }

    out += "\n暴露路径（它为何能看到）：\n";
    for (const auto& f : o.exposure) {
        out += "  · " + padRight(f.field, 14) + " ← " + std::string(provChannelName(f.channel)) + "  " +
               f.path + "  置信 " + fixedStrPlain(f.confidence, 2);
        if (f.forged) out += "  【伪造·已污染】";
        out += "\n";
    }
    return out;
}

}  // namespace gf
