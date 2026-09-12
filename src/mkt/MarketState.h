#pragma once
// 市场状态：订单簿 / 期货 / 波动率 / 做市商 / 保证金 / 信用 / 黑市
#include <array>
#include <string>
#include <vector>

#include "domain/Mind.h"
#include "domain/Resource.h"
#include "domain/Trade.h"
#include "util/Fixed.h"

namespace gf {

enum class OrderSide : u8 { Buy = 0, Sell = 1 };
enum class OrderKind : u8 { Limit = 0, Market, Iceberg, Stop };
enum class Tif : u8 { Day = 0, Gtc };

/// 订单簿聚合档位
struct PriceLevel {
    Fixed px{};
    i64 qty = 0;
    u32 nOrders = 0;
};

/// 活动订单（价格-时间优先的核心载体）
struct Order {
    u64 id = 0;
    u32 owner = 0;
    u32 seq = 0;                 // 全局递增序号（时间优先）
    Fixed px{};
    i64 qty = 0;
    i64 shown = 0;               // 冰量单露出量；普通单 == qty
    i64 filled = 0;
    Fixed avgPx{};
    u8 exch = 0;
    u8 res = 0;
    bool buy = true;
    OrderKind kind = OrderKind::Limit;
    Tif tif = Tif::Gtc;
    u64 placedTick = 0;
    /// 归属的"控制人"（用于 wash trading 检测：同一控制人多账户）
    u32 controller = 0;
    bool synthetic = true;       // 是否为 AI 聚合流生成
};

/// 单交易所单标的的订单簿
struct Book {
    std::vector<Order> orders;       // 活动订单
    std::vector<PriceLevel> bids;    // 派生聚合：价格降序
    std::vector<PriceLevel> asks;    // 派生聚合：价格升序
    Fixed last{};
    Fixed mid{};
    Fixed spread{};
    Fixed open{};
    Fixed high{};
    Fixed low{};
    i64 volume = 0;
    Fixed vwap{};
    Fixed sigma = Fixed::pct(2);     // GARCH(1,1) 当前波动率
    Fixed var20 = Fixed(0);          // 20 期均量（冲击定律分母）
    Fixed impactPerm = Fixed(0);     // 永久冲击残差
    Fixed impactTemp = Fixed(0);     // 临时冲击（按 λ=0.5 半衰回归）
    i64 openInterest = 0;
    bool halted = false;
    /// 本 tick 主动成交的净流（正 = 净买入），驱动跨所价差收敛
    i64 netFlow = 0;
    /// 流动性耗尽标记：簿内报价被吃穿后置位，直到做市商补货
    bool liquidityDrained = false;
};

/// GARCH(1,1) 定点状态：σ²_t = ω + α·r²_{t-1} + β·σ²_{t-1}
struct VolState {
    Fixed omega = Fixed::raw(1);      // 长期方差项
    Fixed alpha = Fixed::raw(80);     // 0.08
    Fixed beta = Fixed::raw(860);     // 0.86 ⇒ α+β = 0.94
    Fixed lastReturn = Fixed(0);
    Fixed lastSigma = Fixed::pct(2);
    Fixed jumpBias = Fixed(0);        // 事件跳跃累积
};

/// 期货报价（期限结构）
struct FuturesQuote {
    Fixed price{};
    Fixed basis{};          // (F - S)/S
    i64 openInterest = 0;
    Fixed convenience = Fixed(0);   // 便利收益
    Fixed carry = Fixed(0);         // 持有成本
};

/// 做市商状态（Avellaneda-Stoikov 简化）
struct MarketMakerState {
    Fixed inventory = Fixed(0);
    Fixed gamma = Fixed::raw(120);      // 风险厌恶 γ
    Fixed baseSpread = Fixed::pct(1);
    Fixed skew = Fixed(0);
    bool halted = false;
    std::string haltReason;
    i64 quotesPlaced = 0;
    /// 库存上限（单位）：达到上限后该侧撤单，形成真实的流动性真空
    i64 inventoryLimit = 20000;
    /// 累计吸收的主动成交（正 = 做市商净买入）
    i64 absorbed = 0;
};

/// 保证金与强平
struct MarginState {
    Fixed initMargin = Fixed::pct(20);
    Fixed maintMargin = Fixed::pct(10);
    Fixed cash = Fixed(0);
    Fixed equity = Fixed(0);
    bool callActive = false;
    int cascadeDepth = 0;
    i64 forcedLiquidations = 0;
};

struct Position {
    u8 res = 0;
    i64 qty = 0;              // 现货净持仓（负 = 空头）
    Fixed avgCost{};
    Fixed realized{};
};

struct FuturesPosition {
    u8 res = 0;
    u8 term = 0;
    i64 qty = 0;
    Fixed entry{};
    Fixed margin{};
    int leverage = 1;
    bool isShort = false;
    u64 openedTick = 0;
    u32 owner = 0;
    u64 expiryTick = 0;
};

/// 四季现货尾部保险：只赔投保后该商品的价格损失，赔付后扣减额度。
struct InsurancePolicy {
    u8 res = 0;
    i64 qty = 0;
    Fixed entry{};
    Fixed remaining{};
    Fixed paid{};
    u64 expiresTick = 0;
};

/// 托管 / 信用证（降低违约概率，但占用资金成本）
struct EscrowRecord {
    u32 id = 0;
    u32 payer = 0;
    u32 payee = 0;
    Fixed amount{};
    u8 res = 0;
    i64 qty = 0;
    u64 openedTick = 0;
    int termTicks = 4;
    bool released = false;
    bool breached = false;
};

/// 债务与信用评级
struct DebtRecord {
    u32 id = 0;
    u32 borrower = 0;
    u32 lender = 0;          // kNoEmpire 表示市场（债券）
    Fixed principal{};
    Fixed rate{};
    int termTicks = 8;
    u64 issuedTick = 0;
    bool defaulted = false;
};

/// 内幕交易信号：事件公布前知情者建仓
struct InsiderSignal {
    u8 res = 0;
    Fixed magnitude{};
    u64 fireTick = 0;
    u32 knower = 0;
    bool consumed = false;
};

enum class ManipKind : u8 { WashTrading = 0, Spoofing, Corner, PumpDump, FrontRunning, Insider, Count };

struct ManipulationRecord {
    u32 actor = 0;
    ManipKind kind = ManipKind::WashTrading;
    u64 tick = 0;
    Fixed score{};
    bool penalized = false;
    Fixed fine{};
    std::string detail;
};

/// 市场冲击事件（shock 命令的数据源）
struct ShockRecord {
    u64 tick = 0;
    u8 res = 0;
    Fixed magnitude{};
    std::string cause;
    Fixed sigmaBefore{};
    Fixed sigmaAfter{};
};

/// 逐主体的市场行为统计：是 ManipulationDetect 序列表征的输入
struct ActorMarketStats {
    u32 actor = 0;
    i64 ordersPlaced = 0;
    i64 ordersCancelled = 0;
    i64 qtyFilled = 0;
    i64 qtyCancelled = 0;
    i64 buyVolume = 0;
    i64 sellVolume = 0;
    Fixed priceRunUp = Fixed(0);     // 最近一次建仓后价格拉升幅度
    /// 本 tick 已用于市场采购的金额（用于 AI 的逐 tick 预算约束）
    Fixed spentThisTick = Fixed(0);
    i64 inventoryPeak = 0;
    i64 spoofScore = 0;              // 挂单后撤比例序列表征
    u64 lastOrderTick = 0;
};

struct ExchangeMarket {
    u8 kind = 0;
    std::array<Book, kCommodityCount> books;
    std::array<std::array<FuturesQuote, kFuturesTerms>, kCommodityCount> futures;
    std::array<MarketMakerState, kCommodityCount> mm;
    Fixed clearingReserve = Fixed(0);
    Fixed volumeTick = Fixed(0);
    /// 封锁/关税对跨所套利的偏差
    Fixed embargo = Fixed(0);
    Fixed tariff = Fixed(0);
};

struct MarketState {
    std::array<ExchangeMarket, kExchangeCount> exchanges;
    /// 帝国间的贸易路线与关税（V3 式贸易网络）
    TradeNetwork trade;
    std::array<VolState, kCommodityCount> vol;
    std::array<Fixed, kCommodityCount> spotIndex{};      // 综合现货指数（跨所加权）

    // 玩家（主体 0）的持仓
    std::vector<Position> positions;
    std::vector<FuturesPosition> futuresPositions;
    std::vector<InsurancePolicy> insurancePolicies;
    MarginState margin;

    // 跨所套利残差（arb 命令）
    std::array<Fixed, kCommodityCount> arbGap{};

    // 汇率：以 credits 为锚的各所结算币价
    std::array<Fixed, kExchangeCount> fx{};

    // 黑市
    Fixed blackMarketPremium = Fixed::pct(35);   // 当前溢价（状态量，会被套利流压缩）
    Fixed blackMarketStructural = Fixed::pct(35); // 结构性溢价（由配给/封锁/关税决定）
    Fixed rationing = Fixed(0);          // 战时配给强度
    std::array<Fixed, kCommodityCount> blackMarketPrice{};
    /// 各标的的套利压力（正 = 有人在 CX 买 / BZ 卖），压缩黑市溢价
    std::array<Fixed, kCommodityCount> arbPressure{};

    // 信用与债务
    std::vector<DebtRecord> debts;
    std::array<Fixed, kMaxEmpires> creditRating{};
    std::vector<EscrowRecord> escrows;

    // 内幕
    std::vector<InsiderSignal> insiderSignals;

    // 操纵检测
    std::vector<ManipulationRecord> manipulations;
    u32 nextEscrowId = 1;
    u32 nextDebtId = 1;

    // 冲击日志
    std::vector<ShockRecord> shocks;

    // 逐主体市场行为统计（操纵检测的输入）
    std::vector<ActorMarketStats> actorStats;

    // 全局序号
    u64 nextOrderId = 1;
    u64 nextSeq = 1;
    /// 本 tick 累计的成交金额（成交未达成退出码 7 的依据）
    Fixed tickNotional = Fixed(0);
    i64 tickFills = 0;
};

/// 主体 0 恒为玩家
inline constexpr u32 kPlayerId = 0;

/// 交易所常量索引
inline constexpr int kExchCX = 0;
inline constexpr int kExchFX = 1;
inline constexpr int kExchBZ = 2;

[[nodiscard]] const char* orderKindName(OrderKind k);
[[nodiscard]] const char* tifName(Tif t);
[[nodiscard]] const char* manipKindName(ManipKind k);
[[nodiscard]] ManipKind manipKindFromName(std::string_view s);

}  // namespace gf
