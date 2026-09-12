#include "gen/ModifierGen.h"

#include <string>
#include <vector>

#include "rng/SplitMix.h"
#include "util/Str.h"

namespace gf {
namespace {

const std::vector<ModifierInfo>& table() {
    static const std::vector<ModifierInfo> t = {
        {kModPanopticonBoost, "panopticon-boost", "泛视网络增强",
         "AI foresight +1，操纵检测率 +20%；你的每一个动作都更早被读懂。"},
        {kModThinTrade, "thin-trade", "航线稀薄", "跨所运费 +30%，套利空间变大；边疆市场更脆弱。"},
        {kModWarEcho, "war-echo", "战火回响", "战争成本 -20%，但信誉恢复速度 -30%。"},
        {kModDataGlut, "data-glut", "数据泛滥", "内幕信号更频繁，但平均可信度下降 25%。"},
        {kModScarceAlloys, "scarce-alloys", "合金短缺", "合金基准价 +35%，巨构成本显著上升。"},
        {kModTrustDeficit, "trust-deficit", "信任赤字", "所有阵营初始观感 -20%。"},
        {kModPsionicDawn, "psionic-dawn", "灵能黎明", "灵能类道具效果 +40%，反制更难。"},
        {kModRegulatorSurge, "regulator-surge", "监管风暴", "操纵行为被查概率 +100%。"},
        {kModMemoryHole, "memory-hole", "记忆空洞", "线索可信度时间衰减加倍。"},
        {kModBoomCycle, "boom-cycle", "繁荣周期", "所有产能 +15%，同时波动率 +20%。"},
        {kModBrokenChain, "broken-chain", "断链纪元", "跨所封锁惩罚减半，套利更顺畅。"},
        {kModSilentArchive, "silent-archive", "静默档案", "chronicle 链缺失的惩罚加倍。"},
    };
    return t;
}

}  // namespace

const std::vector<ModifierInfo>& modifierTable() { return table(); }

bool hasModifier(u64 bits, u64 bit) { return (bits & bit) != 0; }

u64 rollModifiers(u64 seed, int difficulty, std::string& nameOut) {
    SplitMix64 rng(seed ^ 0x1D0DE5ull);
    u64 bits = 0;
    // 难度越高，负面词缀概率越大
    int count = 1 + static_cast<int>(rng.nextU64() % 2) + (difficulty >= 4 ? 1 : 0);
    std::string names;
    for (int i = 0; i < count; ++i) {
        const auto& m = table()[static_cast<std::size_t>(rng.nextU64() % table().size())];
        if (bits & m.bit) continue;
        bits |= m.bit;
        if (!names.empty()) names += " + ";
        names += m.nameZh;
    }
    nameOut = names.empty() ? "无词缀" : names;
    return bits;
}

}  // namespace gf
