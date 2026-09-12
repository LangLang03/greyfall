// 增益与减益总览：把分散在各系统的修正集中到一处，附效果与描述
#include <algorithm>

#include "ai/FactionAI.h"
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/GameState.h"
#include "core/ResolutionEngine.h"
#include "domain/CasusBelli.h"
#include "domain/Empire.h"
#include "domain/Parliament.h"
#include "domain/Policy.h"
#include "gen/EmpireGen.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdEffects(CliEnv& env, const Args& args) {
    (void)args;
    env.loadState();
    const Empire& e = env.player();

    out(style("═══ 增益与减益总览 ═══", Style::Heading));
    out("  列出当前全部生效中的修正。正值为增益，负值为减益。");
    out("");

    auto showMods = [&](const char* title, const char* desc) {
        TextTable t;
        t.header({"属性", "修正", "来源"});
        bool any = false;
        static const struct {
            ModKind k;
            const char* name;
        } kinds[] = {
            {ModKind::ResearchRate, "研究速率"}, {ModKind::BuildRate, "建造速率"},
            {ModKind::TradeMargin, "贸易毛利"},  {ModKind::MilitaryPower, "军事力量"},
            {ModKind::Stability, "稳定度"},      {ModKind::Unrest, "民怨"},
            {ModKind::InfluenceGain, "影响力增益"}, {ModKind::CreditRating, "信用评级"},
            {ModKind::DiploWeight, "外交权重"},     {ModKind::Growth, "人口增长"},
            {ModKind::IntelDefense, "情报防御"},  {ModKind::Detection, "侦测"},
            {ModKind::MarketFee, "市场费率"},     {ModKind::ColonyCost, "殖民成本"},
            {ModKind::ManipulationSkill, "操纵技巧"},
        };
        for (const auto& kd : kinds) {
            Fixed v = empireModifier(e, kd.k);
            if (v.rawValue() == 0) continue;
            any = true;
            t.row({kd.name, (v.rawValue() > 0 ? "+" : "") + fixedStrPlain(v * Fixed(100), 1) + "%",
                   "科技 / 政策 / 法令 / 飞升 / 决议 / 种族 / 领袖 合计"});
        }
        if (!any) return;
        out(style(title, Style::Sub));
        out(std::string("  ") + desc);
        out(t.render());
        out("");
    };
    showMods("属性修正合计", "聚合自所有来源；用 `tech` / `policy` / `resolve` 查看各自的明细。");

    out(nationalEdictReport(env.st, kPlayerId));
    for (int i = 0; i < 5; ++i)
        if (e.ascensions & (1u << i)) out("  已完成飞升：" + std::string(ascensionName(i)));

    // 决议：生效中的持续效果
    {
        TextTable t;
        t.header({"来源", "效果", "剩余"});
        bool any = false;
        for (const auto& a : e.resolutions.active) {
            if (a.ticksLeft == 0) continue;
            const ResolutionDef& d = resolutionDef(static_cast<int>(a.defId));
            any = true;
            t.row({std::string(d.nameZh), resEffectText(d.onTick), std::to_string(a.ticksLeft) + " 季"});
        }
        if (any) {
            out(style("决议（改革）", Style::Sub));
            out("  改革同时只能推行一项；持续 24 季。");
            out(t.render());
            out("");
        }
    }

    // 政策
    {
        TextTable t;
        t.header({"政策组", "政策", "过渡剩余"});
        bool any = false;
        for (int g = 0; g < kPolicyGroupCount; ++g) {
            u8 active = e.policies.active[static_cast<std::size_t>(g)];
            u8 pending = e.policies.pending[static_cast<std::size_t>(g)];
            u8 pid = (pending != kNoPolicy) ? pending : active;
            if (pid == kNoPolicy) continue;
            const PolicyOption* po = policyOptionById(pid);
            if (po == nullptr) continue;
            any = true;
            u8 trans = e.policies.transitionLeft[static_cast<std::size_t>(g)];
            t.row({std::string(policyGroupName(static_cast<PolicyGroup>(g))), std::string(po->nameZh),
                   trans > 0 ? (std::to_string(trans) + " 季过渡") : std::string("已生效")});
        }
        if (any) {
            out(style("政策", Style::Sub));
            out("  持久化法令，五组各选其一；过渡期内效果逐步显现。");
            out(t.render());
            out("");
        }
    }

    // 战争疲劳
    {
        Fixed st1 = wearinessStabilityPenalty(env.st, kPlayerId);
        Fixed un1 = wearinessUnrestPenalty(env.st, kPlayerId);
        Fixed sa1 = wearinessSatisfactionPenalty(env.st, kPlayerId);
        if (st1.rawValue() != 0 || un1.rawValue() != 0 || sa1.rawValue() != 0) {
            TextTable t;
            t.header({"减益", "数值", "说明"});
            t.row({"稳定度", fixedStrPlain(st1 * Fixed(100), 0) + "%",
                   "战争疲劳超过 40% 后开始扣稳定；停战即消除"});
            t.row({"民怨", "+" + fixedStrPlain(un1 * Fixed(100), 0) + "%",
                   "同上"});
            if (sa1.rawValue() != 0)
                t.row({"派系满意度", fixedStrPlain(-sa1 * Fixed(100), 0) + "%",
                       "派系已公开要求停战，你却继续作战"});
            out(style("战争疲劳减益", Style::Sub));
            out("  这是反战情绪的体现。用 `greyfall casus` 查看各场战争的疲劳度，");
            out("  用 `envoy <emp> plead-peace` 求和即可消除。");
            out(t.render());
            out("");
        }
    }

    // 政体与种族
    {
        TextTable t;
        t.header({"项目", "值", "说明"});
        t.row({"政体", std::string(governmentInfo(e.government).nameZh),
               "影响议会门槛、民怨倾向与战争疲劳敏感度"});
        const SpeciesInfo& si = speciesInfo(static_cast<int>(e.species));
        std::string traits;
        for (u8 i = 0; i < si.modCount && i < si.mods.size(); ++i) {
            const Modifier& m = si.mods[i];
            if (!traits.empty()) traits += "、";
            traits += std::string(modKindName(m.kind)) + " " +
                      (m.value.rawValue() > 0 ? "+" : "") + fixedStrPlain(m.value * Fixed(100), 0) + "%";
        }
        t.row({"种族", std::string(si.nameZh), traits.empty() ? "无特质" : traits});
        out(style("政体与种族", Style::Sub));
        out(t.render());
        out("");
    }

    out("  提示：`greyfall effects --detail <属性>` 可查看某条修正的完整来源分解（待实现）。");
    return 0;
}

}  // namespace gf
