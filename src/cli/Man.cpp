#include "cli/Man.h"

#include "cli/Commands.h"
#include "cli/TextTable.h"
#include "core/Errors.h"
#include "util/Fmt.h"
#include "util/Str.h"

namespace gf {
namespace {

constexpr std::string_view kVersion = "0.9.0-d09";
constexpr std::string_view kBuild = "C++20 / 零依赖 / int64 定点 FIX=1000";

}  // namespace

const std::vector<ManEntry>& manEntries() {
    static const std::vector<ManEntry> entries = {
        // ---- 生命周期 ----
        {"help", "生命周期", "greyfall help [--short]",
         "显示总览帮助",
         "列出全部命令、全局选项、退出码与常用工作流。",
         "greyfall help"},
        {"man", "生命周期", "greyfall man <command>",
         "显示单命令手册",
         "给出该命令的完整用法、参数语义、副作用与示例。",
         "greyfall man advance"},
        {"version", "生命周期", "greyfall version",
         "版本与构建信息",
         "打印版本、编译器特性、定点标度、内容资产规模摘要。",
         "greyfall version"},
        {"new", "生命周期", "greyfall new --seed <S> [--difficulty d] [--empires n] [--endless]",
         "以种子开一个新纪元",
         "从种子生成星图、帝国、交易所、危机时间表与剧情骨架。玩家固定为索引 0 的阵营。",
         "greyfall new --seed 5EED-C0FFEE --difficulty 3 --empires 12"},
        {"status", "生命周期", "greyfall status",
         "一屏概览",
         "tick、纪元、幕、国库、国力、AP、待抉择、持仓与威胁摘要。",
         "greyfall status"},
        {"selftest", "生命周期", "greyfall selftest [--verbose]",
         "内置自检",
         "约 40 项断言 + 固定种子 200 tick 自动对局，输出性能与存档体积报告。",
         "greyfall selftest --verbose"},
        {"verify", "生命周期", "greyfall verify <slot>",
         "校验存档完整性",
         "检查头部 CRC、fastReject、HMAC、LZ77、反序列化与 chronicle 链。",
         "greyfall verify main"},
        {"slots", "生命周期", "greyfall slots",
         "列出存档槽",
         "显示每个槽的大小、tick、rollbackCount、是否有备份与检查点。",
         "greyfall slots"},
        {"resume", "生命周期", "greyfall resume",
         "恢复最近进度",
         "等价于 load 最近修改的槽并打印 status。",
         "greyfall resume"},

        {"save", "存档", "greyfall save --as <slot>",
         "另存为槽",
         "把当前状态原子写入指定槽（旧档轮转为 .bak，并刷新环形检查点）。",
         "greyfall save --as before-war"},
        {"load", "存档", "greyfall load <slot>",
         "载入槽",
         "载入指定槽，并在 chronicle 链上留下一条 Load 记录（AI 可见）。",
         "greyfall load before-war"},
        {"rollback", "存档", "greyfall rollback <n>",
         "回退 n 个 tick",
         "从环形检查点回退。rollbackCount 递增并被泛视网络记录 —— AI 会据此提高要价、降低让步概率、优先采用不可逆打击。",
         "greyfall rollback 2"},
        {"export", "存档", "greyfall export <slot> <path> [--plaintext]",
         "导出存档文件",
         "--plaintext 导出未加密状态（显式警告：破坏公平性）。",
         "greyfall export main backup.gsv"},
        {"import", "存档", "greyfall import <path> <slot>",
         "导入存档文件",
         "校验 magic 与完整性后写入指定槽。",
         "greyfall import backup.gsv restored"},
        {"prune", "存档", "greyfall prune --keep N",
         "剪枝旧槽",
         "按修改时间保留最近 N 个槽，当前槽受保护。",
         "greyfall prune --keep 8"},
        {"delete", "存档", "greyfall delete <slot> --yes",
         "删除槽",
         "不可逆操作，必须显式加 --yes。",
         "greyfall delete old-run --yes"},
        {"chronicle", "存档", "greyfall chronicle [--last N]",
         "查看防读档哈希链",
         "显示链头、链节、完整性、回退/载入计数。链文件缺失会触发「档案焚毁」。",
         "greyfall chronicle --last 20"},

        {"resolve", "决议", "greyfall resolve [--detail <id>|--activate <id>|--available]",
         "决议系统",
         "五类决议：主动决议（花代价换收益）、自动触发（条件满足即生效）、可阻止（满足条件即可避免减益）、"
         "倒计时（期限内达成目标转增益，超时转减益）、牺牲换利（永久牺牲一项换另一项）。",
         "greyfall resolve --available\ngreyfall resolve --detail 基建攻坚\ngreyfall resolve --activate 科研拨款"},
        {"victory", "决议", "greyfall victory",
         "胜利条件进度",
         "显示征服、民心、最低派系满意度、财政、基本物资储备、国内秩序及连续治理季数。"
         "各项须在季末连续达标，难度 1～5 分别需要 12/15/18/21/24 季；中断则重新计数。",
         "greyfall victory"},

        {"battles", "军事", "greyfall battles [--empire X]",
         "战斗态势",
         "战斗是持续多季的状态：双方每季结算伤害与组织度，组织度归零的一方撤退。"
         "战斗宽度限制同时展开的战力，堆叠兵力会溢出并产生指挥惩罚。地形与要塞给防守方减伤。",
         "greyfall battles"},
        {"peace", "军事", "greyfall peace [--convene <敌>|--annex <星系>|--reparations <金额>|--tech <科技>|--manpower <千人>|--remove <n>|--conclude|--abandon]",
         "和平会议",
         "战争结束时的清算：占优方用**战争分数**兑换要求 —— 割让星系、战争赔款、"
         "技术转移、人力征调、附庸。每个要求消耗分数，分数耗尽即无法再索取。"
         "一方首都被占领时召开「无条件」会议，战败方无权拒绝。",
         "greyfall peace\ngreyfall peace --convene 3\ngreyfall peace --annex 12\ngreyfall peace --conclude"},
        {"front", "军事", "greyfall front [--detail <emp>]",
         "战线总览",
         "把与每个敌国的边界拆成若干「扇区」（每个扇区是一个可进攻的敌方星系），"
         "显示各扇区的我方可用战力、敌方守备与战力比，并给出姿态建议与建议主攻方向。"
         "每季系统会把闲置舰队按兵力缺口自动派往最需要的扇区。",
         "greyfall front\ngreyfall front --detail 3"},
        {"commanders", "军事", "greyfall commanders [--recruit [--trait T] | --assign <c>:<f>]",
         "指挥官",
         "指挥官提供攻/防/后勤/展开宽度修正，并随战斗累积经验与战绩。"
         "特质：攻势 / 防御 / 后勤 / 机动 / 诡道 / 鼓舞 / 攻坚。",
         "greyfall commanders\ngreyfall commanders --recruit --trait 攻势\ngreyfall commanders --assign 2:0"},

        {"policy", "决议", "greyfall policy [--enact <政策> | --detail <政策>]",
         "政策系统",
         "持久化法令：每组同时只有一项生效，切换需付影响力并经历过渡期，按季收维护费。"
         "五组：经济体制 / 军事体制 / 社会体制 / 外交路线 / 情报体制。",
         "greyfall policy\ngreyfall policy --detail 战时统制\ngreyfall policy --enact 福利国家"},

        {"parliament", "决议", "greyfall parliament [--list|--propose <法案>|--detail <法案>|--persuade <派系>|--vote|--force|--withdraw]",
         "议会立法",
         "与 policy（玩家直接推行）不同：法案必须经**议会表决通过**。"
         "各派系按影响力占有席位，对每项法案有天然立场（赞成/反对/未定）。"
         "玩家可用资源与政治资本拉票改变立场；也可强行通过，但代价极高。"
         "通过门槛由政体决定：民主为简单多数，寡头/独裁/神权/军政府为绝对多数，蜂群/无政府为特别多数。",
         "greyfall parliament\ngreyfall parliament --propose 土地改革\ngreyfall parliament --persuade 劳工\ngreyfall parliament --vote"},

        {"trade", "经济", "greyfall trade [--open <出口方>:<商品>[:税率%]|--close <id>|--tariff <id> <pct>|--detail <id>]",
         "贸易路线与关税战",
         "帝国间的实物贸易流：出口方的剩余物资运往进口方。出口方获得货款，"
         "进口方获得物资并按关税率征税。关税率可在 0~60% 之间调整 —— "
         "提高关税增加财政收入但压低贸易量并恶化关系；对方可实施报复性关税。"
         "战争与禁运会自动中断路线。",
         "greyfall trade\ngreyfall trade --open 2:合金:15\ngreyfall trade --tariff 1 40\ngreyfall trade --detail 1"},

        {"network", "情报", "greyfall network [--establish <emp>|--agents <emp> <±n>|--mission <emp> <任务>|--detail <emp>|--disband <emp>]",
         "间谍网络",
         "持续性情报基础设施：在目标帝国内建立网络，渗透度随时间累积并解锁更强任务。"
         "渗透越深、特工越多，暴露风险越高；达到 100% 即被破获，作废网络并损失声望与关系。"
         "任务：侦察 / 煽动 / 窃取科技 / 破坏 / 潜伏。",
         "greyfall network\ngreyfall network --establish 2\ngreyfall network --agents 2 1\ngreyfall network --mission 2 侦察"},

        // ---- 世界 ----
        {"overview", "世界", "greyfall overview",
         "世界总览",
         "纪元名、词缀、星系/行星/帝国统计、联邦与危机列表。",
         "greyfall overview"},
        {"starmap", "世界", "greyfall starmap [--system X] [--all]",
         "ASCII 星图与航线",
         "o 星系 · 空域 │ 航线 * 首都 X 战线 ¤ 封锁 ◎ 巨构。",
         "greyfall starmap"},
        {"sectors", "世界", "greyfall sectors",
         "星区列表", "星区开发度与所含星系。", "greyfall sectors"},
        {"planets", "世界", "greyfall planets [--empire N] [--id <行星>]",
         "行星星表",
         "列出某帝国全部殖民行星及其编号 —— build 需要行星编号，colony 需要星系编号，"
         "而此前没有任何命令能列出它。含类型、规模、人口、开发度、民怨与建筑数。",
         "greyfall planets\ngreyfall planets --id 3"},
        {"planet", "世界", "greyfall planet <id>",
         "行星详情", "类型、规模、人口、产出、建筑、异常点。", "greyfall planet 7"},
        {"empires", "世界", "greyfall empires [--filter alive]",
         "帝国列表", "种族/伦理/政体/国力/观感。", "greyfall empires"},
        {"relations", "世界", "greyfall relations [--empire X]",
         "关系矩阵", "观感、信任、恐惧、债务、战争状态。", "greyfall relations --empire 3"},
        {"treaties", "世界", "greyfall treaties",
         "条约列表", "全部生效条约与到期 tick。", "greyfall treaties"},
        {"federation", "世界", "greyfall federation [--motions]",
         "联邦与投票", "成员、凝聚力、共同舰队、待决动议与加权投票。", "greyfall federation"},
        {"domestic", "世界", "greyfall domestic",
         "国内派系列表", "各派系影响力/满意度/诉求压力、民怨与政变风险。", "greyfall domestic"},
        {"tech", "世界", "greyfall tech [--branch b]",
         "科技树", "已完成、进行中、可研究项。", "greyfall tech"},
        {"fleets", "世界", "greyfall fleets [--id X]",
         "舰队列表", "位置、战力、士气、补给、命令。", "greyfall fleets"},
        {"ship", "世界", "greyfall ship <design>",
         "舰船设计详情", "船体、模块、成本、战力系数。", "greyfall ship 隼级"},
        {"buildings", "世界", "greyfall buildings [--planet P]",
         "建筑列表", "全建筑表与已建成建筑。", "greyfall buildings"},
        {"logs", "世界", "greyfall logs [--last N] [--phase P]",
         "事件日志", "按 tick 阶段过滤；AI 决策显示 EV 分解。", "greyfall logs --phase ai --last 20"},
        {"history", "世界", "greyfall history [--ticks N]",
         "历史轨迹", "国力/国库/指数随时间变化。", "greyfall history --ticks 40"},
        {"replay", "世界", "greyfall replay --from <tick>",
         "重放并 diff", "从检查点逐 tick 复现，比对状态哈希。", "greyfall replay --from 12"},

        // ---- 市场 ----
        {"market", "市场", "greyfall market --res <资源> [--depth 8] [--exch X]",
         "盘面速览",
         "三所买卖档、spread、σ、V20、持仓与套利残差。",
         "greyfall market --res alloys --depth 8"},
        {"book", "市场", "greyfall book <exch> <res> [--depth 8]",
         "订单簿", "指定交易所的买卖档位与累计深度。", "greyfall book CX alloys --depth 8"},
        {"quote", "市场", "greyfall quote <res>",
         "报价", "跨三所最优买卖价、中间价、价差与最新成交。", "greyfall quote alloys"},
        {"curve", "市场", "greyfall curve <res>",
         "期货期限结构", "F1..F4 价格、基差、contango/backwardation 判定。", "greyfall curve alloys"},
        {"position", "市场", "greyfall position",
         "持仓", "现货与期货净头寸、均价、保证金占用。", "greyfall position"},
        {"pnl", "市场", "greyfall pnl [--ticks N]",
         "盈亏", "已实现/未实现盈亏与权益曲线。", "greyfall pnl --ticks 20"},
        {"vol", "市场", "greyfall vol <res>",
         "波动率", "GARCH 状态、σ、V20、近期跳跃。", "greyfall vol alloys"},
        {"arb", "市场", "greyfall arb [--net]",
         "跨所套利", "毛/净价差（含运费、手续费、封锁与关税）。", "greyfall arb --net"},
        {"shock", "市场", "greyfall shock [--last N]",
         "冲击日志", "事件驱动的价格跳跃与 σ 变化。", "greyfall shock --last 10"},
        {"credit", "市场", "greyfall credit",
         "信用与债务", "信用评级、借贷利差、债券与托管记录。", "greyfall credit"},
        {"fx", "市场", "greyfall fx",
         "汇率", "各所结算币价与配给溢价。", "greyfall fx"},
        {"blackmarket", "市场", "greyfall blackmarket",
         "黑市", "溢价、配给强度、可交易违禁品。", "greyfall blackmarket"},
        {"order", "市场", "greyfall order buy|sell <res> <qty> [@ <price>] [--exch X --tif day|gtc]",
         "下单",
         "限价/市价/冰量/止损单。你的意图会进入 AI 可见的队列（可被前置交易）。",
         "greyfall order buy alloys 4200 @31.4 --tif gtc"},
        {"cancel", "市场", "greyfall cancel <oid>",
         "撤单", "按订单号撤销。", "greyfall cancel 1024"},
        {"modify", "市场", "greyfall modify <oid> [@ <price>] [<qty>]",
         "改单", "原子替换价格/数量（保持时间优先失效，重新排队）。", "greyfall modify 1024 @31.9"},
        {"futures", "市场", "greyfall futures buy|sell <res> <exp> <qty> [--lev n]",
         "期货", "F1..F4 期限合约，带杠杆与保证金。", "greyfall futures sell alloys F3 9000 --lev 3"},
        {"settle", "市场", "greyfall settle",
         "交割结算", "结算到期合约、违约与托管释放。", "greyfall settle"},
        {"borrow", "市场", "greyfall borrow <amt> [--term N]",
         "借款", "按信用评级定价；影响杠杆与违约风险。", "greyfall borrow 50000 --term 8"},
        {"repay", "市场", "greyfall repay [--amount A]",
         "还款", "提前还款降低利差与违约概率。", "greyfall repay --amount 20000"},
        {"escrow", "市场", "greyfall escrow open|release <emp> <amt>",
         "托管/信用证", "降低对手方违约概率，但占用资金成本。", "greyfall escrow open 3 40000"},
        {"insure", "市场", "greyfall insure <res> [--cover N]",
         "保险", "为持仓购买保险，降低尾部损失。", "greyfall insure alloys --cover 5000"},

        // ---- 外交 / 情报 ----
        {"envoy", "外交", "greyfall envoy <emp> <action> [--terms ...]",
         "外交使节",
         "propose-trade|propose-pact|breach|demand-tribute|joint-intel|sanction|declare-war|"
         "plead-peace|vassalize|federate-join|federate-leave|vote <yes|no>",
         "greyfall envoy 3 propose-pact --terms \"互不侵犯 8 季\""},
        {"spy", "外交", "greyfall spy <emp> --mission <m> --budget n",
         "间谍行动",
         "intel|sabotage|forgery|exfil|insider|read-mind。read-mind 读出它对 你的 θ（镜像博弈）。",
         "greyfall spy 赫尔维商盟 --mission read-mind --budget 6"},
        {"gift", "外交", "greyfall gift <emp> <res> <qty>",
         "赠礼", "成本信号：提升观感，但也会进入它的 ToModel。", "greyfall gift 3 alloys 1200"},
        {"intel", "外交", "greyfall intel [--threat|--what-they-know|--counterintel|--decoy]",
         "情报与透明度",
         "--what-they-know 打印泛视网络对玩家的读取报告（含暴露路径与置信度）。",
         "greyfall intel --what-they-know --empire 3"},
        {"propaganda", "外交", "greyfall propaganda <target> <narrative>",
         "舆论战", "改变第三方观感；有被识破的反噬。", "greyfall propaganda 4 \"赫尔维在囤积合金\""},

        {"negotiate", "外交", "greyfall negotiate <emp> [--status | --demand ... | --offer ...]",
         "谈判",
         "AI 不会主动倾向于谈判：只有当谈判权重（领土/经济/制裁/战争/军事劣势累积）超过阈值时才肯谈。"
         "条款可交换，也可单方面索取资源、人力、科技、领土、资金、影响力。"
         "若对方首都已被你占领且已放弃抵抗，则必须无条件接受任何条款；"
         "若对方无力支付，其盟友会按可支付额度代付。",
         "greyfall negotiate 3 --status\ngreyfall negotiate 3 --demand \"credits=20000,alloys=5000\"\ngreyfall negotiate 3 --demand \"system=12,tech=comp6\" --offer \"credits=50000\""},

        // ---- 道具 ----
        {"inventory", "道具", "greyfall inventory [--filter TAG]",
         "库存道具", "按 tag 过滤，显示 tier/来源/污染度。", "greyfall inventory --filter 契约"},
        {"item", "道具", "greyfall item <id> [--explain]",
         "道具详情", "--explain 展示规则引擎的求值轨迹。", "greyfall item 观测密钥 --explain"},
        {"combine", "道具", "greyfall combine <A> <B> [...]",
         "合成", "配方链最深 4 层，失败率受 tag 冲突影响。", "greyfall combine 伪造印信 观测密钥"},
        {"use", "道具", "greyfall use <item> [--target X]",
         "使用道具", "触发效果（含市场干预与信念干预）。", "greyfall use 静默场"},
        {"disassemble", "道具", "greyfall disassemble <item>",
         "拆解回材", "按配方逆运算返还材料（有损耗）。", "greyfall disassemble 数据核心"},
        {"forge-prove", "道具", "greyfall forge-prove <item>",
         "造假冒牌", "以假数据污染 AI 的 ToModel 与 FraudDetect。", "greyfall forge-prove 观测密钥"},
        {"equip", "道具", "greyfall equip <module> <fleet>",
         "装备模块", "载体绑定：改变战力系数与运输滑点。", "greyfall equip 相位炮 2"},

        // ---- 线索 ----
        {"clues", "线索", "greyfall clues [--tag T] [--unlinked] [--subject X]",
         "线索列表", "可信度、来源、所属幕、连接情况。", "greyfall clues --unlinked"},
        {"link", "线索", "greyfall link <A> <B> [--kind corroborate|contradict|prereq|belongs|about]",
         "连接线索", "建立超边。", "greyfall link 137 204"},
        {"unlink", "线索", "greyfall unlink <A> <B>",
         "断开线索", "移除超边。", "greyfall unlink 137 204"},
        {"deduce", "线索", "greyfall deduce [--conclusion X] [--explain|--commit]",
         "推断结论",
         "枚举最小充分集；--explain 只出报告，--commit 落地（错判有真代价）。",
         "greyfall deduce --conclusion C-42 --explain"},
        {"archive", "线索", "greyfall archive <clue>",
         "归档线索", "移出工作集但保留其可信度贡献。", "greyfall archive 137"},
        {"conclusions", "线索", "greyfall conclusions",
         "结论列表", "已解锁/已提交/误判的结论。", "greyfall conclusions"},

        // ---- 国家运营 ----
        {"colony", "运营", "greyfall colony [<system>|--detail <system>]",
         "殖民", "查看容量和报价，支付 2 AP、资金与物资启动殖民工程。按季维护和补给，完工才定居；queue 查看进度。", "greyfall colony 21"},
        {"ship-build", "运营", "greyfall ship-build <design> <system>",
         "建造舰船", "消耗合金/部件与 credits。", "greyfall ship-build 隼级 4"},
        {"fleet", "运营", "greyfall fleet --id X --order <o> [--to <system>]",
         "舰队命令", "move|patrol|embargo|engage|escort|blockade|retreat。", "greyfall fleet --id 2 --order move --to 9"},
        {"edict", "运营", "greyfall edict <id>",
         "颁布法令", "国家法令持续 8 季，支付启动资金与每季维护，最多同时三项；派系援助有规模费用与 8 季间隔。", "greyfall edict 战时配给"},
        {"research", "运营", "greyfall research <branch>",
         "切换研究分支", "6 个分支任选。", "greyfall research 工程"},
        {"species", "运营", "greyfall species [--hunt <星系>|--can <批数>|--labor <制度>|--gene <方向>|--press <帝国> --kind <方式>]",
         "种族、奴役、太空生物、外交施压与基因改造",
         "**太空生物**：星系中的巨型生物（海星/虚空鲸/晶簇虫群/裂隙潜行者），"
         "可猎杀获取资源；海星还能加工成**罐头**（食物→奢侈品）。"
         "**奴役**：自由民/种姓制/蓄奴制，以民怨与外交声誉为代价换产出。"
         "**基因改造**：先完成两项生物科技，投入资金、凝聚力和物资施工 8 季，完工永久改写特质。"
         "**外交施压**：不宣战也能迫使对方进贡。",
         "greyfall species\ngreyfall species --hunt 12\ngreyfall species --labor 蓄奴制"},
        {"revolt", "运营", "greyfall revolt [--suppress <星系>|--concede|--refuse]",
         "起义与党派斗争",
         "境内星系会随民怨与派系不满进入**动乱四阶段**：平静 → 不安 → 叛乱 → 割据。"
         "割据持续 8 季未平息，该星系将脱离控制（自立为国或倒向邻国）。"
         "派系在影响力高且满意度极低时会发出**最后通牒**，你必须让步或拒绝。",
         "greyfall revolt\ngreyfall revolt --suppress 25\ngreyfall revolt --concede"},
        {"base", "军事", "greyfall base [--found <星系>|--upgrade <星系>|--dismantle <星系>]",
         "恒星基地",
         "建在**星系**上的永久设施（区别于建在行星上的建筑）。"
         "提供防御（直接计入防守方战力）、舰队补给与维修。"
         "五级升级链：前哨站 → 星港 → 堡垒 → 要塞 → 星际要塞。"
         "星系易主时基地随之易主并降一级。",
         "greyfall base\ngreyfall base --found 3\ngreyfall base --upgrade 3"},
        {"design", "军事", "greyfall design [--new <舰体> [--name 名称]|--install <设计> --module <模块>|--remove <设计> --at <序号>|--clear <设计>|--refit <舰队> --to <设计>]",
         "舰船设计器",
         "六种舰体各有**战场定位**（屏卫/突击/战列/母舰/隐匿/攻坚），不只是数值差异。"
         "可以新建自定义设计、装卸模块，并把现役舰队**改造**为新设计（需在己方星系、消耗合金与信用点）。",
         "greyfall design\ngreyfall design --new destroyer --name 突击型\ngreyfall design --refit 0 --to 5"},
        {"gov", "运营", "greyfall gov [--elect|--support <编号>|--suppress]",
         "政体机制",
         "展示当前政体的**合法性来源**（选举授权/血统传统/恐惧镇压/绩效表现/信仰教义/共识协同），"
         "以及对应的专属手段：选举制可提前改选与公开支持候选人；威权制可镇压但会累积怨恨。",
         "greyfall gov\ngreyfall gov --elect\ngreyfall gov --suppress"},
        {"personnel", "运营", "greyfall personnel [--recruit|--assign <科学家> --branch <n>|--form [名称]|--add <集团军> --fleet <舰队>|--lead <集团军> --commander <指挥官>|--disband <集团军>]",
         "人事：领袖 / 科学家 / 集团军",
         "领袖提供全国修正并有任期与继承；科学家分管研究分支加速立项；"
         "集团军把多支舰队编成一体，由司令统率并获得协同加成（3 支以上同处一星系时协同度上升）。",
         "greyfall personnel\ngreyfall personnel --recruit\ngreyfall personnel --form 先锋集群"},
        {"casus", "外交", "greyfall casus [--all]",
         "正当战争理由与战争疲劳",
         "列出你持有的全部 casus belli（含来源与失效期）以及各场战争的疲劳度。"
         "没有理由就宣战会遭第三方谴责与国内反弹；理由可以自然产生，"
         "也可以用 `envoy <emp> fabricate` 花影响力伪造。",
         "greyfall casus\ngreyfall casus --all"},
        {"proposal", "外交", "greyfall proposal [--detail <n>|--accept <n>|--reject <n>]",
         "AI 提案箱",
         "AI 会主动提出资源互换、研究协定、互不侵犯、联合情报、领土互换等提议。"
         "每份提案都带**公平度**，负数表示对你不利。接受或拒绝都会影响关系；"
         "若提案不合理，你可以先用 `envoy <emp> condemn` 公开谴责。",
         "greyfall proposal\ngreyfall proposal --detail 1\ngreyfall proposal --accept 1"},
        {"military", "军事", "greyfall military",
         "军备状态",
         "显示当前军力、军力上限、难度加成、舰队合计强度，以及补满所需季数。"
         "军力上限由经济、人口与船坞建筑决定；实际军力每季向它靠拢 5%。",
         "greyfall military"},
        {"build", "运营", "greyfall build <building> <planet>",
         "建造建筑", "进入行星建筑队列。", "greyfall build 合金熔炉 7"},
        {"mega", "运营", "greyfall mega <id> <system>",
         "巨构工程", "分阶段付费施工，完工才有收益。每类限一座，并行阶段限一项；queue 查看进度。", "greyfall mega 戴森环 4"},
        {"ascend", "运营", "greyfall ascend <path>",
         "飞升", "科技解锁后投入资金、凝聚力和物资施工，12 或 24 季后完成；每纪元最多两条，各限一次。", "greyfall ascend 心灵"},
        {"recruit", "运营", "greyfall recruit",
         "招募", "付费训练 3 季，按报名舰队数支付资金与补给，完工补充境内未参战舰队的士气和补给。", "greyfall recruit"},

        // ---- 推进与抉择 ----
        {"advance", "推进", "greyfall advance [--ticks N]",
         "推进 tick",
         "执行完整 14 阶段流水线（AP→抉择→撮合→波动→透视→AI→联邦→内政→战斗→事件→线索→剧情→经济→落盘）。",
         "greyfall advance --ticks 4"},
        {"choose", "推进", "greyfall choose <index>",
         "结算待抉择事件", "存在待抉择时 advance 会以退出码 5 中止。", "greyfall choose 0"},
        {"defer", "推进", "greyfall defer [--ticks N]",
         "延后抉择", "延后会在后续 tick 提高代价。", "greyfall defer --ticks 2"},

        // ---- 纪元 ----
        {"epoch", "纪元", "greyfall epoch --report | --next [--inherit-legacy] | --seed <hex> | --endless",
         "纪元管理", "通关后开下一纪元，可继承制度/债务/仇敌/技术/信誉/被攻破的心智模型。",
         "greyfall epoch --report"},
    };
    return entries;
}

const ManEntry* findManEntry(std::string_view name) {
    for (const auto& e : manEntries())
        if (e.name == name) return &e;
    return nullptr;
}

std::string manPage(std::string_view name) {
    const ManEntry* e = findManEntry(name);
    if (e == nullptr) {
        return "没有 `" + std::string(name) + "` 的手册条目。运行 `greyfall help` 查看全部命令。";
    }
    std::string out;
    out += style(std::string("■ ") + std::string(e->name), Style::Heading);
    out += "  [";
    out += e->group;
    out += "]\n";
    out += "  用法: ";
    out += e->usage;
    out += "\n\n";
    out += wrapJoin(e->summary, 88, "  ");
    out += "\n\n";
    out += wrapJoin(e->detail, 88, "  ");
    out += "\n\n";
    out += "  示例:\n";
    for (auto line : split(e->examples, '\n')) {
        if (line.empty()) continue;
        out += "    $ ";
        out += line;
        out += "\n";
    }
    return out;
}

std::string versionText() {
    std::string out;
    out += "greyfall " + std::string(kVersion) + "\n";
    out += "构建: " + std::string(kBuild) + "\n";
    out += "schema: " + std::to_string(kSchemaVersion) + "  格式: GFAV v1\n";
    out += "代号: 大沉默 / Panopticon 泛视网络\n";
    return out;
}

std::string helpText() {
    std::string out;
    out += style("灰域纪元 GREYFALL", Style::Heading);
    out += "  —— CLI 纯文本多阵营博弈（一次进程调用 = 一个事务，无 REPL）\n\n";
    out += style("用法", Style::Sub);
    out += "\n  greyfall <命令> [位置参数] [--选项=值]\n";
    out += "  等价双写法: greyfall advance --ticks=3  ≡  greyfall --action=advance --ticks=3\n\n";

    for (auto g : commandGroups()) {
        out += style(std::string(g), Style::Sub);
        out += "\n";
        TextTable t;
        t.header({"命令", "说明"});
        for (const auto& c : commandTable()) {
            if (c.group != g) continue;
            t.row({std::string(c.name), std::string(c.summary)});
        }
        out += t.render();
        out += "\n\n";
    }

    out += style("全局选项", Style::Sub);
    out += "\n";
    out += "  --slot <id>       存档槽（默认 main）      --seed <S>      随机种子\n";
    out += "  --data-dir <p>    数据目录                 --key <pw>      追加口令\n";
    out += "  --quiet|--verbose 输出级别                 --no-autosave   不落盘\n";
    out += "  --dry-run         采样评估不写盘           --limit N       输出行数上限\n";
    out += "  --as-of <tick>    以历史时点渲染           --ascii         纯 ASCII 输出\n\n";

    out += style("退出码", Style::Sub);
    out += "\n";
    out += "  0 成功 · 1 参数错误 · 2 存档不存在 · 3 完整性/密钥失败 · 4 非法动作\n";
    out += "  5 存在待抉择事件 · 6 版本不兼容 · 7 市场成交未达成 · 70 内部错误\n\n";

    out += style("快速开始", Style::Sub);
    out += "\n";
    out += "  greyfall new --seed 5EED-C0FFEE --difficulty 3 --empires 12\n";
    out += "  greyfall status\n";
    out += "  greyfall market --res alloys --depth 8\n";
    out += "  greyfall order buy alloys 4200 @31.4 --tif gtc\n";
    out += "  greyfall advance --ticks 1\n";
    out += "  greyfall intel --what-they-know --empire 2\n";
    return out;
}

}  // namespace gf
