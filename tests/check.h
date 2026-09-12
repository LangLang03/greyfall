#pragma once
// 极简测试框架（零依赖）：静态注册 + 断言 + 汇总
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace gftest {

struct Case {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

class Registry {
public:
    static Registry& instance() {
        static Registry r;
        return r;
    }
    void add(const char* suite, const char* name, std::function<void()> fn) {
        cases_.push_back(Case{suite, name, std::move(fn)});
    }
    [[nodiscard]] std::vector<Case>& cases() { return cases_; }

    // 断言计数
    void pass() { ++passed_; }
    void fail(const std::string& msg) {
        ++failed_;
        failures_.push_back(current_ + ": " + msg);
    }
    [[nodiscard]] int passed() const { return passed_; }
    [[nodiscard]] int failed() const { return failed_; }
    [[nodiscard]] const std::vector<std::string>& failures() const { return failures_; }
    void setCurrent(const std::string& c) { current_ = c; }

private:
    std::vector<Case> cases_;
    std::vector<std::string> failures_;
    std::string current_;
    int passed_ = 0;
    int failed_ = 0;
};

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        Registry::instance().add(suite, name, std::move(fn));
    }
};

/// 断言：失败时记录但不中断（便于一次看到全部问题）
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (cond) {                                                                        \
            ::gftest::Registry::instance().pass();                                         \
        } else {                                                                           \
            ::gftest::Registry::instance().fail(std::string(__FILE__) + ":" +              \
                                                std::to_string(__LINE__) + " CHECK(" #cond ")"); \
        }                                                                                  \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        auto va = (a);                                                                     \
        auto vb = (b);                                                                     \
        if (va == vb) {                                                                    \
            ::gftest::Registry::instance().pass();                                         \
        } else {                                                                           \
            ::gftest::Registry::instance().fail(std::string(__FILE__) + ":" +              \
                                                std::to_string(__LINE__) + " CHECK_EQ(" #a ", " #b ")"); \
        }                                                                                  \
    } while (0)

#define TEST(suite, name)                                                                  \
    static void gftest_##suite##_##name();                                                 \
    static ::gftest::Registrar gftest_reg_##suite##_##name(#suite, #name, gftest_##suite##_##name); \
    static void gftest_##suite##_##name()

}  // namespace gftest

#define GF_TEST_MAIN()                                                                     \
    int main(int argc, char** argv) {                                                      \
        std::string filter = argc > 1 ? argv[1] : "";                                      \
        auto& reg = ::gftest::Registry::instance();                                        \
        int ran = 0;                                                                       \
        for (auto& c : reg.cases()) {                                                      \
            if (!filter.empty() && c.suite.find(filter) == std::string::npos &&            \
                c.name.find(filter) == std::string::npos)                                  \
                continue;                                                                  \
            reg.setCurrent(c.suite + "." + c.name);                                        \
            c.fn();                                                                        \
            ++ran;                                                                         \
        }                                                                                  \
        std::printf("运行 %d 个用例，断言 %d 通过，%d 失败\n", ran, reg.passed(), reg.failed()); \
        for (const auto& f : reg.failures()) std::printf("  ✗ %s\n", f.c_str());           \
        return reg.failed() == 0 ? 0 : 1;                                                   \
    }
