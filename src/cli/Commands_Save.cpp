#include "save/Serde.h"
#include <filesystem>
#include "cli/Commands.h"
#include "util/TextTable.h"
#include "core/Errors.h"
#include "save/Chronicle.h"
#include "save/SaveFile.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {

int cmdSave(CliEnv& env, const Args& args) {
    env.loadState();
    std::string target = args.has("as") ? args.get("as") : env.slotId;
    std::string prev = env.slotId;
    env.slotId = target;
    env.noAutosave = false;
    env.commit("save --as", true);
    env.slotId = prev;
    std::vector<u8> bytes;
    (void)env.slots.readSlot(target, bytes);
    out("已保存到槽 '" + target + "'（" + humanBytes(bytes.size()) + "）");
    return 0;
}

int cmdLoad(CliEnv& env, const Args& args) {
    std::string target = args.posCount() > 0 ? args.pos(0) : env.slotId;
    if (!env.slots.exists(target)) fail(ExitCode::NoSave, "存档槽 '" + target + "' 不存在");
    env.slotId = target;
    env.hasState = false;
    env.loadState();
    // 记录一次 load（AI 可见）
    std::error_code ec;
    std::filesystem::create_directories(env.epochDir(), ec);
    (void)Chronicle::append(env.chronicleFile(), ChronicleKind::Load, env.st.tick, 0, env.st.fingerprint(), "load");
    out("已载入槽 '" + target + "'（tick " + std::to_string(env.st.tick) + "，回退计数 " +
        std::to_string(env.st.rollbackCount) + "）");
    return 0;
}

int cmdRollback(CliEnv& env, const Args& args) {
    env.loadState();
    i64 n = args.posCount() > 0 ? args.posInt(0, 1) : args.getInt("ticks", 1);
    if (n <= 0) fail(ExitCode::BadArgs, "rollback 的次数必须为正");
    if (static_cast<u64>(n) > env.st.tick) fail(ExitCode::IllegalAction, "不能回退到负数 tick");
    u64 want = env.st.tick - static_cast<u64>(n);

    // 在环形检查点里找最接近 want 且 <= want 的
    std::vector<u8> best;
    u64 bestTick = 0;
    bool found = false;
    for (int i = 0; i < kCheckpointCount; ++i) {
        std::vector<u8> ck;
        if (!env.slots.readCheckpoint(env.slotId, i, ck)) continue;
        SaveHeaderInfo h = peekSave(ck);
        if (h.createdTick <= want && (!found || h.createdTick > bestTick)) {
            best = ck;
            bestTick = h.createdTick;
            found = true;
        }
    }
    if (!found) {
        fail(ExitCode::IllegalAction,
             "找不到 <= tick " + std::to_string(want) + " 的检查点（检查点环形保留最近 " +
                 std::to_string(kCheckpointCount) + " 个 tick）");
    }
    GameState st;
    std::string detail;
    DecodeStatus s = decodeSave(best, st, env.passphrase, &detail);
    if (s != DecodeStatus::Ok) fail(ExitCode::Integrity, "检查点解码失败：" + detail);
    st.rollbackCount += 1;
    st.logEvent(LogPhase::Chronicle, kLogRollback,
                "回退到 tick " + std::to_string(bestTick) + "（回退计数 " + std::to_string(st.rollbackCount) + "）");
    env.st = std::move(st);
    env.hasState = true;
    std::error_code ec;
    std::filesystem::create_directories(env.epochDir(), ec);
    (void)Chronicle::append(env.chronicleFile(), ChronicleKind::Rollback, bestTick, 0, env.st.fingerprint(),
                            "rollback=" + std::to_string(n));
    env.commit("rollback", true);
    out(style("已回退到 tick " + std::to_string(bestTick), Style::Warn));
    out("  rollbackCount = " + std::to_string(env.st.rollbackCount) +
        " —— 泛视网络记录在案：AI 会提高要价、降低让步概率、优先采用不可逆打击。");
    return 0;
}

int cmdExport(CliEnv& env, const Args& args) {
    std::string id = args.posCount() > 0 ? args.pos(0) : env.slotId;
    std::string path = args.posCount() > 1 ? args.pos(1) : (id + ".gsv");
    if (!env.slots.exists(id)) fail(ExitCode::NoSave, "存档槽 '" + id + "' 不存在");
    std::vector<u8> bytes;
    if (!env.slots.readSlot(id, bytes)) fail(ExitCode::Integrity, "无法读取槽 '" + id + "'");

    if (args.has("plaintext")) {
        warn("--plaintext 会导出未加密状态，破坏公平性（AI 的模型无法察觉，但你自己会知道）");
        GameState st;
        std::string detail;
        if (decodeSave(bytes, st, env.passphrase, &detail) != DecodeStatus::Ok) {
            fail(ExitCode::Integrity, "解码失败：" + detail);
        }
        std::vector<u8> plain = serializeState(st);
        SlotManager::writeFileAtomic(path, plain);
        out("已导出明文状态 → " + path + "（" + humanBytes(plain.size()) + "）");
        return 0;
    }
    SlotManager::writeFileAtomic(path, bytes);
    out("已导出 → " + path + "（" + humanBytes(bytes.size()) + "）");
    return 0;
}

int cmdImport(CliEnv& env, const Args& args) {
    if (args.posCount() < 1) fail(ExitCode::BadArgs, "用法：greyfall import <path> [slot]");
    std::string path = args.pos(0);
    std::string slot = args.posCount() > 1 ? args.pos(1) : env.slotId;
    std::vector<u8> bytes;
    if (!SlotManager::readFile(path, bytes)) fail(ExitCode::NoSave, "无法读取文件 " + path);
    SaveHeaderInfo h = peekSave(bytes);
    if (h.formatMajor != static_cast<u8>(kFormatMajor)) fail(ExitCode::Integrity, "不是合法的 greyfall 存档");
    GameState st;
    std::string detail;
    DecodeStatus s = decodeSave(bytes, st, env.passphrase, &detail);
    if (s != DecodeStatus::Ok) fail(ExitCode::Integrity, "导入失败：" + detail);
    env.slots.writeSlot(slot, bytes);
    out("已导入 " + path + " → 槽 '" + slot + "'");
    return 0;
}

int cmdPrune(CliEnv& env, const Args& args) {
    i64 keep = args.getInt("keep", 8);
    if (keep < 1) fail(ExitCode::BadArgs, "--keep 必须 >= 1");
    std::vector<std::string> removed = pruneSlots(env.slots, static_cast<int>(keep), env.slotId);
    if (removed.empty()) {
        out("没有需要剪枝的槽（保留 " + std::to_string(keep) + " 个）");
        return 0;
    }
    out("已删除 " + std::to_string(removed.size()) + " 个槽：" + join(removed, ", "));
    return 0;
}

int cmdDelete(CliEnv& env, const Args& args) {
    std::string id = args.posCount() > 0 ? args.pos(0) : env.slotId;
    if (!args.has("yes")) {
        fail(ExitCode::BadArgs, "删除是不可逆的：请确认后加 --yes（greyfall delete " + id + " --yes）");
    }
    if (!env.slots.exists(id)) fail(ExitCode::NoSave, "存档槽 '" + id + "' 不存在");
    (void)env.slots.removeSlot(id, false);
    out("已删除槽 '" + id + "'");
    return 0;
}

int cmdChronicle(CliEnv& env, const Args& args) {
    env.loadState(false);
    std::string file = env.chronicleFile();
    bool exists = false;
    (void)Chronicle::head(file, &exists);
    if (!exists) {
        out(style("archive-burned：chronicle 链文件缺失", Style::Bad));
        out("  各方将你视为背约者：所有阵营观感下降，贸易折价消失，AI 直接采用不可逆打击。");
        return 0;
    }
    std::vector<ChronicleEntry> entries = Chronicle::read(file);
    std::string err;
    bool ok = Chronicle::verify(file, &err);
    out(style("Chronicle 哈希链", Style::Heading) + "  " + file);
    out("  链节数：" + std::to_string(entries.size()) + "   链头：" + std::to_string(entries.back().head) +
        "   完整性：" + (ok ? "完整 ✓" : ("断裂 ✗ " + err)));
    u64 rollbacks = Chronicle::count(file, ChronicleKind::Rollback);
    u64 loads = Chronicle::count(file, ChronicleKind::Load);
    out("  回退记录：" + std::to_string(rollbacks) + "   载入记录：" + std::to_string(loads) +
        "   （AI 全知读取这些计数）");
    i64 last = args.getInt("last", 20);
    TextTable t;
    t.header({"seq", "kind", "tick", "stateDigest", "head"});
    std::size_t start = entries.size() > static_cast<std::size_t>(last) ? entries.size() - static_cast<std::size_t>(last) : 0;
    for (std::size_t i = start; i < entries.size(); ++i) {
        const auto& e = entries[i];
        char k[2] = {static_cast<char>(e.kind), 0};
        t.row({std::to_string(e.seq), k, std::to_string(e.tick),
               hexEncode(std::string_view(reinterpret_cast<const char*>(&e.stateDigest), 8)),
               hexEncode(std::string_view(reinterpret_cast<const char*>(&e.head), 8))});
    }
    out(t.render());
    return 0;
}

}  // namespace gf
