#pragma once
// 全局词缀：整局生效的规则修正（例如"泛视网络增强：AI foresight+1，但操纵检测率+20%"）
#include <string>
#include <vector>

#include "core/GameState.h"

namespace gf {

enum ModifierBit : u64 {
    kModPanopticonBoost = 1ull << 0,   // 泛视增强：AI foresight+1，检测率+20%
    kModThinTrade = 1ull << 1,         // 航线稀薄：运费 +30%，套利空间变大
    kModWarEcho = 1ull << 2,           // 战火回响：战争成本 -20%，但信誉恢复 -30%
    kModDataGlut = 1ull << 3,          // 数据泛滥：内幕信号更频繁，可信度更低
    kModScarceAlloys = 1ull << 4,      // 合金短缺：合金基准价 +35%
    kModTrustDeficit = 1ull << 5,      // 信任赤字：所有阵营初始观感 -20%
    kModPsionicDawn = 1ull << 6,       // 灵能黎明：灵能类道具效果 +40%，反制更难
    kModRegulatorSurge = 1ull << 7,    // 监管风暴：操纵被查概率 +100%
    kModMemoryHole = 1ull << 8,        // 记忆空洞：线索时间衰减加倍
    kModBoomCycle = 1ull << 9,         // 繁荣周期：所有产能 +15%，波动 +20%
    kModBrokenChain = 1ull << 10,      // 断链纪元：跨所套利封锁惩罚减半
    kModSilentArchive = 1ull << 11,    // 静默档案：chronicle 缺失惩罚加倍
};

struct ModifierInfo {
    u64 bit;
    const char* idName;
    const char* nameZh;
    const char* desc;
};

[[nodiscard]] const std::vector<ModifierInfo>& modifierTable();
/// 选择词缀（同种子确定）
[[nodiscard]] u64 rollModifiers(u64 seed, int difficulty, std::string& nameOut);
[[nodiscard]] bool hasModifier(u64 bits, u64 bit);

}  // namespace gf
