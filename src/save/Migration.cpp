#include "save/Migration.h"

#include <vector>

#include "core/GameState.h"

namespace gf {
namespace migration {
namespace {

// 字段迁移在版本感知的 Serde 读取中完成；此表供文件检查与 CLI 升级使用。
constexpr Step kSteps[] = {
    {2, 3, "v3：抉择类型与延期、保险保单、期货归属与到期日"},
    {3, 4, "v4：连续治理季数与征服胜利记录"},
    {4, 5, "v5：殖民与国家工程、殖民发展期、限期法令与飞升记录"},
};
constexpr std::size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);

}  // namespace

const Step* steps(std::size_t& count) {
    count = kStepCount;
    return kSteps;
}

bool canUpgrade(int fromSchema) {
    if (fromSchema == kSchemaVersion) return true;
    if (fromSchema > kSchemaVersion) return false;
    int cur = fromSchema;
    while (cur < kSchemaVersion) {
        bool found = false;
        for (std::size_t i = 0; i < kStepCount; ++i) {
            if (kSteps[i].from == cur) {
                cur = kSteps[i].to;
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return cur == kSchemaVersion;
}

bool chain(int fromSchema, GameState& st, std::string* error) {
    if (fromSchema > kSchemaVersion) {
        if (error) *error = "存档版本 " + std::to_string(fromSchema) + " 高于本程序支持的 " +
                            std::to_string(kSchemaVersion);
        return false;
    }
    int cur = fromSchema;
    int guard = 0;
    while (cur < kSchemaVersion) {
        if (++guard > 64) {
            if (error) *error = "迁移链出现环";
            return false;
        }
        const Step* hit = nullptr;
        for (std::size_t i = 0; i < kStepCount; ++i) {
            if (kSteps[i].from == cur) {
                hit = &kSteps[i];
                break;
            }
        }
        if (hit == nullptr) {
            if (error) *error = "缺失从 schema " + std::to_string(cur) + " 出发的迁移步骤";
            return false;
        }
        cur = hit->to;
    }
    st.schemaVersion = static_cast<u32>(kSchemaVersion);
    return true;
}

std::string describe(int fromSchema) {
    if (fromSchema == kSchemaVersion) return "已是最新（schema " + std::to_string(kSchemaVersion) + "）";
    std::string path = std::to_string(fromSchema);
    int cur = fromSchema;
    int guard = 0;
    while (cur < kSchemaVersion && ++guard < 64) {
        const Step* hit = nullptr;
        for (std::size_t i = 0; i < kStepCount; ++i)
            if (kSteps[i].from == cur) {
                hit = &kSteps[i];
                break;
            }
        if (!hit) {
            path += " → [缺失]";
            break;
        }
        path += " → " + std::to_string(hit->to);
        cur = hit->to;
    }
    return path;
}

}  // namespace migration
}  // namespace gf
