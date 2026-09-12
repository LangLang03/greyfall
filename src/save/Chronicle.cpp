#include "save/Chronicle.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>

#include "crypto/Sha256.h"
#include "save/SlotManager.h"
#include "util/Bits.h"
#include "util/Str.h"

namespace fs = std::filesystem;

namespace gf {

std::string Chronicle::pathFor(const std::string& epochDir) { return epochDir + "/chronicle.gfc"; }

u64 Chronicle::linkHash(u64 prevHead, u64 tick, u64 actionDigest, u64 stateDigest) {
    u8 buf[32];
    writeLE64(buf, prevHead);
    writeLE64(buf + 8, tick);
    writeLE64(buf + 16, actionDigest);
    writeLE64(buf + 24, stateDigest);
    Sha256Digest d = sha256(buf, sizeof(buf));
    return readLE64(d.data());
}

bool Chronicle::exists(const std::string& file) {
    std::error_code ec;
    return fs::exists(file, ec);
}

std::vector<ChronicleEntry> Chronicle::read(const std::string& file) {
    std::vector<ChronicleEntry> out;
    std::vector<u8> bytes;
    if (!SlotManager::readFile(file, bytes)) return out;
    std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::string_view sv = trim(line);
        if (sv.empty() || sv[0] == '#') continue;
        // seq kind tick act state prev head [note...]
        auto parts = split(sv, ' ', true);
        if (parts.size() < 7) continue;
        ChronicleEntry e;
        e.seq = static_cast<u64>(parseInt(parts[0], 0));
        e.kind = parts[1].empty() ? ChronicleKind::Tick : static_cast<ChronicleKind>(parts[1][0]);
        e.tick = static_cast<u64>(parseInt(parts[2], 0));
        e.actionDigest = static_cast<u64>(strtoull(std::string(parts[3]).c_str(), nullptr, 16));
        e.stateDigest = static_cast<u64>(strtoull(std::string(parts[4]).c_str(), nullptr, 16));
        e.prevHead = static_cast<u64>(strtoull(std::string(parts[5]).c_str(), nullptr, 16));
        e.head = static_cast<u64>(strtoull(std::string(parts[6]).c_str(), nullptr, 16));
        if (parts.size() > 7) e.note = std::string(sv.substr(static_cast<std::size_t>(parts[7].data() - sv.data())));
        out.push_back(std::move(e));
    }
    return out;
}

u64 Chronicle::head(const std::string& file, bool* existsFlag) {
    std::vector<ChronicleEntry> entries = read(file);
    if (existsFlag) *existsFlag = !entries.empty() || exists(file);
    if (entries.empty()) return 0;
    return entries.back().head;
}

u64 Chronicle::append(const std::string& file, ChronicleKind kind, u64 tick, u64 actionDigest, u64 stateDigest,
                      std::string_view note) {
    std::vector<ChronicleEntry> entries = read(file);
    u64 prevHead = entries.empty() ? 0ull : entries.back().head;
    u64 seq = entries.empty() ? 1ull : entries.back().seq + 1ull;
    u64 h = linkHash(prevHead, tick, actionDigest, stateDigest);

    char buf[512];
    std::snprintf(buf, sizeof(buf), "%llu %c %llu %016llx %016llx %016llx %016llx%s%s\n",
                  static_cast<unsigned long long>(seq), static_cast<char>(kind),
                  static_cast<unsigned long long>(tick), static_cast<unsigned long long>(actionDigest),
                  static_cast<unsigned long long>(stateDigest), static_cast<unsigned long long>(prevHead),
                  static_cast<unsigned long long>(h), note.empty() ? "" : " ",
                  note.empty() ? "" : std::string(note).c_str());
    // 追加写：读旧内容 + 新行，再原子替换（保证链文件不会半写）
    std::string text;
    std::vector<u8> old;
    if (SlotManager::readFile(file, old) && !old.empty()) {
        text.assign(reinterpret_cast<const char*>(old.data()), old.size());
    } else {
        text = "# greyfall chronicle v1 (append-only hash chain)\n";
    }
    text += buf;
    std::vector<u8> out(text.begin(), text.end());
    SlotManager::writeFileAtomic(file, out);
    return h;
}

bool Chronicle::verify(const std::string& file, std::string* error) {
    std::vector<ChronicleEntry> entries = read(file);
    u64 prev = 0;
    u64 expectSeq = 1;
    for (const auto& e : entries) {
        if (e.seq != expectSeq) {
            if (error) *error = "链序号断裂于 " + std::to_string(e.seq);
            return false;
        }
        if (e.prevHead != prev) {
            if (error) *error = "链头不连续于序号 " + std::to_string(e.seq);
            return false;
        }
        u64 h = linkHash(e.prevHead, e.tick, e.actionDigest, e.stateDigest);
        if (h != e.head) {
            if (error) *error = "链节哈希不匹配于序号 " + std::to_string(e.seq);
            return false;
        }
        prev = e.head;
        ++expectSeq;
    }
    return true;
}

u64 Chronicle::count(const std::string& file, ChronicleKind kind) {
    u64 n = 0;
    for (const auto& e : read(file))
        if (e.kind == kind) ++n;
    return n;
}

void Chronicle::burn(const std::string& file, u64 tick) {
    std::error_code ec;
    fs::remove(file, ec);
    (void)append(file, ChronicleKind::Fork, tick, 0xDEADBEEFull, 0x0B0047EDull, "archive-burned");
}

}  // namespace gf
