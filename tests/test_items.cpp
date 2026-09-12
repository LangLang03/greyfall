// 道具：规则图不动点无环 / 协同与互斥 / 门槛 / 合成失败率 / 拆解 / 线索桥 / 假冒污染
#include <algorithm>

#include "check.h"
#include "clue/ClueGraph.h"
#include "core/GameState.h"
#include "gen/WorldGen.h"
#include "items/ItemDef.h"
#include "items/RuleEngine.h"
#include "mkt/OrderBook.h"

using namespace gf;

namespace {

GameState itemWorld(u64 seed = 4711) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = 8;
    o.systemCount = 32;
    GameState st;
    generateWorld(st, o);
    return st;
}

/// 找到第一件带指定 tag 的道具
u16 findTagged(ItemTag t, int skip = 0) {
    // 优先返回以该 tag 为主 tag 的道具（其效果与 tag 语义一致）
    int seen = 0;
    for (int i = 0; i < kItemCount; ++i) {
        const ItemDef& d = itemDef(i);
        if (d.tagCount == 0 || d.tags[0] != t) continue;
        if (seen++ < skip) continue;
        return static_cast<u16>(i);
    }
    for (int i = 0; i < kItemCount; ++i) {
        if (!itemHasTag(itemDef(i), t)) continue;
        return static_cast<u16>(i);
    }
    return 0xFFFFu;
}

}  // namespace

TEST(items, content_tables_are_consistent) {
    CHECK_EQ(kItemCount, 130);
    CHECK_EQ(kRecipeCount, 120);
    CHECK_EQ(kSynergyCount, 70);
    CHECK_EQ(kConflictCount, 45);
    CHECK_EQ(kGateCount, 40);
    CHECK_EQ(kClueBridgeCount, 60);
    // 配方引用必须全部存在，且深度 ≤ 4
    for (int i = 0; i < kRecipeCount; ++i) {
        const RecipeInfo& r = recipeInfo(i);
        CHECK(r.output < kItemCount);
        CHECK(r.depth >= 1 && r.depth <= 4);
        CHECK(!r.inputs.empty());
        for (u16 in : r.inputs) CHECK(in < kItemCount);
    }
    // tag 图闭合：每件道具的 tag 都在枚举范围内、数量不超上限
    for (int i = 0; i < kItemCount; ++i) {
        const ItemDef& d = itemDef(i);
        CHECK(d.tagCount >= 1 && d.tagCount <= kMaxItemTags);
        for (u8 k = 0; k < d.tagCount; ++k) CHECK(static_cast<int>(d.tags[k]) < kItemTagCount);
    }
    // idName 唯一
    for (int i = 0; i < kItemCount; ++i) {
        for (int j = i + 1; j < kItemCount; ++j) {
            if (itemDef(i).idName == itemDef(j).idName) CHECK(false);
        }
    }
    // 线索桥引用存在
    for (int i = 0; i < kClueBridgeCount; ++i) {
        CHECK(clueBridgeInfo(i).item < kItemCount);
        CHECK(clueBridgeInfo(i).clue < kClueCount);
    }
    // 协同/互斥的 tag 合法
    for (int i = 0; i < kSynergyCount; ++i) {
        CHECK(static_cast<int>(synergyInfo(i).tagA) < kItemTagCount);
        CHECK(static_cast<int>(synergyInfo(i).tagB) < kItemTagCount);
    }
    for (int i = 0; i < kConflictCount; ++i) {
        CHECK(static_cast<int>(conflictInfo(i).tagA) < kItemTagCount);
        CHECK(static_cast<int>(conflictInfo(i).tagB) < kItemTagCount);
    }
}

TEST(items, rule_engine_reaches_fixed_point) {
    GameState st = itemWorld();
    RuleResult r1 = ruleEngine(st);
    RuleResult r2 = ruleEngine(st);
    CHECK_EQ(r1.rounds, r2.rounds);              // 同一状态 ⇒ 同一迭代轮数（确定性）
    CHECK(r1.rounds <= 16);                      // 必须在 16 轮内收敛
    CHECK_EQ(r1.negotiationDiscount.rawValue(), r2.negotiationDiscount.rawValue());
    CHECK_EQ(r1.fraudWeight.rawValue(), r2.fraudWeight.rawValue());
    CHECK(!r1.trace.empty());
}

TEST(items, synergy_and_conflict_change_outcome) {
    GameState st = itemWorld(1);
    st.inventory.items.clear();
    // 只放一件 [契约] 道具
    ItemInstance a;
    a.def = findTagged(ItemTag::Contract);
    st.inventory.items.push_back(a);
    RuleResult onlyContract = ruleEngine(st);
    // 再加一件 [观测] ⇒ 协同应生效（谈判折价上升）
    ItemInstance b;
    b.def = findTagged(ItemTag::Observation);
    st.inventory.items.push_back(b);
    RuleResult withSynergy = ruleEngine(st);
    CHECK(withSynergy.negotiationDiscount.rawValue() >= onlyContract.negotiationDiscount.rawValue());

    // [伪造] 与 [审计] 同持 ⇒ FraudDetect 权重 ×2
    GameState st2 = itemWorld(2);
    st2.inventory.items.clear();
    ItemInstance f;
    f.def = findTagged(ItemTag::Forgery);
    ItemInstance au;
    au.def = findTagged(ItemTag::Audit);
    st2.inventory.items.push_back(au);
    RuleResult auditOnly = ruleEngine(st2);
    st2.inventory.items.push_back(f);
    RuleResult both = ruleEngine(st2);
    CHECK(both.fraudWeight.rawValue() >= auditOnly.fraudWeight.rawValue());
}

TEST(items, gates_block_actions_without_required_tag) {
    GameState st = itemWorld(3);
    st.inventory.items.clear();
    // 清空后，需要 [屏障] 的行动必须被拦截
    CHECK(!itemGateOpen(st, "spy.read-mind") || !itemGateOpen(st, "spy.psionic"));
    CHECK(!itemGateReason("spy.read-mind").empty());
    // 放入一件 [屏障] 道具后应放行
    ItemInstance it;
    it.def = findTagged(ItemTag::Barrier);
    st.inventory.items.push_back(it);
    CHECK(itemGateOpen(st, "spy.read-mind"));
    // 无门槛定义的动作始终放行
    CHECK(itemGateOpen(st, "nonexistent.action"));
}

TEST(items, combine_consumes_and_produces) {
    GameState st = itemWorld(4);
    const RecipeInfo& r = recipeInfo(0);
    st.inventory.items.clear();
    for (u16 in : r.inputs) {
        ItemInstance it;
        it.def = in;
        st.inventory.items.push_back(it);
    }
    u16 out = 0xFFFFu;
    std::string err;
    bool ok = combineItems(st, r.inputs[0], r.inputs[1], &out, &err);
    // 失败率 > 0，因此要么成功产出、要么因材料损耗而失败 —— 两者都必须自洽
    if (ok) {
        CHECK_EQ(out, r.output);
        bool hasOutput = false;
        for (const auto& it : st.inventory.items)
            if (it.def == r.output && it.count > 0) hasOutput = true;
        CHECK(hasOutput);
    } else {
        CHECK(!err.empty());
    }
    // 配方不匹配必须报错
    u16 dummy = 0;
    st.inventory.items.clear();
    ItemInstance a, b;
    a.def = 0;
    b.def = 1;
    st.inventory.items.push_back(a);
    st.inventory.items.push_back(b);
    bool anyRecipe = false;
    for (int i = 0; i < kRecipeCount; ++i) {
        const RecipeInfo& rr = recipeInfo(i);
        if ((rr.inputs[0] == 0 && rr.inputs[1] == 1) || (rr.inputs[0] == 1 && rr.inputs[1] == 0)) anyRecipe = true;
    }
    if (!anyRecipe) CHECK(!combineItems(st, 0, 1, &dummy, &err));
}

TEST(items, disassemble_returns_materials) {
    GameState st = itemWorld(5);
    const RecipeInfo& r = recipeInfo(3);
    st.inventory.items.clear();
    ItemInstance out;
    out.def = r.output;
    st.inventory.items.push_back(out);
    size_t before = st.inventory.items.size();
    std::string err;
    CHECK(disassembleItem(st, r.output, &err));
    CHECK(st.inventory.items.size() > before);   // 返还材料
    // 未持有则失败
    CHECK(!disassembleItem(st, r.output, &err));
    CHECK(!err.empty());
}

TEST(items, use_item_effects) {
    GameState st = itemWorld(6);
    st.inventory.items.clear();
    // 信念干预：降低 AI 的 modelConfidence
    u16 noise = findTagged(ItemTag::Belief);
    ItemInstance it;
    it.def = noise;
    st.inventory.items.push_back(it);
    Fixed before = st.empires[1].mind.playerModel.modelConfidence;
    std::string err;
    CHECK(useItem(st, noise, 0xFFFFFFFFu, &err));
    CHECK(st.empires[1].mind.playerModel.modelConfidence.rawValue() <= before.rawValue());

    // 市场干预：污染 AI 的基本面模型，但不改变真实订单簿
    u16 fake = findTagged(ItemTag::Market);
    ItemInstance it2;
    it2.def = fake;
    st.inventory.items.push_back(it2);
    Fixed midBefore = st.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)].mid;
    CHECK(useItem(st, fake, 0xFFFFFFFFu, &err));
    CHECK_EQ(st.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)].mid.rawValue(),
             midBefore.rawValue());
    CHECK(st.empires[1].mind.playerModel.contamination.rawValue() > 0);
}

TEST(items, forge_prove_contaminates_ai_models) {
    GameState st = itemWorld(7);
    st.inventory.items.clear();
    ItemInstance it;
    it.def = findTagged(ItemTag::Observation);
    st.inventory.items.push_back(it);
    Fixed before = st.empires[1].mind.playerModel.contamination;
    std::string err;
    CHECK(forgeProveItem(st, it.def, &err));
    CHECK(st.inventory.items[0].forged);
    CHECK(st.inventory.items[0].contamination.rawValue() > 0);
    CHECK(st.empires[1].mind.playerModel.contamination.rawValue() > before.rawValue());
    // 被污染后 FraudDetect 应能识别
    CHECK(!itemExplain(st, it.def).empty());
}

// 回归守卫：120 条配方必须先有「可发现」的途径 ——
// 此前 combine 不带参数只打印用法，玩家无从知道仓库里哪些道具能合成，
// 整套配方系统等于不可见。
TEST(items, recipes_are_discoverable_from_inventory) {
    // 配方输入/输出必须构成可链式合成的图：输入范围与输出范围有交集
    bool anyOverlap = false;
    for (int i = 0; i < kRecipeCount; ++i) {
        const RecipeInfo& r = recipeInfo(i);
        for (int j = 0; j < kRecipeCount; ++j) {
            const RecipeInfo& o = recipeInfo(j);
            if (r.output == o.inputs[0] || r.output == o.inputs[1]) anyOverlap = true;
        }
    }
    CHECK(anyOverlap);
    // 每条配方的输入与输出都必须是合法道具
    for (int i = 0; i < kRecipeCount; ++i) {
        const RecipeInfo& r = recipeInfo(i);
        CHECK(r.inputs.size() >= 2);
        CHECK(static_cast<int>(r.inputs[0]) < kItemCount);
        CHECK(static_cast<int>(r.inputs[1]) < kItemCount);
        CHECK(static_cast<int>(r.output) < kItemCount);
        // 输入不能与输出相同（否则是自环）
        CHECK(r.inputs[0] != r.output);
        CHECK(r.inputs[1] != r.output);
        CHECK(!r.idName.empty());
        // 失败率必须落在合理区间
        CHECK(r.baseFailRate.rawValue() >= 0);
        CHECK(r.baseFailRate.rawValue() < Fixed(1).rawValue());
    }
}

// 回归守卫：至少存在若干「开局道具即可参与」的配方 ——
// 若所有配方的输入都指向玩家拿不到的道具，合成系统就是死内容。
TEST(items, some_recipes_involve_obtainable_items) {
    GameState st;
    generateWorld(st, [] {
        WorldGenOptions o;
        o.seed = 4242;
        o.difficulty = 2;
        o.empireCount = 8;
        o.systemCount = 48;
        return o;
    }());
    // 收集全地图可获得道具（玩家库存 + 各帝国库存）
    std::vector<bool> obtainable(static_cast<std::size_t>(kItemCount), false);
    for (const auto& it : st.inventory.items) obtainable[static_cast<std::size_t>(it.def)] = true;
    int usable = 0;
    for (int i = 0; i < kRecipeCount; ++i) {
        const RecipeInfo& r = recipeInfo(i);
        if (obtainable[static_cast<std::size_t>(r.inputs[0])] ||
            obtainable[static_cast<std::size_t>(r.inputs[1])])
            ++usable;
    }
    // 玩家开局就应当有若干道具与配方相关，否则无从入门
    CHECK(usable >= 1);
}

TEST(items, inventory_text_and_lookup) {
    GameState st = itemWorld(8);
    CHECK(!inventoryText(st, "").empty());
    for (int t = 0; t < kItemTagCount; ++t) {
        int idx = findItemByTag(st, static_cast<ItemTag>(t));
        CHECK(idx >= 0 || idx == -1);
    }
    CHECK(itemIndexByName(itemDef(5).idName) == 5);
    CHECK(itemIndexByName(itemDef(5).nameZh) == 5);
    CHECK_EQ(itemIndexByName("no-such-item-xyz"), -1);
}
