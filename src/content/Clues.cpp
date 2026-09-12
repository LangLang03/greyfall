#include "clue/ClueDef.h"

#include <array>
#include <string>
#include <vector>

#include "util/Str.h"

namespace gf {
namespace {

constexpr const char* kClusterNames[] = {
    "沉默档案", "难民证词", "异常读数", "航线黑匣", "旧日广播", "叛逃者手记", "赌场流水", "军械清单",
    "灵能残响", "祭仪记录", "检疫通报", "矿脉采样", "拍卖名册", "边境日志", "密约抄本", "枯竭星图",
};

constexpr int kClusters = 16;
constexpr int kPerCluster = kClueCount / kClusters;  // 21（余数补在最后）

std::vector<ClueDef> buildClues() {
    std::vector<ClueDef> v;
    v.reserve(kClueCount);
    for (int i = 0; i < kClueCount; ++i) {
        ClueDef d;
        d.id = static_cast<u16>(i);
        d.act = static_cast<u8>(1 + (i % kActCount));
        d.tier = static_cast<u8>(1 + (i % 5));
        const char* cluster = kClusterNames[i % kClusters];
        d.tags[0] = static_cast<ClueTag>(i % kClueTagCount);
        d.tagCount = 1;
        if (i % 4 == 0) {
            d.tags[1] = static_cast<ClueTag>((i + 5) % kClueTagCount);
            d.tagCount = 2;
        }
        // 初始超边：同簇内相连，并跨簇指向前一幕的一个节点
        int base = (i / kPerCluster) * kPerCluster;
        if (i + 1 < (base + kPerCluster) && i + 1 < kClueCount) d.linked.push_back(static_cast<u16>(i + 1));
        if (i > base) d.linked.push_back(static_cast<u16>(i - 1));
        if (i >= kPerCluster) d.linked.push_back(static_cast<u16>(i - kPerCluster));
        if (i + kPerCluster < kClueCount) d.linked.push_back(static_cast<u16>(i + kPerCluster));
        if (i % 7 == 0 && i >= 3) d.linked.push_back(static_cast<u16>(i - 3));
        d.linkKind = (i % 11 == 0) ? ClueEdgeKind::Contradict
                                   : (i % 5 == 0 ? ClueEdgeKind::Prereq
                                                 : (i % 3 == 0 ? ClueEdgeKind::Belongs : ClueEdgeKind::Corroborate));
        (void)cluster;
        v.push_back(std::move(d));
    }
    return v;
}

const std::vector<std::string>& clueTexts() {
    static const std::vector<std::string> s = [] {
        std::vector<std::string> out;
        out.reserve(kClueCount * 3);
        // 分块布局：[0, N) idName，[N, 2N) nameZh，[2N, 3N) text
        for (int i = 0; i < kClueCount; ++i) out.push_back("clue" + std::to_string(i));
        for (int i = 0; i < kClueCount; ++i)
            out.push_back(std::string(kClusterNames[i % kClusters]) + " · 第 " + std::to_string(i % 21 + 1) +
                          " 号残片");
        for (int i = 0; i < kClueCount; ++i)
            out.push_back(std::string("关于大沉默的第 ") + std::to_string(i % kActCount + 1) +
                          " 幕证据：它指向一个被系统性抹除的观测节点，而抹除者仍在活动。");
        return out;
    }();
    return s;
}

}  // namespace

const ClueDef& clueDef(int idx) {
    static const std::vector<ClueDef> table = [] {
        std::vector<ClueDef> t = buildClues();
        const std::vector<std::string>& strs = clueTexts();
        for (int i = 0; i < kClueCount; ++i) {
            t[static_cast<std::size_t>(i)].idName = strs[static_cast<std::size_t>(i)];
            t[static_cast<std::size_t>(i)].nameZh = strs[static_cast<std::size_t>(kClueCount + i)];
            t[static_cast<std::size_t>(i)].text = strs[static_cast<std::size_t>(kClueCount * 2 + i)];
        }
        return t;
    }();
    if (idx < 0 || idx >= kClueCount) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

int clueIndexByName(std::string_view s) {
    for (int i = 0; i < kClueCount; ++i) {
        if (iequals(clueDef(i).idName, s) || clueDef(i).nameZh == s) return i;
    }
    // 允许 "clue#137" / "#137" / "137"
    std::string_view t = s;
    if (startsWith(t, "clue#")) t = t.substr(5);
    else if (startsWith(t, "clue")) t = t.substr(4);
    if (!t.empty() && (t[0] == '#' || (t[0] >= '0' && t[0] <= '9'))) {
        if (t[0] == '#') t = t.substr(1);
        i64 n = parseInt(t, -1);
        if (n >= 0 && n < kClueCount) return static_cast<int>(n);
    }
    return -1;
}

std::string_view clueEdgeKindName(ClueEdgeKind k) {
    switch (k) {
        case ClueEdgeKind::Corroborate: return "印证";
        case ClueEdgeKind::Contradict: return "矛盾";
        case ClueEdgeKind::Prereq: return "前置";
        case ClueEdgeKind::Belongs: return "归属";
        case ClueEdgeKind::About: return "指涉主体";
        case ClueEdgeKind::Count: break;
    }
    return "?";
}

ClueEdgeKind clueEdgeKindFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(ClueEdgeKind::Count); ++i)
        if (clueEdgeKindName(static_cast<ClueEdgeKind>(i)) == s || iequals(clueEdgeKindName(static_cast<ClueEdgeKind>(i)), s))
            return static_cast<ClueEdgeKind>(i);
    if (s == "corroborate") return ClueEdgeKind::Corroborate;
    if (s == "contradict") return ClueEdgeKind::Contradict;
    if (s == "prereq") return ClueEdgeKind::Prereq;
    if (s == "belongs") return ClueEdgeKind::Belongs;
    if (s == "about") return ClueEdgeKind::About;
    return ClueEdgeKind::Corroborate;
}

std::string_view clueTagName(ClueTag t) {
    switch (t) {
        case ClueTag::Silence: return "沉默";
        case ClueTag::Migrant: return "迁徙";
        case ClueTag::Signal: return "信号";
        case ClueTag::Artifact: return "造物";
        case ClueTag::Witness: return "证人";
        case ClueTag::Ruin: return "废墟";
        case ClueTag::Document: return "文书";
        case ClueTag::Rumor: return "流言";
        case ClueTag::Panopticon: return "泛视";
        case ClueTag::Origin: return "起源";
        case ClueTag::Betrayal: return "背叛";
        case ClueTag::Faction: return "派系";
        case ClueTag::Market: return "市场";
        case ClueTag::Military: return "军事";
        case ClueTag::Tech: return "技术";
        case ClueTag::Ritual: return "仪式";
        case ClueTag::Count: break;
    }
    return "?";
}

ClueTag clueTagFromName(std::string_view s) {
    for (int i = 0; i < kClueTagCount; ++i)
        if (clueTagName(static_cast<ClueTag>(i)) == s) return static_cast<ClueTag>(i);
    return ClueTag::Silence;
}

}  // namespace gf
