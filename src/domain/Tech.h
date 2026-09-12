#pragma once
// 技术树
#include <array>
#include <string>
#include <string>
#include <string_view>
#include <vector>

#include "domain/Species.h"
#include "util/Fixed.h"

namespace gf {

enum class TechBranch : u8 { Physics = 0, Society, Engineering, Biology, Computing, Psionics, Count };
inline constexpr int kTechBranchCount = static_cast<int>(TechBranch::Count);
inline constexpr int kTechCount = 96;

/// 科技带来的修正（并入 empireModifier）
struct TechEffect {
    ModKind kind = ModKind::Count;
    Fixed value = Fixed(0);
};

struct TechInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    TechBranch branch;
    u8 tier;                 // 1..5
    i64 cost;                // 研究点
    std::vector<u8> prereq;
    /// 解锁的建造/舰船/巨构 id（-1 表示无）
    i16 unlockBuilding;
    i16 unlockModule;
    i16 unlockMegastructure;
    i16 unlockAscension;
    i16 unlockItem;
    /// 该科技直接提供的属性修正（1~2 条）
    std::array<TechEffect, 2> effects{};
    u8 effectCount = 0;
};

/// 分支专精：某分支已完成的科技数
struct BranchProgress {
    int completed = 0;
    /// 16/16 完成
    bool mastered = false;
};

struct TechState {
    std::array<Fixed, kTechBranchCount> progress{};   // 每分支累积研究点
    std::array<u32, kTechBranchCount> current{};      // 当前攻关的 tech id（0 = 未选）
    std::vector<u8> completed;
    Fixed rate = Fixed(1);                            // 每 tick 基础研究点
    Fixed rateBonus = Fixed(0);                       // 派生：建筑、科技与政策研究加成
    /// 当前**立项**的科技。用 kNoTech 表示未立项 ——
    /// 不能用 0：科技编号 0 是合法科技，用 0 作哨兵会让它永远无法立项。
    static constexpr u32 kNoTech = 0xFFFFFFFFu;
    /// 研究不再是「花钱买点数、瞬间完成」，而是一个需要时间的项目：
    /// 立项后每季推进，资金只影响推进速度，且推进速度有上限 ——
    /// 因此任何科技都有**最短工期**，钱多也不能跳过。
    u32 project = kNoTech;
    Fixed projectProgress = Fixed(0);
    /// 已投入的季数（用于展示与最短工期判定）
    u32 projectTicks = 0;
    /// 每季为该项目投入的资金（0 = 仅靠基础研究速率）
    Fixed fundingPerTick = Fixed(0);
    /// 本局累计完成数（用于统计）
    u32 totalCompleted = 0;
    /// 各分支研究侧重（0..3）：决定研究点的分配。
    /// 没有侧重就会「所有帝国同时研究全部六个分支」，科技树在中期即被穷尽，
    /// 失去战略取舍。
    std::array<Fixed, kTechBranchCount> focus{};
    /// 主攻分支（最高侧重者）
    [[nodiscard]] TechBranch primaryFocus() const {
        int best = 0;
        for (int b = 1; b < kTechBranchCount; ++b)
            if (focus[static_cast<std::size_t>(b)].rawValue() > focus[static_cast<std::size_t>(best)].rawValue())
                best = b;
        return static_cast<TechBranch>(best);
    }
};

[[nodiscard]] const TechInfo& techInfo(int idx);
[[nodiscard]] int techIndexByName(std::string_view s);
[[nodiscard]] std::string_view techBranchName(TechBranch b);
[[nodiscard]] bool techCompleted(const TechState& st, int techId);
/// 某分支的完成进度
[[nodiscard]] BranchProgress branchProgress(const TechState& st, TechBranch b);
/// 科技提供的修正合计（含分支专精与满级奖励）
[[nodiscard]] Fixed techModifier(const TechState& st, ModKind kind);
/// 单条科技效果的文本
[[nodiscard]] std::string techEffectText(const TechInfo& t);
[[nodiscard]] bool techAvailable(const TechState& st, int techId);
/// 返回本次完成的 tech id 列表
[[nodiscard]] std::vector<u8> techAdvance(TechState& st, Fixed points);

/// 一项科技的最短工期（季）。钱再多也不能比这更快。
[[nodiscard]] int techMinTicks(int tier);

/// 立项：把某项科技设为当前攻关目标。返回是否成功。
[[nodiscard]] bool techStartProject(TechState& st, int techIdx, std::string* err);

/// 每季推进当前立项（**研究需要时间**的唯一入口）。
/// funding 为本季投入的资金；换算成研究点后与基础速率相加，
/// 再按「成本 / 最短工期」截断，保证工期下限。
/// 返回本季完成的科技列表。
[[nodiscard]] std::vector<u8> techTickProject(TechState& st, Fixed funding,
                                              const std::vector<u8>& completedList,
                                              Fixed scientistBonusRaw = Fixed(0));

/// 当前立项还需多少季（估算；未立项返回 -1）
[[nodiscard]] int techEtaTicks(const TechState& st);

}  // namespace gf
