#include "clue/ClueDef.h"

#include <string>
#include <vector>

#include "util/Str.h"

namespace gf {
namespace {

constexpr const char* kActTitles[] = {
    "第一幕 · 静默的广播", "第二幕 · 难民潮",       "第三幕 · 断链",
    "第四幕 · 泛视之眼",   "第五幕 · 盟友的账本",   "第六幕 · 我们之中的沉默",
    "第七幕 · 谁在观测观测者",
};

constexpr const char* kActNames[] = {
    "沉默降临", "迁徙与谣言", "航线断裂", "全知者的注视", "背叛的账本", "内部回声", "观测者悖论",
};

// 每幕 12 个结论，共 84
std::vector<ConclusionDef> buildConclusions() {
    std::vector<ConclusionDef> v;
    v.reserve(kConclusionCount);
    for (int act = 0; act < kActCount; ++act) {
        for (int k = 0; k < 12; ++k) {
            int id = act * 12 + k;
            ConclusionDef d;
            d.id = static_cast<u16>(id);
            d.act = static_cast<u8>(act + 1);
            d.rewardClue = static_cast<u16>((id * 3 + 7) % kClueCount);
            d.rewardItem = static_cast<u16>((id * 5 + 11) % 130);
            d.rewardTech = static_cast<i16>((id % 96));
            d.trueConclusion = (k % 7 != 3);  // 少数结论是"看似真相"的陷阱
            d.requireDiversity = true;
            d.requireInsider = (act >= 2);
            d.marketImpact = Fixed::pct(2 + (id % 9));
            // 合取范式：2~3 个子句，每子句 2~3 个候选线索（来自该幕的 48 个节点）
            int actBase = act * (kClueCount / kActCount);
            int actSpan = kClueCount / kActCount;
            int clauseCount = 2 + (id % 2);
            for (int c = 0; c < clauseCount; ++c) {
                std::vector<u16> clause;
                int picks = 2 + ((id + c) % 2);
                for (int p = 0; p < picks; ++p) {
                    int node = actBase + ((id * 7 + c * 13 + p * 5) % actSpan);
                    if (node < 0 || node >= kClueCount) node = 0;
                    clause.push_back(static_cast<u16>(node));
                }
                d.clauses.push_back(std::move(clause));
            }
            v.push_back(std::move(d));
        }
    }
    return v;
}

const std::vector<std::string>& conclusionStrings() {
    static const std::vector<std::string> s = [] {
        std::vector<std::string> out;
        out.reserve(kConclusionCount * 3);
        for (int i = 0; i < kConclusionCount; ++i) out.push_back("C-" + std::to_string(i));
        for (int i = 0; i < kConclusionCount; ++i) {
            int act = i / 12;
            int k = i % 12;
            out.push_back(std::string(kActNames[act]) + " · 推断 " + std::to_string(k + 1));
        }
        for (int i = 0; i < kConclusionCount; ++i) {
            int act = i / 12;
            out.push_back(std::string("你相信：") + kActTitles[act] +
                          " 的真相并非表面所述。提交此结论将改写他人对你的先验，并立刻反映在价格上。");
        }
        return out;
    }();
    return s;
}

}  // namespace

const ConclusionDef& conclusionDef(int idx) {
    static const std::vector<ConclusionDef> table = [] {
        std::vector<ConclusionDef> t = buildConclusions();
        const std::vector<std::string>& strs = conclusionStrings();
        for (int i = 0; i < kConclusionCount; ++i) {
            t[static_cast<std::size_t>(i)].idName = strs[static_cast<std::size_t>(i)];
            t[static_cast<std::size_t>(i)].nameZh = strs[static_cast<std::size_t>(kConclusionCount + i)];
            t[static_cast<std::size_t>(i)].text = strs[static_cast<std::size_t>(kConclusionCount * 2 + i)];
        }
        return t;
    }();
    if (idx < 0 || idx >= kConclusionCount) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

int conclusionIndexByName(std::string_view s) {
    for (int i = 0; i < kConclusionCount; ++i) {
        if (iequals(conclusionDef(i).idName, s) || conclusionDef(i).nameZh == s) return i;
    }
    std::string_view t = s;
    if (startsWith(t, "C-")) t = t.substr(2);
    i64 n = parseInt(t, -1);
    if (n >= 0 && n < kConclusionCount) return static_cast<int>(n);
    return -1;
}

std::string_view endingDimName(int dim) {
    switch (dim) {
        case 0: return "霸权";
        case 1: return "联邦";
        case 2: return "资本";
        case 3: return "种族";
        case 4: return "知识";
        case 5: return "信仰";
        case 6: return "毁灭";
        case 7: return "超脱";
        default: break;
    }
    return "?";
}

}  // namespace gf
