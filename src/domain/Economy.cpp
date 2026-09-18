#include "domain/Economy.h"

#include <algorithm>
#include <array>
#include <string>

#include "core/GameState.h"
#include "domain/Construction.h"
#include "domain/Corruption.h"
#include "domain/Development.h"
#include "domain/Empire.h"
// 已知分层异味：`empireModifier` 是**域规则**（种族/伦理/公民/开发的修正汇总），
// 却声明在 `gen/EmpireGen.h`（世界生成层）。全项目 19/20 个引用它的文件
// 都已经包含该头，搬迁它会牵动 20 个文件而收益有限 ——
// 记录在此而非默默延续。（正确做法：把实现移到 domain/ModifierUtil.h。）
#include "gen/EmpireGen.h"
#include "domain/ModifierUtil.h"
#include "domain/Planet.h"
#include "domain/Policy.h"
#include "domain/SpeciesAdv.h"
#include "domain/Tech.h"
#include "domain/Trade.h"
#include "mkt/Margin.h"
#include "mkt/MarketEngine.h"
#include "rng/Streams.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

void researchTick(GameState& st) {
    for (auto& e : st.empires) {
        if (!e.alive || e.tech.project == TechState::kNoTech) continue;
        Fixed funding = Fixed(0);
        if (e.tech.fundingPerTick.rawValue() > 0) {
            // 玩家侧的投入来自国库；扣不起就按可支付额度缩减（不会透支）
            Fixed want = e.tech.fundingPerTick;
            Fixed have = fxMax(e.treasury, Fixed(0));
            funding = fxMin(want, have);
            e.treasury -= funding;
            if (e.isPlayer) st.market.margin.cash = e.treasury;
            // 国库见底时自动把投入降档，避免玩家每季都被迫手动调整
            if (funding.rawValue() < want.rawValue()) e.tech.fundingPerTick = funding;
        }
        Fixed sciBonus = Fixed(0);
        for (int b = 0; b < kTechBranchCount; ++b) {
            if (e.tech.current[static_cast<std::size_t>(b)] == e.tech.project) {
                sciBonus = scientistBonus(st, e.id, b);
                break;
            }
        }
        std::vector<u8> done = techTickProject(e.tech, funding, e.tech.completed, sciBonus);
        for (u8 d : done) {
            const TechInfo& ti = techInfo(static_cast<int>(d));
            st.logEvent(LogPhase::Economy, "econ.tech",
                        e.name + " 完成研究【" + std::string(ti.nameZh) + "】→ " + techEffectText(ti),
                        e.id);
            // 不塞进 pending 队列：那会阻塞 advance（退出码 5），
            // 每完成一项科技都强制玩家确认一次过于打扰。
            // 完成记录写入日志，玩家用 `greyfall logs` 或 `research status` 查看。
        }
    }
}

void economyPhase(GameState& st) {
    nationalEdictPhase(st);
    refreshEmpireBonuses(st);
    for (auto& e : st.empires) {
        if (!e.alive) continue;
        Fixed income = Fixed(0);
        Fixed upkeep = developmentUpkeep(st, e.id);
        std::array<Fixed, kCommodityCount> demand;
        for (int c = 0; c < kCommodityCount; ++c) demand[c] = resourceDemand(st, e, c);
        const auto openingStock = e.stock;
        // 本季各商品的**新增产量**（劳役倍率只作用于它，避免复利）
        std::array<Fixed, kCommodityCount> producedByCommodity{};
        Fixed producedThisTick = Fixed(0);

        // 行星产出
        for (u32 sys : e.systems) {
            const SystemNode* s = st.system(sys);
            if (s == nullptr) continue;
            for (u32 pid : s->planets) {
                Planet* p = st.planet(pid);
                if (p == nullptr || p->owner != e.id) continue;
                // 发展度与安抚提升产出
                Fixed stabilityFactor = Fixed::pct(50) + p->stability / Fixed(2);
                std::array<Fixed, kCommodityCount> naturalProduction{};
                for (int c = 0; c < kCommodityCount; ++c)
                    naturalProduction[c] = planetNaturalProduction(*p, static_cast<u8>(c), openingStock[c], demand[c]);
                // ---- 行星发展度成长 ----
                // 开发度原先只在世界生成时设定、永不增长，使经济体缺乏成长循环，
                // 也让「贸易税 = 发展度 × 30」这一收入项冻结不变。
                // 现在：建筑提供基础动力，稳定度加速，民怨抑制，人口提供规模效应。
                {
                    Fixed base = Fixed::bp(25) + Fixed(static_cast<i64>(p->buildings.size())) * Fixed::bp(8);
                    Fixed popFactor = fxSqrt(Fixed(p->pops) / Fixed(10));
                    Fixed dev = base * (Fixed(1) + popFactor) * stabilityFactor *
                                (Fixed(1) - fxClamp(p->unrest, Fixed(0), Fixed(1)));
                    p->development = fxClamp(p->development + dev, Fixed(0), Fixed(10));   // 上限 10（结构体约定范围）
                }
                for (int c = 0; c < kCommodityCount; ++c) {
                    Fixed prod = naturalProduction[static_cast<std::size_t>(c)];
                    if (prod.rawValue() > 0) {
                        e.stock[static_cast<std::size_t>(c)] += prod;
                        producedByCommodity[static_cast<std::size_t>(c)] += prod;
                        producedThisTick += prod;
                        if (c == static_cast<int>(Commodity::Credits)) income += prod;
                    }
                }
                // 建筑加成
                for (u32 bid : p->buildings) {
                    const BuildingInfo& bi = buildingInfo(static_cast<int>(bid & 0xFFu));
                    switch (bi.effect) {
                        case BuildingEffect::ProdCredits:
                            income += bi.effectValue;
                            break;
                        case BuildingEffect::ProdEnergy:
                            e.stock[static_cast<std::size_t>(Commodity::Energy)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdMinerals:
                            e.stock[static_cast<std::size_t>(Commodity::Minerals)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdFood:
                            e.stock[static_cast<std::size_t>(Commodity::Food)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdMedicines:
                            e.stock[static_cast<std::size_t>(Commodity::Medicines)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdAlloys:
                            e.stock[static_cast<std::size_t>(Commodity::Alloys)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdComponents:
                            e.stock[static_cast<std::size_t>(Commodity::Components)] += bi.effectValue;
                            break;
                        case BuildingEffect::ProdUnity:
                            e.unity += bi.effectValue / Fixed(10);
                            break;
                        case BuildingEffect::ProdInfluence:
                            e.influence += bi.effectValue / Fixed(10);
                            break;
                        case BuildingEffect::Stability:
                            e.stability = fxClamp(e.stability + bi.effectValue / Fixed(10), Fixed(0), Fixed(1));
                            break;
                        case BuildingEffect::ProdResearch:
                        case BuildingEffect::Trading:
                        case BuildingEffect::Storage:
                        case BuildingEffect::ClueDiscovery:
                            // 持续加成统一由 refreshEmpireBonuses 重建。
                            break;
                        default:
                            break;
                    }
                    upkeep += Fixed(bi.upkeep);
                }
                // 人头税：受稳定度与民怨调节。
                // 除数决定税基量级 —— 之前用 /1000 导致每行星每季只有约 3 cr，
                // 比舰队维护低两个数量级，所有帝国必然破产。
                Fixed taxRate = Fixed(2) * (Fixed::pct(100) - p->unrest * Fixed::pct(40)) *
                                (Fixed::pct(50) + p->stability / Fixed(2));
                income += Fixed(p->pops) * taxRate / Fixed(20);
                // 贸易税：发展度带来的流通收益
                income += p->development * Fixed(30);
                // 人口维护费
                upkeep += Fixed(p->pops) / Fixed(200);
                // 人口增长
                Fixed growth = Fixed(p->pops) * Fixed::bp(20) * (Fixed(1) + empireModifier(e, ModKind::Growth));
                growth = growth * (Fixed(1) - p->unrest);
                p->pops += growth.rawValue() / FIX;
                if (p->pops < 0) p->pops = 0;
            }
        }

        for (int c = 0; c < kCommodityCount; ++c) {
            const Fixed production = megaProduction(st, e, c);
            e.stock[c] += production;
            producedByCommodity[c] += production;
            producedThisTick += production;
        }
        // ---- 库存消耗与维护 ----
        // 关键：demand 必须真的被消耗，否则库存只增不减 ⇒
        // GDP 无限膨胀、维护费吃掉全部收入、国力指数爆炸。
        int shortages = 0;
        for (int c = 0; c < kCommodityCount; ++c) {
            const CommodityInfo& ci = commodityInfo(c);
            Fixed& stock = e.stock[static_cast<std::size_t>(c)];
            Fixed need = demand[static_cast<std::size_t>(c)];
            if (need.rawValue() > 0) {
                if (stock.rawValue() >= need.rawValue()) {
                    stock -= need;
                } else {
                    if (stock.rawValue() > 0) stock = Fixed(0);
                    ++shortages;
                }
            }
            // 库存维护（按剩余库存计费）
            upkeep += stock * ci.storage / Fixed(1000);
            // 战略物资囤积过剩会招致额外维护
            Fixed storageLimit = Fixed(50000) + e.storageBonus;
            if (ci.cat == EcoCategory::Strategic && stock.rawValue() > storageLimit.rawValue()) {
                upkeep += (stock - storageLimit) / Fixed(500);
            }
        }
        if (shortages > 0) {
            // 短缺对民怨与稳定度的影响都是**结构性**的，
            // 已统一在 FactionAI 的结构性民怨与稳定度目标中计算，此处不再按季扣减。
            if (e.isPlayer && st.tick % 8 == 0) {
                st.logEvent(LogPhase::Economy, "econ.shortage",
                            "本季有 " + std::to_string(shortages) + " 种物资短缺，稳定度与民心受损",
                            e.id, Fixed(shortages));
            }
        }

        // 政策维护费
        upkeep += Fixed(policyUpkeep(st, e.id));
        // 派系满意度的均衡收敛统一在 domesticPhase / FactionAI 中处理，
        // 此处不再重复施加（两处收敛会互相抵消）。
        // 劳役制度：以民怨为代价换取经济产出（蓄奴制 ×1.30）。
        //
        // 为什么作用在**收入**而不是实物产量：实物生产有库存节流 ——
        // 库存远超需求时产量被压到 0（实测开局库存 2400、需求仅 16，
        // 产量从第 1 季起就完全为 0），倍率乘以 0 仍是 0。
        // 收入是纯流量、不受节流影响，奴役的经济意义在这里才看得见。
        {
            Fixed laborMult = laborOutputMultiplier(e.labor);
            if (laborMult.rawValue() != Fixed(1).rawValue()) {
                Fixed gain = (income - upkeep) * (laborMult - Fixed(1));
                if (gain.rawValue() > 0) income += gain;
            }
        }
        // 民生开销：社会主义的福利支出（upkeepBias 为正则加重）。
        // 这是它「工厂效率高」的对价 —— 没有这一项，社会主义就是纯赚。
        {
            Fixed ub = governmentInfo(e.government).upkeepBias;
            if (ub.rawValue() > 0) upkeep += upkeep * ub;
            else if (ub.rawValue() < 0) upkeep += upkeep * ub;   // 负值即减负
        }
        // 腐败：直接按比例抽走净收入。
        // 这是「帝国规模」的真实代价 —— 没有它，疆域扩张永远是纯收益。
        {
            Fixed loss = corruptionIncomeLoss(st, e.id);
            if (loss.rawValue() > 0) {
                Fixed net = income - upkeep;
                if (net.rawValue() > 0) {
                    Fixed skim = net * loss;
                    income -= skim;
                }
            }
        }
        // 记录净收入：AI 的各类支出以它为上限（见 MarketEngine / FactionAI）
        e.lastIncome = income - upkeep;
        e.treasury += income - upkeep;
        // 财政赤字对民怨与稳定度的影响同样是结构性的（见 FactionAI），
        // 此处不再按季扣减。
        if (e.id == kPlayerId) {
            st.market.margin.cash += income - upkeep;
            e.treasury = st.market.margin.cash;
        }

        // 种族张力导致的民怨是**结构性**的，已统一在 FactionAI 的民怨目标中计算，
        // 此处不再按季累加（累加会让民怨在长局中必然饱和）。

        // 研究推进已改由 researchTick() 以「立项 + 逐季推进」的方式处理
        //（见本文件顶部的 researchTick）。这里**不能**再调用 techAdvance：
        // 那是一条绕过最短工期的即时路径，会让「研究需要时间」形同虚设
        //（两条路径并存时，科技会在立项当季就被旧路径结算掉）。

        // GDP 与国力评分。
        // 库存的价值贡献按「开方」计入 —— 否则单纯囤积就能让国力无限增长，
        // 与「经济实力来自产能与流通」的直觉相悖。
        Fixed gdp = Fixed(0);
        for (int c = 0; c < kCommodityCount; ++c) {
            Fixed qty = e.stock[static_cast<std::size_t>(c)];
            Fixed px = st.market.spotIndex[static_cast<std::size_t>(c)];
            if (px.rawValue() <= 0) px = commodityInfo(c).basePrice;
            // 单位换算到「千单位」量级后再开方，保持量纲稳定
            Fixed kilounits = qty / Fixed(1000);
            Fixed contrib = fxSqrt(kilounits) * px;
            gdp += contrib;
        }
        e.gdp = gdp;
        e.economy = fxLerp(e.economy, gdp / Fixed(20), Fixed::pct(10));
        if (e.economy.rawValue() < 0) e.economy = Fixed(0);
        e.score = e.powerIndex();
    }

    researchTick(st);
    developmentPhase(st);
    (void)marginMarkToMarket(st);
}

}  // namespace gf
