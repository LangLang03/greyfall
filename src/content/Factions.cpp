#include "domain/Domestic.h"

#include <string>
#include <vector>

#include "util/Str.h"

namespace gf {
namespace {

constexpr const char* kNames[] = {"军部", "商会", "技术官僚", "原教旨", "劳工", "旧贵族", "民粹", "辛迪加"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<std::size_t>(FactionKind::Count));

constexpr const char* kDemands[] = {
    "要求提高军费至财政的 30%", "要求削减关税并开放航权", "要求把研究预算翻倍",
    "要求取缔异端出版物",       "要求提高粮食配给与最低保障", "要求恢复世袭特权",
    "要求举行全民公投",         "要求赦免灰色贸易",
};

}  // namespace

std::string_view factionKindName(FactionKind k) {
    std::size_t i = static_cast<std::size_t>(k);
    if (i >= static_cast<std::size_t>(FactionKind::Count)) return "?";
    return kNames[i];
}

FactionKind factionKindFromName(std::string_view s) {
    for (int i = 0; i < static_cast<int>(FactionKind::Count); ++i)
        if (factionKindName(static_cast<FactionKind>(i)) == s) return static_cast<FactionKind>(i);
    return FactionKind::Merchant;
}

std::string factionDemandText(FactionKind k) {
    std::size_t i = static_cast<std::size_t>(k);
    if (i >= static_cast<std::size_t>(FactionKind::Count)) i = 0;
    return kDemands[i];
}

}  // namespace gf
