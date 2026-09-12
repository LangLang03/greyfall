# 灰域纪元 Greyfall —— CLI 纯文本多阵营博弈游戏完整实施方案

## 0. 本轮新增的两条硬要求如何落地（先给结论）

| 新要求 | 设计结论 |
|---|---|
| **AI 非常聪明** | 不靠作弊，靠**前瞻搜索 + 玩家心智建模**：AI 用真实撮合市场与前向模拟器对玩家候选动作做 rollout（意图树 + 确定性采样），并用最大似然在线更新玩家的效用权重/风险偏好/贴现因子/策略类型；背叛前算清 `BetrayalEV`（含信誉现值损失、战争成本、第三方联盟扩散）；具备自动制衡霸权（balance-of-power）与"识别玩家惯性套路后拒绝其求和"的能力 |
| **AI 能透视玩家数据** | `diplo::OmniscientReader` 在每 tick 直接读取玩家全部状态（库存/未公开线索/研发进度/舰队坐标/预算/pending 选项/AP 队列）。玩家的欺诈轴因此从「隐藏数据」转为「**布置数据**」：伪造字段带 `Provenance`（暴露路径 + 信号成本），AI 用**来源可信度 + 一致性异常检测**判断"他为何要让我看到这个"（costly signaling）。玩家可用 `intel --what-they-know` 索取透明度报告，用渗透读取 AI 的心智模型形成**镜像博弈** |
| **市场接近真实市场** | 限价订单簿 + 价格-时间优先撮合 + 做市商（库存风险定价价差）+ 平方根市场冲击定律 + EWMA/GARCH 式波动率聚集与跳跃 + 期货期限结构与基差/backwardation + 保证金与强平级联闪崩 + 跨交易所套利偏差（运费/封锁/关税）+ 内幕交易与操纵（wash/spoof/corner/pump&dump，含监管概率与罚则）+ 违约、托管/信用证、信用评级与发债 + 汇率与战时配给黑市 |
| 群星式多阵营/多种族/多国家/多机制 | 12 国（8–16 可配）× 18 种族原型 × 12 伦理 × 24 公民特质 × 14 政体；机制含殖民、舰船设计、巨构、飞升、异常点、联邦投票、危机（外部冲击）、**国内派系双层博弈**（玩家自己也困在重复博弈里，民心不足会政变） |
| 禁止 REPL | 一次进程调用 = 一个事务：`load → validate → apply → tickResolve → commit(autosave) → print → exit`；绝不读 stdin、无循环、无交互提示 |

## 1. 技术栈与工程约束

- C++20（`-std=c++20 -O2 -Wall -Wextra -Wshadow`），CMake ≥ 3.28；已验证环境 GCC 16.2.1 / CMake 4.4.2。
- **零第三方依赖**：SHA-256、ChaCha20、HMAC、LZ77、varint、UTF-8 宽度表、文本表格、伪随机、序列化全部自实现。内容以编译期 `static constexpr` 表落地 → 单二进制，无资源查找路径。
- 全数值 **int64 定点**（`FIX=1000`），无浮点 ⇒ 跨平台字节级可复现；乘除走 `__int128`。
- 目标：`advance --ticks 4`（12 国 × 22 标的 × 3 交易所，≈ 20 万订单簿档位更新）**< 250 ms**；存档 **< 400 KB**。

## 2. 目录结构

```
Game/
├── CMakeLists.txt                    # greyfall 可执行 + greyfall_tests(CTest) + selftest target
├── README.md                         # 规则手册 / 经济模型说明 / AI 行为契约 / 存档格式规范 / CLI 手册
├── src/
│   ├── main.cpp                      # 解析→分发→退出码
│   ├── util/    Fixed.{h,cpp} Str.{h,cpp} Utf8Width.{h,cpp} Fmt.{h,cpp} Span.h Bits.h
│   ├── cli/     ArgParser.{h,cpp} TextTable.{h,cpp} Man.{h,cpp} Commands_{Core,World,Econ,Diplo,Item,Clue,Fleet,Save,Epoch}.cpp
│   ├── crypto/  Sha256.{h,cpp} ChaCha20.{h,cpp} Hmac.{h,cpp} Kdf.{h,cpp}
│   ├── save/    VarInt.h ByteWriter.h ByteReader.h Lz77.{h,cpp} Serde.{h,cpp}
│   │            SaveFile.{h,cpp} SlotManager.{h,cpp} Migration.{h,cpp} Chronicle.{h,cpp}
│   ├── rng/     SplitMix.h Streams.{h,cpp}                # 每子系统独立流 + 消费计数入档
│   ├── core/    Game.{h,cpp} GameState.{h,cpp} TickPipeline.{h,cpp} ActionPoints.{h,cpp}
│   │            Pending.{h,cpp} Errors.h LogCodes.h Replay.{h,cpp}
│   ├── domain/  Resource.h Species.h Empire.h Sector.h Planet.h Fleet.h FleetDesign.h
│   │            Treaty.h Federation.h Domestic.{h,cpp} Crisis.h Tech.h Building.h
│   ├── mkt/     Fix.h OrderBook.{h,cpp} Matching.{h,cpp} MarketMaker.{h,cpp} Impact.{h,cpp}
│   │            VolModel.{h,cpp} Futures.{h,cpp} Margin.{h,cpp} Settlement.{h,cpp}
│   │            Exchange.h Arbitrage.{h,cpp} FX.h BlackMarket.{h,cpp} Debt.{h,cpp}
│   │            Insider.{h,cpp} ManipulationDetect.{h,cpp} MarketState.{h,cpp}
│   ├── ai/      OmniscientReader.{h,cpp} ToModel.{h,cpp} IntentPredictor.{h,cpp}
│   │            ForwardSimulator.{h,cpp} Belief.{h,cpp} Strategy.{h,cpp} Payoff.{h,cpp}
│   │            BetrayalCalculus.{h,cpp} Reputation.{h,cpp} PowerBalancing.{h,cpp}
│   │            FraudDetect.{h,cpp} ComputeBudget.h FactionAI.{h,cpp} FederationVote.{h,cpp}
│   ├── items/   ItemDef.h Registry.{h,cpp} Inventory.{h,cpp} RuleEngine.{h,cpp}
│   │            Recipes.{h,cpp} Synergy.h Conflict.h Effects.{h,cpp} ProvenanceForge.{h,cpp}
│   ├── clue/    ClueDef.h Graph.{h,cpp} Confidence.{h,cpp} Deduce.{h,cpp}
│   │            MinimalSet.{h,cpp} Provenance.{h,cpp} Contradiction.{h,cpp}
│   ├── plot/    Skeleton.h Acts.{h,cpp} BeatResolver.{h,cpp} ConclusionPool.{h,cpp}
│   │            Endings.{h,cpp} EpochFlow.{h,cpp} Narrate.{h,cpp}
│   ├── gen/     EpochSeed.{h,cpp} StarMapGen.{h,cpp} EmpireGen.{h,cpp} ContentGen.{h,cpp}
│   │            AnomalyGen.{h,cpp} ModifierGen.{h,cpp} NameGen.{h,cpp} EventSchedule.{h,cpp}
│   ├── combat/  Resolver.{h,cpp} BattleReport.{h,cpp}
│   └── content/ Species.cpp Ethics.cpp Civics.cpp Governments.cpp Commodities.cpp
│                Items.cpp Recipes.cpp Clues.cpp Conclusions.cpp Events.cpp Anomalies.cpp
│                Buildings.cpp FleetModules.cpp Megastructures.cpp Ascensions.cpp
│                Factions.cpp StoryText.cpp Endings.cpp
└── tests/     check.h main.cpp test_crypto.cpp test_save.cpp test_market.cpp test_ai.cpp
               test_items.cpp test_clue.cpp test_gen.cpp test_replay.cpp test_cli.cpp
```

依赖方向严格单向：`cli → core → {mkt,ai,items,clue,plot,gen,combat} → domain → {save,crypto,rng,util}`；`content` 只读。

## 3. 存档格式（`slot-<id>.gsv`，二进制 + 加密 + 认证）

目录 `$XDG_DATA_HOME/greyfall/saves`（回退 `~/.local/share/greyfall/saves`），`--data-dir` / `GREYFALL_DIR` 可覆盖。

```
off  size 字段                          off  size 字段
0    4    magic "GFAV"                  44   8    createdTick(u64)
4    1    formatMajor=1                 52   4    plainLen
5    1    formatMinor                   56   4    cipherLen
6    1    flags bit0 LZ77|bit1 池|bit2 明文元数据  60  8    lastCommitTick
7    1    reserved                      68   1    rollbackCount
8    4    schemaVersion                 72   7    reserved
12   4    headerCrc32                   76   8    chronicleHeadHash
16   16   kdfSalt                       84   16   fastReject(HMAC[0..16))
32   12   nonce(由 salt 派生)            100  N    ciphertext (ChaCha20 ctr=1)
                                                     100+N 32  HMAC-SHA256(header||ct)
```

- **密钥**：`K = PBKDF2_like(iteratedSHA256, salt || APP_SECRET [|| passphrase], 20000)` → `encKey(32) | macKey(32)`。`APP_SECRET` 编译期内嵌 ⇒ 无需交互即可加解密（CLI 事务模型必需）；`--key` / `GREYFALL_KEY` 追加口令，口令错 → MAC 失败 → 退出码 3。
- **明文编码**：LEB128 varint + zigzag；全局字符串池 intern；**所有容器按 key 排序** ⇒ 同一状态字节唯一（自测断言"两次 save 字节相同"）。
- **原子写**：`.tmp` → `flush` → `fsync` → `rename` → 目录 `fsync`；损坏时回退 `slot-<id>.bak`。环形检查点 `slot-<id>.ck00..11`。
- **版本迁移**：`Migration::chain(schema→current)`；更高版本 → 退出码 6 明确报错。
- **`Chronicle`（防读档重来的博弈化）**：`<epochDir>/chronicle.gfc` 为**仅追加哈希链**（`head_{n+1} = SHA256(head_n || tick || actionDigest || stateDigest)`）。`load`/`rollback` 时写 `rollbackCount++` 与 fork 记录；AI 在 `OmniscientReader` 里能看到它 → 阵营判定"你会重试"，从而**提高要价、降低让步概率、优先采用不可逆打击**（把 save-scum 变成有代价的博弈变量）。链文件缺失 → 触发「档案焚毁」剧情（不奖励，且各方视为背约者）。

## 4. CLI 契约（无 REPL）

双写法等价：`greyfall advance --ticks=3` ≡ `greyfall --action=advance --ticks=3`。
全局：`--slot <id>`(默认 `main`) `--seed` `--data-dir` `--key` `--quiet|--verbose` `--no-autosave` `--dry-run` `--limit N` `--as-of <tick>` `--ascii`（`NO_COLOR` 兼容）。

- 生命周期：`new --seed S [--difficulty d --empires n]` · `status` · `help` · `man <cmd>` · `version` · `selftest` · `verify <slot>` · `slots` · `resume`
- 世界：`overview` · `starmap [--system X]` · `sectors` · `planet <id>` · `empires [--filter]` · `relations [--empire X]` · `treaties` · `federation` · `domestic`（国内派系） · `tech` · `fleets` · `ship <design>` · `buildings` · `logs [--last N --phase P]` · `history [--ticks]` · `replay --from <tick>`
- 市场：`book <exch> <res> [--depth 8]` · `quote <res>` · `curve <res>`（期货期限结构/基差） · `position` · `pnl` · `vol <res>` · `arb [--net]` · `shock [--last N]` · `credit` · `fx` · `blackmarket`
  动作：`order buy|sell <res> <qty> [@ <price>] [--exch X --tif day|gtc]` · `cancel <oid>` · `modify <oid>` · `futures buy|sell <res> <exp> <qty> [--lev n]` · `settle` · `borrow <amt> [--term]` · `repay` · `escrow open|release` · `insure <res> [--cover]`
- 外交/情报：`envoy <emp> <propose-trade|propose-pact|breach|demand-tribute|joint-intel|sanction|declare-war|plead-peace|vassalize|federate-join|federate-leave|vote <yes|no>> [--terms ...]` · `spy <emp> --mission <intel|sabotage|forgery|exfil|insider|read-mind> --budget n` · `gift <emp> <res> <qty>` · `intel [--threat|--what-they-know|--counterintel|--decoy]` · `propaganda <target> <narrative>`
- 道具：`inventory [--filter TAG]` · `item <id>` · `combine <A> <B> [...]` · `use <item> [--target X]` · `disassemble <item>` · `forge-prove <item>`（造假冒牌以污染 AI 模型） · `equip <module> <fleet>`
- 线索：`clues [--tag T --unlinked --subject X]` · `link <A> <B>` · `unlink` · `deduce [--conclusion X --explain|--commit]` · `archive <clue>` · `conclusions`
- 国家运营：`colony <system>` · `ship-build <design> <system>` · `fleet --id X --order <move|patrol|embargo|engage|escort> --to <system>` · `edict <id>` · `research <branch>` · `build <building> <planet>` · `mega <id> <system>` · `ascend <path>` · `recruit`
- 推进与抉择：`advance [--ticks N]` · `choose <index>` · `defer [--ticks N]`
- 存档管理：`save --as <slot>` · `load <slot>` · `rollback <n>` · `export <slot> <path>` · `import <path> <slot>` · `prune --keep N` · `delete <slot> --yes`
- 纪元：`epoch --report` · `epoch --next [--inherit-legacy]` · `epoch --seed <hex>` · `epoch --endless`

**退出码**：`0` 成功 · `1` 参数错误 · `2` 存档不存在 · `3` 完整性/密钥失败 · `4` 非法动作（AP 不足/条件未满足/保证金不足） · `5` 存在待抉择事件 · `6` 版本不兼容 · `7` 市场成交未达成（部分成交时仍 0 并打印残单） · `70` 内部错误。

**文本渲染**：`TextTable`（自带东亚宽度表，不依赖 locale/wcwidth）+ `Fmt` 段落折行；`starmap` 输出 ASCII 航线图（`o` 星系 `·` 空域 `│/│` 航线 `*` 首都 `X` 战线 `¤` 封锁 `◎` 巨构）；盘面数据用等宽表格；所有输出纯文本、可 `| less`。

## 5. Tick 流水线（1 tick = 1 季度，`TickPipeline`）

```
0  AP 复位(4 + 基建/政体加成) & 提交玩家动作队列
1  pending 抉择结算            2  市场：新息到达 → 撮合(N 轮 auction-call) → 做市重报价
3  波动率/库存/保证金更新 → 强平级联 → 交割与违约结算 → 跨所套利收敛 → 汇价
4  OmniscientReader 快照(玩家全字段 + rollbackCount + 各字段暴露路径)
5  ToModel 更新(AI 对玩家的效用/风险/贴现/类型后验) → FraudDetect 异常打分
6  IntentPredictor + ForwardSimulator → 各 AI 阵营动作(外交/背叛/建仓/封锁/殖民/研发/舆论)
7  联邦决议(加权投票 + logrolling)   8  国内派系诉求/民心/政变风险
9  舰队与战线结算(combat)             10  事件调度器(异常/危机/剧情幕)
11 线索可信度传播 & 衰减 & 矛盾裁定     12 结论解锁 → 幕次推进 → 结局评估
13 收入/维护/折旧/种族张力             14 tick++ → chronicle 追加 → autosave
```
各阶段对 `GameState` 做纯函数式修改并使用独立 RNG 子流 ⇒ 动作序列可精确重放（`replay` 命令逐 tick 复现并 diff）。

## 6. 市场：真实微观结构（`src/mkt/`）

### 6.1 标的与场所
现货 22 种（`energy minerals food alloys components medicines supermaterials exotic datacrystals influence unity credits ...`）+ 各族 4 期限期货（F1..F4）；交易所 3 类：`核心区环币交易所(CX)`、`边疆自由市场(FX)`、`黑市(BZ)`。每所有 `depth/fee/transitCost/embargoState/regulator`。

### 6.2 订单簿与撮合
```cpp
struct PriceLevel { Fixed px; std::int64_t qty; std::uint32_t nOrders; };  // 每侧最多 96 档
struct Book { BTreeFixedArray<PriceLevel> bids, asks; u64 seq; Fixed last, mid, spread; };
enum class Tif { Day, Gtc };  enum class OrderKind { Limit, Market, Iceberg, Stop };
// 价格-时间优先；冰量单只显示露出量（真实市场语义），全量在存档中保留
MatchResult Matching::cross(Book&, Order& o, Fixed impactReserve);  // 返回 fill/avgPx/impact/consumed
```
每 tick 分 **8 个 auction call** 批次投放 AI 聚合订单流（避免逐笔爆炸），批次内做集合竞价 + 连续竞价混合，保证撮合确定性（按 `(px, seq)` 稳定排序）。

### 6.3 价格形成（无"公式定价"，全部来自簿内供需）
- 均衡价 = 簿内成交产生的 `last`；基本面漂移由库存/产能/事件决定，通过 **知情交易者的限价单**把信息"发现"进价格（真实价格发现过程）。
- **冲击（平方根定律）**：`Δp_temp = Y * σ_daily * sqrt(Q / V20)`，`Y=0.7(FIX)`；`Δp_perm = κ * Δp_temp`，`κ=0.35`。`p = p_perm + p_temp`（temp 部分按 `λ=0.5` 半衰期回归）。
- **波动率聚集**：`σ²_t = ω + α·r²_{t-1} + β·σ²_{t-1}`（GARCH(1,1) 定点实现，`α+β=0.94`）+ 事件 `jump` 项。
- **做市商（Avellaneda-Stoikov 简化）**：`reservation = mid - q·γ·σ²·Δt`；`spread = base + 2·γ·σ²·Δt + inventoryRisk + embargoPenalty`；风控：库存越限则单边撤单（真实"流动性真空"→闪崩放大器）。

### 6.4 期限结构、保证金与信用
- `basis = (F_n - S)/S = carry(convenience+storage+insurance) - yield`；低库存 → 正便利收益 → **backwardation**；封锁预期 → contango。库存数据部分保密（`insider`/异常解码/线索推断可获取）。
- 保证金：`initMargin = m0 + m1·σ·√T·lev`；逆向触发 `marginCall → 强制平仓 → 吃穿簿深度 → 触发其他持仓者追保`（**级联清算**，实现为批次循环，日志显式打印 `CAUTION: cascade depth=3`）。
- 交割违约：`P(default) = f(cashRatio, reputation, relationship, regimeRisk)`；托管/信用证降低违约但占资金成本 → 形成"信誉即抵押品"的融资博弈；发债影响 `creditRating → 借贷利差`。

### 6.5 操纵、内幕与监管（AI 会主动使用，玩家也可用但有代价）
| 手法 | 实现 | 检测/罚则 |
|---|---|---|
| wash trading | 同一控制人多账户自买自卖制造 `V20` 抬升 | 监管概率 `p=0.03+0.05*violence`；罚没 + 信用崩塌 |
| spoofing | 大单挂而不成交诱导方向 | `ManipulationDetect` 用"下单后撤比例 vs 成交比"做序列表征 |
| corner / 逼空 | 囤积现货使对手无法交割 → 空头挤压 | 现货/流通比阈值告警，联邦可立法限仓 |
| pump & dump | 先建仓→放假情报(道具)→拉抬→出货 | AI 会跟着接盘也会反手做空；`intel` 泄露可让玩家识别 |
| front-running | AI 透视玩家 `order` 队列，抢先吃掉关键档位 | 玩家可拆单（TWAP）、冰量、`--tif day`、延后执行日 |
| insider | AI 在自己事件/研果公布前建仓；玩家 `spy --mission insider` 反向 | 交易前"知情痕迹"评分 → 处罚或作为勒索筹码 |

### 6.6 AI 与市场耦合（透视 → 抢先）
`IntentPredictor` 输出玩家未来 K 期**净需求向量**（例：玩家需 4000 alloys 造巨构），AI 若判断可获利，则在关键档位挂出提价卖单/囤现货；玩家若通过 `counterintel`（污染观测）或 `decoy`（假工程）诱导 AI 误判并高买，即可反杀 —— **透视性带来压迫，反透视带来深度**。

## 7. AI：全知 + 高智能（`src/ai/`）

### 7.1 透视读取
```cpp
struct Observable {               // OmniscientReader::snapshot(state, PLAYER)
  FixedVector resources, budget;  std::vector<ItemView> inventory;      // 含未使用/伪造道具
  std::vector<ClueView> clues;    // 含未 link 的私人线索
  TechView tech;  FleetView fleets;  PendingView pending;               // 未抉择事件与候选支
  OrderIntentView orders;         // 玩家未成交订单（front-running 依据）
  u32 rollbackCount; Hash chronicleHead;                                // 读档历史
  std::vector<FieldExposure> exposure;  // 每字段：被哪条渠道、以何置信度观测
};
```
剧情解释：大沉默后遗留的**泛视网络（Panopticon）**把玩家舰桥遥测变成公开广播 —— 世界"看得见你"，看不见的是**你的意图与你的谎言之所以为你的谎言**。

### 7.2 心智理论（对玩家的在线参数估计）
`ToModel{ wGoal[8], riskAversion, discountδ, typeBelief[合作/剥削/报复/短视], styleHabits[] }`
每 tick 以玩家实际动作做**极大似然/一致性更新**：`θ ← θ + η·(∇_θ U(a_obs) - E[∇U])`（定点梯度，特征为动作-效用梯度）；`modelConfidence` 越高 → AI 越敢提前布防/勒索。玩家可 `spy --mission read-mind` 读出对方对自己的 `θ`，并用故意动作**注入错误梯度**。

### 7.3 前瞻决策（聪明度的来源）
```cpp
// 1) 生成候选动作（外交/市场/军事/研发），按静态效用取 top-K=10
// 2) 对玩家：IntentPredictor 取 top-M=4 条策略路径
// 3) ForwardSimulator.rollout(state', aiAct, playerPolicy, H=3 tick)  // 用同一撮合/战斗内核
//    EV = Σ δ^t · u^own  -  riskPenalty·VaR(下行)  -  allianceRetaliation(扩散图)
// 4) argmax EV；受 ComputeBudget（每季 nodeBudget 与智能等级 foresight∈[1..4]）限制
```
`ForwardSimulator` 复用真实 `Matching`/`combat::Resolver` 的精简版，**AI 因此能预判玩家未来 3 季的需求与打法**。

### 7.4 背叛、信誉与制衡
```cpp
BetrayalEV = PV(背叛收益, T=8) - PV(履约价值) - RepCost(全体观感损失→未来贸易与援助折损)
           - E[warCost] + ExploitGain(if playerModel.可欺)   // 仅在 EV > threshold 时动手
Reputation[f][k] 由观测通道(噪声=距离/宣传/反间谍)更新；宽限期按 grudgeHalfLife 衰减
PowerBalancing: 若玩家 scoreIdx > 0.55 → 形成遏制联盟(军备+封锁+联邦投票串联)
OpinionModel:  对第三方泄露“真实但断章”的情报（挑拨，成本最低）
Entente:       联邦投票 = 实力权重 + 贡献 + logrolling(以贸易让利换票)
```
可玩性护栏：AI 的每一步恶意动机都会写进 `intel --threat` 与 `logs --phase AI`（“为什么它背刺我”可复盘：显示 EV 分解）——**高压但可读懂**，不神秘、不可怒。

## 8. 道具系统：声明式规则图（`src/items/`）

道具即"机制筹码"（`汇票`/`观测密钥`/`伪造印信`/`航线特许`/`托管契约`/`数据核心`/`静默场`/`心灵屏障`…）。
`ItemDef{id, tags[≤6], tier, baseCost, effects[], provenance}`，规则表：
```
CombineRecipe(120)   配方链最深 4 层：A+B→C（失败率受 tag 冲突影响，可 disassemble 回材）
Synergy(70)          同类/异类同持生效：[契约]+[观测] → 谈判折价 -15%
Conflict(45)         互斥相克：[伪造]与[审计]同持 → 被 AI FraudDetect 权重 ×2
Gate(40)             门槛解锁：无[心灵屏障]不可对灵族使用间谍
CarrierBinding(30)   载体绑定：模块装舰队 → 改变战力系数与运输滑点
MarketIntervene(25)  价格干预：制造假库存改变 AI 的基本面模型（不影响真实簿）
BeliefIntervene(20)  信念干预：降低 AI 的 modelConfidence / 注入梯度噪声
ClueBridge(60)       产出桥：使用后生成特定 clue（带 provenance 与可信度起点）
```
`RuleEngine` 每 tick + 每次动作后**迭代到不动点**（≤16 轮，优先级 tag 模式匹配），日志可追踪 `item --explain <id>`。

## 9. 线索超图与推断引擎（`src/clue/`）

- 边类型 5：`印证 corroborate` / `矛盾 contradict` / `前置 prereq` / `归属 belongs` / `指涉主体 about`；节点带 `credibility`（按来源可靠性 + 时间衰减 + 重复来源相关性折扣）与 `provenance`（可被伪造、可被 AI 布置成"真话的谎言"）。
- `Deduce::minimalSatisfyingSets(conclusion)`：在合取范式上枚举最小充分集（节点数上限 18 时剪枝 DFS + 超边支配集剪枝），附加约束：**来源多样性 ≥3**、**无未裁定矛盾**、**至少 1 条来自对手内部**（防单信道污染与 AI 摆拍）。
- 误判代价：`--commit` 错判 → 假剧情分支 + 外交信誉损失 + 市场恐慌（真后果）；`--explain` 只出报告，允许 `link/unlink` 反复修订。

## 10. 剧情、机制与无限生成

- 主线《大沉默》7 幕（每幕 2–4 结论节点）+ 6 条阵营支线簇 + 隐藏第 8 幕；**结局向量 8 维**（霸权/联邦/资本/种族/知识/信仰/毁灭/超脱）→ 12 个命名结局。剧情推进与市场和 AI 模型双向耦合：真相公开会重估 AI 对你意图的先验并引发价格跳跃。
- `EpochGenerator`：`seed → 双曲星图航线图 + 8–16 国（种族/伦理/公民/领袖人格采样并解 trait 冲突）+ 交易所与航运咽喉 + 危机时间表 + 剧情骨架变体填充 + 异常点池 + 全局词缀（规则修正，例"泛视网络增强：AI foresight+1，但操纵检测率+20%"）`。
- 通关 → `epoch --next [--inherit-legacy]`：Legacy（制度遗产/债务/仇敌/技术/信誉/被攻破的心智模型）继承入下纪元，难度与 AI 智能等级递增，**无上限**；`ContentGen` 在纪元边界程序化生成新事件/新道具/新线索节点（与静态内容池合并，保持 tag 图闭合）。
- 双层博弈：国内派系（军部/商会/技工/原教旨…）持续提出要求，压制或收买有代价，民心过低触发政变；AI 会**直接向你国内派系输送资金/情报**打代理人战（真实的内政干预）。

## 11. 内容资产规模（编译期静态表）

| 资产 | 数量 | 文件 |
|---|---|---|
| 种族原型 / 伦理 / 公民特质 / 政体 | 18 / 12 / 24 / 14 | `Species.cpp Ethics.cpp Civics.cpp Governments.cpp` |
| 可交易标的 / 期货 / 交易所 / 巨构 / 建筑 / 舰船模块 | 22 / 4 期 / 3 / 6 / 28 / 36 | `Commodities.cpp Megastructures.cpp Buildings.cpp FleetModules.cpp` |
| 道具 / 配方 / 协同 / 互斥 / 门槛 / 线索桥 | 130 / 120 / 70 / 45 / 40 / 60 | `Items.cpp Recipes.cpp` |
| 线索节点 / 结论 / 最小充分集 / 矛盾对 | 340 / 84 / 210 / 160 | `Clues.cpp Conclusions.cpp` |
| 事件（危机/市场冲击/外交/异常/剧情） | 140（30/25/20/25/40） | `Events.cpp Anomalies.cpp` |
| 主线幕 / 支线簇 / 结局向量 / 命名结局 | 7 / 6 / 8 维 / 12 | `StoryText.cpp Endings.cpp` |

## 12. 实施里程碑（按可交付顺序）

1. **M1 骨架**：`CMakeLists.txt`、`util/Fixed`、`rng/Streams`、`core/Game`、`cli/ArgParser`、`main.cpp`（`help/version/man/status`）。
2. **M2 存档**：`crypto/{Sha256,ChaCha20,Hmac,Kdf}` + `save/{VarInt,Lz77,Serde,SaveFile,SlotManager,Chronicle}` + `slots/save/load/verify/rollback/export/import/prune` + 字节级确定性自测。
3. **M3 世界生成**：`gen/{EpochSeed,StarMapGen,EmpireGen,NameGen}` + `domain/*` + `starmap/overview/empires/relations` + `new` 命令。
4. **M4 市场**：`mkt/*` 全量（OrderBook/Matching/MarketMaker/Impact/VolModel/Futures/Margin/Settlement/Arbitrage/FX/BlackMarket/Debt/Insider/ManipulationDetect）+ `order/book/quote/curve/position/pnl/arb`。
5. **M5 AI**：`ai/*` 全量（OmniscientReader/ToModel/IntentPredictor/ForwardSimulator/BetrayalCalculus/Reputation/PowerBalancing/FraudDetect/FederationVote/FactionAI）+ `envoy/spy/intel` + `--what-they-know` 透明度报告。
6. **M6 道具 & 线索 & 战斗**：`items/*`、`clue/*`、`combat/*`、`domestic/*`。
7. **M7 剧情与纪元**：`plot/*`、`content/*` 全量文本、`epoch/*`、结局评估。
8. **M8 打磨**：`TickPipeline` 完整阶段序、`replay`、性能（<250ms/4ticks）、`selftest` 报告、`README`（规则 + 经济 + AI 行为契约 + 存档规范 + CLI 手册 + 教程对局）。

## 13. 测试

- `tests/`（CTest）：crypto 往返与 MAC 篡改拒绝；save 字节确定性（同状态两次落盘相同）；迁移链；撮合的**单调性与无自成交**；冲击定律随 Q 单调；保证金级联收敛（不无限循环）；AI `BetrayalEV` 可解释（同一状态同 EV）；`FraudDetect` 对注入假库存有响应；道具规则图不动点无环；推断引擎最小集正确性（暴力校验小图）；`gen` 同种子同结果；`replay` 逐 tick 状态哈希一致。
- `greyfall selftest`：内置约 40 项断言 + 一段固定种子自动对局（200 tick）并输出性能/体积报告；`--dry-run` 不落盘的幂等性检查。
- 平衡脚本：`tests/bots.cpp` 提供 3 个基线玩家策略（合作型/剥削型/读档型），跑 50 纪元检验 AI 能剥削弱者、被强者反制、并惩罚 `rollbackCount>0`。

## 14. 风险与对策

| 风险 | 对策 |
|---|---|
| AI 透视导致"必输"体验 | 强度参数化（`--difficulty` 决定 foresight/compute）；AI 动机在 `logs --phase AI` 明示 EV 分解；提供 `counterintel/decoy/read-mind` 反制线；`intel --threat` 实时提示模型置信度 |
| 撮合 + 搜索性能 | AI 聚合订单流（块单）+ 每批 auction-call + `top-K` 意图剪枝 + `nodeBudget` 上限 + 簿档位数上限 96；`--dry-run` 采样评估 |
| 加密存档不可调试 | `export --plaintext`（显式警告破坏公平）、`verify` 摘要、`--no-encrypt` 开发模式（编译期开关） |
| 内容爆炸（130 道具×340 线索）导致 bug | 静态表构建期校验（tag 闭合、引用存在、结论可满足）；`selftest` 中图可达性/无孤点检查 |
| 无浮点导致数值失真 | `FIX=1000` 全程定点 + 关键路径 `__int128`；`selftest` 输出与浮点参考实现偏差表（阈值 <0.2%） |

## 15. 目标会话示例（纯命令式，展示透视与市场）

```bash
greyfall new --seed 5EED-C0FFEE --difficulty 3 --empires 12
greyfall market --res alloys --depth 8            # 看到 3 档买卖、spread、σ、V20
greyfall order buy alloys 4200 @31.4 --tif gtc    # 你的意图进入队列（AI 看得见）
greyfall advance --ticks 1
greyfall intel --what-they-know --empire 赫尔维商盟
  → 已知：库存 alloys 12,400 · 在建【戴森环】需求 +38,000 · 未抉择事件 2 · rollbackCount 0
  → 判断：你 3 季内必须买入 3.8 万合金（置信 0.71） ⇒ 已提价并在 F3 建立多头
greyfall intel --decoy --signal "巨型工程已暂停"   # 布置数据：改变 AI 的梯度
greyfall futures sell alloys F3 9000 --lev 3       # 反手对冲它的多头
greyfall link clue#137 clue#204 && greyfall deduce --conclusion C-42 --explain
greyfall spy 赫尔维商盟 --mission read-mind --budget 6   # 读出它对你的 θ（镜像博弈）
greyfall advance --ticks 2 ; greyfall epoch --report
```