#pragma once
// 剧情骨架：7 幕 + 隐藏第 8 幕
#include <string>
#include <string_view>
#include <vector>

#include "clue/ClueDef.h"
#include "util/Fixed.h"

namespace gf {

struct ActInfo {
    u8 act;
    std::string_view title;
    std::string_view intro;
    std::string_view outro;
    /// 通关该幕所需的最少结论数
    int requiredConclusions;
    /// 幕推进时对市场的冲击强度
    Fixed marketImpact;
};

struct Beat {
    u16 id = 0;
    u8 act = 1;
    std::string title;
    std::string text;
    bool resolved = false;
    /// 关联事件/线索
    i16 eventId = -1;
    i16 clueId = -1;
};

[[nodiscard]] const ActInfo& actInfo(int act);
[[nodiscard]] std::string_view actTitle(int act);
[[nodiscard]] int actCount();

/// 幕内节拍（由 PlotState.pendingBeats 引用）
[[nodiscard]] const std::vector<Beat>& actBeats(int act);

/// 结局向量 → 命名结局
struct EndingInfo {
    u8 id;
    std::string_view idName;
    std::string_view nameZh;
    std::string_view text;
    /// 主导维度（0..7）
    u8 dominant;
};
[[nodiscard]] const EndingInfo& endingInfo(int idx);
[[nodiscard]] int endingCount();
/// 由 8 维向量选出结局 id
[[nodiscard]] int pickEnding(const std::array<Fixed, 8>& vec);

/// 第 8 幕（隐藏）
[[nodiscard]] bool hiddenActAvailable(const PlotState& plot);

}  // namespace gf
