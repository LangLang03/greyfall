#include <algorithm>
#include <string>

#include "cli/Commands.h"
#include "util/TextTable.h"
#include "clue/ClueGraph.h"
#include "core/Errors.h"
#include "domain/Fleet.h"
#include "domain/FleetDesign.h"
#include "items/RuleEngine.h"
#include "mkt/OrderBook.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

int requireItem(const GameState& st, const Args& args, std::size_t idx, const char* usage) {
    (void)st;
    if (args.posCount() <= idx) fail(ExitCode::BadArgs, std::string("用法：") + usage);
    int item = itemIndexByName(args.pos(idx));
    if (item < 0) {
        i64 num = parseInt(args.pos(idx), -1);
        if (num >= 0 && num < kItemCount) return static_cast<int>(num);
        fail(ExitCode::BadArgs, "未知道具【" + args.pos(idx) + "】（可用 idName 或中文名）");
    }
    return item;
}

}  // namespace

int cmdInventory(CliEnv& env, const Args& args) {
    env.loadState();
    std::string filter = args.get("filter", "");
    out(style("═══ 库存道具 ═══", Style::Heading));
    out(inventoryText(env.st, filter));
    RuleResult r = ruleEngine(env.st);
    out("");
    out(style("规则引擎合计（迭代 " + std::to_string(r.rounds) + " 轮）", Style::Sub));
    TextTable t;
    t.header({"效果", "数值"});
    t.row({"谈判折价", fixedStrSigned(r.negotiationDiscount, 2)});
    t.row({"贸易毛利", fixedStrSigned(r.tradeMargin, 2)});
    t.row({"情报获取", fixedStrSigned(r.intelGain, 2)});
    t.row({"反间谍", fixedStrSigned(r.intelDefense, 2)});
    t.row({"外交权重", fixedStrSigned(r.diplomacyWeight, 2)});
    t.row({"战力系数", fixedStrSigned(r.combatBonus, 2)});
    t.row({"运输滑点", fixedStrSigned(r.transportSlippage, 2)});
    t.row({"信念噪声", fixedStrSigned(r.beliefNoise, 2)});
    t.row({"线索产出", fixedStrSigned(r.clueYield, 2)});
    t.row({"保证金减免", fixedStrSigned(r.marginRelief, 2)});
    t.row({"FraudDetect 权重", fixedStrPlain(r.fraudWeight, 2)});
    out(t.render());
    return 0;
}

int cmdItem(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() == 0) {
        out(style("═══ 道具总表（前 40 件） ═══", Style::Heading));
        TextTable t;
        t.header({"#", "idName", "名称", "tier", "tags", "效果", "基础价"});
        for (int i = 0; i < 40 && i < kItemCount; ++i) {
            const ItemDef& d = itemDef(i);
            std::string tags;
            for (u8 k = 0; k < d.tagCount; ++k) {
                if (k) tags += ",";
                tags += itemTagName(d.tags[k]);
            }
            t.row({std::to_string(i), std::string(d.idName), std::string(d.nameZh), std::to_string(d.tier), tags,
                   std::string(itemEffectName(d.effect)), groupDigits(d.baseCost)});
        }
        out(t.render());
        out("");
        out("共 " + std::to_string(kItemCount) + " 件。用 `greyfall item <idName>` 查看单件，`--explain` 显示规则求值轨迹。");
        return 0;
    }
    int item = requireItem(env.st, args, 0, "greyfall item <id> [--explain]");
    const ItemDef& d = itemDef(item);
    TextTable t;
    t.header({"项目", "值"});
    t.row({"编号", std::to_string(item)});
    t.row({"idName", std::string(d.idName)});
    t.row({"名称", std::string(d.nameZh)});
    t.row({"tier", std::to_string(d.tier)});
    std::string tags;
    for (u8 k = 0; k < d.tagCount; ++k) {
        if (k) tags += ", ";
        tags += itemTagName(d.tags[k]);
    }
    t.row({"tags", tags});
    t.row({"效果", std::string(itemEffectName(d.effect)) + " " + fixedStrSigned(d.effectValue, 2)});
    t.row({"基础价", groupDigits(d.baseCost) + " cr"});
    t.row({"可消耗", d.consumable ? "是" : "否"});
    t.row({"可交易", d.tradeable ? "是" : "否"});
    out(t.render());
    out("");
    out(wrapJoin(d.desc, 86, "  "));

    // 持有情况
    int held = 0;
    for (const auto& it : env.st.inventory.items)
        if (it.def == static_cast<u16>(item)) held += static_cast<int>(it.count);
    out("");
    out("持有：" + std::to_string(held) + " 件");
    if (args.has("explain")) {
        out("");
        out(itemExplain(env.st, static_cast<u16>(item)));
    }
    return 0;
}

int cmdCombine(CliEnv& env, const Args& args) {
    env.loadState();
    // 不带参数时列出「与我持有道具相关的配方」——
    // 此前只打印用法，玩家无从知道仓库里哪些东西能合成，
    // 120 条配方等于不可发现。
    if (args.posCount() < 2) {
        out(style("═══ 合成配方 ═══", Style::Heading));
        const auto& items = env.st.inventory.items;
        auto owns = [&](u16 id) {
            for (const auto& it : items)
                if (it.def == id && it.count > 0) return true;
            return false;
        };
        int shown = 0, ready = 0;
        TextTable t;
        t.header({"配方", "材料 A", "材料 B", "产物", "可合成"});
        for (int i = 0; i < kRecipeCount; ++i) {
            const RecipeInfo& r = recipeInfo(i);
            bool haveA = owns(r.inputs[0]);
            bool haveB = owns(r.inputs[1]);
            // 只显示与持有道具相关的配方（或玩家一件都没有时显示全部）
            bool relevant = items.empty() || haveA || haveB;
            if (!relevant) continue;
            std::string mark = (haveA && haveB) ? style("✓ 可以合成", Style::Good)
                                                : (haveA || haveB ? "缺 1 种" : "未持有");
            if (haveA && haveB) ++ready;
            t.row({std::string(r.idName),
                   std::string(itemDef(r.inputs[0]).nameZh) + (haveA ? " ✓" : ""),
                   std::string(itemDef(r.inputs[1]).nameZh) + (haveB ? " ✓" : ""),
                   std::string(itemDef(r.output).nameZh), mark});
            if (++shown >= 20) break;
        }
        if (shown == 0) {
            out("  （仓库中没有与任何配方相关的道具）");
        } else {
            out(t.render());
            out("  匹配到 " + std::to_string(shown) + " 条配方，其中 " + std::to_string(ready) +
                " 条当前即可合成（✓ 表示已持有）。");
        }
        out("");
        out("用法：greyfall combine <A> <B>");
        return 0;
    }
    int a = requireItem(env.st, args, 0, "greyfall combine <A> <B>");
    int b = requireItem(env.st, args, 1, "greyfall combine <A> <B>");
    if (env.player().apLeft < apcost::kCombine) fail(ExitCode::IllegalAction, "行动点不足");
    u16 outItem = 0;
    std::string err;
    bool ok = combineItems(env.st, static_cast<u16>(a), static_cast<u16>(b), &outItem, &err);
    env.commit("combine");
    if (!ok) {
        out(style("合成失败：" + err, Style::Bad));
        return static_cast<int>(ExitCode::NoFill);
    }
    out(style("合成成功 → 【" + std::string(itemDef(outItem).nameZh) + "】", Style::Good));
    return 0;
}

int cmdDisassemble(CliEnv& env, const Args& args) {
    env.loadState();
    int item = requireItem(env.st, args, 0, "greyfall disassemble <item>");
    std::string err;
    if (!disassembleItem(env.st, static_cast<u16>(item), &err)) fail(ExitCode::IllegalAction, err);
    env.commit("disassemble");
    out("已拆解【" + std::string(itemDef(item).nameZh) + "】，材料已回收（有损耗）。");
    return 0;
}

int cmdUse(CliEnv& env, const Args& args) {
    env.loadState();
    int item = requireItem(env.st, args, 0, "greyfall use <item> [--target X]");
    u32 target = static_cast<u32>(args.getInt("target", 0xFFFFFFFFu));
    std::string err;
    if (!useItem(env.st, static_cast<u16>(item), target, &err)) fail(ExitCode::IllegalAction, err);
    env.commit("use");
    const ItemDef& d = itemDef(item);
    out("已使用【" + std::string(d.nameZh) + "】：" + std::string(itemEffectName(d.effect)));
    if (d.effect == ItemEffect::MarketFakeStock) {
        out("  说明：真实订单簿未被改变，但 AI 的基本面模型已被污染。");
    } else if (d.effect == ItemEffect::BeliefNoise) {
        out("  说明：AI 的 modelConfidence 下降 —— 它会更谨慎、更少勒索。");
    }
    return 0;
}

int cmdForgeProve(CliEnv& env, const Args& args) {
    env.loadState();
    int item = requireItem(env.st, args, 0, "greyfall forge-prove <item>");
    if (env.player().apLeft < apcost::kForgeProve) fail(ExitCode::IllegalAction, "行动点不足");
    std::string err;
    if (!forgeProveItem(env.st, static_cast<u16>(item), &err)) fail(ExitCode::IllegalAction, err);
    env.player().apLeft -= apcost::kForgeProve;
    env.commit("forge-prove");
    out(style("已造假冒牌：【" + std::string(itemDef(item).nameZh) + "】", Style::Warn));
    out("  效果：全体 AI 的 ToModel 被污染，modelConfidence 下降。");
    out("  风险：FraudDetect 的一致性异常检测会捕获到无法解释的库存变化，");
    out("        被识破后你会被标记为剥削型，且监管罚没概率上升。");
    return 0;
}

int cmdEquip(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "用法：greyfall equip <module> <fleet>");
    int modIdx = moduleIndexByName(args.pos(0));
    if (modIdx < 0) {
        i64 num = parseInt(args.pos(0), -1);
        if (num >= 0 && num < kModuleCount) modIdx = static_cast<int>(num);
        else fail(ExitCode::BadArgs, "未知模块【" + args.pos(0) + "】");
    }
    i64 fleetId = parseInt(args.pos(1), -1);
    Fleet* f = env.st.fleet(static_cast<u32>(fleetId));
    if (f == nullptr) fail(ExitCode::BadArgs, "舰队不存在");
    if (f->owner != kPlayerId) fail(ExitCode::IllegalAction, "只能装备自己的舰队");
    FleetDesign* d = nullptr;
    for (auto& design : env.player().designs)
        if (design.id == f->design) d = &design;
    if (d == nullptr) fail(ExitCode::Internal, "舰队没有关联的设计");
    const ModuleInfo& mi = moduleInfo(modIdx);
    if (static_cast<int>(d->hull) < static_cast<int>(mi.minHull)) {
        fail(ExitCode::IllegalAction, "船体等级不足：需要 " + std::string(hullClassName(mi.minHull)));
    }
    if (static_cast<int>(d->modules.size()) >= hullInfo(d->hull).slots) {
        fail(ExitCode::IllegalAction, "槽位已满（该船体只有 " + std::to_string(hullInfo(d->hull).slots) + " 个槽）");
    }
    i64 cost = mi.buildCost[static_cast<std::size_t>(Commodity::Alloys)].rawValue() * 5;
    if (env.player().stock[static_cast<std::size_t>(Commodity::Alloys)].rawValue() < cost) {
        fail(ExitCode::IllegalAction, "合金不足（需要 " + std::to_string(cost / FIX) + "）");
    }
    env.player().stock[static_cast<std::size_t>(Commodity::Alloys)] -= Fixed(cost / FIX);
    d->modules.push_back(static_cast<u8>(modIdx));
    switch (mi.effect) {
        case ModuleEffect::Firepower: d->firepower += mi.effectValue; break;
        case ModuleEffect::Defense: d->defense += mi.effectValue; break;
        case ModuleEffect::Speed: d->speed += mi.effectValue; break;
        case ModuleEffect::Supply: d->supplyUse += mi.effectValue; break;
        default: break;
    }
    env.st.logEvent(LogPhase::Economy, "fleet.equip",
                    std::string("为 ") + f->name + " 装备【" + std::string(mi.nameZh) + "】", kPlayerId);
    env.commit("equip");
    out("已为 " + f->name + " 装备【" + std::string(mi.nameZh) + "】（" + std::string(moduleSlotName(mi.slot)) +
        " 槽）");
    out("  载体绑定效果：" + std::string(itemEffectName(ItemEffect::CombatBonus)) + " 与运输滑点已更新。");
    return 0;
}

}  // namespace gf
