#pragma once
// 种族、奴役、太空生物、外交施压与基因改造
//
// 本模块补齐四个此前缺失的系统：
//   1) 太空生物（含「海星」）：星系中的巨型生物，可猎杀并加工成罐头等高价值商品
//   2) 奴役：以民怨与合法性为代价换取产出的劳动制度
//   3) 外交施压：不宣战也能迫使对方让步的渐进手段
//   4) 基因改造：花资源永久改写本国种族的特质
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

// ---------------------------------------------------------------------------
// 1) 太空生物
// ---------------------------------------------------------------------------
enum class FaunaKind : u8 {
    StarJelly = 0,    // 海星：群居、温顺，肉与胶质可加工成罐头
    VoidWhale,        // 虚空鲸：庞大，油脂与骨材价值极高
    CrystalSwarm,     // 晶簇虫群：硅基，可提炼超材料
    RiftStalker,      // 裂隙潜行者：掠食性，会袭击舰队
    Count,
};

[[nodiscard]] std::string_view faunaName(FaunaKind k);
[[nodiscard]] std::string faunaDesc(FaunaKind k);
/// 猎杀该生物所需的最低舰队战力
[[nodiscard]] Fixed faunaThreat(FaunaKind k);
/// 猎杀后获得的商品与数量（写入 out，返回是否成功）
[[nodiscard]] bool faunaLoot(FaunaKind k, int* commodity, i64* amount);

struct FaunaHerd {
    u32 system = 0xFFFFFFFFu;
    FaunaKind kind = FaunaKind::StarJelly;
    i64 population = 100;      // 种群规模，会随季增长
    u64 lastHunted = 0;
};

/// 每 tick：种群增长与扩散
void faunaPhase(GameState& st);
/// 猎杀某星系的太空生物
[[nodiscard]] bool huntFauna(GameState& st, u32 empire, u32 system, std::string* msg);
/// 某星系的生物群（nullptr = 无）
[[nodiscard]] const FaunaHerd* faunaAt(const GameState& st, u32 system);
/// 「海星罐头」：把海星加工成食品罐头（需要先有捕获量）
[[nodiscard]] bool canStarJelly(GameState& st, u32 empire, i64 batches, std::string* msg);
/// 文本
[[nodiscard]] std::string faunaReport(const GameState& st, u32 empire);

// ---------------------------------------------------------------------------
// 2) 奴役
// ---------------------------------------------------------------------------
/// 劳役制度：决定被征服人口如何被对待
enum class LaborPolicy : u8 {
    Free = 0,        // 自由民：无额外产出，无额外民怨
    CasteSystem,     // 种姓制：产出 +15%，民怨 +，合法性 -
    Chattel,         // 蓄奴制：产出 +30%，民怨 ++，第三方观感 --
    Count,
};

[[nodiscard]] std::string_view laborPolicyName(LaborPolicy p);
[[nodiscard]] std::string laborPolicyDesc(LaborPolicy p);
/// 切换劳役制度
[[nodiscard]] bool setLaborPolicy(GameState& st, u32 empire, LaborPolicy p, std::string* msg);
/// 该制度提供的产出倍率
[[nodiscard]] Fixed laborOutputMultiplier(LaborPolicy p);
/// 该制度带来的民怨目标增量
[[nodiscard]] Fixed laborUnrestTarget(LaborPolicy p);
/// 每 tick：奴役制引发的内部张力（派系反应、第三方谴责）
void laborPhase(GameState& st);

// ---------------------------------------------------------------------------
// 3) 外交施压
// ---------------------------------------------------------------------------
enum class PressureKind : u8 {
    Economic = 0,    // 经济施压：关税与禁运威胁
    Military,        // 军事施压：边境演习
    Diplomatic,      // 外交孤立：拉拢其邻国
    Count,
};

[[nodiscard]] std::string_view pressureKindName(PressureKind k);
/// 施加压力：消耗影响力与国库，累积对该国的压力值
[[nodiscard]] bool applyPressure(GameState& st, u32 empire, u32 target, PressureKind k,
                                 std::string* msg);
/// 某国对我国承受的压力（0..1）
[[nodiscard]] Fixed pressureOn(const GameState& st, u32 target, u32 source);
/// 每 tick：压力衰减，并在高压下迫使对方让步
void pressurePhase(GameState& st);

// ---------------------------------------------------------------------------
// 4) 基因改造
// ---------------------------------------------------------------------------
/// 改造方向
enum class GeneMod : u8 {
    Hardy = 0,       // 强健：人口增长 +20%
    Industrious,     // 勤勉：建造速率 +15%
    Erudite,         // 睿智：研究速率 +15%
    Resilient,       // 坚韧：稳定度 +12%
    Docile,          // 温顺：民怨 -15%
    Count,
};

[[nodiscard]] std::string_view geneModName(GeneMod m);
[[nodiscard]] std::string geneModDesc(GeneMod m);
/// 改造所需成本
[[nodiscard]] i64 geneModCost(GeneMod m);
/// 对本国种族施加基因改造（消耗凝聚力和信用点，永久生效）
[[nodiscard]] bool applyGeneMod(GameState& st, u32 empire, GeneMod m, std::string* msg);
/// 每 tick：合成人/基因改造的维护费与伦理反弹
void genePhase(GameState& st);
/// 文本
[[nodiscard]] std::string speciesReport(const GameState& st, u32 empire);

// ---------------------------------------------------------------------------
// 5) 游牧国家
// ---------------------------------------------------------------------------
// 「舰队即是国土」不是一个修辞：游牧帝国**不靠疆域**获取国力，
// 而是靠机动的舰队。定居会拖累它，迁徙则让它更强。
/// 该帝国是否为游牧政体（持有「游牧」伦理或「游牧虫群」种族）
[[nodiscard]] bool isNomadic(const GameState& st, u32 empire);
/// 游牧的「群势」：舰队数与星系数的比值，0..1
[[nodiscard]] Fixed hordeMomentum(const GameState& st, u32 empire);
/// 游牧带来的军事加成（群势越高越强，最高 +35%）
[[nodiscard]] Fixed nomadicMilitaryBonus(const GameState& st, u32 empire);
/// 游牧带来的稳定度惩罚（疆域越大越难维持，最多 -18%）
[[nodiscard]] Fixed nomadicStabilityPenalty(const GameState& st, u32 empire);
/// 迁徙：把首都迁往另一个己方星系（游牧专属，消耗影响力）
[[nodiscard]] bool nomadicMigrate(GameState& st, u32 empire, u32 system, std::string* msg);
/// 每 tick：游牧的群势演化与迁移压力
void nomadicPhase(GameState& st);
/// 游牧态势文本
[[nodiscard]] std::string nomadicReport(const GameState& st, u32 empire);

}  // namespace gf
