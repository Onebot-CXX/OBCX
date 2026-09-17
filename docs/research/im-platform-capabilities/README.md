# 主流 IM/SNS 能力调研与 OBCX Bot API 拆分

> 调研基线：**2026-08-17**。本目录保存当时的研究证据和候选设计，不是当前
> runtime/API 合同。其候选数量、类名和拆分建议不能当作已实现能力。

## 与当前实现的关系

研究确认了巨型通用 Bot 接口不可持续。当前代码已经删除 `IBot`、provider Bot
接口、`QQBot`/`TGBot`、`BotRegistry` 和 live-bot RTTI wrapper，并落地了较窄的
实现：每个进程级 `BotInstallation` 使用固定 recipe 组装 protocol、transport、
event ingress、operation 以及 Telegram 可选组件；Actor 只能看到 data-only
`BotOperationGateway`。当前并未实现本研究提出的 56 项通用 capability 目录，也未
开放用户自定义 component graph 或 typed ingress。

## 历史候选摘要

九个平台的产品与官方开放能力差异很大，无法用 OneBot 11 风格的巨型接口安全
统一。以下计数描述研究候选空间，不描述当前 OBCX 已实现组件：

| 类别 | 精确数量 |
|---|---:|
| 核心领域/数据模块 | **13** |
| 可选 executable transport capability | **21** |
| 运行时基础设施组件 | **12** |
| 平台扩展包 | **10**（九个平台 + 独立 OneBot 11） |
| **候选总数** | **56** |
| **Bridge MVP** | **26**（10 core + 4 capability + 10 runtime + 2 pack） |

Bridge MVP 只覆盖 typed ingress、message send/mutation、media transfer、显式账号路由，以及 `telegram.*`/`onebot11.*` 两个 pack；不一次性实现全部平台能力。

## 关键平台边界

- `qq.official` 与 `onebot11.qq` 分离；OneBot 的数字 ID、好友/群管理、cookie/CSRF 等不证明腾讯官方 API 能力。
- Telegram Bot API 与 TDLib/用户客户端是不同授权模型；Bot API 没有通用 history、contacts、presence、calls 或 secret chat。
- 未发现受支持的个人微信聊天 Bot API；公众号、小程序、微信客服、Open Platform 分面独立。微信客服统一命名为 `wechat.customer_service`。
- WeCom internal app、appchat、group robot、intelligent robot、customer contact、archive 不是一个权限面。
- X 是 SNS + legacy DM/XChat activity，不强制映射为 guild/group IM。
- Matrix stable spec、optional module/profile、MSC 与具体客户端实现必须分层；Feishu 与 Lark 也不能假定 parity。
- business actor 不获得 process component registry、installation 或 live transport；连接、token、cursor、retry 和 media stream 保持 process-owned。当前 Actor Bot 出站面是 `BotOperationGateway`。

## 方法与审计波次

1. **Wave 1：**九个 fresh researcher 分别调研 Discord、X、QQ、微信、企业微信、飞书/Lark、钉钉、Matrix、Telegram；一个 scout 静态审计 OBCX。每个 lane 独立保存调研动作、source register、产品/API 边界、能力、限制、冲突与设计影响。
2. **Wave 2：**两个 fresh reviewer 并行做能力分类与证据审计。
3. **Wave 3：**唯一 repository writer 归档报告并整合矩阵、组件和迁移建议。
4. **Wave 4：**初次只读验证发现的 blocking findings 已由同一 repository writer 修正；中间 recheck 的唯一 DingTalk caveat 随后修正，最终只读验证结论为 **PASS**，并已归档为 [`reviews/final-validation.md`](reviews/final-validation.md)。

审计保存的是可复现的检索/访问记录、来源、证据、冲突和结论，不要求或保存模型私有思维链。十份 Wave 1 平台/本地报告按字节原样归档；run、artifact、transcript 与 SHA-256 映射见 manifest。

## 证据限制

- WeCom 18 个来源中 12 个为 secondary mirror；二手-only 能力在总矩阵中降为 `UNKNOWN`。
- QQ active send 的生命周期说明与当前 SDK helper 冲突，默认关闭并要求按批准 app 探测。
- X 多个 access tier/Activity/DM/media 页面冲突；Feishu/Lark 无完整 parity matrix。
- Discord/X 少数官方 Help 页面只能访问索引，标为 `OFFICIAL — ACCESS-LIMITED`。
- DingTalk 部分 SDK 结论来自第三方 package index；Matrix MSC、Telegram 2026 新模式具有版本/实现差异。
- OBCX lane 是静态代码审计，未执行 runtime/API integration 或 adapter conformance tests。

因此 `UNKNOWN` 是有意的安全结论，不应在实现时被“补齐”为支持。

## 文档导航

| 文档 | 内容 |
|---|---|
| [能力矩阵](capability-matrix.md) | 九个平台 `P:` 产品 / `A:` 官方 API 交叉矩阵、surface key 与冲突 |
| [组件建议](component-proposal.md) | 13/21/12/10 精确目录、依赖、typed protocol、SPI、56 总数与 26 MVP |
| [OBCX 差距与迁移](obcx-gap-analysis.md) | `IBot`/Telegram/Bridge/ownership 证据与 OneBot/Telegram 分阶段迁移 |
| [来源索引](sources.md) | 193 个 web source IDs（177 primary、16 secondary/derived）、22 个本地 file IDs 与质量处置 |
| [审计 manifest](audit-manifest.json) | agent/run/mission/artifact/transcript/hash/变换记录 |
| [能力分类审查](reviews/capability-taxonomy.md) | 独立 taxonomy reviewer 的完整候选与反例 |
| [证据审计](reviews/evidence-audit.md) | 来源质量、冲突、必须 UNKNOWN 的格与整合修正 |
| [最终验证](reviews/final-validation.md) | Wave 4 完整验证历史；初次 `FAIL` 与中间 caveat 均已修正，最终 verdict 为 **PASS** |

### 原始 lane 报告

- [Discord](agents/discord.md) · [X](agents/x.md) · [QQ/OneBot 边界](agents/qq.md)
- [微信](agents/wechat.md) · [企业微信](agents/wecom.md) · [飞书/Lark](agents/lark.md) · [钉钉](agents/dingtalk.md)
- [Matrix](agents/matrix.md) · [Telegram](agents/telegram.md) · [OBCX current](agents/obcx-current.md)

## 非目标与冻结状态

本调研文档本身不定义 runtime，不承诺非官方协议稳定性，不把 actor 变成连接
owner，也不把商业套餐价格写入稳定合同。调研后的当前实现以
[`bot-component-runtime.md`](../../architecture/bot-component-runtime.md) 和
[`qq-telegram-bot-operations.md`](../../architecture/qq-telegram-bot-operations.md)
为准；未来扩展到其他平台仍需独立 ADR、OpenSpec 与 executable conformance tests。
