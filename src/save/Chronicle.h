#pragma once
// Chronicle —— 防读档重来的博弈化：仅追加哈希链
//
//   head_{n+1} = SHA256(head_n || tick || actionDigest || stateDigest)
//
// load / rollback 时写入 R(rollback) / L(load) / F(fork) 记录；
// AI 在 OmniscientReader 里能看到 rollbackCount 与链头 ⇒ 阵营判定"你会重试"，
// 从而抬高要价、降低让步概率、优先采用不可逆打击。
// 链文件缺失 ⇒ 触发「档案焚毁」剧情（不奖励，且各方视为背约者）。
#include <string>
#include <string_view>
#include <vector>

#include "util/Fixed.h"

namespace gf {

enum class ChronicleKind : char { Tick = 'T', Rollback = 'R', Load = 'L', Fork = 'F', Epoch = 'E', New = 'N' };

struct ChronicleEntry {
    u64 seq = 0;
    ChronicleKind kind = ChronicleKind::Tick;
    u64 tick = 0;
    u64 actionDigest = 0;
    u64 stateDigest = 0;
    u64 prevHead = 0;
    u64 head = 0;
    std::string note;
};

class Chronicle {
public:
    /// <epochDir>/chronicle.gfc
    [[nodiscard]] static std::string pathFor(const std::string& epochDir);
    /// 读取链头；文件不存在返回 0 且 exists=false
    [[nodiscard]] static u64 head(const std::string& file, bool* exists = nullptr);
    /// 追加一条记录，返回新的链头
    [[nodiscard]] static u64 append(const std::string& file, ChronicleKind kind, u64 tick, u64 actionDigest,
                                    u64 stateDigest, std::string_view note = {});
    /// 读取全部记录
    [[nodiscard]] static std::vector<ChronicleEntry> read(const std::string& file);
    /// 校验链完整性（逐个重算 head）
    [[nodiscard]] static bool verify(const std::string& file, std::string* error);
    /// 统计某类记录数
    [[nodiscard]] static u64 count(const std::string& file, ChronicleKind kind);
    /// 计算链节
    [[nodiscard]] static u64 linkHash(u64 prevHead, u64 tick, u64 actionDigest, u64 stateDigest);
    /// 链文件是否存在
    [[nodiscard]] static bool exists(const std::string& file);
    /// 档案焚毁：删除链文件并追加一条焚毁标记（不奖励）
    static void burn(const std::string& file, u64 tick);
};

}  // namespace gf
