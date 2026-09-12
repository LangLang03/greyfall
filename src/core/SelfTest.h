#pragma once
// 内置自检：约 40 项断言 + 固定种子自动对局
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct SelfTestResult {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;
    /// 200 tick 自动对局的性能与体积报告
    std::vector<std::string> report;
    bool ok() const { return failed == 0; }
};

/// 运行全部自检（不落盘、不改动调用者的状态）
SelfTestResult runSelfTest(bool verbose);

}  // namespace gf
