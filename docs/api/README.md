# 网易云接口审计与 C++ 移植清单

> 范围已按维护者最新要求确定；本目录是**源码静态审计和移植输入**，不是已实现的 API SDK。没有运行上游 Node.js 服务，没有调用真实账号接口，没有采集真实 Cookie。

## 1. 本轮结果

参考仓库：[NeteaseCloudMusicApiEnhanced/api-enhanced](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced)。

- 固定 commit：`a8c781fd64faab17fedfd46e0615a2609307f163`。
- 上游包版本：`4.40.1`；commit 时间：`2026-09-12T00:46:53+08:00`。
- 盘点 `module/` 中全部 **440 个 JavaScript 模块**：**164 个采用候选、40 个待定、236 个排除**。
- 逐个记录源文件 SHA-256、固定链接、包装路由、可静态观察的官方逻辑路径、协议提示、参数引用及归类理由。
- 对请求／加密公共层及登录、音源、歌单写、云盘上传、一起听等关键流程进行了重点源码审计。

这里数的是**上游模块**，不是去重后的 HTTP 端点数量：有模块只是本地 QR 辅助，有模块组合多个请求，也有多个模块调用同一官方路径。`adopt` 的意思是“需要对应能力，计划用 C++ 实现”，不表示整份 JS 可以照抄，也不表示已通过真实接口验证。

上游是**第三方协议实现参考，不是官方 API 文档**。Cirrus 仍直接访问网易云官方服务，不部署上游服务，不要求用户配置 API，不把上游 `/song/url/v1` 等服务包装路由当成官方地址。

## 2. 阅读入口

| 文件 | 用途 |
| --- | --- |
| [selected-endpoints.md](selected-endpoints.md) | 按业务分类阅读采用／待定接口及路径、参数提示 |
| [inventory.json](inventory.json) | 全部 440 个模块的机器可读决定，含排除理由和源码观察 |
| [upstream-lock.json](upstream-lock.json) | 固定版本、module tree、公共文件校验值与许可来源 |
| [protocol-migration.md](protocol-migration.md) | 不依赖 Node 的请求、加密、Cookie、上传等 C++ 替代边界 |
| [special-contracts.md](special-contracts.md) | 每日推荐、私人 FM、一起听的独立业务契约 |
| [gaps-and-hazards.md](gaps-and-hazards.md) | 确认的源码问题、未确认契约和不可直接移植的行为 |
| [migration-plan.md](migration-plan.md) | 后端优先、UI 最后的实施顺序与门禁 |
| [native-foundation.md](native-foundation.md) | 已实现的 C++ 协议、传输、解码和离线 Qt Test |
| [session-isolation.md](session-isolation.md) | 实际编译依赖、epoch／Cookie 隔离和仍待连接的登录边界 |
| [qr-auth-request-chain.md](qr-auth-request-chain.md) | 已实现的上下文请求链／扫码 port 与协议边界 |
| [authentication-transaction.md](authentication-transaction.md) | weapi 身份验证、快照收据、Committing 及精确凭据交接 |
| [anonymous-bootstrap.md](anonymous-bootstrap.md) | 安装级匿名启动、密钥环恢复／持久化、重置排空边界和本轮定向验证 |
| [account-session-restore.md](account-session-restore.md) | 账号记忆、身份复核后恢复、退出排空旧写再删除、显式保存结果 |
| [backend-runtime.md](backend-runtime.md) | 后台线程装配、Core-only异步入口、启动降级、旧事件隔离与关闭排空 |
| [music-queries-and-lyrics.md](music-queries-and-lyrics.md) | 搜索／歌曲详情／原文·翻译·罗马音歌词、逐行／逐字解析与公开小样本实测 |
| [stream-resolution-and-playback.md](stream-resolution-and-playback.md) | 官方XEAPI音源、试听／TTL、受控CDN临时缓冲、无界面Qt播放与静音PoC |
| [来源与许可说明](../../third_party/api-enhanced/NOTICE.md) | MIT 通知及未授权转移的第三方 SDK 边界 |

## 3. 已确认范围

采用组包括登录、基础用户信息／权益、音乐首页、歌曲、歌词、普通歌单、艺人、专辑、搜索、个人库与历史、下载、云盘、评论、收藏／编辑操作和一起听。

**每日推荐歌曲**独立建模：`recommend_songs` 不是 `playlist_detail`，不能虚构一个远程歌单 ID 后去请求普通歌单接口。相关不感兴趣反馈和历史日推也单独登记。

**私人 FM**明确纳入：`personal_fm`、`personal_fm_mode`、`fm_trash`。虽然其官方路径含 `radio`，它不是本轮排除的电台／播客业务。歌曲喜欢接口也可能使用 `/radio/like`，不能靠 URL 单词过滤业务。

明确排除：视频／MV、电台／播客／有声内容、云贝／云豆、动态／话题、云小编及相关任务审核。购买支付、广告、音乐人后台、私信和账号资料编辑等非歌曲主路径也不顺带迁移。

`artist_detail_dynamic`、`playlist_detail_dynamic` 是实体计数／订阅等动态信息，不是用户动态 feed，保留。`album_detail` 实际访问 vipmall 产品详情，不能误认成普通专辑详情；普通专辑使用 `album` 等模块。

## 4. 数据集的使用边界

`inventory.json` 是审计清单，不是运行时可编辑端点注册表，不应由产品直接加载并执行表达式。

- `source`：不可变上游链接和源文件 hash。
- `wrapperRoute`：上游 Node 服务路由，仅用于对照文档，**不是 Cirrus 请求地址**。
- `requestCalls`：AST 找到的公共 `request(uri, data, options)` 调用；其方法在该版本 transport 中统一为 POST。直接 axios／插件链另见协议文档，不能由空数组推断无网络。
- `cryptoHints`：源码的 createOption 参数提示，`default-eapi` 按锁定配置解释；不是完整控制流证明。
- `queryFieldsObserved`：包括直接访问和对象解构；不等于必填清单、完整参数 schema 或服务器字段。
- `payloadObservations`：采用模块中的 data 初始化／赋值片段，用于评审序列化；不能当可执行代码或完整路径分析。
- `imports`：上游依赖观察，不是允许引入 Cirrus 的包。例如音源模块中的解锁依赖必须删除。
- `constraints`：业务类型／参数白名单；采用模块不等于采用所有分支。
- `blockedBy`：需要额外证据或决策的依赖；例如浏览器 SDK token 与一起听接收协议。
- `implementationStatus=not-implemented`、`verification=static-only`：固定静态审计时点的状态，不随原生增量改写成运行注册表；当前实现／测试进度另见 [native-foundation.md](native-foundation.md)。

动态 URL 中的操作名必须由枚举白名单构造；不能把未经校验的 `query.t` 或 `threadId` 原样拼入 C++ 请求。分页默认值、输入互斥、字段类型、授权和响应 schema 仍须逐 operation 在实现前补齐并建立 fixture。

## 5. 不能机械翻译的关键发现

1. 当前 `song_url_v1` 官方分支使用 **XEAPI**，涉及密钥获取、会话和响应解密；只实现传统 weapi/eapi 不足以复刻该版本主路径。
2. `register_checktoken_v2` 会下载并在 jsdom 中执行浏览器 SDK，**不是简单 HTTP 或 AES 端点**；暂列待定，不暗中引入 Node.js、WebEngine 或任意远程脚本执行。
3. QR 检查存在异常分支引用越作用域变量的问题；歌单增删有 512 时重复曲目重发的逻辑；不能原样迁移。
4. 歌曲 URL 模块含第三方音源解锁分支，明确剔除。旧可用性检查会把不同失败都改写成“暂无版权”，也需重写。
5. 一起听有 9 个模块，但上游文档明确“从机模式待整理”；端到端同步仍缺可靠证据。

## 6. 可重复核对

不需要 Node 或 npm，使用 Python 标准库验证清单：

```bash
python3 tools/validate_api_inventory.py
python3 -m unittest discover -s tools/tests -p 'test_*.py'
```

若已有固定版本的只读参考 checkout，可进一步核对全部模块与公共文件的 hash：

```bash
python3 tools/validate_api_inventory.py --upstream build/reference/api-enhanced
```

本次参考 checkout 位于已忽略的 `build/reference/`，不会打入程序或提交为运行依赖。静态提取临时使用了 AST 工具，仅解析源文本，没有 require／执行上游代码。参考项目在正式仓库中仅保留审计数据、许可、文档及 Python 校验工具；产品原生代码在 src/ 中，二者相互独立。

## 7. 下一步

已建立 C++ 协议、上下文请求链、扫码 port 和离线认证事务，见[实现记录](native-foundation.md)。扫码轮询与总时限、[XEAPI 编码与公钥状态](../tasks/B-002-xeapi.md)已实现。设备profile、[匿名启动](anonymous-bootstrap.md)与[账号安全持久化／恢复／退出](account-session-restore.md)已实现；[后台线程和非阻塞启动／关闭装配](backend-runtime.md)也已完成；[搜索／歌曲详情／三语言歌词](music-queries-and-lyrics.md)已接入；[官方URL与单曲Qt播放](stream-resolution-and-playback.md)已接入并有公开试听静音实测，下一步接队列／歌词时钟，再按 [migration-plan.md](migration-plan.md)推进受控实测与业务。现有 Qt Quick 空窗口保持不变；不做控件、首页或播放页开发。写操作、一起听和受限下载必须先补契约证据，不以清单存在代替功能完成。
