#pragma once
// 帝国（玩家与 AI 共用同一结构，AI 通过 Mind 持有额外信念）
#include <array>
#include <string>
#include <vector>

#include "domain/Building.h"
#include "domain/Domestic.h"
#include "domain/Development.h"
#include "domain/Fleet.h"
#include "domain/Mind.h"
#include "domain/Parliament.h"
#include "domain/SpeciesAdv.h"
#include "domain/SpyNetwork.h"
#include "domain/Trade.h"
#include "domain/Policy.h"
#include "domain/Resolution.h"
#include "domain/Resource.h"
#include "domain/Species.h"
#include "domain/CasusBelli.h"
#include "domain/Government.h"
#include "domain/Personnel.h"
#include "domain/Tech.h"
#include "util/Fixed.h"

namespace gf {

enum class EmpireStance : u8 { Expansionist = 0, Defensive, Mercantile, Scholarly, Zealot, Isolationist,
                               Opportunist, Count };

struct Empire {
    u32 id = 0;
    std::string name;
    std::string adjective;
    std::string rulerName;
    u8 species = 0;
    std::array<u8, 3> ethics{};
    std::array<u8, 4> civics{};
    u8 government = 0;
    EmpireStance stance = EmpireStance::Mercantile;
    bool isPlayer = false;
    bool alive = true;
    bool aiPersonaAggressive = false;
    u32 capital = 0;

    // 经济
    Fixed treasury = Fixed(0);                                   // credits（定点）
    std::array<Fixed, kCommodityCount> stock{};                  // 库存
    std::array<Fixed, kCommodityCount> capacity{};               // 每 tick 产能
    std::array<Fixed, kCommodityCount> demand{};                 // 每 tick 消耗
    Fixed influence = Fixed(0);
    Fixed unity = Fixed(0);
    Fixed creditRating = Fixed::pct(60);
    i64 debt = 0;

    // 国力
    Fixed military = Fixed(0);
    Fixed economy = Fixed(0);
    /// 宗主国（和平会议中附庸化的结果；kNoEmpire = 独立）
    u32 vassalOf = kNoEmpire;
    /// 上季度的净收入（收入 − 维护）。用于把 AI 的支出约束在**收入**规模上，
    /// 而不是按国库百分比支出 —— 后者是几何式抽干，与收入无关。
    Fixed lastIncome = Fixed(0);
    Fixed stability = Fixed::pct(60);
    Fixed legitimacy = Fixed::pct(60);
    Fixed score = Fixed(0);
    Fixed intelDefense = Fixed::pct(40);
    Fixed counterIntel = Fixed(0);
    Fixed propaganda = Fixed(0);

    // 研发
    TechState tech;

    // 领土与武力
    std::vector<u32> systems;
    std::vector<u32> fleets;
    std::vector<FleetDesign> designs;
    std::vector<MegastructureBuild> megas;
    std::vector<u32> colonizing;      // 正在殖民的星系
    std::vector<DevelopmentProject> developmentProjects;
    std::vector<NationalEdict> nationalEdicts;
    u32 ascensions = 0;
    std::vector<u32> buildQueue;      // 正在建造的建筑（planet*256+building）

    // 内政与外交
    Domestic domestic;
    u32 federation = 0xFFFFFFFFu;
    std::array<Fixed, kMaxEmpires> opinion{};   // 对他人观感 -1..1

    /// 安全访问观感数组。
    /// 直接写 `e.opinion[id]` 在 id >= kMaxEmpires 时**越界写入**，
    /// 会破坏相邻字段（实测使国库变成 INT64_MIN 量级的垃圾值）——
    /// 割据产生的新国家 id 很容易超过 16。所有读写都必须走这里。
    [[nodiscard]] Fixed opinionOf(u32 who) const {
        return who < kMaxEmpires ? opinion[who] : Fixed(0);
    }
    void setOpinion(u32 who, Fixed v) {
        if (who < kMaxEmpires) opinion[who] = v;
    }
    void addOpinion(u32 who, Fixed delta) {
        if (who < kMaxEmpires) opinion[who] = fxClamp(opinion[who] + delta, Fixed(-1), Fixed(1));
    }
    u32 lastWarTick = 0;
    /// 上一次真正投入兵力的入侵 tick。
    /// 用于「入侵冷却」：没有它，AI 只要处于战争状态就会**每 tick** 重复生成
    /// 入侵候选并重复出兵同一星系（实测 t=17→t=52 连续 36 季刷屏，
    /// 其中多数是「出兵 0 支舰队」的空打），战争因此变成无意义的磨盘。
    u64 lastInvadeTick = 0;
    /// 正当战争理由（casus belli）：没有它宣战会有严重政治代价
    std::vector<CasusBelli> casusBelli;
    /// 战争疲劳（按对手分别累计）
    std::vector<WarWeariness> weariness;
    /// 人事：领袖、科研部科学家、集团军
    Ruler ruler;
    std::vector<Scientist> scientists;
    std::vector<Formation> formations;
    /// 舰船设计库与下一个设计编号
    u32 nextDesignId = 1;
    /// 政体运行状态：选举进程、镇压与怨恨、继承危机
    GovernmentState gov;
    /// 意识形态压力：每个来源对一个帝国的长期渗透积累（0..1）
    std::array<Fixed, kMaxEmpires> ideologyPressure{};
    /// 外交压力：每个来源对我国施加的胁迫积累（0..1）
    std::array<Fixed, kMaxEmpires> pressure{};
    /// 劳役制度（自由民 / 种姓制 / 蓄奴制）
    LaborPolicy labor = LaborPolicy::Free;
    /// 已完成的基因改造
    std::array<bool, static_cast<std::size_t>(GeneMod::Count)> geneMods{};

    /// 造舰队列：舰船需要船坞与工期，不能瞬间爆兵。
    struct ShipOrder {
        u32 system = 0;      // 建造星系
        u32 design = 0;      // 设计 id
        u32 ticksLeft = 0;
        u32 totalTicks = 1;
        i64 paidCredits = 0;
    };
    std::vector<ShipOrder> shipQueue;

    /// 腐败度 0..1：随帝国规模增长，直接侵蚀收入
    Fixed corruption = Fixed(0);

    /// 建筑提供的持续性加成（每季由建筑重新累计，读档后当季重建）
    Fixed tradeBonus = Fixed(0);    // 贸易运力加成
    Fixed storageBonus = Fixed(0);  // 库存上限加成
    Fixed clueBonus = Fixed(0);     // 线索发现加成

    // AI 心智（玩家自己也有一份，用于 read-mind 之后的镜像显示）
    EmpireMind mind;

    // 行动点
    int apMax = 4;
    int apLeft = 4;

    // 决议系统：生效中的效果、进行中的倒计时、历史记录
    EmpireResolutions resolutions;
    // 政策系统：持久化法令（分组互斥、有维护费、有过渡期）
    PolicyState policies;
    // 议会：派系席位、法案表决、政治资本
    Parliament parliament;
    // 贸易：进出口量、关税与出口收入
    EmpireTradeStats trade;
    // 情报机构：间谍网络、特工池
    SpyAgency spy;

    // 统计
    i64 popTotal = 0;
    Fixed gdp = Fixed(0);
    i64 coloniesFounded = 0;
    i64 warsWon = 0;
    i64 betrayalsCommitted = 0;
    i64 betrayalsSuffered = 0;

    /// 综合国力指数（PowerBalancing 的输入，0..1 归一前的原始值）
    [[nodiscard]] Fixed powerIndex() const;
    /// 有效产能（含建筑/科技）
    [[nodiscard]] Fixed effectiveCapacity(Commodity c) const;
    /// 库存 + 期货净头寸的可用量
    [[nodiscard]] Fixed availableStock(Commodity c) const;
};

[[nodiscard]] std::string_view stanceName(EmpireStance s);
struct GameState;
/// 从已建成建筑与当前修正重算；不保存、不可逐季累加。
void refreshEmpireBonuses(GameState& st);
[[nodiscard]] EmpireStance stanceFromName(std::string_view s);
[[nodiscard]] std::string_view goalDimName(GoalDim d);
[[nodiscard]] std::string_view actorTypeName(ActorType t);

}  // namespace gf
