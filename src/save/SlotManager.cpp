#include "save/SlotManager.h"

// 平台抽象：Windows 没有 POSIX 的 fsync(2)/open(2)，改用 _commit/_open。
// 其余 POSIX 名称（stat / unlink / getpid）mingw-w64 已提供兼容层。
#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/types.h>
#define GF_FSYNC(fd) ::_commit(fd)
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#define GF_FSYNC(fd) ::fsync(fd)
#endif

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#include "core/Errors.h"
#include "util/Str.h"

namespace fs = std::filesystem;

namespace gf {
namespace {

void fsyncPath(const std::string& path) {
#if defined(_WIN32)
    // Windows 不允许以只读方式打开目录，也没有目录 fsync 语义。
    // NTFS 的元数据由系统保证，这里退化为空操作。
    (void)path;
#else
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd >= 0) {
        GF_FSYNC(fd);
        ::close(fd);
    }
#endif
}

std::string dirOf(const std::string& path) {
    std::size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    if (slash == 0) return path.substr(0, 1);
    return path.substr(0, slash);
}

bool validSlotId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (char c : id) {
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
                  c == '_' || c == '.';
        if (!ok) return false;
    }
    return id != "." && id != "..";
}

}  // namespace

std::string SlotManager::defaultDataDir() {
    if (const char* env = std::getenv("GREYFALL_DIR"); env && *env) return std::string(env);
#if defined(_WIN32)
    // Windows 惯例：%APPDATA%\greyfall\saves；回退到 %USERPROFILE%
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata)
        return std::string(appdata) + "\\greyfall\\saves";
    if (const char* up = std::getenv("USERPROFILE"); up && *up)
        return std::string(up) + "\\greyfall\\saves";
    return std::string(".\\greyfall\\saves");
#else
    std::string base;
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        base = std::string(xdg);
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        base = std::string(home) + "/.local/share";
    } else {
        base = ".";
    }
    return base + "/greyfall/saves";
#endif
}

SlotManager::SlotManager(std::string dir) : dir_(std::move(dir)) {}

std::string SlotManager::slotPath(std::string_view id) const {
    if (!validSlotId(id)) fail(ExitCode::BadArgs, "非法存档槽名：" + std::string(id));
    return dir_ + "/slot-" + std::string(id) + ".gsv";
}
std::string SlotManager::bakPath(std::string_view id) const {
    if (!validSlotId(id)) fail(ExitCode::BadArgs, "非法存档槽名：" + std::string(id));
    return dir_ + "/slot-" + std::string(id) + ".bak";
}
std::string SlotManager::tmpPath(std::string_view id) const {
    if (!validSlotId(id)) fail(ExitCode::BadArgs, "非法存档槽名：" + std::string(id));
    return dir_ + "/slot-" + std::string(id) + ".tmp";
}
std::string SlotManager::checkpointPath(std::string_view id, int idx) const {
    if (!validSlotId(id)) fail(ExitCode::BadArgs, "非法存档槽名：" + std::string(id));
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d", idx % kCheckpointCount);
    return dir_ + "/slot-" + std::string(id) + ".ck" + buf;
}
std::string SlotManager::exportPath(std::string_view file) const { return dir_ + "/" + std::string(file); }

void SlotManager::ensureDir() const {
    std::error_code ec;
    fs::create_directories(dir_, ec);
    if (ec && !fs::exists(dir_)) {
        fail(ExitCode::Internal, "无法创建存档目录 " + dir_ + "：" + ec.message());
    }
}

bool SlotManager::fileExists(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool SlotManager::exists(std::string_view id) const { return fileExists(slotPath(id)); }

bool SlotManager::readFile(const std::string& path, std::vector<u8>& out) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    struct stat st{};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return false;
    }
    out.resize(static_cast<std::size_t>(st.st_size));
    std::size_t got = 0;
    while (got < out.size()) {
        ssize_t n = ::read(fd, out.data() + got, out.size() - got);
        if (n <= 0) break;
        got += static_cast<std::size_t>(n);
    }
    ::close(fd);
    out.resize(got);
    return got == static_cast<std::size_t>(st.st_size);
}

void SlotManager::writeFileAtomic(const std::string& path, const std::vector<u8>& data) {
    // 父目录不存在时自动创建（并发/首写场景下必须健壮）
    {
        std::error_code ec;
        fs::create_directories(dirOf(path), ec);
    }

    // 临时文件名必须全局唯一：固定后缀 ".tmp" 在并发写同一路径时会被别的进程
    // rename 走，导致本进程 rename 报 ENOENT。
    static std::atomic<unsigned> seq{0};

    // 极少数文件系统/并发清理场景下，刚写完的临时文件可能被外部移除；
    // 因此整次写入带重试，并在 rename 前用 stat 自校验。
    std::string lastError;
    for (int attempt = 0; attempt < 3; ++attempt) {
        char suffix[64];
        std::snprintf(suffix, sizeof(suffix), ".tmp.%ld.%u", static_cast<long>(::getpid()), seq.fetch_add(1));
        std::string tmp = path + suffix;

        int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            lastError = "无法写入 " + tmp + "：" + std::strerror(errno);
            continue;
        }
        std::size_t off = 0;
        bool writeOk = true;
        while (off < data.size()) {
            ssize_t n = ::write(fd, data.data() + off, data.size() - off);
            if (n <= 0) {
                writeOk = false;
                lastError = "写入失败 " + tmp + "：" + std::strerror(errno);
                break;
            }
            off += static_cast<std::size_t>(n);
        }
        if (writeOk && GF_FSYNC(fd) != 0) {
            writeOk = false;
            lastError = "落盘失败 " + tmp + "：" + std::strerror(errno);
        }
        ::close(fd);
        if (!writeOk) {
            ::unlink(tmp.c_str());
            continue;
        }

        // 自校验：临时文件必须存在且长度正确
        struct stat st{};
        if (::stat(tmp.c_str(), &st) != 0 || !S_ISREG(st.st_mode) ||
            static_cast<std::size_t>(st.st_size) != data.size()) {
            lastError = "临时文件校验失败 " + tmp;
            ::unlink(tmp.c_str());
            continue;
        }

        // rename：对 EINTR 重试；ENOENT 说明临时文件被外部移除，直接整次重写
        int rc = -1;
        for (int r = 0; r < 4; ++r) {
            rc = ::rename(tmp.c_str(), path.c_str());
            if (rc == 0) break;
            if (errno != EINTR) break;
        }
        if (rc == 0) {
            fsyncPath(dirOf(path));
            return;
        }
        lastError = "rename 失败 " + path + "：" + std::strerror(errno);
        ::unlink(tmp.c_str());
    }
    fail(ExitCode::Internal, "原子写失败（重试 3 次）：" + lastError);
}

void SlotManager::writeSlot(std::string_view id, const std::vector<u8>& data) {
    ensureDir();
    std::string path = slotPath(id);
    std::string bak = bakPath(id);
    // 旧档轮转为 .bak
    if (fileExists(path)) {
        std::error_code ec;
        fs::remove(bak, ec);
        fs::rename(path, bak, ec);
        if (ec) {
            // 退化为复制
            std::vector<u8> old;
            if (readFile(path, old)) writeFileAtomic(bak, old);
        }
    }
    writeFileAtomic(path, data);
}

bool SlotManager::readSlot(std::string_view id, std::vector<u8>& out) const {
    return readFile(slotPath(id), out);
}

bool SlotManager::readSlotWithFallback(std::string_view id, std::vector<u8>& out, bool* usedBackup) const {
    if (usedBackup) *usedBackup = false;
    if (readFile(slotPath(id), out)) return true;
    if (readFile(bakPath(id), out)) {
        if (usedBackup) *usedBackup = true;
        return true;
    }
    return false;
}

void SlotManager::writeCheckpoint(std::string_view id, int idx, const std::vector<u8>& data) {
    ensureDir();
    writeFileAtomic(checkpointPath(id, idx), data);
}

bool SlotManager::readCheckpoint(std::string_view id, int idx, std::vector<u8>& out) const {
    return readFile(checkpointPath(id, idx), out);
}

void SlotManager::rotateCheckpoints(std::string_view id, const std::vector<u8>& data) {
    ensureDir();
    // 环形：ck00 → ck01 → ... → ck11 → ck00
    std::error_code ec;
    fs::remove(checkpointPath(id, 0), ec);
    for (int i = 1; i < kCheckpointCount; ++i) {
        std::string from = checkpointPath(id, i);
        if (fileExists(from)) fs::rename(from, checkpointPath(id, i - 1), ec);
    }
    writeFileAtomic(checkpointPath(id, kCheckpointCount - 1), data);
}

bool SlotManager::removeSlot(std::string_view id, bool keepBackup) {
    bool any = false;
    std::error_code ec;
    any |= fs::remove(slotPath(id), ec);
    if (!keepBackup) any |= fs::remove(bakPath(id), ec);
    for (int i = 0; i < kCheckpointCount; ++i) any |= fs::remove(checkpointPath(id, i), ec);
    return any;
}

SlotInfo SlotManager::inspect(std::string_view id) const {
    SlotInfo info;
    info.id = std::string(id);
    info.path = slotPath(id);
    std::vector<u8> bytes;
    info.hasBackup = fileExists(bakPath(id));
    for (int i = 0; i < kCheckpointCount; ++i)
        if (fileExists(checkpointPath(id, i))) {
            info.hasCheckpoints = true;
            break;
        }
    if (readFile(info.path, bytes)) {
        info.size = bytes.size();
        info.header = peekSave(bytes);
        info.valid = info.header.formatMajor == static_cast<u8>(kFormatMajor);
    }
    return info;
}

std::vector<SlotInfo> SlotManager::list() const {
    std::vector<SlotInfo> out;
    std::error_code ec;
    if (!fs::exists(dir_, ec)) return out;
    for (const auto& entry : fs::directory_iterator(dir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        if (!startsWith(name, "slot-") || !endsWith(name, ".gsv")) continue;
        std::string id = name.substr(5, name.size() - 5 - 4);
        out.push_back(inspect(id));
    }
    std::sort(out.begin(), out.end(), [](const SlotInfo& a, const SlotInfo& b) { return a.id < b.id; });
    return out;
}

std::vector<std::string> pruneSlots(SlotManager& mgr, int keep, const std::string& protect) {
    std::vector<SlotInfo> slots = mgr.list();
    std::vector<std::pair<long long, std::string>> withTime;
    for (const auto& s : slots) {
        long long t = 0;
        struct stat st{};
        if (::stat(s.path.c_str(), &st) == 0) t = static_cast<long long>(st.st_mtime);
        withTime.emplace_back(t, s.id);
    }
    std::sort(withTime.begin(), withTime.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    std::vector<std::string> removed;
    for (std::size_t i = 0; i < withTime.size(); ++i) {
        if (static_cast<int>(i) < keep) continue;
        if (withTime[i].second == protect) continue;
        if (keep <= 0) keep = 1;
        (void)mgr.removeSlot(withTime[i].second, false);
        removed.push_back(withTime[i].second);
    }
    return removed;
}

}  // namespace gf
