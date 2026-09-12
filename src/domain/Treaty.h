#pragma once
// 条约、战争与外交关系
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

enum class TreatyKind : u8 {
    TradePact = 0,
    NonAggression,
    DefensivePact,
    ResearchPact,
    JointIntel,
    Federation,
    Vassalage,
    War,
    Sanction,
    Embargo,
    Ceasefire,
    TributeDemand,
    Count,
};

struct Treaty {
    TreatyKind kind = TreatyKind::TradePact;
    u32 a = 0;            // 提出方
    u32 b = 0;            // 接受方
    u64 signedTick = 0;
    i64 expireTick = -1;  // -1 = 永久
    Fixed terms = Fixed(0);      // 主要条款强度（赔款/让利比例等）
    Fixed compliance = Fixed(1); // 履约度 0..1
    std::string note;
};

/// 双向关系（矩阵以 (a,b) 有序对存储）
struct Relation {
    Fixed opinion = Fixed(0);       // -1..1
    Fixed trust = Fixed::pct(50);
    Fixed fear = Fixed(0);
    Fixed debt = Fixed(0);          // a 欠 b
    u32 border = 0;                 // 接壤星系数
    u64 lastWar = 0;
    u32 warScore = 0;
    bool atWar = false;
    bool embargo = false;
};

[[nodiscard]] std::string_view treatyKindName(TreatyKind k);
[[nodiscard]] TreatyKind treatyKindFromName(std::string_view s);
/// 建立/刷新条约（同 kind 同向已存在则更新）
void upsertTreaty(std::vector<Treaty>& list, const Treaty& t);
/// 移除某方与某方的某类条约
void removeTreaty(std::vector<Treaty>& list, TreatyKind kind, u32 a, u32 b);
[[nodiscard]] bool hasTreaty(const std::vector<Treaty>& list, TreatyKind kind, u32 a, u32 b);

/// 宣战/停战必须是**双向对称**的。
/// 单向设置 atWar 会导致一方认为在打仗、另一方认为和平 ——
/// 战斗判定、外交 AI、求和逻辑会各自得出不同结论。
void declareWar(struct GameState& st, u32 a, u32 b, bool atWar);
[[nodiscard]] bool atWarWith(const struct GameState& st, u32 a, u32 b);
/// 该帝国是否与任何人为敌
[[nodiscard]] bool atWarWithAnyone(const struct GameState& st, u32 a);

}  // namespace gf
