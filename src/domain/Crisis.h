#pragma once
// 危机与异常点
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

inline constexpr int kEventCount = 140;
inline constexpr int kAnomalyCount = 25;

enum class EventPhase : u8 {
    Tick = 0,     // 常规 tick 事件
    Crisis,       // 危机（外部冲击）
    MarketShock,  // 市场冲击
    Diplomatic,   // 外交事件
    Anomaly,      // 异常点
    Story,        // 剧情幕
    Count,
};

enum class EventScope : u8 { Global = 0, Empire, System, Planet, Market, Count };

struct EventInfo {
    u8 id;
    std::string_view idName;
    std::string_view title;
    std::string_view body;
    EventPhase phase;
    EventScope scope;
    i64 weight;            // 调度权重
    Fixed severity;        // 强度倍率
    /// 需要玩家抉择
    bool hasChoices;
    u8 choiceCount;
    /// 前置条件（简化）：最低 tick、是否需要某科技
    i64 minTick;
    i16 requireTech;
    i16 requireCommodity;  // 受影响标的（-1 无）
};

/// 一个待抉择事件的实例
enum class ChoiceKind : u8 { Event = 0, Faction };
struct PendingChoice {
    u32 id = 0;
    u8 eventId = 0;
    u32 scopeTarget = 0;         // 帝国/星系/行星 id
    u64 createdTick = 0;
    int deferCount = 0;
    std::vector<std::string> options;      // 选项文本
    std::vector<std::string> hints;        // 选项后果提示（模糊）
    ChoiceKind kind = ChoiceKind::Event;
    u8 subject = 0;                       // 派系种类
    u64 deferredUntil = 0;                // 到期前不阻塞推进
};

struct CrisisState {
    u32 id = 0;
    u8 defId = 0;
    std::string name;
    bool active = false;
    u64 startTick = 0;
    Fixed severity = Fixed(1);
    u32 target = 0;
    Fixed progress = Fixed(0);
    std::vector<u32> participants;
};

struct AnomalyInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view desc;
    Fixed difficulty;
    i16 rewardClue;   // 产出线索节点
    i16 rewardItem;   // 产出道具
    i16 rewardTech;
};

[[nodiscard]] const EventInfo& eventInfo(int idx);
[[nodiscard]] const AnomalyInfo& anomalyInfo(int idx);
[[nodiscard]] std::string_view eventPhaseName(EventPhase p);

}  // namespace gf
