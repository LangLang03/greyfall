#include "domain/Crisis.h"

#include <array>
#include <string>
#include <vector>

#include "util/Str.h"

namespace gf {
namespace {

struct AnomalySeed {
    const char* id;
    const char* zh;
    const char* desc;
    Fixed diff;
    i16 clue;
    i16 item;
    i16 tech;
};

const AnomalySeed kSeeds[] = {
    {"gravityWell", "引力异常区", "局部引力常数偏离标准值，导航系统在这里会失准。", Fixed::raw(1200), 12, 40, 0},
    {"derelictStation", "废弃观测站", "大沉默前留下的观测站，数据仍在校验。", Fixed::raw(800), 3, 12, 5},
    {"breathingMeteor", "会呼吸的陨石", "周期性释放气体的岩体，内部有空洞。", Fixed::raw(600), 21, 60, 6},
    {"negentropyCrystal", "逆熵晶体", "局部熵减，周围仪器会自发修复。", Fixed::raw(2000), 45, 88, 13},
    {"silentBeacon", "沉默信标", "持续发送无人能解码的窄带信号。", Fixed::raw(900), 1, 20, 12},
    {"ghostFleet", "无人舰队残骸", "一支没有船员记录的舰队残骸。", Fixed::raw(1500), 30, 75, 26},
    {"timeCapsule", "时间胶囊", "封存着一段不属于当前纪元的记录。", Fixed::raw(1100), 55, 100, 55},
    {"mirrorPlanet", "镜像行星", "地表结构与你的首都星完全一致。", Fixed::raw(2200), 70, 110, 45},
    {"hollowIdol", "空壳神像", "内部空无一物，却对灵能探测有强响应。", Fixed::raw(1300), 88, 118, 72},
    {"languageVirus", "语言病毒", "接触者的措辞会逐渐趋同。", Fixed::raw(1600), 95, 121, 44},
    {"selfRepairRuin", "自修复废墟", "每天清晨，废墟的形状都与昨日略有不同。", Fixed::raw(1000), 110, 90, 9},
    {"cryoBay", "低温休眠仓", "数以千计的休眠舱，部分仍在运行。", Fixed::raw(700), 40, 30, 14},
    {"starChartShard", "星图碎片", "包含一片被官方星图抹除的星区。", Fixed::raw(500), 5, 42, 7},
    {"nonEuclidStation", "非欧几何站", "内部空间不符合三维直觉。", Fixed::raw(2400), 130, 125, 61},
    {"ghostSignal", "幽灵信号源", "完全复制了你舰队七年前的通讯。", Fixed::raw(1700), 66, 95, 46},
    {"symbioticMat", "共生菌毯", "覆盖整片大陆，可消化绝大多数有机物。", Fixed::raw(1400), 77, 65, 15},
    {"antimatterSpring", "反物质泉", "自然形成的反物质源，危险而珍贵。", Fixed::raw(2600), 150, 105, 30},
    {"memorySea", "记忆之海", "液体中溶解着可被读取的记忆片段。", Fixed::raw(1900), 105, 128, 73},
    {"brokenRing", "断裂的环", "一圈人造结构，被从中间整齐切开。", Fixed::raw(1800), 160, 112, 50},
    {"observerEye", "观测者之眼", "一个持续追踪你舰队的未知物体。", Fixed::raw(3000), 200, 126, 65},
    {"erasedColony", "被抹除的殖民地", "存在过，但所有记录都被系统性删除。", Fixed::raw(2100), 210, 115, 56},
    {"seventhElement", "第七种元素", "不属于任何已知元素周期表的物质。", Fixed::raw(2500), 220, 120, 31},
    {"echoWell", "回声井", "向其中发出的任何信号都会以变形的形式返回。", Fixed::raw(1200), 230, 98, 62},
    {"namelessCamp", "无名者营地", "拒绝被登记的人的聚居地。", Fixed::raw(900), 240, 122, 17},
    {"terminalAccord", "终末协议", "一份以所有人的沉默为条款的协议。", Fixed::raw(4000), 300, 129, 66},
};
static_assert(sizeof(kSeeds) / sizeof(kSeeds[0]) == static_cast<std::size_t>(kAnomalyCount));

}  // namespace

const AnomalyInfo& anomalyInfo(int idx) {
    static const std::vector<AnomalyInfo> table = [] {
        std::vector<AnomalyInfo> v;
        v.reserve(kAnomalyCount);
        for (int i = 0; i < kAnomalyCount; ++i) {
            AnomalyInfo a;
            a.id = static_cast<u8>(i);
            a.idName = kSeeds[i].id;
            a.nameZh = kSeeds[i].zh;
            a.desc = kSeeds[i].desc;
            a.difficulty = kSeeds[i].diff;
            a.rewardClue = kSeeds[i].clue;
            a.rewardItem = kSeeds[i].item;
            a.rewardTech = kSeeds[i].tech;
            v.push_back(a);
        }
        return v;
    }();
    if (idx < 0 || idx >= kAnomalyCount) idx = 0;
    return table[static_cast<std::size_t>(idx)];
}

}  // namespace gf
