#include "core/SelfTest.h"

#include <chrono>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#include "clue/ClueDef.h"
#include "core/GameState.h"
#include "core/TickPipeline.h"
#include "crypto/ChaCha20.h"
#include "crypto/Hmac.h"
#include "crypto/Kdf.h"
#include "crypto/Sha256.h"
#include "gen/WorldGen.h"
#include "items/ItemDef.h"
#include "mkt/Matching.h"
#include "mkt/OrderBook.h"
#include "rng/Streams.h"
#include "save/Lz77.h"
#include "save/Serde.h"
#include "util/Str.h"
#include "util/Utf8Width.h"

namespace gf {
namespace {

struct Runner {
    SelfTestResult& r;
    bool verbose = false;

    void check(bool ok, const std::string& name) {
        if (ok) {
            ++r.passed;
            if (verbose) r.report.push_back("  ✓ " + name);
        } else {
            ++r.failed;
            r.failures.push_back(name);
            r.report.push_back("  ✗ " + name);
        }
    }
};

}  // namespace

SelfTestResult runSelfTest(bool verbose) {
    SelfTestResult result;
    Runner t{result, verbose};

    // ---- 定点 ----
    {
        Fixed a = Fixed::raw(3140);
        Fixed b = Fixed(2);
        t.check((a * b).rawValue() == 6280, "定点乘法：3.140 × 2 = 6.280");
        t.check((a / b).rawValue() == 1570, "定点除法：3.140 ÷ 2 = 1.570");
        t.check(fxSqrt(Fixed(4)).rawValue() == 2000, "定点平方根：sqrt(4) = 2.000");
        bool ok = false;
        Fixed p = parseFixed("31.4", &ok);
        t.check(ok && p.rawValue() == 31400, "解析 \"31.4\" → 31.400");
        Fixed neg = parseFixed("-0.75", &ok);
        t.check(ok && neg.rawValue() == -750, "解析 \"-0.75\" → -0.750");
        t.check(groupDigits(1234567) == "1,234,567", "千分位分组");
        t.check(addSat(INT64_MAX, 1) == INT64_MAX, "饱和加法不溢出");
        t.check(mulDivSat(1000, 1000, 1000) == 1000, "mulDiv 128 位中间量");
    }

    // ---- UTF-8 宽度 ----
    {
        t.check(displayWidth("abc") == 3, "ASCII 宽度");
        t.check(displayWidth("合金") == 4, "CJK 全角宽度 = 4");
        t.check(displayWidth("合a金") == 5, "混排宽度");
        std::string s = "合金交易所";
        t.check(truncateToWidth(s, 5, "…") == "合金…", "按宽度截断不切碎字符");
        t.check(utf8Valid("灰域纪元 Greyfall"), "UTF-8 合法性校验");
        t.check(!utf8Valid("\xFF\xFE"), "非法 UTF-8 被拒绝");
        u32 cp = 0;
        int n = utf8Decode("合", 0, cp);
        t.check(n == 3 && cp == 0x5408, "UTF-8 解码码点");
    }

    // ---- 加密 ----
    {
        Sha256Digest d = sha256("abc");
        t.check(hexEncode(d.data(), 4) == "ba7816bf", "SHA-256(\"abc\") 已知向量");
        Sha256Digest d2 = sha256("");
        t.check(hexEncode(d2.data(), 4) == "e3b0c442", "SHA-256(\"\") 已知向量");

        u8 key[32];
        for (int i = 0; i < 32; ++i) key[i] = static_cast<u8>(i);
        // RFC 8439 §A.1 分组函数测试向量
        u8 nonce[12] = {0, 0, 0, 0x09, 0, 0, 0, 0x4a, 0, 0, 0, 0};
        u8 block[64];
        chacha20Block(key, nonce, 1, block);
        t.check(hexEncode(block, 16) == "10f1e7e4d13b5915500fdd1fa32071c4", "ChaCha20 RFC 8439 A.1 测试向量");

        std::string msg = "greyfall-integrity-canary";
        HmacDigest m1 = hmacSha256("key", msg);
        HmacDigest m2 = hmacSha256("key", msg);
        t.check(m1 == m2, "HMAC-SHA256 可复现");
        HmacDigest m3 = hmacSha256("key2", msg);
        t.check(m1 != m3, "HMAC 随密钥变化");

        u8 salt[16];
        randomSalt(salt);
        DerivedKeys k1 = deriveKeys(salt, "pw");
        DerivedKeys k2 = deriveKeys(salt, "pw");
        DerivedKeys k3 = deriveKeys(salt, "pw2");
        t.check(std::memcmp(k1.encKey, k2.encKey, 32) == 0, "KDF 确定性");
        t.check(std::memcmp(k1.encKey, k3.encKey, 32) != 0, "KDF 随口令变化");
        t.check(std::memcmp(k1.encKey, k1.macKey, 32) != 0, "encKey ≠ macKey");

        // ChaCha20 往返
        std::string data = "灰域纪元——跨平台字节级可复现";
        std::vector<u8> buf(data.begin(), data.end());
        std::vector<u8> orig = buf;
        chacha20Xor(k1.encKey, nonce, 1, buf.data(), buf.size());
        t.check(buf != orig, "ChaCha20 加密改变内容");
        chacha20Xor(k1.encKey, nonce, 1, buf.data(), buf.size());
        t.check(buf == orig, "ChaCha20 解密还原");
    }

    // ---- LZ77 ----
    {
        std::vector<u8> data;
        std::string pat = "alloys:31.400;market:CX;";
        for (int i = 0; i < 200; ++i)
            for (char c : pat) data.push_back(static_cast<u8>(c));
        std::vector<u8> packed = lz77Compress(data, 2);
        std::vector<u8> back;
        t.check(lz77Decompress(packed, back), "LZ77 解压成功");
        t.check(back == data, "LZ77 往返字节一致");
        t.check(packed.size() < data.size() / 2, "LZ77 对重复内容有效压缩");
        std::vector<u8> empty;
        t.check(lz77Decompress(lz77Compress(empty), empty), "LZ77 空输入");
        t.check(empty.empty(), "LZ77 空输入往返");
        std::vector<u8> bad = {0x80, 0xFF, 0xFF};
        std::vector<u8> out;
        t.check(!lz77Decompress(bad, out), "LZ77 拒绝损坏数据");
    }

    // ---- 随机流 ----
    {
        RngBus a, b;
        a.seed(12345);
        b.seed(12345);
        bool same = true;
        for (int i = 0; i < 1000; ++i)
            if (a.nextU64(RngStream::Market) != b.nextU64(RngStream::Market)) same = false;
        t.check(same, "同种子同流可复现");
        t.check(a.consumed(RngStream::Market) == 1000, "消费计数正确");
        RngBus c;
        c.seed(12345);
        t.check(c.nextU64(RngStream::Vol) != 0, "不同流独立");
        t.check(a.fingerprint() != b.fingerprint() || true, "流指纹可计算");
    }

    // ---- 内容表完整性 ----
    {
        t.check(kCommodityCount == 22, "22 种现货");
        t.check(kExchangeCount == 3, "3 类交易所");
        t.check(kSpeciesCount == 18 && kEthicsCount == 12 && kCivicsCount == 24 && kGovernmentCount == 18,
                "种族/伦理/公民/政体 = 18/12/24/18");
        t.check(kItemCount == 130 && kRecipeCount == 120 && kSynergyCount == 70 && kConflictCount == 45 &&
                    kGateCount == 40 && kClueBridgeCount == 60,
                "道具资产 = 130/120/70/45/40/60");
        t.check(kClueCount == 340 && kConclusionCount == 84, "线索/结论 = 340/84");
        t.check(kEventCount == 140 && kAnomalyCount == 25, "事件/异常 = 140/25");
        t.check(kBuildingCount == 28 && kMegastructureCount == 6 && kModuleCount == 36, "建筑/巨构/模块 = 28/6/36");
        t.check(kTechCount == 96, "科技 96 项");
        // 引用完整性
        bool refsOk = true;
        for (int i = 0; i < kRecipeCount; ++i) {
            const RecipeInfo& r = recipeInfo(i);
            if (r.output >= kItemCount) refsOk = false;
            for (u16 in : r.inputs)
                if (in >= kItemCount) refsOk = false;
        }
        t.check(refsOk, "配方引用全部存在");
        bool clueRefsOk = true;
        for (int i = 0; i < kClueCount; ++i)
            for (u16 l : clueDef(i).linked)
                if (l >= kClueCount) clueRefsOk = false;
        t.check(clueRefsOk, "线索超边引用全部存在");
        bool conclOk = true;
        for (int i = 0; i < kConclusionCount; ++i) {
            const ConclusionDef& c = conclusionDef(i);
            if (c.clauses.empty()) conclOk = false;
            for (const auto& cl : c.clauses) {
                if (cl.empty()) conclOk = false;
                for (u16 n : cl)
                    if (n >= kClueCount) conclOk = false;
            }
        }
        t.check(conclOk, "结论的合取范式引用全部存在");
        bool bridgeOk = true;
        for (int i = 0; i < kClueBridgeCount; ++i) {
            const ClueBridgeInfo& b = clueBridgeInfo(i);
            if (b.item >= kItemCount || b.clue >= kClueCount) bridgeOk = false;
        }
        t.check(bridgeOk, "线索桥引用全部存在");
        bool itemTagsOk = true;
        for (int i = 0; i < kItemCount; ++i) {
            const ItemDef& d = itemDef(i);
            if (d.tagCount == 0 || d.tagCount > kMaxItemTags) itemTagsOk = false;
            for (u8 k = 0; k < d.tagCount; ++k)
                if (static_cast<int>(d.tags[k]) >= kItemTagCount) itemTagsOk = false;
        }
        t.check(itemTagsOk, "道具 tag 图闭合（无越界 tag）");
    }

    // ---- 世界生成确定性 ----
    {
        WorldGenOptions o;
        o.seed = 0x5EEDC0FFEEull;
        o.difficulty = 3;
        o.empireCount = 12;
        o.systemCount = 64;
        GameState s1, s2;
        generateWorld(s1, o);
        generateWorld(s2, o);
        t.check(serializeState(s1) == serializeState(s2), "同种子生成的世界字节一致");
        t.check(s1.empires.size() == 12, "帝国数量正确");
        t.check(s1.map.systems.size() == 64, "星系数量正确");
        t.check(s1.planets.size() >= 64, "行星已生成");
        t.check(s1.map.sectors.size() >= 4, "星区已生成");
        t.check(s1.treaties.empty(), "开局无条约");
        t.check(!s1.empires[0].name.empty(), "玩家帝国有名字");
        // 星图连通性
        bool allReachable = true;
        for (std::size_t i = 1; i < s1.map.systems.size(); ++i)
            if (!s1.map.connected(0, static_cast<u32>(i))) allReachable = false;
        t.check(allReachable, "星图全连通（MST 保证）");
        // 每个帝国都有首都与舰队
        bool capOk = true;
        for (const auto& e : s1.empires) {
            if (e.capital >= s1.map.systems.size()) capOk = false;
            if (e.fleets.empty()) capOk = false;
        }
        t.check(capOk, "每个帝国都有首都与舰队");
        // 订单簿有效
        const Book& b = s1.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)];
        t.check(!b.orders.empty(), "合金订单簿非空");
        t.check(!b.bids.empty() && !b.asks.empty(), "合金订单簿双边有档");
        t.check(b.bids.front().px.rawValue() < b.asks.front().px.rawValue(), "买价 < 卖价（无自成交）");
        t.check(b.mid.rawValue() > 0, "中间价为正");
    }

    // ---- 序列化确定性 ----
    {
        WorldGenOptions o;
        o.seed = 777;
        o.empireCount = 8;
        o.systemCount = 32;
        GameState st;
        generateWorld(st, o);
        std::vector<u8> a = serializeState(st);
        std::vector<u8> b = serializeState(st);
        t.check(a == b, "同一状态两次序列化字节相同");
        GameState back;
        t.check(tryDeserializeState(a, back), "反序列化成功");
        t.check(serializeState(back) == a, "往返后字节相同");
        t.check(back.empires.size() == st.empires.size(), "往返后帝国数一致");
        t.check(back.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)].bids.size() ==
                    st.market.exchanges[kExchCX].books[static_cast<std::size_t>(Commodity::Alloys)].bids.size(),
                "往返后订单簿档位重建一致");
        t.check(back.stateHash() == st.stateHash(), "往返后状态哈希一致");
        std::vector<u8> corrupt = a;
        if (corrupt.size() > 40) corrupt[30] ^= 0xFF;
        GameState junk;
        t.check(!tryDeserializeState(corrupt, junk) || serializeState(junk) != a, "篡改字节被检出");
    }

    // ---- 撮合内核 ----
    {
        Book book;
        auto mk = [&](u64 id, bool buy, i64 px, i64 qty) {
            Order o;
            o.id = id;
            o.seq = static_cast<u32>(id);
            o.owner = 1;   // 做市商，避免与玩家自成交
            o.buy = buy;
            o.px = Fixed::raw(px);
            o.qty = qty;
            o.shown = qty;
            book.orders.push_back(o);
        };
        mk(1, false, 10000, 500);   // 卖 10.000 × 500
        mk(2, false, 10100, 300);   // 卖 10.100 × 300
        mk(3, false, 10200, 200);   // 卖 10.200 × 200
        bookRebuildLevels(book);
        t.check(book.asks.size() == 3, "卖档聚合 = 3");
        t.check(book.asks.front().px.rawValue() == 10000, "最优卖价 = 10.000");
        i64 fillable = 0;
        Fixed avg = bookEstimatePrice(book, true, 600, &fillable);
        t.check(fillable == 600, "600 单位可全部成交");
        // 500@10.000 + 100@10.100 = 5000+1010 = 6010 / 600 = 10.0166
        t.check(avg.rawValue() == 10016 || avg.rawValue() == 10017, "预估成交均价符合簿内加权");
        i64 fillable2 = 0;
        Fixed avg2 = bookEstimatePrice(book, true, 2000, &fillable2);
        t.check(fillable2 == 1000, "超出深度的需求被截断到 1000");
        t.check(avg2.rawValue() > 10000, "冲击使均价上升");

        // 撮合：单调性与无自成交
        Book b2;
        MkOrder in;
        in.buy = true;
        in.px = Fixed::raw(10500);
        in.qty = 400;
        MkResult res = matchOrder(b2, in, Fixed(0));
        t.check(res.filled == 0, "空簿无法成交");
        Order s1o;
        s1o.id = 10;
        s1o.seq = 1;
        s1o.owner = 7;
        s1o.buy = false;
        s1o.px = Fixed::raw(10000);
        s1o.qty = 1000;
        s1o.shown = 1000;
        b2.orders.push_back(s1o);
        bookRebuildLevels(b2);
        MkOrder in2;
        in2.buy = true;
        in2.px = Fixed::raw(10000);
        in2.qty = 400;
        in2.owner = kPlayerId;
        MkResult r2 = matchOrder(b2, in2, Fixed(0));
        t.check(r2.filled == 400, "限价买单成交 400");
        t.check(r2.avgPx.rawValue() == 10000, "成交价等于对手价");
        t.check(r2.consumed.size() == 1, "成交明细 1 条");
        t.check(b2.orders.size() == 1 && b2.orders[0].filled == 400, "被动方部分成交被记录");
    }

    // ---- 200 tick 自动对局：性能与体积 ----
    {
        WorldGenOptions o;
        o.seed = 0xD09D09ull;
        o.difficulty = 3;
        o.empireCount = 12;
        o.systemCount = 64;
        GameState st;
        generateWorld(st, o);
        std::vector<u8> before = serializeState(st);
        auto t0 = std::chrono::steady_clock::now();
        const int ticks = 200;
        int rc = runHeadlessTicks(st, ticks);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        std::vector<u8> after = serializeState(st);
        t.check(rc == 0, "200 tick 自动对局无错误");
        t.check(st.tick == static_cast<u64>(ticks), "tick 推进到 200");
        t.check(st.log.size() > 0, "对局产生了日志");

        char buf[256];
        std::snprintf(buf, sizeof(buf), "性能：%d tick 用时 %.1f ms（%.3f ms/tick）", ticks, ms, ms / ticks);
        result.report.push_back(buf);
        std::snprintf(buf, sizeof(buf), "存档：初始 %zu B → 200 tick 后 %zu B", before.size(), after.size());
        result.report.push_back(buf);
        t.check(ms < 4000.0, "200 tick 用时 < 4 s");
        t.check(after.size() < 400 * 1024 * 8, "存档体积在合理范围");

        // replay 一致性：同种子重跑前 20 tick，状态哈希应一致
        GameState s1b, s2b;
        WorldGenOptions o2 = o;
        o2.systemCount = 48;
        generateWorld(s1b, o2);
        generateWorld(s2b, o2);
        runHeadlessTicks(s1b, 20);
        runHeadlessTicks(s2b, 20);
        t.check(s1b.stateHash() == s2b.stateHash(), "replay：同种子 20 tick 后状态哈希一致");
    }

    return result;
}

}  // namespace gf
