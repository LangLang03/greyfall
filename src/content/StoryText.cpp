#include "plot/Skeleton.h"

#include <string>
#include <vector>

#include "util/Str.h"

namespace gf {
namespace {

constexpr ActInfo kActs[] = {
    {1, "第一幕 · 静默的广播",
     "大沉默之后，银河的通讯频道第一次同时安静下来。你的舰桥遥测却在同一刻变成了公开广播——"
     "有人把观测者的眼睛，装进了每个人的口袋。",
     "你确认了：沉默不是结束，而是一次协议的生效。", 2, Fixed::pct(2)},
    {2, "第二幕 · 难民潮",
     "数以百万计的难民船沿着废弃航线涌来。他们带着同一个故事：某个星区被从星图上删掉了，"
     "连同那里的居民。",
     "你发现难民的证词彼此印证，指向同一个被抹除的观测节点。", 3, Fixed::pct(3)},
    {3, "第三幕 · 断链",
     "三条主航线在同一夜断裂。官方解释是引力潮，但你的测绘船带回了截然不同的读数。",
     "你拼出了断裂航线的几何：它们围出的正是那个被抹除的星区。", 4, Fixed::pct(4)},
    {4, "第四幕 · 泛视之眼",
     "你终于看清了泛视网络：它不隐藏任何人的数据，它只是让所有人同时看见彼此——"
     "包括你此刻正在犹豫要不要读档。",
     "你理解了全知者的逻辑：可见性本身就是统治。", 5, Fixed::pct(5)},
    {5, "第五幕 · 盟友的账本",
     "你的盟友开始报出精确到小数点的价码。他们看的不是你说了什么，而是你做了什么——"
     "以及你重试过几次。",
     "你意识到：在这个纪元里，信誉是唯一无法伪造的抵押品。", 6, Fixed::pct(6)},
    {6, "第六幕 · 我们之中的沉默",
     "派系的最后通牒摆上了桌面。街垒之夜过后，你发现真正的对手不在边境，而在你的议会里。",
     "你活过了政变的清晨，但代价写在了账本上。", 7, Fixed::pct(7)},
    {7, "第七幕 · 谁在观测观测者",
     "所有线索指向同一个结论：泛视网络不是遗留物，它仍在被维护——而维护者需要一名新的观测者。",
     "你站在观测者的位置上，第一次看见了全部。", 8, Fixed::pct(9)},
};
static_assert(sizeof(kActs) / sizeof(kActs[0]) == static_cast<std::size_t>(kActCount));

std::vector<Beat> buildActBeats(int act) {
    std::vector<Beat> v;
    // 每幕 2~4 个节拍
    int n = 2 + (act % 3);
    for (int i = 0; i < n; ++i) {
        Beat b;
        b.id = static_cast<u16>(act * 10 + i);
        b.act = static_cast<u8>(act);
        b.title = std::string(actTitle(act)) + " · 节拍 " + std::to_string(i + 1);
        b.text = "节拍推进：本幕的关键事实需要至少 " + std::to_string(actInfo(act).requiredConclusions) +
                 " 个已提交结论才能解锁。";
        b.eventId = static_cast<i16>(95 + (act * 5 + i) % 40);  // 指向剧情事件池
        b.clueId = static_cast<i16>((act * 40 + i * 7) % kClueCount);
        v.push_back(std::move(b));
    }
    return v;
}

}  // namespace

int actCount() { return kActCount; }

const ActInfo& actInfo(int act) {
    if (act < 1) act = 1;
    if (act > kActCount) act = kActCount;
    return kActs[act - 1];
}

std::string_view actTitle(int act) { return actInfo(act).title; }

const std::vector<Beat>& actBeats(int act) {
    static std::vector<std::vector<Beat>> cache(kActCount + 1);
    if (act < 1) act = 1;
    if (act > kActCount) act = kActCount;
    if (cache[static_cast<std::size_t>(act)].empty()) cache[static_cast<std::size_t>(act)] = buildActBeats(act);
    return cache[static_cast<std::size_t>(act)];
}

bool hiddenActAvailable(const PlotState& plot) {
    if (plot.hiddenActUnlocked) return true;
    // 隐藏第 8 幕：集齐 7 幕全部真相结论且无误判
    int trueConclusions = 0;
    for (u16 c : plot.committedConclusions) {
        if (conclusionDef(static_cast<int>(c)).trueConclusion) ++trueConclusions;
    }
    return trueConclusions >= 12 && plot.falseConclusions.empty();
}

}  // namespace gf
