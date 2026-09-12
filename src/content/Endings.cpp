#include "plot/Skeleton.h"

#include <string>
#include <vector>

namespace gf {
namespace {

constexpr EndingInfo kEndings[] = {
    {0, "hegemon", "霸权结局 · 唯一的观测者",
     "你成为了泛视网络的新主人。所有舰队的位置、所有条约的底价、所有谎言的成本，都在你的桌面上一览无余。"
     "银河安静了下来——不是因为它和平，而是因为它不敢出声。",
     0},
    {1, "federation", "联邦结局 · 众声的议会",
     "你把观测权拆成了三百份，分发给了每一个曾被你读取过的阵营。协议脆弱、争吵不休、效率低下——"
     "但没有任何单一意志能再次让银河沉默。",
     1},
    {2, "capital", "资本结局 · 一切的清算价",
     "你没有赢下战争，你买下了它。当最后一份债务被偿还，你发现整个银河的航线图其实是一张资产负债表，"
     "而你是唯一的债权人。",
     2},
    {3, "species", "种族结局 · 血脉的终章",
     "你的种族成为了纪元的标准：语言、基因、历法，甚至沉默的方式。其他文明没有被消灭，"
     "它们只是慢慢地，变得和你一样。",
     3},
    {4, "knowledge", "知识结局 · 第八幕的门",
     "你读完了所有线索，包括那条不该存在的。大沉默不是灾难，而是一次交接——"
     "而现在，你知道了下一位观测者的名字。",
     4},
    {5, "faith", "信仰结局 · 沉默的语法",
     "你不再需要解释大沉默。它成了圣典的第一句，而你成了它的第一个读者。"
     "文明在共同的沉默里找到了彼此。",
     5},
    {6, "destruction", "毁灭结局 · 无人观测的星区",
     "你烧掉了泛视网络，连同它记录的一切。没有人再被观测，也没有人再记得曾经有过观测者。"
     "星图上的某一角，永远是空白。",
     6},
    {7, "transcendence", "超脱结局 · 观测者悖论",
     "你把自己上传进了观测网络，成为了那个被观测了七个纪元的存在。"
     "从内部看，全知与无知没有区别——但你已经不需要区别了。",
     7},
    {8, "deadlock", "僵局结局 · 谁都没有赢",
     "所有阵营都活了下来，所有阵营都没有达成目的。银河进入了一种稳定的、彼此监视的冷战，"
     "直到下一个纪元有人按下重启。",
     0},
    {9, "ruin", "废墟结局 · 借来的时间",
     "你赢了每一场战斗，输了整场博弈。国库见底、民心尽失、盟友尽散，"
     "留下的只有一份写在废墟上的复盘报告。",
     6},
    {10, "puppet", "傀儡结局 · 别人桌上的棋子",
     "你以为自己在读别人的账本，直到你发现自己的每一笔支出都被预测过。"
     "这个纪元的结局在你第一次读档时就已经写好了。",
     1},
    {12, "conquest", "征服结局 · 唯一的颜色",
     "所有的对手都不在了。你清点星图，发现上面只剩下一种颜色。"
     "但真正罕见的不是这件事——而是民怨、稳定、合法性与派系满意度同时停在了令人难以置信的水平上。"
     "所以这不只是征服，而是一种被广泛接受的秩序。",
     0},
    {11, "silence", "沉默结局 · 无人记录",
     "档案焚毁，哈希链断裂。没有人能证明你做过什么，也没有人愿意再和你签约。"
     "你活了下来，但你的名字从所有记录里消失了。",
     6},
};
static_assert(sizeof(kEndings) / sizeof(kEndings[0]) == 13);

}  // namespace

int endingCount() { return 13; }

const EndingInfo& endingInfo(int idx) {
    if (idx < 0 || idx >= 13) idx = 0;
    return kEndings[static_cast<std::size_t>(idx)];
}

int pickEnding(const std::array<Fixed, 8>& vec) {
    // 主导维度
    int best = 0;
    for (int i = 1; i < 8; ++i)
        if (vec[static_cast<std::size_t>(i)].rawValue() > vec[static_cast<std::size_t>(best)].rawValue()) best = i;
    Fixed bestVal = vec[static_cast<std::size_t>(best)];
    // 总量太低 → 僵局 / 废墟
    Fixed total = Fixed(0);
    for (const auto& x : vec) total += x;
    if (total < Fixed(2)) {
        // 国库与民心决定是僵局还是废墟
        return vec[static_cast<std::size_t>(6)].rawValue() > Fixed::pct(50).rawValue() ? 8 : 9;
    }
    switch (best) {
        case 0: return bestVal > Fixed(6) ? 0 : 10;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 5;
        case 6: return bestVal > Fixed(5) ? 6 : 9;
        case 7: return 7;
        default: break;
    }
    return 8;
}

}  // namespace gf
