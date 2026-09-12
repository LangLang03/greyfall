#pragma once
// 存档槽管理：目录解析、原子写、备份回退、环形检查点、剪枝
#include <string>
#include <vector>

#include "save/SaveFile.h"
#include "util/Fixed.h"

namespace gf {

inline constexpr int kCheckpointCount = 12;

struct SlotInfo {
    std::string id;
    std::string path;
    u64 size = 0;
    bool hasBackup = false;
    bool hasCheckpoints = false;
    bool valid = false;
    SaveHeaderInfo header;
};

class SlotManager {
public:
    /// $GREYFALL_DIR > $XDG_DATA_HOME/greyfall/saves > ~/.local/share/greyfall/saves
    [[nodiscard]] static std::string defaultDataDir();
    /// 默认构造：使用环境推导出的数据目录（CliEnv 需要可默认构造）
    SlotManager() : dir_(defaultDataDir()) {}
    explicit SlotManager(std::string dir);

    [[nodiscard]] const std::string& dir() const { return dir_; }
    [[nodiscard]] std::string slotPath(std::string_view id) const;
    [[nodiscard]] std::string bakPath(std::string_view id) const;
    [[nodiscard]] std::string tmpPath(std::string_view id) const;
    [[nodiscard]] std::string checkpointPath(std::string_view id, int idx) const;
    [[nodiscard]] std::string exportPath(std::string_view file) const;

    void ensureDir() const;
    [[nodiscard]] bool exists(std::string_view id) const;
    [[nodiscard]] std::vector<SlotInfo> list() const;
    [[nodiscard]] SlotInfo inspect(std::string_view id) const;

    /// 原子写：.tmp → flush → fsync → rename → 目录 fsync；成功时把旧档轮转为 .bak
    void writeSlot(std::string_view id, const std::vector<u8>& data);
    /// 读取（失败返回 false）；损坏时不回退
    [[nodiscard]] bool readSlot(std::string_view id, std::vector<u8>& out) const;
    /// 读取并回退到 .bak（用于损坏恢复）
    [[nodiscard]] bool readSlotWithFallback(std::string_view id, std::vector<u8>& out, bool* usedBackup) const;

    void writeCheckpoint(std::string_view id, int idx, const std::vector<u8>& data);
    [[nodiscard]] bool readCheckpoint(std::string_view id, int idx, std::vector<u8>& out) const;
    void rotateCheckpoints(std::string_view id, const std::vector<u8>& data);

    [[nodiscard]] bool removeSlot(std::string_view id, bool keepBackup);

    /// 通用原子写文件（export 等）
    static void writeFileAtomic(const std::string& path, const std::vector<u8>& data);
    [[nodiscard]] static bool readFile(const std::string& path, std::vector<u8>& out);
    [[nodiscard]] static bool fileExists(const std::string& path);

private:
    std::string dir_;
};

/// 剪枝：保留最近 keep 个槽（按修改时间），返回删除的槽 id 列表
[[nodiscard]] std::vector<std::string> pruneSlots(SlotManager& mgr, int keep, const std::string& protect);

}  // namespace gf
