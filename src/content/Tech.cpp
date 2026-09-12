#include "domain/Tech.h"

#include <array>
#include <string>

#include "util/Str.h"

namespace gf {
namespace {

// 96 项科技：6 分支 × 16 层。id 名形如 phys1..phys16（静态存储，供 string_view 引用）
const std::array<std::string, kTechCount>& techIds() {
    static const std::array<std::string, kTechCount> ids = [] {
        std::array<std::string, kTechCount> a;
        const char* pre[kTechBranchCount] = {"phys", "soc", "eng", "bio", "comp", "psi"};
        for (int b = 0; b < kTechBranchCount; ++b)
            for (int t = 0; t < 16; ++t)
                a[static_cast<std::size_t>(b * 16 + t)] = std::string(pre[b]) + std::to_string(t + 1);
        return a;
    }();
    return ids;
}

const char* kBranchZh[kTechBranchCount] = {"物理", "社会", "工程", "生物", "计算", "灵能"};

const std::array<std::string, kTechCount>& techDescs() {
    static const std::array<std::string, kTechCount> d = [] {
        std::array<std::string, kTechCount> a;
        for (int b = 0; b < kTechBranchCount; ++b)
            for (int t = 0; t < 16; ++t) {
                int tier = t / 3 + 1;
                if (tier > 5) tier = 5;
                a[static_cast<std::size_t>(b * 16 + t)] =
                    std::string(kBranchZh[b]) + "分支第 " + std::to_string(tier) + " 层成果；解锁后续节点与相关装配。";
            }
        return a;
    }();
    return d;
}

const char* kNames[kTechBranchCount][16] = {
    // Physics 物理
    {"引力操控", "量子隧穿", "零点能", "时空褶皱", "奇点解析", "真空衰变抑制", "超光速理论", "维度折叠",
     "场论统一", "暗物质捕获", "弦论工程", "因果保护", "时间对称", "质量操控", "能量债务", "终末物理"},
    // Society 社会
    {"星际治理", "贸易协定", "公共舆论", "殖民宪章", "外交礼仪", "情报机构", "心理画像", "战时经济",
     "联邦宪法", "文化输出", "人口政策", "教育体系", "司法统一", "社会福利", "银河议会", "文明收敛"},
    // Engineering 工程
    {"基础合金", "轨道建造", "空间站", "推进器", "装甲板", "护盾发生器", "船坞技术", "自动化工厂",
     "巨构工程", "纳米装配", "行星工程", "质量投射器", "曲率引擎", "恒星提灯", "黑洞收割", "工程奇点"},
    // Biology 生物
    {"基因图谱", "克隆技术", "延寿疗法", "生态改造", "神经接口", "共生体", "生物合金", "群体意识",
     "适应性免疫", "人工生态圈", "异种解剖", "记忆编辑", "生物计算", "地表重塑", "进化引导", "生命编织"},
    // Computing 计算
    {"计算基础", "密码学", "分布式网络", "智能辅助", "量子计算", "泛视协议", "预测模型", "意识上传",
     "自复制代码", "加密破解", "模拟宇宙", "思维入侵", "超图推理", "混沌预测", "心智防火墙", "计算奇点"},
    // Psionics 灵能
    {"心灵感应", "灵能屏障", "预知", "意念操纵", "群体幻觉", "灵魂锚定", "异空间感知", "心灵瘟疫",
     "意识投射", "灵能武器", "思维读取", "梦境航行", "灵能巨构", "神谕算法", "集体潜意识", "超脱"},
};

// 跨分支前置：少数关键节点需要另一分支的成果
struct CrossReq {
    int tech;
    int reqTech;
};
// 跨分支前置：编号必须写成 branch*16 + t。
// 早期误写成 branch*1 + t，导致这些门槛落在完全无关的科技上
// （社会分支的**入口**科技被门控在计算分支之后，该分支因此永远无法开始研究）。
constexpr int T(int branch, int t) { return branch * 16 + t; }

constexpr CrossReq kCross[] = {
    {T(1, 6), T(4, 4)},    // 社会 t7（心理画像）需要 计算 t5（量子计算）
    {T(1, 8), T(0, 5)},    // 社会 t9（联邦宪法）需要 物理 t6
    {T(2, 8), T(0, 6)},    // 工程 t9（巨构工程）需要 物理 t7
    // 计算 t6（泛视协议）需要 灵能 t1 —— 但入口科技不可被依赖，
    // 改为指向灵能的第 2 项（t=1），语义仍是「先掌握灵能基础」。
    {T(4, 5), T(5, 1)},
    {T(5, 14), T(4, 14)},  // 灵能 t15 需要 计算 t15
    {T(3, 14), T(5, 13)},  // 生物 t15 需要 灵能 t14
    {T(0, 14), T(2, 14)},  // 物理 t15 需要 工程 t15
    {T(1, 15), T(4, 15)},  // 社会 t16 需要 计算 t16
};

i64 techCost(int tier) {
    static constexpr i64 base[] = {0, 120, 320, 780, 1800, 4200, 9600};
    int t = tier;
    if (t < 1) t = 1;
    if (t > 6) t = 6;
    return base[t];
}

}  // namespace

const TechInfo& techInfo(int idx) {
    static const std::vector<TechInfo> table = [] {
        std::vector<TechInfo> v;
        v.reserve(kTechCount);
        for (int b = 0; b < kTechBranchCount; ++b) {
            for (int t = 0; t < 16; ++t) {
                TechInfo ti;
                int id = b * 16 + t;
                ti.id = static_cast<u8>(id);
                ti.idName = techIds()[static_cast<std::size_t>(id)];
                ti.nameZh = kNames[b][t];
                ti.desc = techDescs()[static_cast<std::size_t>(id)];
                ti.branch = static_cast<TechBranch>(b);
                int tier = t / 3 + 1;
                if (tier > 5) tier = 5;
                ti.tier = static_cast<u8>(tier);
                ti.cost = techCost(tier) + t * 60;
                if (t > 0) ti.prereq.push_back(static_cast<u8>(b * 16 + t - 1));
                ti.unlockBuilding = -1;
                ti.unlockModule = -1;
                ti.unlockMegastructure = -1;
                ti.unlockAscension = -1;
                ti.unlockItem = -1;
                v.push_back(std::move(ti));
            }
        }
        for (const auto& cr : kCross) {
            if (cr.tech < 0 || cr.tech >= kTechCount) continue;
            if (cr.reqTech < 0 || cr.reqTech >= kTechCount) continue;
            // 入口科技（每分支第 0 项）是分支的唯一入口，绝不能带任何前置，
            // 否则该分支会永远无法开始研究。
            if (cr.tech % 16 == 0) continue;
            v[static_cast<std::size_t>(cr.tech)].prereq.push_back(static_cast<u8>(cr.reqTech));
        }
        // 不变式：每分支入口必须无前置
        for (int b = 0; b < kTechBranchCount; ++b) {
            v[static_cast<std::size_t>(b * 16)].prereq.clear();
        }
        // 解锁映射（与 content/Buildings.cpp、FleetModules.cpp 的 requireTech 对应）
        auto setB = [&](int id, int b) { v[static_cast<std::size_t>(id)].unlockBuilding = static_cast<i16>(b); };
        setB(2, 3);   // eng3 → 合金熔炉
        setB(3, 4);   // eng4 → 部件工厂
        setB(1, 5);   // eng2 → 研究实验室
        setB(7, 6);   // eng8 → 贸易枢纽
        setB(8, 7);   // eng9 → 轨道船坞
        setB(9, 14);  // eng10 → 深核矿场
        setB(12, 10); // comp13 → 档案穹顶
        setB(13, 27); // comp14 → 异常解析站
        setB(20, 12); // soc5 → 凝聚尖塔
        setB(21, 13); // soc6 → 影响力公署
        setB(30, 17); // phys15 → 反物质环
        setB(31, 18); // phys16 → 量子锻造厂
        setB(33, 19); // comp2 → 数据圣殿
        setB(26, 20); // eng11 → 轨道船台
        setB(24, 21); // eng9 → 护盾弧阵
        setB(40, 24); // soc9 → 黑库
        auto setM = [&](int id, int m) { v[static_cast<std::size_t>(id)].unlockMegastructure = static_cast<i16>(m); };
        setM(50, 0);  // phys3 → 戴森环
        setM(55, 1);  // phys8 → 环世界
        setM(60, 2);  // phys13 → 物质解压器
        setM(45, 3);  // eng14 → 科研枢纽
        setM(65, 4);  // comp6 → 泛视尖塔
        setM(58, 5);  // eng11 → 哨兵阵列
        auto setMod = [&](int id, int m) { v[static_cast<std::size_t>(id)].unlockModule = static_cast<i16>(m); };
        setMod(5, 1);
        setMod(6, 9);
        setMod(11, 17);
        setMod(13, 24);
        setMod(17, 28);
        setMod(19, 34);
        setMod(25, 11);
        setMod(26, 13);
        setMod(30, 6);
        setMod(31, 4);
        setMod(32, 16);
        setMod(33, 23);
        setMod(60, 33);
        setMod(61, 35);
        setMod(72, 6);
        setMod(72, 12);

        // ---- 为每项科技赋予主题化属性修正 ----
        // 量级随 tier 递增：tier1 约 +2%，tier5 约 +10%。
        // 每分支两条主轴交替，16 项合计可显著改变国家走向。
        auto setEffect = [&](int id, ModKind a, Fixed va, ModKind bKind, Fixed vb) {
            TechInfo& ti = v[static_cast<std::size_t>(id)];
            ti.effects[0] = TechEffect{a, va};
            ti.effects[1] = TechEffect{bKind, vb};
            ti.effectCount = 2;
        };
        for (int b = 0; b < kTechBranchCount; ++b) {
            for (int t = 0; t < 16; ++t) {
                int id = b * 16 + t;
                int tier = t / 3 + 1;
                if (tier > 5) tier = 5;
                // 每 tier 递增 2%，tier5 = +10%
                Fixed mag = Fixed::pct(2 * tier);
                // 同分支内交替强化两条主轴
                ModKind primary = ModKind::Count, secondary = ModKind::Count;
                switch (static_cast<TechBranch>(b)) {
                    case TechBranch::Physics:
                        primary = (t % 2 == 0) ? ModKind::ResearchRate : ModKind::MilitaryPower;
                        secondary = (t % 2 == 0) ? ModKind::Detection : ModKind::Stability;
                        break;
                    case TechBranch::Society:
                        primary = (t % 2 == 0) ? ModKind::Stability : ModKind::InfluenceGain;
                        secondary = (t % 2 == 0) ? ModKind::Unrest : ModKind::DiploWeight;
                        break;
                    case TechBranch::Engineering:
                        primary = (t % 2 == 0) ? ModKind::BuildRate : ModKind::MilitaryPower;
                        secondary = (t % 2 == 0) ? ModKind::TradeMargin : ModKind::CreditRating;
                        break;
                    case TechBranch::Biology:
                        primary = (t % 2 == 0) ? ModKind::Growth : ModKind::Stability;
                        secondary = (t % 2 == 0) ? ModKind::Unrest : ModKind::ColonyCost;
                        break;
                    case TechBranch::Computing:
                        primary = (t % 2 == 0) ? ModKind::ResearchRate : ModKind::Detection;
                        secondary = (t % 2 == 0) ? ModKind::ManipulationSkill : ModKind::IntelDefense;
                        break;
                    case TechBranch::Psionics:
                        primary = (t % 2 == 0) ? ModKind::IntelDefense : ModKind::ManipulationSkill;
                        secondary = (t % 2 == 0) ? ModKind::DiploWeight : ModKind::Stability;
                        break;
                    default: break;
                }
                Fixed v1 = mag;
                Fixed v2 = mag / Fixed(2);
                // 殖民成本与民怨：负值才是收益
                if (primary == ModKind::ColonyCost || primary == ModKind::Unrest) v1 = -v1;
                if (secondary == ModKind::ColonyCost || secondary == ModKind::Unrest) v2 = -v2;
                setEffect(id, primary, v1, secondary, v2);
            }
        }
        return v;
    }();
    if (idx < 0 || idx >= kTechCount) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

int techIndexByName(std::string_view s) {
    for (int i = 0; i < kTechCount; ++i) {
        const TechInfo& t = techInfo(i);
        if (iequals(t.idName, s) || t.nameZh == s) return i;
    }
    return -1;
}

}  // namespace gf
