#include "domain/Crisis.h"

#include <string>
#include <vector>

#include "domain/Resource.h"
#include "util/Str.h"

namespace gf {
namespace {

// 30 危机
const char* kCrisis[] = {
    "大沉默回响", "泛视网络过载", "边疆大饥荒", "合金断供", "难民舰队入境", "黑市崩盘", "私掠者潮",
    "联邦分裂危机", "灵能瘟疫", "机械觉醒", "航道坍缩", "恒星耀斑", "反物质泄漏", "信用冻结",
    "殖民地叛乱", "派系内战", "数据晶虫害", "生态崩溃", "封锁升级", "瘟疫检疫",
    "维和部队哗变", "遗物共振", "时间褶皱", "虚空潮汐", "观测者降临", "旧日武器解锁",
    "加密货币挤兑", "巨构事故", "银河议会停摆", "沉默信号重现",
};
// 25 市场冲击
const char* kShock[] = {
    "合金恐慌抛售", "能源价格跳涨", "药品禁运", "反物质投机泡沫", "粮油囤积", "航运保险费暴涨",
    "拍卖行操纵曝光", "央行加息", "期货逼空", "做市商撤单", "保证金级联", "跨境套利崩塌",
    "数据晶矿难", "超材料工艺泄露", "违禁品大案", "信用评级下调", "主权债违约", "汇率脱钩",
    "储备释放", "战备采购", "关税突袭", "白市黑市并轨", "结算系统故障", "内幕抛售",
    "指数再平衡",
};
// 20 外交
const char* kDiplo[] = {
    "使节遇刺", "边境摩擦", "联合军演", "情报交换提案", "王室联姻", "贸易协定谈判",
    "难民遣返争议", "战俘交换", "共同科研计划", "联邦入盟申请", "宗藩要求", "经济制裁威胁",
    "间谍网络曝光", "历史旧账追索", "航线通行权谈判", "联合舰队巡航", "宗教朝圣通行",
    "技术转让争议", "傀儡政权请求", "求和使团",
};
// 25 异常
const char* kAnomalyEvents[] = {
    "引力异常区", "废弃观测站", "会呼吸的陨石", "逆熵晶体", "沉默信标", "无人舰队残骸",
    "时间胶囊", "镜像行星", "空壳神像", "语言病毒", "自修复废墟", "低温休眠仓",
    "星图碎片", "非欧几何站", "幽灵信号源", "共生菌毯", "反物质泉", "记忆之海",
    "断裂的环", "观测者之眼", "被抹除的殖民地", "第七种元素", "回声井", "无名者营地",
    "终末协议",
};
// 40 剧情
const char* kStory[] = {
    "第一幕：静默的广播", "第一幕：第一批难民", "第一幕：被删除的星图", "第一幕：档案馆的空白",
    "第一幕：旧日誓言", "第二幕：航线断裂", "第二幕：走私者的证词", "第二幕：禁运清单",
    "第二幕：失踪的测绘船", "第二幕：黑市的预言", "第二幕：边境的哭声", "第三幕：断链",
    "第三幕：泛视协议", "第三幕：全知者的邀请", "第三幕：观测站事故", "第三幕：数据晶之梦",
    "第三幕：加密的忏悔", "第四幕：观测者的注视", "第四幕：全景监狱", "第四幕：镜中盟友",
    "第四幕：隐私的价格", "第四幕：被读取的舰队", "第五幕：背叛的账本", "第五幕：盟友的报价",
    "第五幕：双重间谍", "第五幕：联邦表决前夜", "第五幕：被出售的航线", "第五幕：血誓的代价",
    "第六幕：内部回声", "第六幕：派系的最后通牒", "第六幕：街垒之夜", "第六幕：告密者名单",
    "第六幕：政变的清晨", "第七幕：观测者悖论", "第七幕：谁在观测", "第七幕：沉默的答案",
    "第七幕：最后的广播", "第七幕：第八幕的门", "第七幕：交出真相", "第七幕：超脱之径",
};

std::string makeId(std::string_view prefix, int i) { return std::string(prefix) + std::to_string(i); }

std::vector<EventInfo> buildEvents() {
    std::vector<EventInfo> v;
    v.reserve(kEventCount);
    int id = 0;
    auto add = [&](const char* title, EventPhase phase, EventScope scope, i64 weight, Fixed severity,
                   bool choices, u8 choiceCount, i64 minTick, i16 reqTech, i16 reqCom, std::string_view prefix) {
        EventInfo e;
        e.id = static_cast<u8>(id);
        e.title = title;
        e.phase = phase;
        e.scope = scope;
        e.weight = weight;
        e.severity = severity;
        e.hasChoices = choices;
        e.choiceCount = choiceCount;
        e.minTick = minTick;
        e.requireTech = reqTech;
        e.requireCommodity = reqCom;
        (void)prefix;
        v.push_back(e);
        ++id;
    };
    for (int i = 0; i < 30; ++i)
        add(kCrisis[i], EventPhase::Crisis, (i % 3 == 0) ? EventScope::Global : EventScope::Empire, 100 - i % 30,
            Fixed::raw(1200 + i * 40), true, static_cast<u8>(2 + (i % 2)), static_cast<i64>(i * 2), -1,
            static_cast<i16>((i * 3) % kCommodityCount), "cri");
    for (int i = 0; i < 25; ++i)
        add(kShock[i], EventPhase::MarketShock, EventScope::Market, 120 - i % 40,
            Fixed::raw(800 + i * 60), false, 0, static_cast<i64>(i), -1, static_cast<i16>((i * 5) % kCommodityCount),
            "shk");
    for (int i = 0; i < 20; ++i)
        add(kDiplo[i], EventPhase::Diplomatic, EventScope::Empire, 90 - i % 20, Fixed::raw(600 + i * 30), true,
            static_cast<u8>(2 + (i % 3)), static_cast<i64>(i * 3), -1, -1, "dip");
    for (int i = 0; i < 25; ++i)
        add(kAnomalyEvents[i], EventPhase::Anomaly, EventScope::System, 70 - i % 15, Fixed::raw(500 + i * 50), i % 2 == 0,
            static_cast<u8>(2 + (i % 2)), static_cast<i64>(i * 2), -1, -1, "ano");
    for (int i = 0; i < 40; ++i)
        add(kStory[i], EventPhase::Story, EventScope::Empire, 200 - i % 50, Fixed::raw(700 + i * 25), i % 3 == 0,
            static_cast<u8>(2 + (i % 3)), static_cast<i64>(i * 2), -1, -1, "sto");
    // idName 持久化
    static std::vector<std::string> ids;
    ids.clear();
    ids.reserve(kEventCount);
    for (int i = 0; i < 30; ++i) ids.push_back(makeId("cri", i));
    for (int i = 0; i < 25; ++i) ids.push_back(makeId("shk", i));
    for (int i = 0; i < 20; ++i) ids.push_back(makeId("dip", i));
    for (int i = 0; i < 25; ++i) ids.push_back(makeId("ano", i));
    for (int i = 0; i < 40; ++i) ids.push_back(makeId("sto", i));
    for (int i = 0; i < kEventCount && i < static_cast<int>(v.size()); ++i)
        v[static_cast<std::size_t>(i)].idName = ids[static_cast<std::size_t>(i)];
    for (int i = 0; i < kEventCount; ++i) {
        v[static_cast<std::size_t>(i)].body = "";
    }
    return v;
}

const std::vector<std::string>& eventBodies() {
    static const std::vector<std::string> s = [] {
        std::vector<std::string> out;
        out.reserve(kEventCount);
        for (int i = 0; i < kEventCount; ++i) {
            out.push_back("事件 #" + std::to_string(i) +
                          "：来自边疆的报告抵达你的桌面。泛视网络已把它广播给所有人——包括你的对手。"
                          "你必须在代价与信誉之间做出选择。");
        }
        return out;
    }();
    return s;
}

}  // namespace

const EventInfo& eventInfo(int idx) {
    static const std::vector<EventInfo> table = [] {
        std::vector<EventInfo> t = buildEvents();
        const std::vector<std::string>& bodies = eventBodies();
        for (int i = 0; i < kEventCount; ++i) t[static_cast<std::size_t>(i)].body = bodies[static_cast<std::size_t>(i)];
        return t;
    }();
    if (idx < 0 || idx >= kEventCount) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

std::string_view eventPhaseName(EventPhase p) {
    switch (p) {
        case EventPhase::Tick: return "常规";
        case EventPhase::Crisis: return "危机";
        case EventPhase::MarketShock: return "市场冲击";
        case EventPhase::Diplomatic: return "外交";
        case EventPhase::Anomaly: return "异常";
        case EventPhase::Story: return "剧情";
        case EventPhase::Count: break;
    }
    return "?";
}

std::string eventOptionText(int eventId, int optionIndex) {
    const EventInfo& e = eventInfo(eventId);
    static const char* kVerbs[3][3] = {
        {"公开处理（提升合法性，但暴露立场）", "秘密处理（保留余地，但风险自担）", "转嫁给第三方（低成本，损信誉）"},
        {"强硬回应（军事/制裁）", "克制回应（外交/让利）", "拖延观望（延后代价上升）"},
        {"全额投入（消耗资源，快速生效）", "最小投入（省钱，效果迟缓）", "拒绝（保留资源，激怒相关方）"},
    };
    int row = (static_cast<int>(e.phase) + eventId) % 3;
    int col = optionIndex % 3;
    return kVerbs[row][col];
}

}  // namespace gf
