#include "core/Errors.h"

namespace gf {

const char* exitCodeName(ExitCode c) {
    switch (c) {
        case ExitCode::Ok: return "成功";
        case ExitCode::BadArgs: return "参数错误";
        case ExitCode::NoSave: return "存档不存在";
        case ExitCode::Integrity: return "完整性/密钥失败";
        case ExitCode::IllegalAction: return "非法动作";
        case ExitCode::PendingChoice: return "存在待抉择事件";
        case ExitCode::VersionMismatch: return "版本不兼容";
        case ExitCode::NoFill: return "市场成交未达成";
        case ExitCode::Internal: return "内部错误";
    }
    return "未知";
}

}  // namespace gf
