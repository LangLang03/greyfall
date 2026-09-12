#pragma once
// 正当战争理由（casus belli）与战争疲劳
//
// 设计动机：
//   早期版本可以随时对任何国家宣战，没有任何政治代价 ——
//   这是「最高难度开局零伤亡打全图」的制度性原因之一：
//   没有开战门槛，最优解就是尽快开打。
//
// 现行规则：
//   * 宣战必须持有对目标的 casus belli；
//   * 理由可以**自然产生**（领土争端、边界摩擦、盟友受侵、反制制裁），
//     也可以**人为制造**（`envoy <emp> fabricate` 花影响力伪造宣称，会过期）；
//   * 没有理由就宣战，会遭到全体第三方谴责与国内反弹；
//   * 战争会累积**战争疲劳**，疲劳过高触发反战事件与减益，
//     派系会要求停战；停战后疲劳与相关减益一并消除。
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

enum class CasusBelliKind : u8 {
    None = 0,
    TerritorialDispute,   // 领土争端：对方持有与我接壤的星系
    BorderIncident,       // 边界摩擦（自动/事件产生）
    Retaliation,          // 反制对方的制裁或禁运
    AllyDefense,          // 盟友遭到对方攻击
    FabricatedClaim,      // 伪造的宣称（花影响力造出来，会过期）
    BrokenTreaty,         // 对方撕毁条约
    Count,
};

[[nodiscard]] std::string_view casusBelliName(CasusBelliKind k);
[[nodiscard]] std::string casusBelliDesc(CasusBelliKind k);

struct CasusBelli {
    CasusBelliKind kind = CasusBelliKind::None;
    u32 target = 0xFFFFFFFFu;
    u64 gainedTick = 0;
    i64 expireTick = -1;      // -1 = 不过期
    std::string note;
};

/// 一个方向上的战争疲劳
struct WarWeariness {
    u32 enemy = 0xFFFFFFFFu;
    Fixed value = Fixed(0);   // 0..1
    u64 startTick = 0;
    bool peaceDemanded = false;   // 派系已公开要求停战
    u32 lastEventTick = 0;
};

/// 每 tick：更新战争疲劳、触发反战事件、施加/撤销减益
void warWearinessPhase(GameState& st);

/// 自然产生的正当理由（每 tick 检查）：领土争端、边界摩擦、反制制裁、盟友受侵
void casusBelliPhase(GameState& st);

/// 是否持有对目标的正当理由
[[nodiscard]] bool hasCasusBelli(const GameState& st, u32 empire, u32 target);
[[nodiscard]] const CasusBelli* findCasusBelli(const GameState& st, u32 empire, u32 target);
/// 伪造宣称（消耗影响力），返回是否成功
[[nodiscard]] bool fabricateClaim(GameState& st, u32 empire, u32 target, std::string* msg);
/// 移除对目标的理由（停战后调用）
void clearCasusBelli(GameState& st, u32 empire, u32 target);

/// 战争疲劳带来的减益（供 modifier 聚合与 UI 使用）
[[nodiscard]] Fixed wearinessStabilityPenalty(const GameState& st, u32 empire);
[[nodiscard]] Fixed wearinessUnrestPenalty(const GameState& st, u32 empire);
[[nodiscard]] Fixed wearinessSatisfactionPenalty(const GameState& st, u32 empire);

/// 文本
[[nodiscard]] std::string casusBelliReport(const GameState& st, u32 empire);
[[nodiscard]] std::string wearinessReport(const GameState& st, u32 empire);

}  // namespace gf
