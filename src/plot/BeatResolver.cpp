// 剧情系统：幕次推进、结论解锁、结局评估、纪元报告。
//
// 这个文件曾经是 653 行的 God 文件，混合了 7 种职责。现已按职责拆分：
//   · 经济结算与研究推进 → domain/Economy.{h,cpp}
//   · 事件系统与抉择结算 → plot/EventSystem.{h,cpp}
//   · 留下的只有剧情本身：线索结论 → 幕次推进 → 结局评估
#include "plot/BeatResolver.h"

#include <algorithm>
#include <string>

#include "clue/ClueGraph.h"
#include "core/GameState.h"
#include "domain/Empire.h"
#include "mkt/MarketEngine.h"
#include "plot/Skeleton.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int evaluateEnding(const GameState& st) {
    std::array<Fixed, 8> vec = st.plot.endingVector;
    const Empire& p = st.empires[kPlayerId];
    // 由当前局面补充各维度
    vec[0] += Fixed(static_cast<i64>(p.systems.size())) / Fixed(3);
    vec[1] += p.influence / Fixed(300);
    vec[2] += p.treasury / Fixed(40000);
    vec[3] += Fixed(static_cast<i64>(p.fleets.size()));
    vec[4] += Fixed(static_cast<i64>(st.plot.committedConclusions.size())) / Fixed(2);
    vec[5] += p.unity / Fixed(300);
    vec[6] += (Fixed(1) - p.domestic.unrest) * Fixed(3);
    vec[7] += Fixed(static_cast<i64>(p.tech.completed.size())) / Fixed(8);
    // 失误惩罚
    vec[6] -= Fixed(static_cast<i64>(st.plot.falseConclusions.size())) * Fixed::pct(50);
    if (st.chronicleBurned) vec[6] -= Fixed(2);
    int id = pickEnding(vec);
    return id;
}

void plotPhase(GameState& st) {
    // 1) 结论自动解锁（只记录，不自动提交）
    for (int i = 0; i < kConclusionCount; ++i) {
        const ConclusionDef& def = conclusionDef(i);
        if (def.act != st.plot.act) continue;
        u16 id = static_cast<u16>(i);
        if (std::find(st.plot.conclusionsReached.begin(), st.plot.conclusionsReached.end(), id) !=
            st.plot.conclusionsReached.end())
            continue;
        if (!conclusionUnlockable(st, id, nullptr)) continue;
        st.plot.conclusionsReached.push_back(id);
        st.logEvent(LogPhase::Plot, kLogDeduce,
                    "结论解锁【" + std::string(def.nameZh) + "】（可用 deduce --commit 提交）", kPlayerId);
    }

    // 2) 幕次推进
    //
    // 推进口径 = max(已提交, 已解锁)。
    // 旧口径只数「玩家用 deduce --commit 主动提交的结论」，于是 7 幕剧情
    // 完全取决于玩家是否知道那个命令 —— 实测 154 季仍停在第 1 幕，
    // 且游戏内**没有任何提示**告诉玩家这一点。
    // 现在：线索凑齐后结论会自动解锁（见上一步），解锁数同样计入推进。
    // 主动提交仍有独立价值（`--commit` 会立刻引发市场冲击与信誉变化，
    // 并且是触发剧情结局与「误判」分支的唯一途径），但不再是主线前进的独木桥。
    const ActInfo& act = actInfo(static_cast<int>(st.plot.act));
    int committed = 0;
    for (u16 c : st.plot.committedConclusions)
        if (conclusionDef(static_cast<int>(c)).act == st.plot.act) ++committed;
    int reached = 0;
    for (u16 c : st.plot.conclusionsReached)
        if (conclusionDef(static_cast<int>(c)).act == st.plot.act) ++reached;
    const int progress = std::max(committed, reached);
    if (progress >= act.requiredConclusions && st.plot.act < kActCount) {
        ++st.plot.act;
        st.act = st.plot.act;
        st.logEvent(LogPhase::Plot, kLogAct,
                    "幕次推进 → " + std::string(actTitle(static_cast<int>(st.plot.act))), kPlayerId);
        // 幕推进引发市场冲击
        marketApplyShock(st, static_cast<u8>(Commodity::DataCrystals), act.marketImpact, "剧情推进");
        st.plot.endingVector[4] += Fixed::pct(30) * act.marketImpact;
    }

    // 3) 隐藏第 8 幕
    if (!st.plot.hiddenActUnlocked && hiddenActAvailable(st.plot)) {
        st.plot.hiddenActUnlocked = true;
        st.logEvent(LogPhase::Plot, kLogAct, "【隐藏幕解锁】第八幕的门开启了 —— 观测者悖论", kPlayerId);
    }

    // 4) 剧情结局由实际提交的结论触发；征服胜利在季末独立判定。
    int committedAll = static_cast<int>(st.plot.committedConclusions.size());
    if (!st.ended && !st.victory.achieved && committedAll >= 40) {
        st.endingId = static_cast<u8>(evaluateEnding(st));
        st.ended = true;
        const EndingInfo& en = endingInfo(st.endingId);
        st.logEvent(LogPhase::Plot, kLogEnding,
                    "【结局】" + std::string(en.nameZh) + "：" + std::string(en.text), kPlayerId);
    }
    if (st.endless && st.ended) {
        st.ended = false;   // 无尽模式继续
    }
}

/// epoch --report 的文本。
std::string epochReport(const GameState& st) {
    std::string out;
    out += "═══ 纪元报告 ═══\n";
    out += "纪元 #" + std::to_string(st.epochIndex) + "：" + st.epochName + "\n";
    out += "词缀：" + st.modifierName + "\n";
    out += "回合：" + std::to_string(st.tick) + " 季    当前幕：第 " + std::to_string(static_cast<int>(st.plot.act)) +
           " 幕 / " + std::to_string(kActCount) + "\n";
    out += "难度：" + std::to_string(st.difficulty) + "    AI 前瞻：" + std::to_string(st.aiForesight) + " 季\n";
    out += "读档次数：" + std::to_string(st.rollbackCount) + "    chronicle 链头：" +
           std::to_string(st.chronicleHead) + "\n\n";

    out += "结局向量（8 维）：\n";
    static const char* kDims[] = {"霸权", "联邦", "资本", "种族", "知识", "信仰", "毁灭", "超脱"};
    for (int i = 0; i < 8; ++i) {
        Fixed v = st.plot.endingVector[static_cast<std::size_t>(i)];
        out += "  " + padRight(kDims[i], 6) + " " + bar(v / Fixed(8), 24) + " " + fixedStrPlain(v, 2) + "\n";
    }
    if (st.ended) {
        const EndingInfo& en = endingInfo(st.endingId);
        out += "\n" + style("结局：" + std::string(en.nameZh), Style::Heading) + "\n";
        out += wrapJoin(en.text, 86, "  ") + "\n";
    } else {
        // 这里曾经写「或推进到 tick 200」—— 全代码库**不存在任何按 tick 的结局判定**，
        // 那句提示是假的（实测推到 245 季仍无结局），与 TUTORIAL.md 也互相矛盾。
        // 现在给出真实的缺口。
        const ActInfo& act = actInfo(static_cast<int>(st.plot.act));
        int progress = 0;
        for (u16 c : st.plot.conclusionsReached)
            if (conclusionDef(static_cast<int>(c)).act == st.plot.act) ++progress;
        for (u16 c : st.plot.committedConclusions)
            if (conclusionDef(static_cast<int>(c)).act == st.plot.act) ++progress;
        out += "\n（结局尚未达成：本幕第 " + std::to_string(static_cast<int>(st.plot.act)) + " 幕需 " +
               std::to_string(act.requiredConclusions) + " 项结论，已完成 " +
               std::to_string(std::min(progress, act.requiredConclusions)) +
               " 项；线索凑齐后会自动解锁并推进，也可用 `deduce --commit` 主动提交）\n";
    }
    out += "\n已提交结论：" + std::to_string(st.plot.committedConclusions.size()) + "    误判：" +
           std::to_string(st.plot.falseConclusions.size()) + "    已发现线索：" +
           std::to_string(st.plot.knownClues.size()) + "\n";
    if (st.plot.hiddenActUnlocked) out += style("隐藏第八幕已解锁\n", Style::Accent);
    return out;
}

}  // namespace gf
