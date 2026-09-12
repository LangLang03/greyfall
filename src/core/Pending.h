#pragma once
// 待抉择事件队列与延后机制
#include <string>
#include <vector>

#include "domain/Crisis.h"
#include "util/Fixed.h"

namespace gf {

/// 玩家已提交但尚未在 tick 中结算的动作（AI 透过 OmniscientReader 看到的就是它）
enum class PlannedKind : u8 {
    Order = 0, Futures, Cancel, Colony, ShipBuild, FleetOrder, Edict, Research, Build, Mega, Ascend,
    Envoy, Spy, Gift, Propaganda, UseItem, Combine, Deduce, Custom,
};

struct PlannedAction {
    PlannedKind kind = PlannedKind::Custom;
    std::string command;      // 原始命令文本（重放用）
    std::string detail;
    u64 queuedTick = 0;
    int apCost = 0;
    /// 结构性字段（AI 从前瞻里要读的"意图"）
    u8 res = 0;
    i64 qty = 0;
    Fixed px{};
    u32 target = 0;
    u8 exch = 0;
};

/// 抉择队列：存在 pending 时 advance 返回退出码 5
struct PendingQueue {
    std::vector<PendingChoice> items;
    u32 nextId = 1;

    [[nodiscard]] bool empty() const { return items.empty(); }
    [[nodiscard]] bool hasBlocking(u64 tick) const {
        for (const auto& c : items) if (c.deferredUntil <= tick) return true;
        return false;
    }
    [[nodiscard]] std::size_t size() const { return items.size(); }
    PendingChoice& push(const PendingChoice& c) {
        items.push_back(c);
        items.back().id = nextId++;
        return items.back();
    }
    /// 移除指定索引的抉择（choose 命令）
    bool removeAt(std::size_t idx);
    [[nodiscard]] PendingChoice* find(u32 id);
};

}  // namespace gf
