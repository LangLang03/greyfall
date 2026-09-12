#include "cli/ArgParser.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "core/Errors.h"
#include "util/Str.h"

namespace gf {
namespace {

// 布尔型选项：出现即真，不消费后续 token
constexpr std::array<std::string_view, 26> kBoolOptions = {
    "quiet",      "verbose",     "ascii",       "dry-run",     "no-autosave", "yes",
    "explain",    "commit",      "net",         "unlinked",    "threat",      "what-they-know",
    "counterintel", "decoy",     "report",      "next",        "endless",     "inherit-legacy",
    "plaintext",  "no-encrypt",  "force",       "all",         "help",        "short",
    "no-color",   "json",
};

}  // namespace

bool Args::isBoolOption(std::string_view name) {
    for (auto b : kBoolOptions)
        if (b == name) return true;
    return false;
}

Args Args::parse(int argc, char** argv) {
    Args a;
    for (int i = 1; i < argc; ++i) a.raw_.emplace_back(argv[i]);

    for (int i = 1; i < argc; ++i) {
        std::string tok = argv[i];
        if (tok.size() >= 2 && tok[0] == '-' && tok[1] == '-') {
            std::string body = tok.substr(2);
            std::string name, value;
            bool hasValue = false;
            std::size_t eq = body.find('=');
            if (eq != std::string::npos) {
                name = body.substr(0, eq);
                value = body.substr(eq + 1);
                hasValue = true;
            } else {
                name = body;
            }
            if (name.empty()) fail(ExitCode::BadArgs, "空选项名");
            if (!hasValue && !isBoolOption(name)) {
                // 预读：若下一个 token 不以 '-' 开头则作为值；否则视为布尔真
                if (i + 1 < argc) {
                    std::string next = argv[i + 1];
                    if (!next.empty() && next[0] != '-') {
                        value = next;
                        hasValue = true;
                        ++i;
                    }
                }
            }
            if (name == "action") {
                if (!a.action_.empty() && a.action_ != value) {
                    fail(ExitCode::BadArgs, "--action 与子命令冲突：" + a.action_ + " vs " + value);
                }
                a.action_ = value;
                continue;
            }
            if (!hasValue) {
                a.options_[name] = "1";
            } else {
                a.options_[name] = value;
            }
        } else {
            if (a.action_.empty()) {
                a.action_ = tok;
            } else {
                a.positional_.push_back(tok);
            }
        }
    }
    return a;
}

bool Args::has(std::string_view name) const { return options_.find(name) != options_.end(); }

std::string Args::get(std::string_view name, std::string def) const {
    auto it = options_.find(name);
    if (it == options_.end()) return def;
    return it->second;
}

i64 Args::getInt(std::string_view name, i64 def) const {
    auto it = options_.find(name);
    if (it == options_.end()) return def;
    bool ok = false;
    i64 v = parseInt(it->second, def);
    ok = !it->second.empty();
    if (!ok) return def;
    return v;
}

Fixed Args::getFixed(std::string_view name, Fixed def) const {
    auto it = options_.find(name);
    if (it == options_.end()) return def;
    bool ok = false;
    Fixed v = parseFixed(it->second, &ok);
    return ok ? v : def;
}

bool Args::getBool(std::string_view name) const { return has(name); }

std::string Args::pos(std::size_t i, std::string def) const {
    if (i >= positional_.size()) return def;
    return positional_[i];
}

i64 Args::posInt(std::size_t i, i64 def) const {
    if (i >= positional_.size()) return def;
    return parseInt(positional_[i], def);
}

Fixed Args::posFixed(std::size_t i, Fixed def) const {
    if (i >= positional_.size()) return def;
    bool ok = false;
    Fixed v = parseFixed(positional_[i], &ok);
    return ok ? v : def;
}

}  // namespace gf
