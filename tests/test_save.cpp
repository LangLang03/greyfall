// 存档：字节级确定性 / 加密往返 / MAC 篡改拒绝 / 迁移链 / Chronicle 哈希链 / 原子写与备份回退
#include <cstring>
#include <filesystem>

#include "check.h"
#include "core/GameState.h"
#include "crypto/Sha256.h"
#include "gen/WorldGen.h"
#include "save/Chronicle.h"
#include "save/Lz77.h"
#include "save/Migration.h"
#include "save/SaveFile.h"
#include "core/TickPipeline.h"
#include "save/Serde.h"
#include "save/SlotManager.h"

using namespace gf;

namespace {

GameState makeWorld(u64 seed = 1234, int empires = 8, int systems = 32) {
    WorldGenOptions o;
    o.seed = seed;
    o.difficulty = 2;
    o.empireCount = empires;
    o.systemCount = systems;
    GameState st;
    generateWorld(st, o);
    return st;
}

std::string tmpDir(std::string_view name) {
    std::string d = std::string("build/test-data/save/") + std::string(name);
    std::filesystem::remove_all(d);
    std::filesystem::create_directories(d);
    return d;
}

}  // namespace

TEST(save, byte_determinism) {
    GameState a = makeWorld(777);
    GameState b = makeWorld(777);
    std::vector<u8> sa = serializeState(a);
    std::vector<u8> sb = serializeState(b);
    CHECK(sa == sb);
    // 同一状态连续两次序列化必须逐字节相同
    CHECK(serializeState(a) == serializeState(a));
    // 不同种子必须不同
    GameState c = makeWorld(778);
    CHECK(serializeState(c) != sa);
}

TEST(save, roundtrip_equality) {
    GameState a = makeWorld(4242);
    std::vector<u8> bytes = serializeState(a);
    GameState b;
    CHECK(tryDeserializeState(bytes, b));
    CHECK(serializeState(b) == bytes);
    CHECK_EQ(b.tick, a.tick);
    CHECK_EQ(b.empires.size(), a.empires.size());
    CHECK_EQ(b.map.systems.size(), a.map.systems.size());
    CHECK_EQ(b.clues.size(), a.clues.size());
    CHECK_EQ(b.clueEdges.size(), a.clueEdges.size());
    CHECK_EQ(b.stateHash(), a.stateHash());
}

TEST(save, lz77_roundtrip_and_reject_corrupt) {
    std::vector<u8> data;
    for (int i = 0; i < 5000; ++i) {
        const char* s = "alloys:31.400;CX;bid;ask;volume;";
        for (const char* p = s; *p; ++p) data.push_back(static_cast<u8>(*p));
    }
    std::vector<u8> packed = lz77Compress(data, 2);
    std::vector<u8> back;
    CHECK(lz77Decompress(packed, back));
    CHECK(back == data);
    CHECK(packed.size() < data.size() / 3);
    // 随机数据也要能往返
    std::vector<u8> rnd;
    u64 x = 88172645463325252ull;
    for (int i = 0; i < 3000; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        rnd.push_back(static_cast<u8>(x & 0xFF));
    }
    std::vector<u8> rback;
    CHECK(lz77Decompress(lz77Compress(rnd, 3), rback));
    CHECK(rback == rnd);
    // 截断必须被拒绝
    std::vector<u8> trunc(packed.begin(), packed.end() - 3);
    std::vector<u8> out;
    CHECK(!lz77Decompress(trunc, out));
}

TEST(save, encrypted_roundtrip_and_mac_rejection) {
    GameState a = makeWorld(31337);
    std::vector<u8> bytes = encodeSave(a, "");
    GameState b;
    std::string detail;
    CHECK(decodeSave(bytes, b, "", &detail) == DecodeStatus::Ok);
    CHECK_EQ(b.stateHash(), a.stateHash());

    // 1) 口令错误 ⇒ fastReject 失败
    GameState c;
    CHECK(decodeSave(bytes, c, "wrong-password", &detail) != DecodeStatus::Ok);

    // 2) 正文篡改 ⇒ CRC 覆盖不到，但 HMAC 必须拒绝
    std::vector<u8> tampered = bytes;
    tampered[kSaveHeaderSize + 5] ^= 0xFF;
    GameState d;
    CHECK(decodeSave(tampered, d, "", &detail) == DecodeStatus::MacFail);

    // 3) 头部篡改 ⇒ CRC 失败
    std::vector<u8> hdr = bytes;
    hdr[60] ^= 0x01;
    GameState e;
    CHECK(decodeSave(hdr, e, "", &detail) == DecodeStatus::HeaderCrcFail);

    // 4) 截断 ⇒ fastReject / TooShort
    std::vector<u8> cut(bytes.begin(), bytes.begin() + kSaveHeaderSize + 4);
    GameState f;
    DecodeStatus s = decodeSave(cut, f, "", &detail);
    CHECK(s == DecodeStatus::FastReject || s == DecodeStatus::TooShort);

    // 5) 非 greyfall 文件
    std::vector<u8> junk(200, 0xAB);
    GameState g;
    CHECK(decodeSave(junk, g, "", &detail) == DecodeStatus::BadMagic);
}

TEST(save, compression_shrinks_save) {
    GameState a = makeWorld(555);
    std::vector<u8> plain = serializeState(a);
    SaveOptions compressed;
    compressed.compress = true;
    std::vector<u8> c = encodeSave(a, "", compressed);
    SaveOptions raw;
    raw.compress = false;
    std::vector<u8> u = encodeSave(a, "", raw);
    CHECK(c.size() < u.size());
    SaveHeaderInfo h = peekSave(c);
    CHECK((h.flags & kFlagLz77) != 0);
    CHECK_EQ(h.plainLen, static_cast<u32>(plain.size()));
}

TEST(save, migration_chain) {
    CHECK(migration::canUpgrade(kSchemaVersion));
    CHECK(!migration::canUpgrade(kSchemaVersion + 1));
    GameState st;
    std::string err;
    CHECK(migration::chain(kSchemaVersion, st, &err));
    CHECK(migration::chain(kSchemaVersion + 1, st, &err) == false);
    CHECK(!migration::describe(kSchemaVersion).empty());
}

TEST(save, chronicle_hash_chain) {
    std::string dir = tmpDir("chronicle");
    std::string file = Chronicle::pathFor(dir);
    CHECK(!Chronicle::exists(file));
    u64 h1 = Chronicle::append(file, ChronicleKind::Tick, 1, 111, 222);
    u64 h2 = Chronicle::append(file, ChronicleKind::Tick, 2, 333, 444);
    CHECK(h1 != h2);
    CHECK_EQ(Chronicle::head(file), h2);
    std::string err;
    CHECK(Chronicle::verify(file, &err));
    CHECK_EQ(Chronicle::count(file, ChronicleKind::Tick), 2ull);
    // 篡改一行 ⇒ 链校验必须失败
    std::vector<u8> bytes;
    CHECK(SlotManager::readFile(file, bytes));
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    // 篡改第一条真实记录（跳过 # 开头的注释行）的 head 字段
    std::size_t recStart = 0;
    while (recStart < text.size() && (text[recStart] == '#' || text[recStart] == '\n')) {
        std::size_t nl = text.find('\n', recStart);
        if (nl == std::string::npos) break;
        recStart = nl + 1;
    }
    std::size_t recEnd = text.find('\n', recStart);
    if (recEnd == std::string::npos) recEnd = text.size();
    bool tampered = false;
    if (recEnd > recStart) {
        char& ch = text[recEnd - 1];
        ch = (ch == 'a') ? 'b' : 'a';
        tampered = true;
    }
    CHECK(tampered);
    std::vector<u8> out(text.begin(), text.end());
    SlotManager::writeFileAtomic(file, out);
    CHECK(!Chronicle::verify(file, &err));
    // 焚毁：链文件消失且留下一条 Fork 记录
    Chronicle::burn(Chronicle::pathFor(dir), 5);
    CHECK(Chronicle::exists(file));
    CHECK_EQ(Chronicle::count(file, ChronicleKind::Fork), 1ull);
}

TEST(save, slot_atomic_write_and_backup_fallback) {
    std::string dir = tmpDir("slots");
    SlotManager mgr(dir);
    GameState a = makeWorld(9001);
    std::vector<u8> first = encodeSave(a, "");
    mgr.writeSlot("main", first);
    CHECK(mgr.exists("main"));
    a.tick = 42;
    std::vector<u8> second = encodeSave(a, "");
    CHECK(first != second);
    mgr.writeSlot("main", second);
    CHECK(SlotManager::fileExists(mgr.bakPath("main")));   // 旧档轮转为 .bak
    std::vector<u8> read;
    CHECK(mgr.readSlot("main", read));
    CHECK(read == second);
    // 主档被写坏（内容可读但解码必然失败）
    std::vector<u8> broken(second.size(), 0x00);
    SlotManager::writeFileAtomic(mgr.slotPath("main"), broken);
    GameState junk;
    std::string detail;
    CHECK(decodeSave(broken, junk, "", &detail) != DecodeStatus::Ok);
    // 主档不可读（删除）⇒ readSlotWithFallback 必须回退到 .bak
    std::filesystem::remove(mgr.slotPath("main"));
    bool usedBackup = false;
    CHECK(mgr.readSlotWithFallback("main", read, &usedBackup));
    CHECK(usedBackup);
    CHECK(read == first);
    // 检查点环形轮转
    for (int i = 0; i < kCheckpointCount + 3; ++i) mgr.rotateCheckpoints("main", second);
    std::vector<u8> ck;
    CHECK(mgr.readCheckpoint("main", kCheckpointCount - 1, ck));
    // 槽名合法性
    bool threw = false;
    try {
        (void)mgr.slotPath("../etc/passwd");
    } catch (const GameError&) {
        threw = true;
    }
    CHECK(threw);
    // 列表与删除（先把主档写回，确保列表里有两个槽）
    mgr.writeSlot("main", second);
    mgr.writeSlot("alt", second);
    CHECK(mgr.list().size() >= 2);
    (void)mgr.removeSlot("alt", false);
    CHECK(!mgr.exists("alt"));
}

TEST(save, growing_state_stays_bounded) {
    GameState st = makeWorld(2024, 12, 48);
    size_t initial = serializeState(st).size();
    // 玩家与 AI 各做一批动作后再落盘，体积不应爆炸
    for (int i = 0; i < 30; ++i) {
        runHeadlessTicks(st, 1);
    }
    size_t grown = serializeState(st).size();
    CHECK(initial > 0);
    CHECK(grown < 400 * 1024);   // 方案 §1：存档 < 400 KB
}
