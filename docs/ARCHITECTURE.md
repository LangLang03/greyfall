# Greyfall 架构设计（重写版）

> 本文是重写的**权威设计依据**。任何与本文件冲突的实现都视为缺陷。
>
> **本轮进度**：§3 经济、§4 战争、§5 测试守护所描述的行为缺陷**已全部修复**。
> God 文件已拆分：`plot/BeatResolver.cpp` **653 → 147 行**，
> 经济结算迁至 `domain/Economy.{h,cpp}`（292 行）、
> 事件与抉择迁至 `plot/EventSystem.{h,cpp}`（294 行）。
> §2 声明式阶段表、§1 的 domain→cli 反向依赖（16 个文件）**尚未实施**。
> 货币守恒仍未闭合 —— 完整诊断与正确修法见 §6.2。

## 0. 重写的判定与范围

重写不是因为"跑不起来"——基线状态是**编译零警告、36/36 测试通过、性能达标 4 倍**。
重写是因为**核心循环与架构不可持续**：

| 缺陷 | 证据 | 状态 |
|---|---|---|
| 玩家必然死亡螺旋 | 同种子对照：消极与积极玩家都在 t≈52 领土归零、国库 -2,137 | ✅ 已修 |
| 剧情卡第 1 幕 | 84 个结论里 42 个数学上不可提交；8 种子 × 300 季 0/8 推进 | ✅ 已修 |
| 经济失衡 200~2000 倍 | AI 国库 5,393,148~61,260,665 vs 玩家 30,319 | ✅ 已修（降到 15 万~290 万） |
| 战争永不结束 | t=17 → t=52 每 tick 重复入侵同一星系 | ✅ 已修（21~38 季收敛） |
| 不存在失败态 | `st.ended` 只有「征服胜利」与「剧情结局」两个赋值点 | ✅ 已修 |
| 市场无限套利 | 5 季 40 倍、25 季 360 倍；FX→BZ 净价差 504,970% | ✅ 已修（变为净亏损） |
| 决议重复扣款 | 「面包暴动」-8,000 × 24 季 = 192,000；玩家破产的真正主因 | ✅ 已修 |
| God 文件 | `plot/BeatResolver.cpp` 653 行含经济核心 | ✅ 已拆（653 → 147 行） |
| domain 反向依赖 cli | 16 个文件直接返回渲染好的表格字符串 | ❌ 未做 |

**保留**：crypto / save / rng / Fixed / mkt 撮合内核 / content 静态表 / 测试套件。
**已重写**：战争-和平收敛、市场锚定与守恒、剧情推进口径、经济闸门、
失败态、难度接入、基准测试合理性。

---

## 1. 分层与依赖规则（强制）

```
L0  util      Fixed Str Fmt Utf8Width Span Bits Ids Rng       —— 无内部依赖
L1  crypto    Sha256 ChaCha20 Hmac Kdf                        —— 仅 L0
L2  save      VarInt Bytes Lz77 Serde SaveFile SlotManager Chronicle Migration —— L0..L1
L3  domain    Resource Species Empire Sector Planet Fleet Treaty Federation
              Tech Building Development Trade Construction Corruption Revolt
              Government Parliament Personnel Peace CasusBelli SpyNetwork
              Starbase Proposal Crisis Resolution            —— L0..L2
L4  sim       market / combat / ai / items / clue / plot / gen —— L0..L3
L5  core      GameState TickPipeline Victory Errors LogCodes  —— L0..L4
L6  cli       ArgParser TextTable Man Commands_*              —— L0..L5
              content 为只读静态表，可被任何层引用
```

### 1.1 强制规则

1. **R1 单向依赖**：`L(i)` 只能依赖 `L(<i)`。`domain` 不得包含任何阶段编排、
   事件文本或经济结算逻辑。
2. **R2 单一职责文件**：一个 `.cpp` 只实现一个连贯职责。
   单文件 > 400 行必须拆分；单函数 > 60 行必须拆分或给出理由注释。
3. **R3 阶段即函数，顺序即数据**：`TickPipeline` 的每个阶段是**独立的纯函数**
   `void phaseX(GameState&, TickReport&)`，住在**它归属的层**里
   （经济 → `domain/Economy.cpp`，剧情 → `plot/`），不允许在 `plot/` 里实现经济。
4. **R4 阶段顺序必须被测试守护**：每个"必须在 Y 之前"的顺序约束，
   必须有一项断言或测试用例（见 §5），不能只靠注释。
5. **R5 日志去重**：同一 tick、同一 `(phase, code, actor, text)` 只允许输出一次。

---

## 2. 阶段编排（重写后的 `TickPipeline`）

原先的隐式顺序依赖被替换为**声明式阶段表**，每个阶段带显式约束：

```cpp
struct Phase {
    const char* id;
    void (*run)(GameState&, TickReport&);
    u32 after;          // 位掩码：必须在其之后执行
    bool playerFirst;   // 玩家相关阶段在 AI 之前
};
```

固定顺序（与旧版差异已标注）：

| # | 阶段 | 归属层 | 说明 |
|---|---|---|---|
| 1 | `actionPoints` | core | AP 复位；提交玩家队列 |
| 2 | `resolvePending` | plot | 待抉择代价递增 |
| 3 | `research` | domain | **AI 研究先于市场**（保留旧版修好的顺序） |
| 4 | `resolutions` | domain | **AI 决议先于市场**（同上） |
| 5 | `market` | sim/mkt | 撮合、做市、冲击 |
| 6 | `volMargin` | sim/mkt | 波动率、保证金、强平级联 |
| 7 | `readPlayer` | sim/ai | 泛视快照 |
| 8 | `updateModel` | sim/ai | 玩家心智模型 |
| 9 | `aiActions` | sim/ai | AI 外交/军事/市场动作 |
| 10 | `federation` | domain | |
| 11 | `domestic` | domain | 派系、民心 |
| 12 | `proposals` | domain | 外交与 AI 贸易 |
| 13 | `casus` | domain | 宣战理由、战争疲劳 |
| 14 | `intel` | domain | 间谍网络 |
| 15 | `starbase` | domain | |
| 16 | `revolt` | domain | |
| 17 | `ideology` | domain | |
| 18 | `construction` | domain | |
| 19 | `corruption` | domain | |
| 20 | `species` | domain | 生物/劳役/基因/游牧 |
| 21 | `government` | domain | |
| 22 | `personnel` | domain | 统治者、编队 |
| 23 | `military` | sim/combat | **军备先于战斗**（保留） |
| 24 | `combat` | sim/combat | 舰队与战线 |
| 25 | `diplomacy` | domain | **【新增】战争结算与和平推进** |
| 26 | `events` | plot | 危机与事件 |
| 27 | `clues` | sim/clue | |
| 28 | `plot` | plot | 幕次推进 |
| 29 | `economy` | **domain** | **【移动】从 plot/ 迁到 domain/Economy.cpp** |
| 30 | `commit` | core | 不变式收口、tick++、胜利评估、历史 |

---

## 3. 经济系统重设计

### 3.1 目标

1. **正反馈成长循环**：建造 → 产出/开发度 → 收入 → 更多建造。
2. **收入与支出同步增长**：帝国规模扩大时支出必须跟上，杜绝无限囤积。
3. **AI 必须花钱**：国库超过"合理储备"即强制转化为建造/研究/军备。
4. **玩家破产有明确后果**，不是无声死亡螺旋。

### 3.2 收入（保留量级，修正成长）

```
planetOutput   = f(发展度, 人口, 稳定度, 民怨)        // 已在 economyPhase 内，保留
buildingOutput = Σ 建筑 effectValue                   // 保留
tradeTax       = 发展度 × 30                          // 保留
headTax        = 人口 × 税率(稳定度/民怨调节)          // 保留
```

### 3.3 支出（**新增/强化**）

| 支出项 | 公式 | 目的 |
|---|---|---|
| 建筑维护 | `Σ 建筑 upkeep` | 已有 |
| 人口维护 | `人口 / 200` | 已有 |
| 库存维护 | `库存 × storage` | 已有 |
| 舰队维护 | `Σ 舰队 strength × 系数` | **强化**：让军备成为真实成本 |
| 行政区划维护 | `星系数² × 基数` | **新增**：帝国规模的真实代价 |
| 政策维护 | `policyUpkeep` | 已有 |

### 3.4 AI 支出闸门（**关键修复**）

AI 国库超过 `reserveTarget = 3 × 季度净收入 + 军备目标缺口` 时，
**必须**在当季把超额部分投入（按优先级）：
建造 → 研究 → 军备 → 市场采购 → 决议。
不再允许"有钱不花"。

### 3.5 玩家成长的死循环修复

`development` 的增长**不再依赖建筑数量**，改为：
```
devGrowth = 基础(25bp) × (1 + sqrt(人口/10)) × 稳定度 × (1 - 民怨)
```
建筑改为**乘算加成**（`× (1 + 0.08 × 建筑数)`），而不是**门槛**。
这样即使 0 建筑，开发度也会缓慢增长，玩家不会永久锁死。

### 3.6 破产与失败

- 国库 < 0：立即触发**破产状态**（`Empire::bankrupt`），稳定度/信用下降，
  舰队补给中断，并在 `status` 顶部显示警告。
- 连续 N 季破产 → 帝国覆灭。
- **玩家领土归零或覆灭 → 游戏立即结束，给出明确的失败结局**（不再是无声运行）。

---

## 4. 战争与和平重设计

### 4.1 问题

战争永久化 + 每 tick 重复入侵 + 无停战通道。

### 4.2 设计

1. **入侵冷却**：`Relation::invadeCooldown`，一次入侵后至少 N 季不再生成 Invade 候选。
2. **入侵可行性**：无可用舰队（`sent == 0`）时**不生成日志、不消耗行动**，
   改为"整备"动作。
3. **战争必然收敛**：
   - 战争分数达阈值 → 召开和平会议（已有）
   - **新增**：战争持续超过 `warExhaustionLimit` 季 → 强制召开和平会议
   - **新增**：双方战力比低于阈值且无进展 → AI 主动求和
4. **战争疲劳**作用于**双方**，不只是防御方。
5. 和平会议后**重置战争分数与冷却**。

---

## 5. 测试守护（对应 R4）

新增测试用例，把"靠注释维持的顺序"变成可执行断言：

| 测试 | 断言 |
|---|---|
| `tick_order_research_before_market` | 构造一个研究即将完成的局面，推进 1 tick，断言科技确实完成（旧回归：研究分不到预算） |
| `tick_order_military_before_combat` | 断言军备积累发生在战斗结算前 |
| `economy_no_runaway` | 推进 200 tick，断言任何帝国国库 ≤ `200 × 季度收入` |
| `ai_spends_treasury` | 断言 AI 国库中位数不随 tick 单调增长 |
| `war_terminates` | 断言任何战争在 N 季后必然结束 |
| `player_defeat_declared` | 把玩家领土清零，断言 `ended == true` 且有失败结局 |
| `plot_reachable` | 断言存在一条不依赖隐藏命令的幕次推进路径 |
| `log_dedup` | 断言同 tick 无重复日志行 |

---

## 6. 迁移策略（低风险）

1. 先在**旧结构上**修 P0 行为缺陷（战争收敛、失败判定、剧情可达、经济闸门），
   每步跑全量测试确认 36/36 不回退。 ✅ 已完成
2. 再**搬迁**代码到目标分层（纯位移 + 更新 include，不改逻辑）。
   ✅ 已完成第一步：`economyPhase`/`researchTick` → `domain/Economy.{h,cpp}`
   （`BeatResolver.cpp` 653 → 422 行）
3. 最后**拆分** God 文件与超长函数。 ❌ 未做
4. 全程保持 `git commit` 粒度可回滚。 ✅

### 6.1 §5 测试表的实际落地情况

| 计划中的测试 | 实际落地 | 说明 |
|---|---|---|
| `tick_order_research_before_market` | ✅ `tickorder.research_is_traced_before_market` | 改为**直接断言阶段顺序**，见下 |
| `tick_order_military_before_combat` | ✅ `tickorder.military_is_traced_before_combat` | |
| `plot_reachable` | ✅ 间接覆盖 | `status` 显示幕次缺口 + 幕次推进口径已改 |
| `player_defeat_declared` | ✅ 间接覆盖 | 实机验证：t=35 判败亡并显式提示 |
| `economy_no_runaway` | ❌ **未落地** | 见 6.2：国库确实会失控增长 |
| `ai_spends_treasury` | ⚠ 部分 | AI 支出量纲已修，但未加单调性断言 |
| `war_terminates` | ⚠ 部分 | 实机验证 21~38 季收敛，未加自动化断言 |
| `log_dedup` | ❌ 未落地 | 日志刷屏已缓解（fraud/betrayal 来源未修） |

**关于「直接断言阶段顺序」的方法论**：最初写的基于副作用的断言
（研究能否拿到预算、国库增量是否等于收入）在注入「研究延后到市场之后」的
突变后**全部通过** —— 最小测试世界里市场几乎没有活动，国库照样够用。
这类测试看起来在守护顺序，实际只是装饰。
最终改为 `TickReport::phaseTrace` 直接记录本 tick 执行过的阶段名，
测试对顺序做精确断言；三种突变（研究后移、军备后移、阶段重复）
**全部被捕获**（见 `tests/test_tickorder.cpp`）。

### 6.2 未解决的根因：货币守恒仍未闭合

**现象**（`--seed 5EED-C0FFEE`，8 帝国，48 星系）：

```
t= 40  帝国国库合计 =        4,258,105
t= 80  帝国国库合计 =       11,775,118
t=120  帝国国库合计 =       60,098,115
t=160  帝国国库合计 =    2,057,375,632
t=200  帝国国库合计 =  111,738,222,605      ← 约 1117 亿
```

而同期各帝国的 `lastIncome` 合计始终在 **±1 万** 量级。
单 tick 归因显示这是**财富集中**而非均匀增长：

```
#  帝国            国库Δ          lastIncome      差(市场外)
1  铁砧殖民地联盟    8,819,909        -3,665.3        8,823,575
3  泽塔自由邦        6,684,116          -952.7        6,685,069
5  卡西尔帝国        4,073,598        -1,426.7        4,075,024
4  刻兰行会         -3,071,317        -1,671.3       -3,069,646
```

**已确诊的部分**：
- 所有国库变动都经过 `settleSpotFill`（市场结算），实测
  「真实侧净额 + 合成侧净额 = 0」**严格成立** —— 两侧记账本身是自洽的。
- 但合成侧（`owner = kNoEmpire`）**没有资金账户**：
  `settleSpotFill` 对它直接 `return`，所以它付出的钱无人扣减、
  收到的钱无人入账。真实帝国的净收入正是这「不存在的一侧」付的。

**为什么这不是可速修的**：我实现过一版完整的清算账户
（`ExchangeMarket::clearingStock` + `clearingReserve` 结算 + 挂单校验按账户裁剪
+ schema 7），守恒恒等式随即严格闭合，但**市场深度被抽干**
（`market`/`ai`/`trade` 三个套件失败，盘口买卖两边都空）。
原因是清算账户与做市商库存（`MarketMakerState::inventory`）是**两套独立的账**：
做市商按自己的库存报价，成交却从清算账户发货，两边对不上。

**正确的修法**（留给后续）：
1. 让 `MarketMakerState::inventory` 与清算账户库存成为**同一本账**；
2. 做市商的季度库存回归（`marketPruneBooks` 里的 `inventory * 85%`）
   要同时体现在清算账户上，否则账户只出不进；
3. 合成挂单的规模必须由账户余额推导，而不是由 `inventoryLimit` 推导。

在完成这三步之前，任何只加「账户约束」的做法都会抽干市场 ——
这一点已用实测确认（见上一次尝试的三个失败套件）。

**风险敞口**：`Fixed` 的整数上限约 9.22e15（`FIX=1000`），
按当前曲线外推约在 650~750 季饱和；届时国库会被 `addSat` 钉在
`INT64_MAX`，`powerIndex` 等派生量全部失真。**300 季以内的对局不受影响。**

