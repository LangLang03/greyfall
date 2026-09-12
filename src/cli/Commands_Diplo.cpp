#include <algorithm>
#include <string>

#include "ai/AiCore.h"
#include "ai/Negotiation.h"
#include "gen/EmpireGen.h"
#include "ai/BetrayalCalculus.h"
#include "ai/FederationVote.h"
#include "ai/FraudDetect.h"
#include "ai/IntentPredictor.h"
#include "ai/OmniscientReader.h"
#include "ai/PowerBalancing.h"
#include "ai/Reputation.h"
#include "ai/ToModel.h"
#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "core/TickPipeline.h"
#include "domain/Fog.h"
#include "domain/Treaty.h"
#include "clue/ClueGraph.h"
#include "items/RuleEngine.h"
#include "mkt/Insider.h"
#include "mkt/ManipulationDetect.h"
#include "mkt/OrderBook.h"
#include "rng/Streams.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

const Empire& requireEmpire(const GameState& st, const Args& args, std::size_t idx, const char* usage) {
    if (args.posCount() <= idx) fail(ExitCode::BadArgs, std::string("用法：") + usage);
    // 支持编号或名称
    std::string key = args.pos(idx);
    i64 id = parseInt(key, -1);
    if (id >= 0 && id < static_cast<i64>(st.empires.size())) return st.empires[static_cast<std::size_t>(id)];
    for (const auto& e : st.empires)
        if (e.name == key) return e;
    fail(ExitCode::BadArgs, "找不到帝国【" + key + "】（可用编号或全名）");
}

}  // namespace

int cmdEnvoy(CliEnv& env, const Args& args) {
    env.loadState();
    const Empire& target = requireEmpire(env.st, args, 0, "greyfall envoy <emp> <action> [--terms ...]");
    if (target.isPlayer) fail(ExitCode::IllegalAction, "不能对自己派遣使节");
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "缺少外交动作");
    std::string action = toLower(args.pos(1));
    if (env.player().apLeft < apcost::kEnvoy) {
        fail(ExitCode::IllegalAction, "行动点不足（需要 " + std::to_string(apcost::kEnvoy) + "）");
    }
    Fixed terms = args.has("terms") ? args.getFixed("terms", Fixed::pct(10)) : Fixed::pct(10);
    RuleResult rules = ruleEngine(env.st);
    Fixed conceded = Fixed(0);

    Relation& rel = env.st.relation(kPlayerId, target.id);
    std::string message;

    if (action == "propose-trade" || action == "propose-pact") {
        Treaty t;
        t.kind = (action == "propose-trade") ? TreatyKind::TradePact : TreatyKind::NonAggression;
        t.a = kPlayerId;
        t.b = target.id;
        t.signedTick = env.st.tick;
        t.expireTick = static_cast<i64>(env.st.tick) + 12;
        t.terms = terms;
        // 接受概率：观感 + 实力差 + 谈判折价 + 信誉
        Fixed accept = Fixed::pct(40) + rel.opinion * Fixed::pct(30) +
                       reputationOf(env.st, target.id, kPlayerId) * Fixed::pct(20) + rules.negotiationDiscount +
                       rules.diplomacyWeight;
        accept -= env.st.empires[target.id].mind.playerModel.modelConfidence * Fixed::pct(15);
        // 读档者被抬高要价
        accept -= Fixed(static_cast<i64>(env.st.rollbackCount)) * Fixed::pct(8);
        if (env.st.chronicleBurned) accept -= Fixed::pct(30);
        conceded = terms;
        if (env.st.rng.chance(RngStream::Diplo, fxClamp(accept, Fixed(0), Fixed(1)))) {
            upsertTreaty(env.st.treaties, t);
            rel.opinion = fxClamp(rel.opinion + Fixed::pct(5), Fixed(-1), Fixed(1));
            message = std::string("接受") + (t.kind == TreatyKind::TradePact ? "贸易协定" : "互不侵犯条约");
        } else {
            message = "拒绝（接受概率仅 " + fixedStrPlain(accept * Fixed(100), 1) + "%）";
            rel.opinion = fxClamp(rel.opinion - Fixed::pct(2), Fixed(-1), Fixed(1));
        }
        env.st.logEvent(LogPhase::Model, kLogEnvoy,
                        "向 " + target.name + " 提议" + message + "；谈判折价 " +
                            fixedStrPlain(rules.negotiationDiscount, 2),
                        kPlayerId);
    } else if (action == "breach") {
        // 毁约：立即获得短期收益，长期信誉崩塌
        removeTreaty(env.st.treaties, TreatyKind::TradePact, kPlayerId, target.id);
        removeTreaty(env.st.treaties, TreatyKind::NonAggression, kPlayerId, target.id);
        Fixed grab = env.st.empires[target.id].treasury * Fixed::pct(10);
        env.player().treasury += grab;
        env.st.market.margin.cash = env.player().treasury;
        env.st.empires[target.id].treasury -= grab;
        rel.opinion = fxClamp(rel.opinion - Fixed::pct(50), Fixed(-1), Fixed(1));
        env.player().betrayalsCommitted += 1;
        reputationUpdateAll(env.st, kPlayerId, -Fixed::pct(25));
        for (auto& e : env.st.empires)
            if (e.id != kPlayerId) e.mind.playerModel.typeBelief[static_cast<std::size_t>(ActorType::Exploit)] += Fixed::pct(15);
        message = "毁约成功，夺取 " + fixedStr(grab, 1) + " cr —— 全体观感 -25%，AI 将你判为剥削型";
        env.st.logEvent(LogPhase::Model, kLogEnvoy, message, kPlayerId, grab);
    } else if (action == "demand-tribute") {
        Fixed power = env.player().powerIndex();
        Fixed theirs = env.st.empires[target.id].powerIndex();
        Fixed success = Fixed::pct(20) + (power - theirs) * Fixed(2) + rules.diplomacyWeight;
        if (env.st.rng.chance(RngStream::Diplo, fxClamp(success, Fixed(0), Fixed::pct(90)))) {
            Fixed tribute = env.st.empires[target.id].treasury * terms;
            env.st.empires[target.id].treasury -= tribute;
            env.player().treasury += tribute;
            env.st.market.margin.cash = env.player().treasury;
            rel.opinion = fxClamp(rel.opinion - Fixed::pct(15), Fixed(-1), Fixed(1));
            message = "索取成功：" + fixedStr(tribute, 1) + " cr";
        } else {
            rel.opinion = fxClamp(rel.opinion - Fixed::pct(8), Fixed(-1), Fixed(1));
            message = "被拒绝（成功率 " + fixedStrPlain(success * Fixed(100), 1) + "%）";
        }
        env.st.logEvent(LogPhase::Model, kLogEnvoy, "向 " + target.name + " 索贡：" + message, kPlayerId);
    } else if (action == "joint-intel") {
        if (!itemGateOpen(env.st, "spy.read-mind")) {
            fail(ExitCode::IllegalAction, itemGateReason("spy.read-mind"));
        }
        // 联合情报：共享线索，双方可信度都提高
        if (!env.st.plot.knownClues.empty()) {
            u16 pick = env.st.plot.knownClues[env.st.rng.pick(RngStream::Diplo, env.st.plot.knownClues.size())];
            env.st.clues[pick].credibility = fxClamp(env.st.clues[pick].credibility + Fixed::pct(10), Fixed(0), Fixed(1));
            env.st.empires[target.id].mind.reputation[kPlayerId] += Fixed::pct(5);
            message = "已与 " + target.name + " 交换情报，线索 " + std::string(clueDef(pick).idName) +
                      " 可信度 +10%";
        } else {
            message = "你还没有可用于交换的线索";
        }
        env.st.logEvent(LogPhase::Model, kLogEnvoy, message, kPlayerId);
    } else if (action == "sanction") {
        Relation& r2 = env.st.relation(kPlayerId, target.id);
        r2.embargo = true;
        env.st.empires[target.id].treasury -= env.st.empires[target.id].treasury * Fixed::pct(5);
        for (auto& e : env.st.empires)
            if (e.id != kPlayerId && e.id != target.id) e.addOpinion(kPlayerId, Fixed::pct(-2));
        message = "已对 " + target.name + " 实施制裁（其国库 -5%，第三方观感 -2%）";
        env.st.logEvent(LogPhase::Model, kLogEnvoy, message, kPlayerId);
    } else if (action == "condemn") {
        // 谴责：公开指责对方的提案或行为。
        // 即使对方的提案是「合理」的，你也可以谴责 —— 这是政治姿态，
        // 代价是关系恶化与信誉损耗，收益是国内民意与第三方观感。
        Relation& r2 = env.st.relation(kPlayerId, target.id);
        r2.opinion = fxClamp(r2.opinion - Fixed::pct(12), Fixed(-1), Fixed(1));
        Relation& rf = env.st.relation(target.id, kPlayerId);
        rf.opinion = fxClamp(rf.opinion - Fixed::pct(8), Fixed(-1), Fixed(1));
        // 第三方观感：谴责会被视为强硬姿态，部分国家认同，部分反感
        for (auto& e : env.st.empires) {
            if (e.id == kPlayerId || e.id == target.id || !e.alive) continue;
            Fixed sameStance = (e.stance == env.player().stance) ? Fixed::pct(2) : Fixed::pct(-2);
            e.setOpinion(kPlayerId, fxClamp(e.opinionOf(kPlayerId) + sameStance, Fixed(-1), Fixed(1)));
        }
        // 国内：强硬姿态取悦军部与民粹，激怒商会（贸易受损）与劳工（怕征兵）
        for (auto& f : env.player().domestic.factions) {
            if (f.kind == FactionKind::Military || f.kind == FactionKind::Populist)
                f.satisfaction = fxClamp(f.satisfaction + Fixed::pct(5), Fixed(0), Fixed(1));
            if (f.kind == FactionKind::Merchant || f.kind == FactionKind::Labor)
                f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(4), Fixed(0), Fixed(1));
        }
        message = "已公开谴责 " + target.name + "（对方观感 -12%，我方信誉 -8%，军部/民粹满意 +5%，商会/劳工 -4%）";
        env.st.logEvent(LogPhase::Model, kLogEnvoy, message, kPlayerId);
    } else if (action == "fabricate") {
        std::string msg;
        if (!fabricateClaim(env.st, kPlayerId, target.id, &msg)) {
            fail(ExitCode::IllegalAction, msg);
        }
        env.player().apLeft -= apcost::kEnvoy;
        env.commit("fabricate");
        out(style(msg, Style::Warn));
        return 0;
    } else if (action == "declare-war") {
        // 宣战必须持有正当理由。这是「开局零伤亡打全图」的制度性闸门：
        // 早期可以随时对任何国家开战，最优解因此永远是尽快开打。
        const CasusBelli* cb = findCasusBelli(env.st, kPlayerId, target.id);
        if (cb == nullptr && !args.has("no-casus")) {
            out(style("没有正当的战争理由。", Style::Bad));
            out("  对 " + target.name + " 开战需要 casus belli。当前没有。");
            out("");
            out("  获取途径：");
            out("    · `greyfall envoy " + std::to_string(target.id) +
                " fabricate`  花 400 影响力伪造宣称（30 季后失效）");
            out("    · 领土争端：对方持有与你接壤的星系 —— 每 5 季自动评估");
            out("    · 反制制裁 / 盟友受侵 / 边界摩擦");
            out("");
            out("  查看当前持有的全部理由：`greyfall casus`");
            out("  若执意开战（会遭全体第三方谴责、国内反弹）：加 --no-casus");
            return static_cast<int>(ExitCode::NoFill);
        }
        declareWar(env.st, kPlayerId, target.id, true);
        env.player().lastWarTick = static_cast<u32>(env.st.tick);
        if (cb != nullptr) {
            message = "已向 " + target.name + " 宣战（理由：" +
                      std::string(casusBelliName(cb->kind)) + "）";
        } else {
            // 无理由强行开战：第三方谴责 + 国内反弹
            for (auto& o : env.st.empires) {
                if (o.id == kPlayerId || o.id == target.id || !o.alive) continue;
                o.setOpinion(kPlayerId, fxClamp(o.opinion[kPlayerId] - Fixed::pct(20), Fixed(-1), Fixed(1)));
            }
            env.player().stability = fxClamp(env.player().stability - Fixed::pct(8), Fixed(0), Fixed(1));
            for (auto& f : env.player().domestic.factions) {
                if (f.kind == FactionKind::Merchant || f.kind == FactionKind::Labor)
                    f.satisfaction = fxClamp(f.satisfaction - Fixed::pct(8), Fixed(0), Fixed(1));
            }
            message = "已向 " + target.name + " 宣战（**无正当理由**：第三方观感 -20%，稳定 -8%）";
        }
        env.st.logEvent(LogPhase::Combat, kLogWar, message, kPlayerId);
    } else if (action == "plead-peace") {
        // 战争疲劳让求和更容易被接受
        Fixed fatigueBonus = Fixed(0);
        for (const auto& w : env.st.empires[target.id].weariness)
            if (w.enemy == kPlayerId) fatigueBonus = w.value * Fixed::pct(40);
        Fixed odds = Fixed::pct(50) - rel.warScore * Fixed::pct(15) + rules.diplomacyWeight + fatigueBonus;
        if (env.st.rng.chance(RngStream::Diplo, fxClamp(odds, Fixed::pct(5), Fixed::pct(95)))) {
            declareWar(env.st, kPlayerId, target.id, false);
            rel.warScore = 0;
            message = "对方接受停火";
        } else {
            message = "对方拒绝停火（当前战果 " + std::to_string(rel.warScore) + "）";
        }
        env.st.logEvent(LogPhase::Combat, kLogWar, "向 " + target.name + " 求和：" + message, kPlayerId);
    } else if (action == "vassalize") {
        Fixed odds = Fixed::pct(10) + (env.player().powerIndex() - env.st.empires[target.id].powerIndex()) * Fixed(3);
        if (env.st.rng.chance(RngStream::Diplo, fxClamp(odds, Fixed(0), Fixed::pct(80)))) {
            Treaty t;
            t.kind = TreatyKind::Vassalage;
            t.a = kPlayerId;
            t.b = target.id;
            t.signedTick = env.st.tick;
            t.expireTick = -1;
            upsertTreaty(env.st.treaties, t);
            message = "已使 " + target.name + " 成为附庸";
        } else {
            message = "拒绝称臣（成功率 " + fixedStrPlain(odds * Fixed(100), 1) + "%）";
            rel.opinion -= Fixed::pct(10);
        }
        env.st.logEvent(LogPhase::Model, kLogEnvoy, message, kPlayerId);
    } else if (action == "federate-join" || action == "federate-leave") {
        if (action == "federate-join") {
            if (env.st.federations.empty()) fail(ExitCode::IllegalAction, "本纪元还没有联邦");
            std::string err;
            FederalMotion m = proposeMotion(env.st, 0, VoteSubject::AdmitMember, kPlayerId, target.id);
            (void)resolveMotion(env.st, 0, m);
            env.st.federations[0].motions.back() = m;
            message = m.passed ? "联邦表决通过，你已加入" : "联邦表决否决了你的入盟申请";
        } else {
            if (env.player().federation == 0xFFFFFFFFu) fail(ExitCode::IllegalAction, "你不在任何联邦中");
            u32 fid = env.player().federation;
            env.player().federation = 0xFFFFFFFFu;
            auto& members = env.st.federations[fid].members;
            members.erase(std::remove(members.begin(), members.end(), kPlayerId), members.end());
            message = "已退出联邦（凝聚力下降）";
            env.st.federations[fid].cohesion -= Fixed::pct(15);
        }
        env.st.logEvent(LogPhase::Federation, "fed.player", message, kPlayerId);
    } else if (action == "vote") {
        if (args.posCount() < 3) fail(ExitCode::BadArgs, "用法：envoy <emp> vote <yes|no> [--motion N]");
        bool yes = toLower(args.pos(2)) == "yes";
        u32 motionId = static_cast<u32>(args.getInt("motion", 0));
        std::string err;
        if (!playerVote(env.st, motionId, yes, &err)) fail(ExitCode::IllegalAction, err);
        message = std::string("已投票：") + (yes ? "赞成" : "反对");
    } else {
        fail(ExitCode::BadArgs,
             "未知外交动作【" + action +
                 "】。可用：propose-trade|propose-pact|breach|demand-tribute|joint-intel|sanction|"
                 "declare-war|plead-peace|vassalize|federate-join|federate-leave|condemn|vote");
    }

    env.player().apLeft -= apcost::kEnvoy;
    // 对方更新对你的模型
    for (auto& e : env.st.empires) {
        if (e.isPlayer || e.id != target.id) continue;
        e.mind.playerModel.observations += 1;
        e.setOpinion(kPlayerId, rel.opinion);
    }
    (void)conceded;
    env.commit("envoy");
    out("外交结果：" + message);
    out("  AP 剩余 " + std::to_string(env.player().apLeft) + "/" + std::to_string(env.player().apMax) +
        "    谈判折价 " + fixedStrPlain(rules.negotiationDiscount, 2) + "    外交权重 " +
        fixedStrPlain(rules.diplomacyWeight, 2));
    return 0;
}

int cmdSpy(CliEnv& env, const Args& args) {
    env.loadState();
    const Empire& target = requireEmpire(env.st, args, 0, "greyfall spy <emp> --mission <m> --budget n");
    if (target.isPlayer) fail(ExitCode::IllegalAction, "不能对自己派遣间谍");
    std::string mission = toLower(args.get("mission", "intel"));
    i64 budget = args.getInt("budget", 3);
    if (budget <= 0) fail(ExitCode::BadArgs, "--budget 必须为正");
    if (env.player().apLeft < apcost::kSpy) {
        fail(ExitCode::IllegalAction, "行动点不足（需要 " + std::to_string(apcost::kSpy) + "）");
    }
    // 门槛检查
    if (mission == "read-mind" && !itemGateOpen(env.st, "spy.read-mind")) {
        fail(ExitCode::IllegalAction, itemGateReason("spy.read-mind"));
    }
    if (mission == "forgery" && !itemGateOpen(env.st, "clue.forge")) {
        fail(ExitCode::IllegalAction, itemGateReason("clue.forge"));
    }

    Fixed cost = Fixed(budget * 2000);
    if (env.st.market.margin.cash.rawValue() < cost.rawValue()) {
        fail(ExitCode::IllegalAction, "预算不足：需要 " + fixedStr(cost, 0));
    }
    env.st.market.margin.cash -= cost;
    env.player().treasury = env.st.market.margin.cash;

    // 失败概率：目标的反间谍与侦测
    Fixed detect = target.intelDefense + target.counterIntel +
                   empireModifier(target, ModKind::Detection);
    RuleResult rules = ruleEngine(env.st);
    Fixed success = fxClamp(Fixed::pct(75) + rules.intelGain - detect * Fixed::pct(60) +
                                Fixed(budget) * Fixed::pct(3),
                            Fixed::pct(5), Fixed::pct(97));
    bool ok = env.st.rng.chance(RngStream::Diplo, success);
    env.player().apLeft -= apcost::kSpy;

    out(style("═══ 间谍行动：" + mission + " → " + target.name + " ═══", Style::Heading));
    out("成功率 " + fixedStrPlain(success * Fixed(100), 1) + "%（对方反间谍 " + fixedStrPlain(detect, 2) + "）");
    out("");

    if (!ok) {
        // 被抓获：观感下降 + 对方获得你的模型信息
        Relation& rel = env.st.relation(kPlayerId, target.id);
        rel.opinion = fxClamp(rel.opinion - Fixed::pct(15), Fixed(-1), Fixed(1));
        for (auto& e : env.st.empires)
            if (e.id != kPlayerId) e.mind.playerModel.modelConfidence += Fixed::pct(10);
        env.st.logEvent(LogPhase::Model, kLogSpy, "间谍行动【" + mission + "】被 " + target.name + " 破获", kPlayerId);
        env.commit("spy");
        out(style("行动失败：网络被破获。", Style::Bad));
        out("  后果：对方观感 -15%，全体 AI 对你的 modelConfidence +10%。");
        return 0;
    }

    if (mission == "intel") {
        out(spyReport(env.st, target.id, static_cast<int>(budget)));
    } else if (mission == "read-mind") {
        out(toModelReport(env.st, target.id, kPlayerId));
        out("");
        out(style("镜像博弈：你现在看到了它的 θ。可以用 decoy 注入错误梯度。", Style::Accent));
    } else if (mission == "insider") {
        out(insiderReport(env.st, target.id));
    } else if (mission == "sabotage") {
        Fixed damage = Fixed(budget * 300) * (Fixed(1) - detect * Fixed::pct(30));
        env.st.empires[target.id].military -= damage;
        env.st.empires[target.id].treasury -= Fixed(budget * 1500);
        out("破坏成功：其军力 -" + fixedStr(damage, 0) + "，国库 -" + fixedStr(Fixed(budget * 1500), 0));
        env.st.logEvent(LogPhase::Model, kLogSpy, "对 " + target.name + " 的破坏行动成功", kPlayerId);
    } else if (mission == "exfil") {
        // 窃取情报换取线索
        Provenance p;
        p.channel = ProvChannel::SpyNetwork;
        p.credibility = Fixed::pct(70) + Fixed(budget) * Fixed::pct(2);
        p.tick = env.st.tick;
        p.source = target.id;
        p.signalCost = Fixed(budget);
        u16 clue = static_cast<u16>(env.st.rng.pick(RngStream::Clue, kClueCount));
        out("渗透成功：获得对手内部情报。");
        env.st.logEvent(LogPhase::Clue, kLogClue, "从 " + target.name + " 内部取得线索材料", kPlayerId);
        clueDiscover(env.st, clue, p, nullptr);
        out("  获得线索【" + std::string(clueDef(clue).nameZh) + "】（来源：对手内部）");
    } else if (mission == "forgery") {
        // 制造伪证：污染目标的判断
        for (auto& e : env.st.empires) {
            if (e.isPlayer) continue;
            e.mind.playerModel.contamination =
                fxClamp(e.mind.playerModel.contamination + Fixed::pct(8) * Fixed(budget), Fixed(0), Fixed(1));
            e.mind.playerModel.modelConfidence =
                fxClamp(e.mind.playerModel.modelConfidence - Fixed::pct(3) * Fixed(budget), Fixed(0), Fixed(1));
        }
        out("伪造投放成功：全体 AI 的模型污染 +" + fixedStrPlain(Fixed::pct(8) * Fixed(budget) * Fixed(100), 1) +
            "%");
        env.st.logEvent(LogPhase::Ai, kLogFraud, "向 " + target.name + " 投放伪造情报", kPlayerId);
    } else {
        fail(ExitCode::BadArgs, "未知任务【" + mission + "】。可用：intel|sabotage|forgery|exfil|insider|read-mind");
    }

    env.commit("spy");
    out("");
    out("AP 剩余 " + std::to_string(env.player().apLeft) + "/" + std::to_string(env.player().apMax));
    return 0;
}

int cmdGift(CliEnv& env, const Args& args) {
    env.loadState();
    const Empire& target = requireEmpire(env.st, args, 0, "greyfall gift <emp> <res> <qty>");
    if (args.posCount() < 3) fail(ExitCode::BadArgs, "缺少资源或数量");
    int res = commodityIndexByName(args.pos(1));
    if (res < 0) fail(ExitCode::BadArgs, "未知资源【" + args.pos(1) + "】");
    i64 qty = parseInt(args.pos(2), -1);
    if (qty <= 0) fail(ExitCode::BadArgs, "数量必须为正");
    if (env.player().stock[static_cast<std::size_t>(res)].rawValue() < qty * FIX) {
        fail(ExitCode::IllegalAction, "库存不足");
    }
    env.player().stock[static_cast<std::size_t>(res)] -= Fixed(qty);
    env.st.empires[target.id].stock[static_cast<std::size_t>(res)] += Fixed(qty);

    RuleResult rules = ruleEngine(env.st);
    // 成本信号：赠礼越贵重，观感提升越大（但也是可被读取的信号）
    Fixed value = Fixed(qty) * commodityInfo(res).basePrice;
    Fixed gain = fxClamp(value / Fixed(20000), Fixed::pct(2), Fixed::pct(25)) + rules.diplomacyWeight;
    Relation& rel = env.st.relation(kPlayerId, target.id);
    rel.opinion = fxClamp(rel.opinion + gain, Fixed(-1), Fixed(1));
    env.st.empires[target.id].setOpinion(kPlayerId, rel.opinion);
    // 赠礼作为成本信号进入对方的模型
    env.st.empires[target.id].mind.reputation[kPlayerId] += Fixed::pct(5);
    env.st.empires[target.id].mind.playerModel.observations += 1;

    env.st.logEvent(LogPhase::Model, "diplo.gift",
                    "向 " + target.name + " 赠予 " + std::string(commodityInfo(res).nameZh) + " ×" +
                        std::to_string(qty) + "（观感 +" + fixedStrPlain(gain, 2) + "）",
                    kPlayerId, value);
    env.commit("gift");
    out("已赠予 " + target.name + " " + std::string(commodityInfo(res).nameZh) + " ×" + groupDigits(qty));
    out("  观感 → " + fixedStrSigned(rel.opinion, 2) + "    对方信誉记录 +0.05");
    out(style("  （成本信号：AI 会把它读成「你想要什么」，而不仅是「你好意」）", Style::Dim));
    return 0;
}

int cmdIntel(CliEnv& env, const Args& args) {
    env.loadState();
    GameState& st = env.st;
    u32 target = static_cast<u32>(args.getInt("empire", 1));
    if (target >= st.empires.size()) fail(ExitCode::BadArgs, "帝国编号越界");

    if (args.has("what-they-know")) {
        // 默认看最强 AI 眼中的你
        u32 observer = target;
        if (!args.has("empire")) {
            observer = 1;
            Fixed best = Fixed(-1);
            for (const auto& e : st.empires) {
                if (e.isPlayer) continue;
                if (e.powerIndex().rawValue() > best.rawValue()) {
                    best = e.powerIndex();
                    observer = e.id;
                }
            }
        }
        out(whatTheyKnowReport(st, observer, kPlayerId));
        return 0;
    }
    if (args.has("threat")) {
        out(threatReport(st, target));
        return 0;
    }
    if (args.has("fog") || args.has("picture")) {
        out(style("═══ 情报图景：" + std::string(st.empire(target) ? st.empire(target)->name : "?") +
                      " ═══",
                  Style::Heading));
        out("  战争迷雾下，每类信息都有**情报门槛**；未达门槛只能看到区间。");
        out("");
        out(intelPicture(st, kPlayerId, target));
        out("");
        out(style("情报来源分解", Style::Sub));
        out(intelSourceBreakdown(st, kPlayerId, target));
        out("");
        out("  提高情报的途径：渗透间谍网络（每点渗透约 +0.7%）、");
        out("  缔结研究协定/联合情报（+12%/+22%）、开通贸易路线（+6%）、接壤（+8%）。");
        out("  对方的反间谍与情报防御会扣减。");
        return 0;
    }
    if (args.has("counterintel")) {
        out(style("═══ 反情报态势 ═══", Style::Heading));
        TextTable t;
        t.header({"AI 主体", "对你模型置信度", "已识别的套路", "被污染", "读取覆盖率"},
                 {Align::Left, Align::Right, Align::Left, Align::Right, Align::Right});
        Observable obs = omniscientSnapshot(st, kPlayerId);
        for (const auto& e : st.empires) {
            if (e.isPlayer) continue;
            static const char* kHabitNames[] = {"囤积", "杠杆", "外交", "军事", "情报", "工程", "舆论", "读档"};
            int bestHabit = 0;
            for (int i = 1; i < kHabitDim; ++i)
                if (e.mind.playerPattern[static_cast<std::size_t>(i)].rawValue() >
                    e.mind.playerPattern[static_cast<std::size_t>(bestHabit)].rawValue())
                    bestHabit = i;
            t.row({e.name, fixedStrPlain(e.mind.playerModel.modelConfidence, 2),
                   std::string(kHabitNames[bestHabit]) + " " +
                       fixedStrPlain(e.mind.playerPattern[static_cast<std::size_t>(bestHabit)] * Fixed(100), 0) + "%",
                   fixedStrPlain(e.mind.playerModel.contamination * Fixed(100), 1) + "%",
                   fixedStrPlain(obs.coverage * Fixed(100), 1) + "%"});
        }
        out(t.render());
        out("");
        out("可用的反制线：");
        out("  greyfall intel --decoy --signal \"...\"   布置假数据（改变 AI 的梯度）");
        out("  greyfall forge-prove <item>              造假冒牌，污染它的模型");
        out("  greyfall spy <emp> --mission read-mind   读出它的 θ（镜像博弈）");
        out("  greyfall spy <emp> --mission forgery     向它投放伪造情报");
        return 0;
    }
    if (args.has("decoy")) {
        std::string signal = args.get("signal", "巨型工程已暂停");
        // 布置数据：降低 AI 的 modelConfidence 并注入梯度噪声
        Fixed noise = Fixed::pct(12);
        for (auto& e : st.empires) {
            if (e.isPlayer) continue;
            toModelInjectNoise(e.mind.playerModel, noise, Fixed::pct(6));
        }
        env.st.logEvent(LogPhase::Ai, "intel.decoy",
                        "布置假信号：" + signal + "（全体 AI 的 modelConfidence -0.12，梯度噪声注入）", kPlayerId);
        env.commit("intel");
        out(style("已布置数据：" + signal, Style::Good));
        out("  效果：全体 AI 的 modelConfidence -0.12，你的观测被标记为「经布置」。");
        out("  注意：FraudDetect 会随之上升 —— 反复使用会暴露。");
        return 0;
    }

    // 默认：情报总览
    out(style("═══ 情报总览 ═══", Style::Heading));
    TextTable t;
    t.header({"主体", "国力", "军力", "对你观感", "威胁", "信誉", "模型置信度", "背叛 EV"},
             {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right,
              Align::Right});
    for (const auto& e : st.empires) {
        if (e.isPlayer) continue;
        t.row({e.name, fixedStr(e.powerIndex(), 2), fixedStr(e.military, 0),
               fixedStrSigned(e.opinionOf(kPlayerId), 2), fixedStrPlain(e.mind.threat[kPlayerId], 2),
               fixedStrPlain(e.mind.reputation[kPlayerId], 2),
               fixedStrPlain(e.mind.playerModel.modelConfidence, 2), fixedStrSigned(e.mind.lastBetrayalEV, 2)});
    }
    out(t.render());
    out("");
    out("用 `greyfall intel --what-they-know --empire N` 查看它究竟看到了你的什么。");
    out("用 `greyfall intel --threat --empire N` 查看它为什么想背刺你（含 EV 分解）。");
    return 0;
}

int cmdPropaganda(CliEnv& env, const Args& args) {
    env.loadState();
    if (args.posCount() < 2) fail(ExitCode::BadArgs, "用法：greyfall propaganda <target> <narrative>");
    const Empire& target = requireEmpire(env.st, args, 0, "greyfall propaganda <target> <narrative>");
    std::string narrative = args.pos(1);
    if (env.player().apLeft < apcost::kPropaganda) fail(ExitCode::IllegalAction, "行动点不足");
    RuleResult rules = ruleEngine(env.st);
    Fixed cost = Fixed::raw(3000 * (1 + static_cast<i64>(narrative.size()) / 20));
    if (env.st.market.margin.cash.rawValue() < cost.rawValue()) fail(ExitCode::IllegalAction, "资金不足");
    env.st.market.margin.cash -= cost;
    env.player().treasury = env.st.market.margin.cash;
    env.player().apLeft -= apcost::kPropaganda;

    // 效果：第三方观感变化 + 有被识破的反噬
    Fixed impact = Fixed::pct(6) + rules.diplomacyWeight;
    int affected = 0;
    for (auto& e : env.st.empires) {
        if (e.id == kPlayerId || e.id == target.id) continue;
        e.addOpinion(target.id, Fixed(-1) * (impact));
        e.addOpinion(kPlayerId, Fixed(-1) * (impact * Fixed::pct(30)));
        ++affected;
    }
    // 被识破概率
    Fixed exposure = target.propaganda + env.st.empires[target.id].counterIntel;
    if (env.st.rng.chance(RngStream::Diplo, fxClamp(exposure * Fixed::pct(40), Fixed(0), Fixed::pct(60)))) {
        env.st.relation(kPlayerId, target.id).opinion -= Fixed::pct(20);
        out(style("舆论战被识破！", Style::Bad));
        out("  对方公开了溯源证据：其观感 -20%，第三方对你的观感也受损。");
    } else {
        out(style("舆论战奏效。", Style::Good));
    }
    out("  受影响第三方 " + std::to_string(affected) + " 国，对 " + target.name + " 的观感 -" +
        fixedStrPlain(impact, 2));
    out("  叙事：\"" + narrative + "\"");
    env.st.logEvent(LogPhase::Model, "diplo.propaganda",
                    "对 " + target.name + " 发动舆论战：" + narrative, kPlayerId, cost);
    env.commit("propaganda");
    return 0;
}


int cmdNegotiate(CliEnv& env, const Args& args) {
    env.loadState();
    const Empire& target = requireEmpire(env.st, args, 0, "greyfall negotiate <emp> [--status|--demand ...|--offer ...]");
    if (target.isPlayer) fail(ExitCode::IllegalAction, "不能与自己谈判");

    // 态势报告
    if (args.has("status") || (!args.has("demand") && !args.has("offer"))) {
        out(negotiationReport(env.st, target.id, kPlayerId));
        out("");
        out("用法：");
        out("  greyfall negotiate <emp> --status");
        out("  greyfall negotiate <emp> --demand \"credits=10000,alloys=5000,tech=comp6,system=12,manpower=300\"");
        out("  greyfall negotiate <emp> --demand \"credits=20000\" --offer \"tech=comp6,alloys=1000\"");
        out("");
        out("条款键：credits / influence / unity / manpower / tech=<科技名> / system=<星系编号> / <资源名>=<数量>");
        return 0;
    }

    NegotiationTerms terms;
    std::string err;
    if (args.has("demand")) {
        if (!parseTerms(args.get("demand"), terms.demand, &err)) fail(ExitCode::BadArgs, err);
    }
    if (args.has("offer")) {
        if (!parseTerms(args.get("offer"), terms.offer, &err)) fail(ExitCode::BadArgs, err);
    }
    if (terms.demand.empty() && terms.offer.empty()) {
        fail(ExitCode::BadArgs, "至少要给出 --demand 或 --offer");
    }
    if (env.player().apLeft < apcost::kEnvoy) {
        fail(ExitCode::IllegalAction, "行动点不足（谈判需要 " + std::to_string(apcost::kEnvoy) + "）");
    }

    // 展示条款
    out(style("═══ 谈判：" + target.name + " ═══", Style::Heading));
    if (!terms.demand.empty()) {
        out("  你要求对方付出：");
        for (const auto& t : terms.demand) out("    · " + termText(env.st, t));
    }
    if (!terms.offer.empty()) {
        out("  你愿意付出：");
        for (const auto& t : terms.offer) out("    · " + termText(env.st, t));
    }
    out("");

    NegotiationResult r = negotiate(env.st, kPlayerId, target.id, terms);
    env.player().apLeft -= apcost::kEnvoy;

    // 无论成败都给出三档评价 —— 这是玩家决定「要不要谴责」的依据
    out(style("提案评价：" + std::string(dealRatingName(r.rating)), Style::Sub));
    out("  " + r.ratingNote);
    out("  要求价值 " + fixedStr(r.demandValue, 0) + "    出让价值 " + fixedStr(r.offerValue, 0));
    if (!r.accepted) {
        env.commit("negotiate");
        out("");
        out(style("谈判破裂。", Style::Bad));
        out("  " + r.reason);
        out("  （对方只在谈判权重超过阈值时才肯谈；用 `negotiate " + std::to_string(target.id) +
            " --status` 查看权重分解）");
        out("");
        out("  你可以用 `greyfall envoy " + std::to_string(target.id) +
            " condemn` 公开谴责对方 —— 即使提案本身是合理的。");
        return static_cast<int>(ExitCode::NoFill);
    }

    out("");
    out(style("谈判成功。", Style::Good));
    out("  " + r.reason);
    if (!r.applied.empty()) {
        out("");
        for (const auto& line : r.applied) out("    " + line);
    }
    if (!r.allyContributions.empty()) {
        out("");
        out(style("盟友代付：", Style::Warn));
        for (const auto& line : r.allyContributions) out("    " + line);
    }
    env.commit("negotiate");
    out("");
    out("  剩余 AP " + std::to_string(env.player().apLeft) + "/" + std::to_string(env.player().apMax));
    return 0;
}

}  // namespace gf
