// 并发压力：多个进程同时向同一路径做原子写
//
// 回归守卫：早期实现使用固定后缀 ".tmp"，当两个进程写同一路径时，
// 先完成的进程会把临时文件 rename 走，后者随即在 rename 处报 ENOENT。
// 现在临时名包含 pid + 递增序号，并以本用例长期守护该性质。
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>

#include "save/SlotManager.h"

using namespace gf;

int main(int argc, char** argv) {
    const char* envDir = std::getenv("GREYFALL_RACE_DIR");
    std::string dir = envDir != nullptr ? envDir : "build/test-data/race";
    int procs = argc > 1 ? std::atoi(argv[1]) : 16;
    int iters = argc > 2 ? std::atoi(argv[2]) : 60;
    if (procs < 2) procs = 2;
    if (procs > 64) procs = 64;
    if (iters < 1) iters = 1;
    if (iters > 2000) iters = 2000;

    std::string target = dir + "/shared.bin";
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir, ec);
    }

    std::vector<pid_t> kids;
    kids.reserve(static_cast<std::size_t>(procs));
    for (int i = 0; i < procs; ++i) {
        pid_t p = fork();
        if (p == 0) {
            // 子进程：全部写同一个目标路径，最大化竞态
            for (int k = 0; k < iters; ++k) {
                std::vector<u8> data(1024, static_cast<u8>((i * 31 + k) & 0xFF));
                try {
                    SlotManager::writeFileAtomic(target, data);
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "子进程 %d 第 %d 次写入失败：%s\n", i, k, e.what());
                    _exit(1);
                }
            }
            _exit(0);
        }
        kids.push_back(p);
    }

    int bad = 0;
    for (pid_t p : kids) {
        int st = 0;
        if (waitpid(p, &st, 0) < 0) {
            ++bad;
            continue;
        }
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) ++bad;
    }

    // 最终态必须完整可读（原子写的核心保证）
    std::vector<u8> out;
    bool readOk = SlotManager::readFile(target, out);
    bool sizeOk = readOk && out.size() == 1024;

    std::printf("并发原子写：%d 进程 × %d 次 → 失败进程 %d，结果文件 %s\n", procs, iters, bad,
                sizeOk ? "完整" : "损坏");
    return (bad == 0 && sizeOk) ? 0 : 1;
}
