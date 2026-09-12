#include "gen/NameGen.h"

#include <array>

namespace gf {
namespace {

constexpr const char* kStems[] = {
    "赫尔维", "塔兰",   "瑟兰",   "卡西尔", "伊斯",   "瓦尔",   "诺德",   "阿刻",   "泽塔",   "缪恩",
    "伽兰",   "赫斯",   "欧泊",   "拉刻",   "西格",   "泰伦",   "维兰",   "乌尔",   "缇亚",   "那刹",
    "刻兰",   "索恩",   "拜星",   "灰港",   "白垩",   "寂岭",   "双环",   "远钟",   "铁砧",   "霜羽",
    "长昼",   "沉钟",   "碎阶",   "夜潮",   "残响",   "初火",
};

constexpr const char* kSuffixes[] = {
    "帝国", "联邦", "共和国", "商盟", "教团", "联合体", "汗国", "议会", "氏族", "行会",
    "共主邦联", "殖民地联盟", "技术政体", "托管地", "教权国", "自由邦",
};

constexpr const char* kAdjectives[] = {
    "赫尔维的", "塔兰的", "瑟兰的", "远钟的", "铁砧的", "霜羽的", "长昼的", "沉钟的", "夜潮的", "初火的",
};

constexpr const char* kRulerTitles[] = {
    "执政官", "大公", "首席", "大主教", "元老", "督军", "总理", "守护者", "至高者", "代言人",
};

constexpr const char* kRulerNames[] = {
    "阿德里安", "瑟琳娜", "卡西安", "薇拉", "塔尔", "伊南", "奥兰", "弥涅", "瑞恩", "洛安",
    "赫尔加", "塔西", "维恩", "索菲", "厄本", "卡莉", "诺兰", "伊莱", "慕兰", "泽诺",
};

constexpr const char* kSystemPrefix[] = {
    "阿尔法", "贝塔", "伽马", "德尔塔", "艾普", "泽塔", "伊塔", "西塔", "约塔", "卡帕",
    "兰布达", "缪", "纽", "克西", "奥密", "派", "柔", "西格", "陶", "宇普",
};

constexpr const char* kSystemNoun[] = {
    "港", "门", "径", "铃", "井", "脊", "渊", "环", "塔", "渡", "礁", "垠", "墩", "眼", "喉", "锚",
};

constexpr const char* kPlanetPrefix[] = {
    "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII",
};

constexpr const char* kFleetPrefix[] = {
    "第一", "第二", "第三", "第四", "第五", "第六", "第七", "第八", "第九", "第十",
    "先锋", "后卫", "远征", "铁壁", "游骑", "静默",
};

constexpr const char* kShipWords[] = {
    "隼", "矛", "盾", "锤", "刃", "炬", "鸦", "鲸", "蛛", "萤", "岩", "潮",
};

constexpr const char* kShipSuffix[] = {"级", "型", "号", "式"};

constexpr const char* kEpochWords[] = {
    "大沉默", "泛视", "断链", "回声", "空白纪", "长夜", "低语", "灰烬", "铁誓", "远钟", "空壳", "双影",
};

}  // namespace

std::string NameGen::pick(const char* const* arr, std::size_t n) {
    return std::string(arr[rng_.nextU64() % n]);
}

std::string NameGen::empire() { return pick(kStems, std::size(kStems)) + pick(kSuffixes, std::size(kSuffixes)); }

std::string NameGen::empireAdj() { return pick(kAdjectives, std::size(kAdjectives)); }

std::string NameGen::ruler() { return pick(kRulerTitles, std::size(kRulerTitles)) + " " + pick(kRulerNames, std::size(kRulerNames)); }

std::string NameGen::system() {
    return pick(kSystemPrefix, std::size(kSystemPrefix)) + pick(kSystemNoun, std::size(kSystemNoun));
}

std::string NameGen::planet(int index) {
    std::size_t i = static_cast<std::size_t>(index) % std::size(kPlanetPrefix);
    return std::string(kPlanetPrefix[i]) + " · " + system();
}

std::string NameGen::sector() { return pick(kEpochWords, std::size(kEpochWords)) + "星区"; }

std::string NameGen::fleet() { return pick(kFleetPrefix, std::size(kFleetPrefix)) + "舰队"; }

std::string NameGen::shipDesign() {
    return pick(kShipWords, std::size(kShipWords)) + pick(kShipSuffix, std::size(kShipSuffix));
}

std::string NameGen::federation() { return pick(kStems, std::size(kStems)) + "星海联邦"; }

std::string NameGen::epoch() {
    return pick(kEpochWords, std::size(kEpochWords)) + "纪元 · " + std::to_string(rng_.nextU64() % 9000 + 1000);
}

}  // namespace gf
