// CLI：参数解析双写法等价 / 命令表完整 / 退出码语义 / 文档与实现一致
#include <algorithm>
#include <set>

#include "check.h"
#include "cli/ArgParser.h"
#include "cli/Commands.h"
#include "cli/Man.h"
#include "core/Errors.h"
#include "util/Str.h"

using namespace gf;

namespace {

Args parse(std::vector<std::string> toks) {
    std::vector<char*> argv;
    static std::vector<std::string> storage;
    storage = std::move(toks);
    for (auto& s : storage) argv.push_back(s.data());
    return Args::parse(static_cast<int>(argv.size()), argv.data());
}

}  // namespace

TEST(cli, dual_syntax_equivalence) {
    Args a = parse({"greyfall", "advance", "--ticks=3"});
    Args b = parse({"greyfall", "--action=advance", "--ticks=3"});
    CHECK_EQ(a.action(), std::string("advance"));
    CHECK_EQ(b.action(), std::string("advance"));
    CHECK_EQ(a.getInt("ticks", 0), b.getInt("ticks", 0));
    CHECK_EQ(a.getInt("ticks", 0), 3);
}

TEST(cli, option_forms) {
    Args a = parse({"greyfall", "advance", "--ticks", "5"});
    CHECK_EQ(a.getInt("ticks", 0), 5);
    Args b = parse({"greyfall", "advance", "--ticks=7"});
    CHECK_EQ(b.getInt("ticks", 0), 7);
    // 布尔选项不消费下一个 token
    Args c = parse({"greyfall", "advance", "--verbose", "extra"});
    CHECK(c.has("verbose"));
    CHECK_EQ(c.posCount(), 1ull);
    CHECK_EQ(c.pos(0), std::string("extra"));
    // 位置参数与选项混合顺序无关
    Args d = parse({"greyfall", "order", "buy", "alloys", "4200", "@31.4", "--tif", "gtc"});
    CHECK_EQ(d.action(), std::string("order"));
    CHECK_EQ(d.posCount(), 4ull);   // buy alloys 4200 @31.4
    CHECK_EQ(d.pos(1), std::string("alloys"));
    CHECK_EQ(d.get("tif", ""), std::string("gtc"));
    // 全局槽与数据目录
    Args e = parse({"greyfall", "status", "--slot", "alt", "--quiet"});
    CHECK_EQ(e.get("slot", "main"), std::string("alt"));
    CHECK(e.has("quiet"));
}

TEST(cli, numeric_and_fixed_parsing) {
    bool ok = false;
    CHECK_EQ(parseFixed("31.4", &ok).rawValue(), 31400);
    CHECK(ok);
    CHECK_EQ(parseFixed("-0.75", &ok).rawValue(), -750);
    CHECK(ok);
    CHECK_EQ(parseFixed("1,234.5", &ok).rawValue(), 1234500);
    CHECK(ok);
    (void)parseFixed("not-a-number", &ok);
    CHECK(!ok);
    CHECK_EQ(parseInt("42", -1), 42);
    CHECK_EQ(parseInt("-7", 0), -7);
    CHECK_EQ(parseInt("x", 99), 99);
    CHECK_EQ(parseSeed("5EED-C0FFEE"), 0x5EEDC0FFEEull);
    CHECK(parseSeed("not-hex-seed") != 0);
}

TEST(cli, exit_codes_are_documented) {
    CHECK_EQ(static_cast<int>(ExitCode::Ok), 0);
    CHECK_EQ(static_cast<int>(ExitCode::BadArgs), 1);
    CHECK_EQ(static_cast<int>(ExitCode::NoSave), 2);
    CHECK_EQ(static_cast<int>(ExitCode::Integrity), 3);
    CHECK_EQ(static_cast<int>(ExitCode::IllegalAction), 4);
    CHECK_EQ(static_cast<int>(ExitCode::PendingChoice), 5);
    CHECK_EQ(static_cast<int>(ExitCode::VersionMismatch), 6);
    CHECK_EQ(static_cast<int>(ExitCode::NoFill), 7);
    CHECK_EQ(static_cast<int>(ExitCode::Internal), 70);
    for (int c = 0; c <= 7; ++c) CHECK(exitCodeName(static_cast<ExitCode>(c)) != nullptr);
}

TEST(cli, command_table_is_complete_and_unique) {
    std::set<std::string> seen;
    for (const auto& c : commandTable()) {
        CHECK(!c.name.empty());
        CHECK(c.fn != nullptr);
        CHECK(!c.group.empty());
        CHECK(!c.summary.empty());
        CHECK(seen.insert(std::string(c.name)).second);   // 名字唯一
    }
    // 方案 §4 中列出的核心命令必须全部存在
    const char* required[] = {
        "help", "man", "version", "new", "status", "selftest", "verify", "slots", "resume",
        "overview", "starmap", "sectors", "planet", "empires", "relations", "treaties", "federation",
        "domestic", "tech", "fleets", "ship", "buildings", "logs", "history", "replay",
        "book", "quote", "curve", "position", "pnl", "vol", "arb", "shock", "credit", "fx",
        "blackmarket", "order", "cancel", "modify", "futures", "settle", "borrow", "repay",
        "escrow", "insure", "envoy", "spy", "gift", "intel", "propaganda",
        "inventory", "item", "combine", "use", "disassemble", "forge-prove", "equip",
        "clues", "link", "unlink", "deduce", "archive", "conclusions",
        "colony", "ship-build", "fleet", "edict", "research", "build", "mega", "ascend", "recruit",
        "advance", "choose", "defer",
        "save", "load", "rollback", "export", "import", "prune", "delete", "chronicle", "epoch",
    };
    for (const char* r : required) {
        CHECK(findCommand(r) != nullptr);
        CHECK(findManEntry(r) != nullptr);
    }
    CHECK(!commandGroups().empty());
    CHECK(!helpText().empty());
    CHECK(!versionText().empty());
}

TEST(cli, man_pages_render) {
    for (const auto& e : manEntries()) {
        std::string page = manPage(e.name);
        CHECK(page.find(std::string(e.name)) != std::string::npos);
        CHECK(page.find("用法") != std::string::npos);
    }
    CHECK(manPage("no-such-command").find("没有") != std::string::npos);
}

TEST(cli, unknown_flags_become_boolean) {
    // 未登记的选项若无值则视为布尔真，不应崩溃
    Args a = parse({"greyfall", "status", "--some-unknown-flag"});
    CHECK(a.has("some-unknown-flag"));
    CHECK(a.getBool("some-unknown-flag"));
}

TEST(cli, slot_names_are_validated) {
    // 目录穿越必须被拒绝
    Args a = parse({"greyfall", "status", "--slot", "../evil"});
    CHECK_EQ(a.get("slot", ""), std::string("../evil"));
    SlotManager mgr("build/test-data/cli/slot-guard");
    bool threw = false;
    try {
        (void)mgr.slotPath(a.get("slot", "main"));
    } catch (const GameError& e) {
        threw = true;
        CHECK_EQ(static_cast<int>(e.code()), static_cast<int>(ExitCode::BadArgs));
    }
    CHECK(threw);
    CHECK(!mgr.slotPath("main").empty());
}
