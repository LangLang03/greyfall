// 贸易路线与关税战 CLI
#include <algorithm>

#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "domain/Trade.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

/// 解析商品名或编号
int parseCommodity(const std::string& s) {
    for (int i = 0; i < kCommodityCount; ++i)
        if (commodityName(i) == s) return i;
    i64 n = parseInt(s, -1);
    if (n >= 0 && n < kCommodityCount) return static_cast<int>(n);
    return -1;
}

}  // namespace

int cmdTrade(CliEnv& env, const Args& args) {
    env.loadState();
    const bool hasOp = args.has("open") || args.has("close") || args.has("tariff") || args.has("detail");

    if (!hasOp) {
        out(style("═══ 贸易 ═══", Style::Heading));
        out("  贸易路线把**出口方的剩余物资**运往**进口方**，双方各得其所：");
        out("    出口方获得货款；进口方获得物资，并按关税率征税。");
        out("  关税率是可调的政治工具 —— 提高关税增加财政收入，但压低贸易量并恶化关系。");
        out("");
        out(tradeText(env.st, kPlayerId));
        out("");
        out("用法：greyfall trade --open <出口方>:<商品>[:关税率%]   开设路线");
        out("      greyfall trade --close <路线编号>                 关闭路线");
        out("      greyfall trade --tariff <路线编号> <税率%>         调整关税");
        out("      greyfall trade --detail <路线编号>                路线详情");
        return 0;
    }

    // --open <exporter>:<commodity>[:tariff]
    if (args.has("open")) {
        std::string spec = args.get("open");
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true) {
            std::size_t pos = spec.find(':', start);
            if (pos == std::string::npos) {
                parts.push_back(spec.substr(start));
                break;
            }
            parts.push_back(spec.substr(start, pos - start));
            start = pos + 1;
        }
        if (parts.size() < 2) fail(ExitCode::BadArgs, "用法：--open <出口方>:<商品>[:关税率%]");
        i64 exporter = parseInt(parts[0], -1);
        if (exporter < 0 || exporter >= static_cast<i64>(env.st.empires.size())) {
            fail(ExitCode::BadArgs, "出口方编号越界");
        }
        int c = parseCommodity(parts[1]);
        if (c < 0) fail(ExitCode::BadArgs, "未知商品【" + parts[1] + "】");
        // 默认关税：沿用我方对该伙伴的现行税率，否则 10%
        Fixed tariff = Fixed::pct(10);
        if (parts.size() >= 3) {
            i64 raw = parseInt(parts[2], 10);
            tariff = Fixed::pct(raw);
        }
        std::string err;
        if (!tradeOpen(env.st, static_cast<u32>(exporter), kPlayerId, static_cast<u8>(c), tariff, &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("trade");
        const TradeRoute* r = findRoute(env.st, static_cast<u32>(exporter), kPlayerId, static_cast<u8>(c));
        out(style("已开通贸易路线", Style::Good));
        if (r != nullptr) out(tradeRouteText(env.st, *r));
        out("  关税收入归我方，货款支付给对方。");
        return 0;
    }

    // --close <id>
    if (args.has("close")) {
        i64 id = parseInt(args.get("close"), -1);
        if (id < 0) fail(ExitCode::BadArgs, "用法：--close <路线编号>");
        std::string err;
        if (!tradeClose(env.st, static_cast<u32>(id), kPlayerId, &err)) fail(ExitCode::IllegalAction, err);
        env.commit("trade");
        out("已关闭路线 #" + std::to_string(id));
        return 0;
    }

    // --tariff <id> <pct>
    if (args.has("tariff")) {
        if (args.posCount() < 1) fail(ExitCode::BadArgs, "用法：--tariff <路线编号> <税率%>");
        i64 id = parseInt(args.get("tariff"), -1);
        i64 pct = parseInt(args.pos(0), 0);
        if (id < 0) fail(ExitCode::BadArgs, "路线编号无效");
        if (pct < 0 || pct > kMaxTariffPct) {
            fail(ExitCode::BadArgs, "关税率须在 0~" + std::to_string(kMaxTariffPct) + "% 之间");
        }
        std::string err;
        if (!tradeSetTariff(env.st, static_cast<u32>(id), kPlayerId, Fixed::pct(pct), &err)) {
            fail(ExitCode::IllegalAction, err);
        }
        env.commit("trade");
        out("路线 #" + std::to_string(id) + " 关税率 → " + std::to_string(pct) + "%");
        if (pct >= 30) out(style("  高关税会压低贸易量，并可能招致对方报复。", Style::Warn));
        return 0;
    }

    // --detail <id>
    if (args.has("detail")) {
        i64 id = parseInt(args.get("detail"), -1);
        const TradeRoute* found = nullptr;
        for (const auto& r : env.st.market.trade.routes)
            if (r.id == static_cast<u32>(id)) found = &r;
        if (found == nullptr) fail(ExitCode::BadArgs, "找不到路线 #" + std::to_string(id));
        out(style("贸易路线 #" + std::to_string(found->id), Style::Heading));
        out(tradeRouteText(env.st, *found));
        out("");
        TextTable t;
        t.header({"项目", "值"});
        const Empire* ex = env.st.empire(found->exporter);
        const Empire* im = env.st.empire(found->importer);
        t.row({"出口方", ex ? ex->name : "?"});
        t.row({"进口方", im ? im->name : "?"});
        t.row({"商品", std::string(commodityName(found->commodity))});
        t.row({"关税率", fixedStrPlain(found->tariff * Fixed(100), 0) + "%（由进口方征收）"});
        t.row({"名义运力", fixedStr(found->capacity, 0)});
        t.row({"本季运输", fixedStr(found->volume, 0)});
        t.row({"结算单价", fixedStr(found->unitPrice, 1)});
        t.row({"累计关税收入", fixedStr(found->tariffRevenue, 0)});
        t.row({"累计出口收入", fixedStr(found->exportRevenue, 0)});
        t.row({"开设于", std::to_string(found->establishedTick) + " 季"});
        t.row({"路径", std::to_string(found->path.size()) + " 跳"});
        t.row({"路径风险", fixedStrPlain(found->pathRisk * Fixed(100), 0) + "%" +
                                (found->pathNote.empty() ? std::string() : ("（" + found->pathNote + "）"))});
        if (!found->path.empty()) {
            std::string chain;
            for (std::size_t i = 0; i < found->path.size() && i < 10; ++i) {
                const SystemNode* sn = env.st.system(found->path[i]);
                if (i > 0) chain += " → ";
                chain += sn ? sn->name : "?";
                if (i == 9 && found->path.size() > 10) chain += " → …";
            }
            t.row({"路线", chain});
        }
        if (!found->disrupted.empty()) t.row({"中断原因", found->disrupted});
        out(t.render());
        return 0;
    }

    fail(ExitCode::BadArgs, "未知操作。用 `greyfall trade` 查看全部用法");
}

}  // namespace gf
