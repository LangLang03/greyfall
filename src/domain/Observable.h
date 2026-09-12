#pragma once
// AI 观测与意图预测的数据结构。
//
// 放在 domain 层是为了保持单向依赖：ai → domain。
// 这些结构作为 GameState 的「派生缓存」成员存在，因此每个 GameState 各有一份，
// 不会在多个对局之间串味（replay / selftest 要求逐字节可复现）。
#include <array>
#include <string>
#include <vector>

#include "domain/Provenance.h"
#include "domain/Resource.h"
#include "util/Fixed.h"

namespace gf {

/// 每个字段的暴露路径：被哪条渠道、以何置信度观测到
struct FieldExposure {
    std::string field;
    ProvChannel channel = ProvChannel::Panopticon;
    Fixed confidence = Fixed::pct(90);
    Fixed signalCost = Fixed(0);
    std::string path;         // 人类可读的暴露路径
    bool forged = false;      // 该字段是否被玩家布置过
};

struct ItemView {
    u16 def = 0;
    u32 count = 1;
    bool forged = false;
    Fixed contamination = Fixed(0);
    ProvChannel channel = ProvChannel::Panopticon;
    Fixed credibility = Fixed::pct(90);
};

struct ClueView {
    u16 def = 0;
    bool known = false;
    Fixed credibility = Fixed(0);
    ProvChannel channel = ProvChannel::Panopticon;
    bool linked = false;
    u8 act = 1;
};

struct FleetView {
    u32 id = 0;
    u32 system = 0;
    u32 target = 0;
    Fixed strength = Fixed(0);
    Fixed morale = Fixed(0);
    u8 order = 0;
};

struct PendingView {
    u32 id = 0;
    u8 eventId = 0;
    int options = 0;
    int deferCount = 0;
};

struct OrderIntentView {
    u8 res = 0;
    bool buy = true;
    i64 qty = 0;
    Fixed px{};
    u8 exch = 0;
    u64 queuedTick = 0;
    bool exploited = false;
};

/// AI 眼中的完整画像（玩家或任意主体）
struct Observable {
    u32 subject = 0;
    std::array<Fixed, kCommodityCount> resources{};
    Fixed treasury = Fixed(0);
    Fixed influence = Fixed(0);
    Fixed unity = Fixed(0);
    Fixed military = Fixed(0);
    Fixed economy = Fixed(0);
    Fixed stability = Fixed(0);
    std::vector<ItemView> inventory;
    std::vector<ClueView> clues;
    std::array<Fixed, 6> techProgress{};   // kTechBranchCount 固定为 6
    std::vector<u8> techCompleted;
    std::vector<FleetView> fleets;
    std::vector<PendingView> pending;
    std::vector<OrderIntentView> orders;
    u32 rollbackCount = 0;
    u64 chronicleHead = 0;
    bool chronicleBurned = false;
    /// 在建工程的「未来需求向量」——front-running 的核心依据
    std::array<Fixed, kCommodityCount> futureDemand{};
    std::vector<u32> megastructureIds;
    std::vector<u32> colonizing;
    std::vector<FieldExposure> exposure;
    Fixed coverage = Fixed(1);
    int contaminatedFields = 0;
};

/// IntentPredictor 的输出
struct IntentPrediction {
    std::array<Fixed, kCommodityCount> netDemand{};
    Fixed confidence = Fixed(0);
    std::string topPath;
    std::vector<std::string> alternatives;
    int horizon = 3;
    Fixed deceptionLevel = Fixed(0);
    std::array<Fixed, kCommodityCount> urgency{};
};

/// 跨 tick 的观测缓存（派生数据，不入档）
struct ReaderCache {
    std::array<Observable, 16> prev{};
    std::array<bool, 16> valid{};
    std::array<IntentPrediction, 16> intent{};
};

}  // namespace gf
