#pragma once
// 贸易路线与关税战（Victoria 3 式）
//
// 与已有机制的关系：
//   Exchange.tariff  —— 同一帝国内不同交易所之间的价差摩擦
//   Treaty::Embargo  —— 单向禁运（政治行为）
//   TradeRoute       —— **帝国之间**的实物贸易流：出口方的剩余物资运往进口方，
//                       进口方按关税率征税，双方各得其所。
//
// 核心循环：
//   出口方有剩余 ⇒ 开路线 ⇒ 每季外运 ⇒ 出口方获得售价收入
//   进口方有缺口 ⇒ 开路线 ⇒ 每季入库 ⇒ 进口方获得物资 + 关税收入
//   关税率是可调的政治工具：提高关税能增加财政收入、但会压低贸易量并恶化关系
//   ⇒ 关税战：一方提高关税，另一方可以报复性提高，贸易量崩塌，双方受损
#include <string>
#include <vector>

#include "util/Fixed.h"

namespace gf {

struct GameState;
struct Empire;

/// 一条贸易路线
struct TradeRoute {
    u32 id = 0;
    u32 exporter = 0;        // 出口方帝国
    u32 importer = 0;        // 进口方帝国
    u8 commodity = 0;        // 商品
    Fixed volume = Fixed(0);         // 本季实际运输量
    Fixed capacity = Fixed(0);       // 名义运力（由双方经济体量决定）
    Fixed tariff = Fixed::pct(10);   // 进口方关税率（0~60%）
    Fixed unitPrice = Fixed(0);      // 本季结算单价
    bool active = true;
    u32 establishedTick = 0;
    /// 已累计的关税收入（进口方）
    Fixed tariffRevenue = Fixed(0);
    /// 已累计的出口收入（出口方）
    Fixed exportRevenue = Fixed(0);
    /// 上次中断原因（战争/禁运等）
    std::string disrupted;
    /// 连续无货可运的季数（用于自动关闭失效路线）
    u32 dormantTicks = 0;
    /// 实体路径：从出口方首都到进口方首都经过的星系序列。
    /// 路线不再抽象 —— 沿途的封锁、海盗、航行风险都会削减运力，
    /// 舰队「阻断」命令因此能真正切断敌人的贸易。
    std::vector<u32> path;
    /// 本季的路径风险系数（0 = 通畅，1 = 完全中断）
    Fixed pathRisk = Fixed(0);
    /// 过境费：路线穿过第三国领土时，该国按货值抽取的通行费总额（本季）
    Fixed transitFee = Fixed(0);
    /// 途经的第三国（既非出口方也非进口方）帝国编号，去重
    std::vector<u32> transitEmpires;
    /// 路径风险说明（用于 UI）
    std::string pathNote;
};

/// 贸易网络（全局，挂在 MarketState 上）
struct TradeNetwork {
    std::vector<TradeRoute> routes;
    u32 nextRouteId = 1;
    /// 本季全世界的汇总
    Fixed importVolume = Fixed(0);
    Fixed exportVolume = Fixed(0);
    Fixed tariffIncome = Fixed(0);
    Fixed exportIncome = Fixed(0);
};

/// 单个帝国的贸易统计（挂在 Empire 上）
struct EmpireTradeStats {
    /// 本季进口 / 出口量
    Fixed importVolume = Fixed(0);
    Fixed exportVolume = Fixed(0);
    /// 本季关税收入（进口方）/ 出口收入（出口方）
    Fixed tariffIncome = Fixed(0);
    Fixed exportIncome = Fixed(0);
    /// 各贸易伙伴对我方征收的平均关税率（用于关税战判定）
    std::vector<std::pair<u32, Fixed>> avgTariffByPartner;
    /// 参与中的路线数
    u32 routeCount = 0;
    /// 本季已支付的进口货款（用于按经济体量限制支出）
    Fixed spentThisTick = Fixed(0);
};

/// 关税上限
inline constexpr i64 kMaxTariffPct = 60;
/// 每季每条约路的最大运力系数
inline constexpr i64 kRouteCapacityScale = 1000;

[[nodiscard]] const TradeNetwork& tradeNetwork(const GameState& st);
[[nodiscard]] TradeNetwork& tradeNetworkMut(GameState& st);

/// 找一条已有路线（任一方向）
[[nodiscard]] const TradeRoute* findRoute(const GameState& st, u32 a, u32 b, u8 commodity);
/// 该帝国当前对某伙伴的平均关税率
[[nodiscard]] Fixed avgTariffToward(const GameState& st, u32 empire, u32 partner);

/// 计算两个星系之间的最短路径（按跳数）。无路径时返回空。
[[nodiscard]] std::vector<u32> tradePathBetween(const GameState& st, u32 from, u32 to);

/// 风险加权路径：除跳数外还计入沿途归属、封锁与战争状态。
/// 单纯的最短路会让贸易无条件穿过战区；加权后会绕开危险区域。
[[nodiscard]] std::vector<u32> tradePathWeighted(const GameState& st, u32 from, u32 to, u32 exporter,
                                                 u32 importer);
/// 某条路径当前的风险评估（封锁 / 海盗 / 航行风险 / 交战方领土）
[[nodiscard]] Fixed tradePathRisk(const GameState& st, u32 exporter, u32 importer,
                                  const std::vector<u32>& path, std::string* note);

/// 某帝国某商品的每季产出（流量口径，供贸易判断与 UI 使用）
[[nodiscard]] Fixed empireProduction(const GameState& st, const struct Empire& e, u8 c);
/// 可出口剩余（产出 − 需求，流量口径）
[[nodiscard]] Fixed exportableSurplus(const GameState& st, const struct Empire& e, u8 c);
/// 进口需求（需求 − 产出，流量口径）
[[nodiscard]] Fixed importNeed(const GameState& st, const struct Empire& e, u8 c);

/// 开一条路线（双方需处于非战争状态，且出口方确有剩余）
[[nodiscard]] bool tradeOpen(GameState& st, u32 exporter, u32 importer, u8 commodity, Fixed tariff,
                             std::string* err);
/// 关闭路线
[[nodiscard]] bool tradeClose(GameState& st, u32 routeId, u32 requester, std::string* err);
/// 调整关税率
[[nodiscard]] bool tradeSetTariff(GameState& st, u32 routeId, u32 requester, Fixed tariff, std::string* err);

/// 每季结算：运力计算、物资转移、关税与收入、中断判定
void tradePhase(GameState& st);

/// AI 决策：自动开设有利可图的路线、并对高关税伙伴实施报复
void tradeAiPhase(GameState& st);

/// 文本
[[nodiscard]] std::string tradeText(const GameState& st, u32 empire);
[[nodiscard]] std::string tradeRouteText(const GameState& st, const TradeRoute& r);

}  // namespace gf
