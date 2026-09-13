# Greyfall 架构设计（重写版）

> 本文是重写的**权威设计依据**。任何与本文件冲突的实现都视为缺陷。

## 0. 重写的判定与范围

重写不是因为"跑不起来"——基线状态是**编译零警告、36/36 测试通过、性能达标 4 倍**。
重写是因为**核心循环与架构不可持续**：

| 缺陷 | 证据 | 影响 |
|---|---|---|
| 玩家必然死亡螺旋 | 同种子对照：消极玩家与积极玩家都在 t=52 领土归零、国库 -2,137 | 游戏无法通关 |
| 剧情 154 季卡第 1 幕 | `status` 实测 | 7 幕内容不可达 |
| 经济失衡 200~2000 倍 | AI 国库 5,393,148~61,260,665 vs 玩家 30,319 | 无策略空间 |
| 战争永不结束 | t=17 → t=52 每 tick 重复入侵同一星系 | 无意义磨盘 |
| God 文件 + 分层违规 | `plot/BeatResolver.cpp` 629 行含经济核心 | 无法维护 |

**保留**：crypto / save / rng / Fixed / mkt 撮合内核 / content 静态表 / 测试套件。
**重写**：阶段编排、经济、战争-和平、剧情推进、日志与反馈。

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
   每步跑全量测试确认 36/36 不回退。
2. 再**搬迁**代码到目标分层（纯位移 + 更新 include，不改逻辑）。
3. 最后**拆分** God 文件与超长函数。
4. 全程保持 `git commit` 粒度可回滚。
