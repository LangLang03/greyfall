#include "items/ItemDef.h"

#include <array>
#include <string>
#include <vector>

#include "rng/SplitMix.h"
#include "util/Str.h"

namespace gf {
namespace {

// 每个 tag 一组具象名词，组合出 130 件"机制筹码"
constexpr const char* kNouns[][6] = {
    {"托管契约", "航线特许", "停战担保", "雇佣文书", "血誓契", "长期供货契"},          // Contract
    {"观测密钥", "深空听筒", "泛视碎片", "遥测透镜", "监视浮标", "静默观测台"},          // Observation
    {"伪造印信", "影印档案", "伪证词", "假订单簿", "伪造舰籍", "影子合约"},              // Forgery
    {"审计令", "账目透视仪", "溯源探针", "合规扫描器", "审计徽记", "对账矩阵"},          // Audit
    {"心灵屏障", "静默场", "记忆屏蔽环", "思维防火墙", "灵能绝缘层", "空白信标"},        // Barrier
    {"假库存凭证", "仓单幻影", "囤积许可", "做市授权", "结算延宕令", "保证金银契"},      // Market
    {"信念噪声器", "梯度污染源", "认知棱镜", "怀疑种子", "心智迷雾", "先验扰动器"},      // Belief
    {"档案钥匙", "记忆晶片", "死者手记", "航线日志", "黑匣残片", "译码石板"},            // Clue
    {"载体挂架", "舰载模块座", "运输舱", "护航浮台", "母舰接口", "货柜组"},              // Carrier
    {"汇票", "信用证", "债券凭证", "托管票据", "期权合约", "对冲凭证"},                  // Currency
    {"战地医疗包", "延寿血清", "免疫贴片", "神经修复剂", "基因补丁", "镇静雾剂"},        // Medical
    {"相位雷", "轨道炮台", "护航无人机", "要塞组件", "轨道雷幕", "登陆舱"},              // Military
    {"数据核心", "加密卷宗", "统计模型", "预测引擎", "语料仓库", "索引节点"},            // Data
    {"先民遗物", "沉默之瞳", "纪元碑", "空壳神像", "断代之石", "幽蓝结晶"},              // Relic
    {"走私舱", "匿踪涂层", "黑市通行证", "洗单终端", "违禁样本", "暗港坐标"},            // Contraband
};

constexpr const char* kPrefix[] = {"", "精制", "军规", "黑市", "古代", "实验型", "量产", "残缺"};
constexpr const char* kSuffix[] = {"", "·甲型", "·乙型", "·丙型", "·改", "·II", "·III", "·原型"};

constexpr ItemEffect kEffectForTag(int tag) {
    switch (static_cast<ItemTag>(tag)) {
        case ItemTag::Contract: return ItemEffect::NegotiationDiscount;
        case ItemTag::Observation: return ItemEffect::IntelGain;
        case ItemTag::Forgery: return ItemEffect::MarketFakeStock;
        case ItemTag::Audit: return ItemEffect::IntelDefense;
        case ItemTag::Barrier: return ItemEffect::BeliefNoise;
        case ItemTag::Market: return ItemEffect::MarketFakeStock;
        case ItemTag::Belief: return ItemEffect::BeliefNoise;
        case ItemTag::Clue: return ItemEffect::ClueYield;
        case ItemTag::Carrier: return ItemEffect::CombatBonus;
        case ItemTag::Currency: return ItemEffect::MarginRelief;
        case ItemTag::Medical: return ItemEffect::StabilityBoost;
        case ItemTag::Military: return ItemEffect::CombatBonus;
        case ItemTag::Data: return ItemEffect::ResearchBoost;
        case ItemTag::Relic: return ItemEffect::DiplomacyWeight;
        case ItemTag::Contraband: return ItemEffect::TradeMargin;
        default: return ItemEffect::None;
    }
}

bool skipVariant(int slot, int p) {
    // 确定性稀疏化：p>0 的变体约 58% 被跳过，保证两处生成器索引一致
    if (p == 0) return false;
    return ((slot * 37 + 11) % 100) > 42;
}

std::vector<ItemDef> buildItems() {
    std::vector<ItemDef> v;
    v.reserve(kItemCount);
    int idx = 0;
    for (int tag = 0; tag < kItemTagCount && idx < kItemCount; ++tag) {
        for (int n = 0; n < 6 && idx < kItemCount; ++n) {
            for (int p = 0; p < 8 && idx < kItemCount; ++p) {
                int slot = (tag * 6 + n) * 8 + p;
                if (skipVariant(slot, p)) continue;
                ItemDef d;
                d.id = static_cast<u16>(idx);
                d.tags[0] = static_cast<ItemTag>(tag);
                d.tagCount = 1;
                if (idx % 3 == 0 && tag + 1 < kItemTagCount) {
                    d.tags[1] = static_cast<ItemTag>((tag + 1 + (idx % 5)) % kItemTagCount);
                    d.tagCount = 2;
                }
                d.tier = static_cast<u8>(1 + (idx % 5));
                d.baseCost = 800 + (idx * 137) % 42000;
                d.effect = kEffectForTag(tag);
                d.effectValue = Fixed::pct(5 + static_cast<i64>(idx % 25));
                d.consumable = (idx % 4 == 0);
                d.tradeable = (static_cast<ItemTag>(tag) != ItemTag::Relic);
                v.push_back(d);
                ++idx;
            }
        }
    }
    while (static_cast<int>(v.size()) < kItemCount) {
        ItemDef d;
        d.id = static_cast<u16>(v.size());
        d.tags[0] = static_cast<ItemTag>(v.size() % kItemTagCount);
        d.tagCount = 1;
        d.tier = static_cast<u8>(1 + (v.size() % 5));
        d.baseCost = 1200 + static_cast<i64>(v.size()) * 91;
        d.effect = kEffectForTag(static_cast<int>(d.tags[0]));
        d.effectValue = Fixed::pct(8);
        v.push_back(d);
    }
    return v;
}

// 持久化的 idName / desc 字符串池
const std::vector<std::string>& itemIdStrings() {
    static const std::vector<std::string> s = [] {
        std::vector<std::string> out;
        out.reserve(kItemCount * 2);
        for (int idx = 0; idx < kItemCount; ++idx) out.push_back("itm" + std::to_string(idx));
        for (int idx = 0; idx < kItemCount; ++idx)
            out.push_back("机制筹码 #" + std::to_string(idx) + "：可用于外交折价、市场干预或信念干预。");
        return out;
    }();
    return s;
}

const std::vector<std::string>& itemNamesZh() {
    static const std::vector<std::string> s = [] {
        std::vector<std::string> out;
        out.reserve(kItemCount);
        int idx = 0;
        for (int tag = 0; tag < kItemTagCount && idx < kItemCount; ++tag) {
            for (int n = 0; n < 6 && idx < kItemCount; ++n) {
                for (int p = 0; p < 8 && idx < kItemCount; ++p) {
                    int slot = (tag * 6 + n) * 8 + p;
                    if (skipVariant(slot, p)) continue;
                    out.push_back(std::string(kPrefix[p]) + kNouns[tag][n] + kSuffix[(slot * 13 + 5) % 8]);
                    ++idx;
                }
            }
        }
        while (static_cast<int>(out.size()) < kItemCount) out.push_back("未命名筹码 " + std::to_string(out.size()));
        return out;
    }();
    return s;
}

std::vector<RecipeInfo> buildRecipes() {
    std::vector<RecipeInfo> v;
    v.reserve(kRecipeCount);
    SplitMix64 rng(0xC0FFEE17ull);
    for (int i = 0; i < kRecipeCount; ++i) {
        RecipeInfo r;
        r.id = static_cast<u16>(i);
        u16 a = static_cast<u16>(rng.nextU32() % 40);
        u16 b = static_cast<u16>(40 + rng.nextU32() % 60);
        u16 out = static_cast<u16>(60 + rng.nextU32() % (kItemCount - 60));
        r.inputs = {a, b};
        r.output = out;
        r.outputCount = 1;
        r.depth = static_cast<u8>(1 + (i % 4));
        r.baseFailRate = Fixed::pct(5 + (i % 25));
        v.push_back(std::move(r));
    }
    // idName 持久化
    static std::vector<std::string> names;
    names.clear();
    for (int i = 0; i < kRecipeCount; ++i) names.push_back("rcp" + std::to_string(i));
    for (int i = 0; i < kRecipeCount; ++i) v[static_cast<std::size_t>(i)].idName = names[static_cast<std::size_t>(i)];
    return v;
}

std::vector<SynergyInfo> buildSynergies() {
    std::vector<SynergyInfo> v;
    v.reserve(kSynergyCount);
    int n = 0;
    for (int a = 0; a < kItemTagCount && n < kSynergyCount; ++a) {
        for (int b = a + 1; b < kItemTagCount && n < kSynergyCount; ++b) {
            if ((a * 7 + b * 3) % 5 != 0) continue;  // 取约半数组合
            SynergyInfo s;
            s.id = static_cast<u16>(n);
            s.tagA = static_cast<ItemTag>(a);
            s.tagB = static_cast<ItemTag>(b);
            s.effect = (n % 4 == 0) ? ItemEffect::NegotiationDiscount
                                    : (n % 4 == 1 ? ItemEffect::IntelGain
                                                  : (n % 4 == 2 ? ItemEffect::MarginRelief
                                                                : ItemEffect::DiplomacyWeight));
            s.value = Fixed::pct(5 + (n % 20));
            v.push_back(s);
            ++n;
        }
    }
    while (static_cast<int>(v.size()) < kSynergyCount) {
        SynergyInfo s;
        s.id = static_cast<u16>(v.size());
        s.tagA = static_cast<ItemTag>(v.size() % kItemTagCount);
        s.tagB = static_cast<ItemTag>((v.size() + 3) % kItemTagCount);
        s.effect = ItemEffect::TradeMargin;
        s.value = Fixed::pct(6);
        v.push_back(s);
    }
    static std::vector<std::string> names;
    static std::vector<std::string> descs;
    names.clear();
    descs.clear();
    for (int i = 0; i < kSynergyCount; ++i) {
        names.push_back("syn" + std::to_string(i));
        descs.push_back(std::string(itemTagName(v[static_cast<std::size_t>(i)].tagA)) + " + " +
                        std::string(itemTagName(v[static_cast<std::size_t>(i)].tagB)) + " 同持生效");
    }
    for (int i = 0; i < kSynergyCount; ++i) {
        v[static_cast<std::size_t>(i)].idName = names[static_cast<std::size_t>(i)];
        v[static_cast<std::size_t>(i)].desc = descs[static_cast<std::size_t>(i)];
    }
    return v;
}

std::vector<ConflictInfo> buildConflicts() {
    std::vector<ConflictInfo> v;
    v.reserve(kConflictCount);
    const ItemTag pairs[][2] = {
        {ItemTag::Forgery, ItemTag::Audit}, {ItemTag::Barrier, ItemTag::Belief}, {ItemTag::Contract, ItemTag::Contraband},
        {ItemTag::Market, ItemTag::Audit},  {ItemTag::Clue, ItemTag::Forgery},   {ItemTag::Military, ItemTag::Currency},
        {ItemTag::Data, ItemTag::Forgery},  {ItemTag::Relic, ItemTag::Contraband}, {ItemTag::Medical, ItemTag::Military},
    };
    for (int i = 0; i < kConflictCount; ++i) {
        const auto& p = pairs[i % 9];
        ConflictInfo c;
        c.id = static_cast<u16>(i);
        c.tagA = p[0];
        c.tagB = p[1];
        c.penalty = Fixed::pct(50 + (i % 100));
        v.push_back(c);
    }
    static std::vector<std::string> descs;
    descs.clear();
    for (int i = 0; i < kConflictCount; ++i)
        descs.push_back(std::string(itemTagName(v[static_cast<std::size_t>(i)].tagA)) + " 与 " +
                        std::string(itemTagName(v[static_cast<std::size_t>(i)].tagB)) + " 相克：" +
                        std::string(itemTagName(v[static_cast<std::size_t>(i)].tagA)) + " 的收益被削弱");
    for (int i = 0; i < kConflictCount; ++i) v[static_cast<std::size_t>(i)].desc = descs[static_cast<std::size_t>(i)];
    return v;
}

std::vector<GateInfo> buildGates() {
    std::vector<GateInfo> v;
    v.reserve(kGateCount);
    const char* actions[] = {"spy.psionic", "spy.read-mind", "breach", "market.intervene", "belief.inject",
                             "clue.forge",   "fleet.cloak",     "trade.black", "envoy.vassalize", "mega.build"};
    const ItemTag req[] = {ItemTag::Barrier, ItemTag::Barrier, ItemTag::Audit, ItemTag::Market, ItemTag::Belief,
                           ItemTag::Forgery, ItemTag::Carrier, ItemTag::Contraband, ItemTag::Contract, ItemTag::Currency};
    static std::vector<std::string> acts;
    static std::vector<std::string> descs;
    acts.clear();
    descs.clear();
    for (int i = 0; i < kGateCount; ++i) {
        GateInfo g;
        g.id = static_cast<u16>(i);
        g.required = req[i % 10];
        acts.push_back(actions[i % 10]);
        g.action = acts.back();
        descs.push_back(std::string("缺少【") + std::string(itemTagName(g.required)) + "】类道具时，" +
                        std::string(actions[i % 10]) + " 不可执行");
        v.push_back(g);
    }
    for (int i = 0; i < kGateCount; ++i) v[static_cast<std::size_t>(i)].desc = descs[static_cast<std::size_t>(i)];
    return v;
}

std::vector<ClueBridgeInfo> buildBridges() {
    std::vector<ClueBridgeInfo> v;
    v.reserve(kClueBridgeCount);
    for (int i = 0; i < kClueBridgeCount; ++i) {
        ClueBridgeInfo b;
        b.id = static_cast<u16>(i);
        b.item = static_cast<u16>((i * 7) % kItemCount);
        b.clue = static_cast<u16>((i * 13) % 340);
        b.credibilityStart = Fixed::pct(45 + (i % 40));
        v.push_back(b);
    }
    static std::vector<std::string> descs;
    descs.clear();
    for (int i = 0; i < kClueBridgeCount; ++i)
        descs.push_back("使用后产出线索 #" + std::to_string(v[static_cast<std::size_t>(i)].clue));
    for (int i = 0; i < kClueBridgeCount; ++i) v[static_cast<std::size_t>(i)].desc = descs[static_cast<std::size_t>(i)];
    return v;
}

}  // namespace

const ItemDef& itemDef(int idx) {
    static const std::vector<ItemDef> table = [] {
        std::vector<ItemDef> t = buildItems();
        const std::vector<std::string>& ids = itemIdStrings();
        const std::vector<std::string>& names = itemNamesZh();
        for (int i = 0; i < kItemCount; ++i) {
            t[static_cast<std::size_t>(i)].idName = ids[static_cast<std::size_t>(i)];
            t[static_cast<std::size_t>(i)].nameZh = names[static_cast<std::size_t>(i)];
            t[static_cast<std::size_t>(i)].desc = ids[static_cast<std::size_t>(kItemCount + i)];
        }
        return t;
    }();
    if (idx < 0 || idx >= kItemCount) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

int itemIndexByName(std::string_view s) {
    for (int i = 0; i < kItemCount; ++i) {
        if (iequals(itemDef(i).idName, s) || itemDef(i).nameZh == s) return i;
    }
    return -1;
}

std::string_view itemTagName(ItemTag t) {
    switch (t) {
        case ItemTag::Contract: return "契约";
        case ItemTag::Observation: return "观测";
        case ItemTag::Forgery: return "伪造";
        case ItemTag::Audit: return "审计";
        case ItemTag::Barrier: return "屏障";
        case ItemTag::Market: return "市场干预";
        case ItemTag::Belief: return "信念干预";
        case ItemTag::Clue: return "线索桥";
        case ItemTag::Carrier: return "载体";
        case ItemTag::Currency: return "金融";
        case ItemTag::Medical: return "医疗";
        case ItemTag::Military: return "军用";
        case ItemTag::Data: return "数据";
        case ItemTag::Relic: return "遗物";
        case ItemTag::Contraband: return "违禁";
        case ItemTag::Count: break;
    }
    return "?";
}

ItemTag itemTagFromName(std::string_view s) {
    for (int i = 0; i < kItemTagCount; ++i)
        if (itemTagName(static_cast<ItemTag>(i)) == s) return static_cast<ItemTag>(i);
    for (int i = 0; i < kItemTagCount; ++i)
        if (iequals(itemTagName(static_cast<ItemTag>(i)), s)) return static_cast<ItemTag>(i);
    return ItemTag::Contract;
}

std::string_view itemEffectName(ItemEffect e) {
    switch (e) {
        case ItemEffect::None: return "无";
        case ItemEffect::NegotiationDiscount: return "谈判折价";
        case ItemEffect::TradeMargin: return "贸易毛利";
        case ItemEffect::IntelGain: return "情报获取";
        case ItemEffect::IntelDefense: return "反间谍";
        case ItemEffect::DiplomacyWeight: return "外交权重";
        case ItemEffect::CombatBonus: return "战力系数";
        case ItemEffect::TransportSlippage: return "运输滑点";
        case ItemEffect::MarketFakeStock: return "假库存";
        case ItemEffect::BeliefNoise: return "信念噪声";
        case ItemEffect::ClueYield: return "线索产出";
        case ItemEffect::ResearchBoost: return "研究加速";
        case ItemEffect::StabilityBoost: return "稳定加成";
        case ItemEffect::MarginRelief: return "保证金减免";
        case ItemEffect::CreditBoost: return "信用加成";
        case ItemEffect::Count: break;
    }
    return "?";
}

const RecipeInfo& recipeInfo(int idx) {
    static const std::vector<RecipeInfo> table = buildRecipes();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}
const SynergyInfo& synergyInfo(int idx) {
    static const std::vector<SynergyInfo> table = buildSynergies();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}
const ConflictInfo& conflictInfo(int idx) {
    static const std::vector<ConflictInfo> table = buildConflicts();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}
const GateInfo& gateInfo(int idx) {
    static const std::vector<GateInfo> table = buildGates();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}
const ClueBridgeInfo& clueBridgeInfo(int idx) {
    static const std::vector<ClueBridgeInfo> table = buildBridges();
    if (idx < 0 || idx >= static_cast<int>(table.size())) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

bool itemHasTag(const ItemDef& d, ItemTag t) noexcept {
    for (u8 i = 0; i < d.tagCount && i < kMaxItemTags; ++i)
        if (d.tags[i] == t) return true;
    return false;
}

}  // namespace gf
