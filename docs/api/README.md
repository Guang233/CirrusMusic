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
| [discovery-browse-reads.md](discovery-browse-reads.md) | 新碟／艺人／相似9Weapi，原read16、seed SongId／年月显式，month/week optional分组／相似歌曲100有界未知分页批；106ops/49登记，复杂百科门禁仍G，不自动下游／UI |
| [discovery-public-response-verification.md](discovery-public-response-verification.md) | 11次native公开metadata，301即停；原mapper2差异已合成修正，188／22affected1190＋thread18，5fresh0／14objects6998逐对象依赖禁入0，旧165／1167历史不累加，未修后重发实网 |
| [daily-recommended-playlists.md](daily-recommended-playlists.md) | 独立原账号每日推荐歌单，空Weapi／recommend100／1MiB与纯文本宽ID；同token私有scope与read16，62／affected753＋thread14；83ops／45登记，仅合成、无日期cache／自动下游 |
| [special-contracts.md](special-contracts.md) | 每日推荐、私人 FM、一起听的独立业务契约 |
| [together-http-core.md](together-http-core.md) | 核心12 HTTP-only后续明确选择：标准Eapi-ECB／Status Eapi／窄interface.music host／原PC profile待实证、原账号／shared8／四结果，部分metadata/playlist显式unmapped；97ops/48，新109/affected911＋runtime17，非NIM/同步/实网/播放器queue-report-timer |
| [together-protocol-material-audit.md](together-protocol-material-audit.md) | 用户Android摘要＋固定9 HTTP静态对照；NIM/chatRoomId/type/毫秒/seq线索、crypto/profile/字段/schema冲突与G1–G6，f384b7d历史85ops/47、WIP不恢复，无该审计轮代码验证；后续HTTP-only另页，仍无NIM/SDK/实网 |
| [daily-recommendation-history.md](daily-recommendation-history.md) | 原account／MUSIC_U/read16，2Weapi日期与原source token成员private选择，候选dates/songs／366/500/1/4MiB，86／839＋thread15，85ops／47登记；无会员/window/schema实证／cache／自动下游／反馈写，前三和第四后续暂停 |
| [discovery-and-fm.md](discovery-and-fm.md) | 已实现的日推读取、FM动态条目供给、账号隔离；基础读取包证据，不等于听歌上报 |
| [collection-writes.md](collection-writes.md) | 红心／收藏到歌单及移除／专辑／艺人／播客五类10动作、8operation，原账号／8槽／Unknown、不重试，仅合成；整歌单收藏需求保留、维护者暂无v2资料明确暂缓 |
| [playlist-subscription-v2-prerequisite.md](playlist-subscription-v2-prerequisite.md) | 整歌单收藏v2前置静态调查：body/header来源分离与Cookie携带、SDK／原生／许可门禁和待实现TC，无新增API／测试或实网 |
| [playlist-management-writes.md](playlist-management-writes.md) | 创建／单独改名／描述3operation，固定NORMAL、创建ID候选与Unknown；共享原账号／8槽，仅合成、不重试／自动对账 |
| [playlist-delete-and-order-writes.md](playlist-delete-and-order-writes.md) | 单目标删除／歌曲顺序／账号歌单顺序3operation，完整范围置换工厂／原来源token与Unknown，仅合成 |
| [playlist-cover-update.md](playlist-cover-update.md) | JPEG字节有界／新像素重编码与元数据去除，Gui仅基础设施；2Weapi＋固定NOS rawPOST、阶段Unknown／原8槽，仅合成，不读路径／真实上传 |
| [playlist-tags-and-publication-writes.md](playlist-tags-and-publication-writes.md) | 标签Catalogue成员工厂／原token／预算及单向公开固定numeric0，2operation／原8槽／Unknown，仅合成，无清空／反向转换／自动读 |
| [catalog-introduction-and-metadata-reads.md](catalog-introduction-and-metadata-reads.md) | 四类详情optional资料扩充、独立艺人分节／歌曲百科基础2operation，原16槽／纯文本／token，候选schema、无自动下游 |
| [account-library-reads.md](account-library-reads.md) | 当前账号播客订阅／云盘列表与详情三项；独立CloudSongId、MUSIC_U与原token、bounded候选schema，仅合成，无云盘音源／播放／写入 |
| [personal-library.md](personal-library.md) | 当前账号歌单、喜欢ID、收藏专辑及艺人；有界只读与账号校验，无库缓存／写入 |
| [local-listening-history.md](local-listening-history.md) | 媒体线程的有界观测累计、Queue／FM会话历史、清空与账号隔离；无持久化／上报 |
| [explicit-listening-reports.md](explicit-listening-reports.md) | StartPlay／Play显式报告、clientlog/osx profile、未知写入结果；无自动触发／重试 |
| [cloud-history-reads.md](cloud-history-reads.md) | 当前账号最近听过、一周／全部时间听歌排行；原顺序、有界只读，不推算时长；仅合成响应验证 |
| [chart-reads.md](chart-reads.md) | 榜单列表／内容摘要；展示预览不是可播放歌曲，复用歌单详情／分页与显式选歌播放，响应仅合成验证 |
| [playlist-discovery-reads.md](playlist-discovery-reads.md) | 推荐、普通／精品歌单及分类／热门／精品标签6项；有界只读、不同分页／来源，原歌单／显式播放复用，仅合成验证 |
| [search-assistance-reads.md](search-assistance-reads.md) | web实体建议／简略及详细热搜3operation，独立四组预览ID／raw文本分值／strict order-20/16/100及未知；原16槽/token，纯文本，117＋affected631与线程12，仅合成，无mobile-PC降级／自动search/detail/image/cache/history/UI |
| [entity-search-reads.md](entity-search-reads.md) | 已实现专辑／艺人／歌单、播客与全站声音搜索5项；独立ID、限定e_r门禁、无Cookie公开结构证据，消费端新增范围，不含声音播放／工作台 |
| [podcast-and-recent-reads.md](podcast-and-recent-reads.md) | 消费端详情／节目3项与原账号recent4项，独立ProgramId／VoiceId；公开列表-462已停，其余候选schema仅合成，声音播放另包 |
| [comment-writes.md](comment-writes.md) | 评论5action／operation，4variant／原account-token-generation／独立8槽及business NotSent-Unknown；明确批准固定原生v3 GET→2XEAPI动作，无SDK／JS／外部Core token／创建ID猜测／自动读／重试；仅合成760＋11／新134，无实网／UI |
| [comment-reads.md](comment-reads.md) | 资源评论五类／八operation；四类普通及五类通用资源、严格分页／楼层／统计missing，候选schema仅合成；无Voice／艺人绑定、写入／账号历史／通知 |
| [spoken-media-seam.md](spoken-media-seam.md) | 离线阶段1：逻辑kind／AudioTrackId结构、无身份物理MediaSource／原唯一owner与媒体限额，28／affected373＋thread13、0fail／原audio1skip；83ops／46登记，G1/G2未闭、无实际spoken入口 |
| [spoken-playback-contract.md](spoken-playback-contract.md) | 身份／官方音源补证与独立播放设计：mainTrackId文档路线、v1代码候选、工作台不可替代、6项实证门禁；docs-only，无声音播放实现 |
| [fm-continuous-playback.md](fm-continuous-playback.md) | ListeningSession的FM连续播放、模式切换、候选保留与有界失败 |
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

**私人 FM**明确纳入：`personal_fm`、`personal_fm_mode`、`fm_trash`。虽然其官方路径含 `radio`，它不是固定审计当时排除的电台／播客业务。歌曲喜欢接口也可能使用 `/radio/like`，不能靠 URL 单词过滤业务。

固定审计当时排除视频／MV、电台／播客／有声内容、云贝／云豆、动态／话题、云小编及相关任务审核。维护者随后明确新增播客／声音消费端范围，并交付[5项实体／播客／声音搜索](entity-search-reads.md)；inventory／selected-endpoints仍是原440模块、164／40／236决定的历史静态审计，不伪称已随产品增量重新分类。当前仍排除视频、云贝／云豆、动态、云小编与创作者上传／审核工作台；购买支付、广告、音乐人后台、私信和账号资料编辑等非消费主路径也不顺带迁移。

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
5. 一起听有9个采用候选模块，上游“从机模式待整理”；后续[Android摘要](together-protocol-material-audit.md)已提供NIM等线索，但鉴权/envelope/SELFHOST、seq/version/时钟恢复权限与Linux许可未闭，HTTP层有profile/字段/schema差异，不自动恢复草稿或认定完整同步。

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

最新[公开发现窗口与映射修正](discovery-public-response-verification.md)11次原native metadata、业务301即停；月榜optional分组与相似歌100有界未知分页批修正后188／22affected1190＋runtime18，0fail/skip（旧165／1167为331b4fb历史）。106ops／49登记不增，无SDK/new deps/自动下游/UI或修后重发实网。复杂百科见data.blocks6/rnData部分shape，资源绑定／块类／余块语义仍G、不注册假入口；其他暂停不解除。账号等级＋音乐收藏计数仍候选未实现，FM mode/trash需原来源/真实time，next不等于垃圾桶。

[106operation／44FR ledger](backend-coverage-audit.md)已跟随增量更新，25families/38E63W5X/I-S-L-G-P-X分开；原53f8f3e的docs-only时点是97／48，不把本包fresh tests倒算到旧审计。固定164adopt与operation非同单位，不作差值；完整Together/v2/cloud/spoken仍暂停、APK禁读/UI最后。

此前[一起听核心12 HTTP-only](together-http-core.md)已按后续选择合成接入，97ops／48登记，109新／15affected911＋runtime17；仅HTTP暂停被覆盖，不接NIM/SDK/new deps/live/timer/player/queue/report，APK撤权且从未检查，完整同步及v2/云盘/spoken下一阶段继续暂停，UI最后。schema/profile/optional/playlist不作实网或全协议承诺，stash不恢复。

此前[历史日推两read](daily-recommendation-history.md)经维护者选择合成接入，85ops／47登记、新86／17 affected839＋thread15，0fail/skip，16fresh／14objects6985实编dep禁入0；日期必须原account/source token的日期DTO成员，候选schema和会员/window待证，无cache／自动下游。前三项与第四项G1/G2下一阶段均暂停，阶段1保留，UI最后；其他小包需另审计确认，不自动重开账号／−462／SDK／音频。

已建立 C++ 协议、上下文请求链、扫码 port 和离线认证事务，见[实现记录](native-foundation.md)。扫码轮询与总时限、[XEAPI 编码与公钥状态](../tasks/B-002-xeapi.md)已实现。设备profile、[匿名启动](anonymous-bootstrap.md)与[账号安全持久化／恢复／退出](account-session-restore.md)已实现；[后台线程和非阻塞启动／关闭装配](backend-runtime.md)也已完成；[搜索／歌曲详情／三语言歌词](music-queries-and-lyrics.md)已接入；[官方URL与单曲Qt播放](stream-resolution-and-playback.md)已接入并有公开试听静音实测，[队列／六轨歌词时钟](queue-and-lyric-clock.md)也已完成后端最小闭环，[专辑／艺人／歌单只读目录](catalog-queries.md)也已接入并离线验证，[每日推荐读取／私人FM条目供给](discovery-and-fm.md)也已接入；[FM连续播放接线](fm-continuous-playback.md)已离线接通，[个人库四项读取](personal-library.md)也已接入，[本地可观测累计／会话历史](local-listening-history.md)已另包接入，[显式上报能力](explicit-listening-reports.md)也已接入；[最近记录／排行读取](cloud-history-reads.md)也已另包离线接入，[榜单列表／内容摘要及歌单选歌播放链](chart-reads.md)也已离线接通，[六项歌单发现读取](playlist-discovery-reads.md)已成组离线接通；自动时机留待播放接线，详细本地历史分析按需求再定；再按 [migration-plan.md](migration-plan.md)推进受控实测与业务。现有 Qt Quick 空窗口保持不变；不做控件、首页或播放页开发。写操作、一起听和受限下载必须先补契约证据，不以清单存在代替功能完成。
