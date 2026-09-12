#include "items/RuleEngine.h"

#include <algorithm>
#include <set>

#include "ai/ToModel.h"
#include "clue/ClueGraph.h"
#include "mkt/OrderBook.h"
#include "rng/Streams.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

void applyEffect(RuleResult& r, ItemEffect e, Fixed v, std::vector<std::string>* trace, const std::string& who) {
    switch (e) {
        case ItemEffect::NegotiationDiscount:
            r.negotiationDiscount += v;
            break;
        case ItemEffect::TradeMargin:
            r.tradeMargin += v;
            break;
        case ItemEffect::IntelGain:
            r.intelGain += v;
            break;
        case ItemEffect::IntelDefense:
            r.intelDefense += v;
            break;
        case ItemEffect::DiplomacyWeight:
            r.diplomacyWeight += v;
            break;
        case ItemEffect::CombatBonus:
            r.combatBonus += v;
            break;
        case ItemEffect::TransportSlippage:
            r.transportSlippage += v;
            break;
        case ItemEffect::BeliefNoise:
            r.beliefNoise += v;
            break;
        case ItemEffect::ClueYield:
            r.clueYield += v;
            break;
        case ItemEffect::ResearchBoost:
            r.researchBoost += v;
            break;
        case ItemEffect::StabilityBoost:
            r.stabilityBoost += v;
            break;
        case ItemEffect::MarginRelief:
            r.marginRelief += v;
            break;
        case ItemEffect::CreditBoost:
            r.creditBoost += v;
            break;
        case ItemEffect::MarketFakeStock:
        case ItemEffect::None:
        case ItemEffect::Count:
            break;
    }
    if (trace != nullptr) {
        trace->push_back("    · " + who + " → " + std::string(itemEffectName(e)) + " " + fixedStrSigned(v, 3));
    }
}

}  // namespace

RuleResult ruleEngine(const GameState& st) {
    RuleResult r;
    r.trace.push_back("规则引擎求值（迭代到不动点，上限 16 轮）");

    // 玩家当前持有的 tag 集合
    std::vector<ItemTag> tags;
    for (const auto& it : st.inventory.items) {
        if (it.count == 0) continue;
        const ItemDef& d = itemDef(static_cast<int>(it.def));
        for (u8 k = 0; k < d.tagCount; ++k) tags.push_back(d.tags[k]);
    }
    auto hasTag = [&tags](ItemTag t) { return std::find(tags.begin(), tags.end(), t) != tags.end(); };

    // 第 0 轮：基础效果（道具自身）
    for (const auto& it : st.inventory.items) {
        if (it.count == 0) continue;
        const ItemDef& d = itemDef(static_cast<int>(it.def));
        Fixed v = d.effectValue * Fixed(static_cast<i64>(it.count));
        // 伪造道具的污染会削弱其正向效果
        if (it.forged) v = v * Fixed::pct(60);
        applyEffect(r, d.effect, v, &r.trace, std::string(d.nameZh));
    }
    // 载体绑定：装备在舰队上的模块改变战力系数
    for (const auto& e : st.empires) {
        if (!e.isPlayer) continue;
        for (const auto& d : e.designs) {
            for (u8 m : d.modules) {
                const ModuleInfo& mi = moduleInfo(static_cast<int>(m));
                if (mi.slot != ModuleSlot::Utility) continue;
                if (mi.effect == ModuleEffect::Stealth) r.combatBonus += Fixed::pct(3);
                if (mi.effect == ModuleEffect::Cargo) r.transportSlippage -= Fixed::pct(5);
            }
        }
    }

    // 迭代：协同 / 互斥 / 门槛，直到不动点或不变量不再变化
    const Fixed epsilon = Fixed::bp(1);
    for (int round = 1; round <= 16; ++round) {
        r.rounds = round;
        Fixed before = r.negotiationDiscount + r.tradeMargin + r.intelGain + r.intelDefense + r.diplomacyWeight +
                       r.combatBonus + r.beliefNoise + r.clueYield + r.marginRelief + r.fraudWeight;
        // 协同
        for (int i = 0; i < kSynergyCount; ++i) {
            const SynergyInfo& s = synergyInfo(i);
            if (!hasTag(s.tagA) || !hasTag(s.tagB)) continue;
            // 同 tag 只结算一次（用行为幂等标记）
            std::string key = "syn" + std::to_string(i);
            bool applied = false;
            for (const auto& t : r.trace)
                if (t.find(key) != std::string::npos) applied = true;
            if (applied) continue;
            std::vector<std::string> tr;
            applyEffect(r, s.effect, s.value, &tr, std::string(itemTagName(s.tagA)) + "+" +
                                                       std::string(itemTagName(s.tagB)));
            r.trace.push_back("  [" + key + "] 协同生效：" + std::string(s.desc));
            for (auto& line : tr) r.trace.push_back(line);
        }
        // 互斥
        for (int i = 0; i < kConflictCount; ++i) {
            const ConflictInfo& c = conflictInfo(i);
            if (!hasTag(c.tagA) || !hasTag(c.tagB)) continue;
            std::string key = "con" + std::to_string(i);
            bool applied = false;
            for (const auto& t : r.trace)
                if (t.find(key) != std::string::npos) applied = true;
            if (applied) continue;
            // 相克：削弱对应效果，并提高 FraudDetect 权重
            if (c.tagA == ItemTag::Forgery || c.tagB == ItemTag::Forgery) {
                r.fraudWeight += Fixed(1) * c.penalty;
            }
            r.negotiationDiscount -= r.negotiationDiscount * c.penalty * Fixed::pct(50);
            r.tradeMargin -= r.tradeMargin * c.penalty * Fixed::pct(50);
            r.trace.push_back("  [" + key + "] 互斥相克：" + std::string(c.desc) + "（FraudDetect 权重 →" +
                              fixedStrPlain(r.fraudWeight, 2) + "）");
        }
        Fixed after = r.negotiationDiscount + r.tradeMargin + r.intelGain + r.intelDefense + r.diplomacyWeight +
                      r.combatBonus + r.beliefNoise + r.clueYield + r.marginRelief + r.fraudWeight;
        if (fxAbs(after - before).rawValue() <= epsilon.rawValue()) break;
    }
    if (r.rounds >= 16) r.trace.push_back("  ⚠ 达到迭代上限 16 轮（可能存在环形规则）");

    r.negotiationDiscount = fxClamp(r.negotiationDiscount, Fixed(0), Fixed::pct(60));
    r.tradeMargin = fxClamp(r.tradeMargin, Fixed(0), Fixed::pct(60));
    r.fraudWeight = fxClamp(r.fraudWeight, Fixed(1), Fixed(4));
    return r;
}

bool itemGateOpen(const GameState& st, std::string_view action) {
    for (int i = 0; i < kGateCount; ++i) {
        const GateInfo& g = gateInfo(i);
        if (g.action != action) continue;
        for (const auto& it : st.inventory.items) {
            if (it.count == 0) continue;
            if (itemHasTag(itemDef(static_cast<int>(it.def)), g.required)) return true;
        }
        return false;
    }
    return true;   // 无门槛定义 ⇒ 放行
}

std::string itemGateReason(std::string_view action) {
    for (int i = 0; i < kGateCount; ++i) {
        const GateInfo& g = gateInfo(i);
        if (g.action == action) return std::string(g.desc);
    }
    return "无门槛限制";
}

bool combineItems(GameState& st, u16 a, u16 b, u16* out, std::string* err) {
    const RecipeInfo* recipe = nullptr;
    for (int i = 0; i < kRecipeCount; ++i) {
        const RecipeInfo& r = recipeInfo(i);
        if (r.inputs.size() < 2) continue;
        if ((r.inputs[0] == a && r.inputs[1] == b) || (r.inputs[0] == b && r.inputs[1] == a)) {
            recipe = &r;
            break;
        }
    }
    if (recipe == nullptr) {
        if (err) *err = "没有匹配的配方（A + B → C）";
        return false;
    }
    // 检查持有
    auto has = [&st](u16 item) {
        for (const auto& it : st.inventory.items)
            if (it.def == item && it.count > 0) return true;
        return false;
    };
    if (!has(a) || !has(b)) {
        if (err) *err = "缺少材料";
        return false;
    }
    // 失败率受 tag 冲突影响
    RuleResult rules = ruleEngine(st);
    Fixed fail = recipe->baseFailRate;
    fail = fail * rules.fraudWeight;
    bool ok = !st.rng.chance(RngStream::Items, fail);

    // 消耗材料
    auto consume = [&st](u16 item) {
        for (auto& it : st.inventory.items) {
            if (it.def == item && it.count > 0) {
                --it.count;
                return;
            }
        }
    };
    consume(a);
    consume(b);
    if (!ok) {
        // 不要带「合成失败」前缀：调用方已经会加，重复会出现「合成失败：合成失败（…）」
        if (err) *err = "工艺未达标（失败率 " + fixedStrPlain(fail * Fixed(100), 1) + "%，材料已损耗）";
        st.logEvent(LogPhase::Economy, "item.combine.fail", "合成失败，材料损耗", kPlayerId);
        return false;
    }
    ItemInstance ni;
    ni.def = recipe->output;
    ni.count = recipe->outputCount;
    ni.prov.channel = ProvChannel::DirectObservation;
    ni.prov.credibility = Fixed::pct(85);
    ni.prov.tick = st.tick;
    ni.acquiredTick = st.tick;
    st.inventory.items.push_back(ni);
    if (out != nullptr) *out = recipe->output;
    st.logEvent(LogPhase::Economy, "item.combine",
                "合成成功 → 【" + std::string(itemDef(recipe->output).nameZh) + "】（配方深度 " +
                    std::to_string(recipe->depth) + "）",
                kPlayerId);
    return true;
}

bool disassembleItem(GameState& st, u16 item, std::string* err) {
    const RecipeInfo* recipe = nullptr;
    for (int i = 0; i < kRecipeCount; ++i) {
        if (recipeInfo(i).output == item) {
            recipe = &recipeInfo(i);
            break;
        }
    }
    bool removed = false;
    for (auto& it : st.inventory.items) {
        if (it.def != item || it.count == 0) continue;
        --it.count;
        removed = true;
        break;
    }
    if (!removed) {
        if (err) *err = "未持有该道具";
        return false;
    }
    if (recipe == nullptr) {
        st.logEvent(LogPhase::Economy, "item.disassemble", "拆解后无可回收材料（非合成产物）", kPlayerId);
        return true;
    }
    for (u16 in : recipe->inputs) {
        ItemInstance ni;
        ni.def = in;
        ni.count = 1;
        ni.prov.channel = ProvChannel::Analysis;
        ni.prov.credibility = Fixed::pct(70);
        ni.prov.tick = st.tick;
        ni.acquiredTick = st.tick;
        st.inventory.items.push_back(ni);
    }
    st.logEvent(LogPhase::Economy, "item.disassemble",
                "拆解【" + std::string(itemDef(item).nameZh) + "】回收 " + std::to_string(recipe->inputs.size()) +
                    " 件材料（有损耗）",
                kPlayerId);
    return true;
}

bool useItem(GameState& st, u16 item, u32 target, std::string* err) {
    ItemInstance* inst = nullptr;
    for (auto& it : st.inventory.items) {
        if (it.def == item && it.count > 0) {
            inst = &it;
            break;
        }
    }
    if (inst == nullptr) {
        if (err) *err = "未持有该道具";
        return false;
    }
    const ItemDef& d = itemDef(static_cast<int>(item));
    switch (d.effect) {
        case ItemEffect::MarketFakeStock: {
            // 制造假库存：改变 AI 的基本面模型，不影响真实簿
            for (auto& e : st.empires) {
                if (e.isPlayer) continue;
                e.mind.playerModel.contamination =
                    fxClamp(e.mind.playerModel.contamination + d.effectValue, Fixed(0), Fixed(1));
            }
            st.logEvent(LogPhase::Ai, "item.fakeStock",
                        "布置假库存信号：AI 的基本面模型被污染（真实订单簿未改变）", kPlayerId);
            break;
        }
        case ItemEffect::BeliefNoise: {
            for (auto& e : st.empires) {
                if (e.isPlayer) continue;
                toModelInjectNoise(e.mind.playerModel, d.effectValue, d.effectValue / Fixed(2));
            }
            st.logEvent(LogPhase::Ai, "item.beliefNoise",
                        "信念干预：全体 AI 的 modelConfidence 下降 " + fixedStrPlain(d.effectValue, 2), kPlayerId);
            break;
        }
        case ItemEffect::ClueYield: {
            // 线索桥：产出特定线索
            for (int i = 0; i < kClueBridgeCount; ++i) {
                const ClueBridgeInfo& b = clueBridgeInfo(i);
                if (b.item != item) continue;
                Provenance p;
                p.channel = ProvChannel::Analysis;
                p.credibility = b.credibilityStart;
                p.tick = st.tick;
                p.source = kPlayerId;
                clueDiscover(st, b.clue, p, nullptr);
                break;
            }
            break;
        }
        case ItemEffect::IntelGain: {
            st.logEvent(LogPhase::Model, "item.intel", "情报道具生效：下一次侦察收获更大", kPlayerId);
            break;
        }
        case ItemEffect::NegotiationDiscount:
        case ItemEffect::DiplomacyWeight: {
            if (target < st.empires.size()) {
                Empire& t = st.empires[target];
                t.addOpinion(kPlayerId, d.effectValue);
            }
            st.logEvent(LogPhase::Model, "item.diplo",
                        "外交道具生效：谈判折价 +" + fixedStrPlain(d.effectValue, 2), kPlayerId);
            break;
        }
        case ItemEffect::MarginRelief: {
            st.market.margin.initMargin = fxMax(Fixed::pct(5), st.market.margin.initMargin - d.effectValue);
            st.logEvent(LogPhase::Market, "item.margin",
                        "保证金减免生效：初始保证金 → " + fixedStrPlain(st.market.margin.initMargin, 2), kPlayerId);
            break;
        }
        case ItemEffect::StabilityBoost: {
            st.empires[kPlayerId].domestic.unrest =
                fxClamp(st.empires[kPlayerId].domestic.unrest - d.effectValue, Fixed(0), Fixed(1));
            break;
        }
        case ItemEffect::ResearchBoost: {
            st.empires[kPlayerId].tech.rate += d.effectValue / Fixed(10);
            break;
        }
        default:
            st.logEvent(LogPhase::Economy, "item.use",
                        "使用【" + std::string(d.nameZh) + "】（被动效果已在规则引擎中生效）", kPlayerId);
            break;
    }
    if (d.consumable) --inst->count;
    return true;
}

bool forgeProveItem(GameState& st, u16 item, std::string* err) {
    ItemInstance* inst = nullptr;
    for (auto& it : st.inventory.items) {
        if (it.def == item && it.count > 0) {
            inst = &it;
            break;
        }
    }
    if (inst == nullptr) {
        if (err) *err = "未持有该道具";
        return false;
    }
    const ItemDef& d = itemDef(static_cast<int>(item));
    bool hasForgeryTag = itemHasTag(d, ItemTag::Forgery);
    inst->forged = true;
    inst->contamination = fxClamp(inst->contamination + Fixed::pct(40) + (hasForgeryTag ? Fixed::pct(20) : Fixed(0)),
                                  Fixed(0), Fixed(1));
    inst->prov.forged = true;
    inst->prov.forger = kPlayerId;
    inst->prov.channel = ProvChannel::Forgery;
    inst->prov.credibility = Fixed::pct(25);
    // 污染全体 AI 的模型
    for (auto& e : st.empires) {
        if (e.isPlayer) continue;
        e.mind.playerModel.contamination =
            fxClamp(e.mind.playerModel.contamination + inst->contamination * Fixed::pct(30), Fixed(0), Fixed(1));
        e.mind.playerModel.modelConfidence =
            fxClamp(e.mind.playerModel.modelConfidence - Fixed::pct(8), Fixed(0), Fixed(1));
    }
    st.logEvent(LogPhase::Ai, kLogFraud,
                "造假冒牌【" + std::string(d.nameZh) + "】：污染强度 " + fixedStrPlain(inst->contamination, 2) +
                    "（AI 的 FraudDetect 权重会因此上升，双刃剑）",
                kPlayerId, inst->contamination);
    return true;
}

std::string itemExplain(const GameState& st, u16 item) {
    const ItemDef& d = itemDef(static_cast<int>(item));
    std::string out;
    out += "═══ 道具规则求值：" + std::string(d.nameZh) + " ═══\n";
    out += "ID：" + std::string(d.idName) + "    tier " + std::to_string(d.tier) + "    基础价 " + groupDigits(d.baseCost) +
           " cr\n";
    out += "Tags：";
    for (u8 i = 0; i < d.tagCount; ++i) out += std::string(itemTagName(d.tags[i])) + " ";
    out += "\n效果：" + std::string(itemEffectName(d.effect)) + " " + fixedStrSigned(d.effectValue, 2) + "\n\n";

    // 相关协同
    out += "相关协同：\n";
    int n = 0;
    for (int i = 0; i < kSynergyCount; ++i) {
        const SynergyInfo& s = synergyInfo(i);
        if (!itemHasTag(d, s.tagA) && !itemHasTag(d, s.tagB)) continue;
        out += "  · " + std::string(itemTagName(s.tagA)) + "+" + std::string(itemTagName(s.tagB)) + " → " +
               std::string(itemEffectName(s.effect)) + " " + fixedStrSigned(s.value, 2) + "\n";
        if (++n >= 5) break;
    }
    if (n == 0) out += "  （无）\n";
    out += "相关互斥：\n";
    n = 0;
    for (int i = 0; i < kConflictCount; ++i) {
        const ConflictInfo& c = conflictInfo(i);
        if (!itemHasTag(d, c.tagA) && !itemHasTag(d, c.tagB)) continue;
        out += "  · " + std::string(c.desc) + "\n";
        if (++n >= 5) break;
    }
    if (n == 0) out += "  （无）\n";
    out += "相关门槛：\n";
    n = 0;
    for (int i = 0; i < kGateCount; ++i) {
        const GateInfo& g = gateInfo(i);
        if (g.required != d.tags[0]) continue;
        out += "  · " + std::string(g.desc) + "\n";
        if (++n >= 4) break;
    }
    if (n == 0) out += "  （无）\n";

    out += "\n全局求值轨迹：\n";
    RuleResult r = ruleEngine(st);
    for (const auto& line : r.trace) out += line + "\n";
    out += "\n合计：谈判折价 " + fixedStrPlain(r.negotiationDiscount, 2) + "，贸易毛利 " +
           fixedStrPlain(r.tradeMargin, 2) + "，情报获取 " + fixedStrPlain(r.intelGain, 2) + "，反间谍 " +
           fixedStrPlain(r.intelDefense, 2) + "，外交权重 " + fixedStrPlain(r.diplomacyWeight, 2) +
           "，战力系数 " + fixedStrPlain(r.combatBonus, 2) + "，FraudDetect 权重 " + fixedStrPlain(r.fraudWeight, 2) +
           "（迭代 " + std::to_string(r.rounds) + " 轮）\n";
    return out;
}

std::string inventoryText(const GameState& st, const std::string& tagFilter) {
    std::string out;
    int shown = 0;
    for (const auto& it : st.inventory.items) {
        if (it.count == 0) continue;
        const ItemDef& d = itemDef(static_cast<int>(it.def));
        if (!tagFilter.empty()) {
            bool match = false;
            for (u8 k = 0; k < d.tagCount; ++k)
                if (std::string(itemTagName(d.tags[k])) == tagFilter) match = true;
            if (!match) continue;
        }
        out += "  " + padRight(std::string(d.idName), 9) + padRight(std::string(d.nameZh), 22) + "×" +
               padLeft(std::to_string(it.count), 3) + "  tier " + std::to_string(d.tier) + "  [" ;
        for (u8 k = 0; k < d.tagCount; ++k) {
            if (k) out += ",";
            out += itemTagName(d.tags[k]);
        }
        out += "]  " + std::string(itemEffectName(d.effect)) + " " + fixedStrPlain(d.effectValue, 2);
        out += "  来源:" + std::string(provChannelName(it.prov.channel));
        if (it.forged) out += style("  【伪造 污染 " + fixedStrPlain(it.contamination, 2) + "】", Style::Bad);
        out += "\n";
        ++shown;
    }
    if (shown == 0) out = "  （没有符合条件的道具）\n";
    return out;
}

int findItemByTag(const GameState& st, ItemTag tag) {
    // 优先返回以该 tag 为主 tag 的道具（其效果与 tag 语义一致）
    for (const auto& it : st.inventory.items) {
        if (it.count == 0) continue;
        const ItemDef& d = itemDef(static_cast<int>(it.def));
        if (d.tagCount > 0 && d.tags[0] == tag) return static_cast<int>(it.def);
    }
    for (const auto& it : st.inventory.items) {
        if (it.count == 0) continue;
        if (itemHasTag(itemDef(static_cast<int>(it.def)), tag)) return static_cast<int>(it.def);
    }
    return -1;
}

}  // namespace gf
