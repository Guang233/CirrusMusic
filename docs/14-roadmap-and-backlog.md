# 14 · 实施路线与任务拆解

最新B-030：[歌曲百科受限纯文本](api/song-wiki-text-subset.md)已按维护者明确选择接入，仅两类文本、原请求关联、内容身份未独立验证；完整百科仍G。107ops/50登记，88／23affected1278＋runtime19范围内通过。维护者后续明确两个账号API先不做，先设计[播放状态／六轨歌词本机IPC＋三平台系统媒体](tasks/B-031-playback-observation.md)，原设计后按继续选择接O1 Core投影/接缝/窄桥的限定I/S（证据见B-031§9.1），登记51但非全51验收；IPC/三平台native仍G、无新平台依赖；Core投影→只读IPC→Linux MPRIS→Windows SMTC/macOS Now Playing各平台验收，再单独推进持久化／恢复。不自动再访问服务、恢复paused或先做UI。

> 最新顺序：**后端先行，UI 最后**。已完成 Qt 骨架、VS Code 基础配置和上游静态 API 审计；协议／认证／后台装配与首批音乐查询已实现；[搜索／详情／歌词](api/music-queries-and-lyrics.md)有少量官方公开实测。最新定向测试见HANDOFF，尚无真实账号／完整播放验收。接口清单工具测试不等于产品测试。

## 1. 阶段总览

本版替代早期“先 Gallery、再页面垂直切片”的安排。M0～M4 以 C++ 单测、fake transport、CLI／测试 harness 验收；不开发自绘控件和页面。API 清单的 phase 也是这里的后端阶段，不代表完整产品已发布。

| 阶段 | 交付物 | 退出门禁 |
| --- | --- | --- |
| M0 范围与协议调查 | 固定上游、采用／待定／排除清单、许可、协议依赖和缺口 | 静态审计、协议向量及单个试听播放PoC已完成；广泛真实契约／设备／打包仍有门禁 |
| M1 原生基础与会话 | 分层 targets、Qt Network、加密／解码、Cookie、设备与匿名状态、登录、错误模型、存储基础 | 离线测试通过；授权环境完成登录／恢复／退出及账号隔离验证 |
| M2 查询与播放后端 | 搜索、歌曲／歌词、专辑／艺人／歌单、推荐／每日歌曲／FM、个人库／评论读取；播放器和队列 | 查询契约、真实音源与 seek、歌词、连续播放、取消与晚到响应测试通过 |
| M3 写操作与一起听 | 喜欢／收藏、歌单编辑、评论写、真实播放上报；一起听原型与双端协议验证 | 写结果未知／部分成功可对账；v2 token 与一起听接收链有证据，否则登记受限能力并由维护者决定 |
| M4 文件与后端稳定性 | 下载、离线、本地文件、云盘上传／管理、恢复、非视觉 Linux 服务、打包 PoC | 后端合同冻结；授权／上传／崩溃恢复及长期播放通过，剩余缺口明确获批 |
| M5 UI 与完整产品验收 | Tokens／Controls／Gallery、ViewModels、页面、沉浸播放、托盘／桌面入口、无障碍与正式包 | 全部 P0、目标 P1、真实桌面／性能／许可／安全和端到端验收通过 |
| M6 后续增强 | 桌面歌词、导入导出、高级音频等独立扩展 | 每项专项设计与 capability 验收；不重新带回已排除业务 |

旧版人周估算不再沿用：新增一起听、XEAPI 和浏览器 SDK 依赖使原估算依据失效。协议 PoC 后再估工期，不给没有证据的交付日期。

需求中的后端阶段表示技术能力目标；涉及可视展示、交互、键盘和无障碍的完整用户验收统一在 M5。不能把后端测试通过宣称为整个 FR 完成。

## 2. 关键依赖链

```mermaid
flowchart LR
    Audit[固定源码与范围审计] --> Protocol[原生传输与协议]
    Protocol --> Session[登录与会话]
    Session --> Catalog[歌曲目录与推荐/FM]
    Protocol --> Audio[真实音源与播放后端]
    Catalog --> Writes[音乐库/歌单/评论写]
    Audio --> Together[一起听与双端同步]
    Writes --> Files[下载/云盘/本地与后端稳定性]
    Together --> Gate[后端验收与缺口决策]
    Files --> Gate
    Gate --> UI[最后实现UI与整体验收]
```

受阻能力有明确门禁；可直接推进不依赖它的后端工作。图不授权自动委派，不要求为了等待 token 或一起听协议而先写 UI。

## 3. 可转 issue 的工作包

保留原 B 编号以便追踪，B-025 明确撤销而不复用。

| ID | 工作包及阶段 | 证据／依赖 |
| --- | --- | --- |
| B-001 | 固定上游和范围；逐 operation 契约、DTO 与 fixture（M0～M4） | 静态部分见 api/；首批3项公开查询有小样本实测，其他契约仍需逐项验证 |
| B-002 | weapi/eapi/XEAPI 编码、密钥状态与测试向量（M1） | weapi/eapi 与 [XEAPI](tasks/B-002-xeapi.md) 编码、公钥注册和会话密钥已有离线向量；公开XEAPI公钥获取／音源及weapi/eapi有小样本实测，真实账号未验证 |
| B-003 | Qt 官方流 PoC 与 IPlaybackBackend（M2） | [官方试听MP3、TLS／Range、静音播放／seek已验证](api/stream-resolution-and-playback.md)；当前完整临时缓冲，非流式起播；设备／打包仍待验证 |
| B-004 | Qt/编译器、app ID、发行 Runtime 和许可（M1/M4） | app ID 和自身许可仍待确认；不阻塞协议单测 |
| B-005 | 分层 CMake、CI、测试入口、日志（M1） | presets 与协议基础 CTest 已提供；完整分层／业务 CI 尚未实现 |
| B-006 | Domain IDs、Track/Page/Queue/Error 与 fake ports（M1/M2） | 大 ID、空字段、队列不变量 |
| B-007 | Tokens、Controls、Gallery、焦点与主题（M5） | 后端门禁后才开始 |
| B-008 | SQLite worker、profile、migration、设置后端（M1/M4） | [DatabaseWorker、迁移校验、升级备份与设备 ID](tasks/B-008-storage-foundation.md) 已实现；音乐库 schema、设置后端未做 |
| B-009 | 网络调度、主机策略、取消、重试和脱敏（M1） | 独立于 UI，含 mock 服务／故障注入 |
| B-010 | SessionCoordinator、凭证保管、扫码／恢复／退出（M1） | 离线认证事务、[轮询与总时限](tasks/B-010-qr-polling.md)及[密钥环接缝](tasks/B-010-secret-store.md)及[匿名启动](api/anonymous-bootstrap.md)已实现；[账号记忆／恢复／退出](api/account-session-restore.md)也已实现；[后台线程和非视觉启动／关闭装配](api/backend-runtime.md)已完成；尚未接入模板入口／UI，未真实验收 |
| B-011 | 搜索与目录 repositories/use cases（M2）；ViewModels（M5） | [歌曲搜索／详情](api/music-queries-and-lyrics.md)及[专辑／艺人／歌单只读目录](api/catalog-queries.md)已接入；[当前账号个人库四项读取](api/personal-library.md)也已离线接入；[四类资料／独立介绍](api/catalog-introduction-and-metadata-reads.md)也已连续合成补齐，复用原描述／optional扩充，原16槽与纯文本；[搜索建议／简略及详细热搜](api/search-assistance-reads.md)3项辅助读取已合成接入，typed四型预览／strict schema／原token与16槽，117＋受影响631及线程12，无自动search/detail/image/play/cache/history；广泛实测、复杂百科／UI及ViewModels未做 |
| B-012 | PlaybackCoordinator、后端、队列与 generation（M2） | [单曲协调器／旧事件隔离](api/stream-resolution-and-playback.md)已接入；[队列与有限自动续播](api/queue-and-lyric-clock.md)已实现，后续[default-off持久化／纯状态恢复](tasks/listening-state-persistence.md)已写，不load/play/lyrics／不恢复进度或FM，队列包55458e5；长期稳定性／实际存储与设备／打包门禁待验证 |
| B-013 | 歌词获取／解析／同步（M2） | [逐行／逐字、原文／翻译／罗马音独立解析](api/music-queries-and-lyrics.md)已实现；[媒体时钟／seek／试听偏移定位](api/queue-and-lyric-clock.md)已实现，声学延迟与UI动画未验证 |
| B-014 | AppShell、路由、列表、迷你栏与沉浸页（M5） | 复用稳定后端；性能、键盘、无障碍 |
| B-015 | 喜欢、收藏、歌单编辑和写对账（M3） | [五类收藏订阅写](api/collection-writes.md)已合成接入：红心／收藏到歌单及移除／专辑／艺人／播客，8槽与Unknown、不自动重试／对账；整歌单收藏／取消此前因v2 SDK延期，现维护者要求补齐，[原生v2前置调查](api/playlist-subscription-v2-prerequisite.md)已核对固定SDK／body-header-Cookie与TS分歧，原生issuer／许可／绑定未解，维护者暂无资料、明确暂缓，实际材料与明确恢复后再审，不绕过／用v3替代；[创建／改名／描述](api/playlist-management-writes.md)已合成接入，共享8槽／创建ID候选及Unknown，不猜同名唯一；[删除／完整范围排序](api/playlist-delete-and-order-writes.md)已合成接入，原token与完整置换工厂；[标签／隐私歌单单向公开](api/playlist-tags-and-publication-writes.md)已合成接入，Catalogue工厂／原token／8槽／Unknown；[封面更新](api/playlist-cover-update.md)已合成接入，有界JPEG／获准Gui仅基础设施、3阶段／原账号／Unknown；标签清空／公开转私密／格式扩展／对账／同步及实网仍未交付 |
| B-016 | 推荐、独立每日歌曲、榜单、FM、历史（M2/M3） | [日推读取与FM有界供给](api/discovery-and-fm.md)已接入；[FM连续播放／模式切换／失败预算](api/fm-continuous-playback.md)已离线接通，服务端跨日语义／缓存仍未交付；[推荐／普通／精品歌单与三类标签](api/playlist-discovery-reads.md)6项已成组离线接入，原歌单／显式播放复用；[本地观测累计／256条会话历史](api/local-listening-history.md)已接入，[显式报告能力](api/explicit-listening-reports.md)已接入；自动时机／来源扩展未接入；后续[长期本地明细](tasks/listening-state-persistence.md)已提供default-off存储，独立UUID／UTC／原实际playedMs、累计upsert与keyset分页，256仅内存窗口不删数据库，无token／自动report；无新增或执行测试，SQL实际运行／durability与统计聚合／dashboard未验或未做，禁止用进度条位置或墙钟差代替已听时长；真实上报另授权 |
| B-017 | Linux MPRIS／Windows SMTC／macOS Now Playing非视觉服务（本轮提前设计）；托盘／通知仍M5 | 共用B-031 Core投影与唯一命令桥，三平台独立真实build/runtime/权限门禁；只是设计、未加DBus/native依赖，不耦合页面 |
| B-018 | 下载、Range/hash、离线、空间管理（M4） | 不新增专用下载音源API，未来以播放接口实际可听源为下载条件，不额外查询下载资格／标志，不接加密下载文件支路；音源自身试听范围／TTL／实际访问限制保留，文件／恢复另包，下载功能未实现 |
| B-019 | 文件访问、扫描、标签与去重（M4），文件选择交互（M5） | 坏文件、符号链接、授权撤销 |
| B-020 | 云盘浏览／播放、评论读取／点赞（M2～M4） | [资源评论只读](api/comment-reads.md)五类／八operation已合成接入；严格资源／安全文本；[评论点赞／取消与其余写](api/comment-writes.md)已明确批准原生v3并合成接入，独立8槽／原账号／Unknown，无实网／UI／账号历史／通知；[播客订阅／云盘列表及详情](api/account-library-reads.md)3项原账号只读已合成接入，云盘后续API／音源／播放／写暂缓，保留已交付读取 |
| B-021 | 设置后端／恢复（M4）；设置页与国际化（M5） | 断网、设备、休眠、缩放 |
| B-022 | Flatpak/AppImage 验证（M4），正式发布（M5） | 最小权限、SBOM、安装升级 |
| B-023 | 后端总回归（M4）；完整产品回归（M5） | 二者单独报告，不以无 UI 测试替代用户验收 |
| B-024 | 评论发布／回复／删除（M3）、云盘上传管理（M4） | [评论五动作](api/comment-writes.md)已封闭原生v3 GET／XEAPI合成接入，无SDK／JS／新库；原账号／business NotSent-Unknown、无创建ID推断或自动重试／refresh。仅合成760＋11，真实issuer／审核／本人权限／删除效果／UI未验收，云盘上传等后续接口当前明确暂缓，未交付 |
| B-025 | **撤销：MV 与视频播放焦点** | 用户明确排除，不进入未来队列 |
| B-026 | 桌面歌词、导入导出、高级音频（M6） | 每项专项 ADR 和测试 |
| B-027 | 一起听核心HTTP12合成已接；完整同步仍待证（M3） | 后续明确选择[12 HTTP-only](api/together-http-core.md)：全Eapi-ECB（Status）、窄host/原PC profile/shared8/四结果/部分metadata、playlist显式unmapped。97ops/48登记，新109/15affected911＋runtime17，0fail/skip；不接NIM/SDK/新库/实网/播放器queue-report-timer。仅HTTP暂停被覆盖，固定旧WIP不恢复、APK撤权且未检查；[Android审计G1–G6](api/together-protocol-material-audit.md)仍约束完整实时同步、双账号另授权，其他paused/UI最后不变 |
| B-028 | 上游清单校验与漂移检查（M0 起持续） | Python 校验器；升级上游需重新审计 |
| B-029 | 常规发现9typed读取合成已接／复杂百科门禁（M2–M3） | 维护者明确先补发现：[新碟/艺人/相似](api/discovery-browse-reads.md)9Weapi/read16，原query-kind/token-generation/取消关闭，无自动下游；106ops/49登记不变；[后续公开窗口](api/discovery-public-response-verification.md)11次native metadata／301即停，月榜optional分组与相似歌曲100有界未知分页批修正，188/22affected1190＋thread18、5fresh0/14objects6998逐对象依赖禁入0/UI0；旧165/1167历史不累加。complexWiki观察data.blocks6/rnData部分结构，Song绑定／块类缺证Q-016，未注册，R-033／TC-DISCOVERY-BROWSE；广泛服务/权益/UI另验收，原paused/APK禁读/stash不动 |
| B-030 | 歌曲百科受限纯文本子集合成已接（M2） | [固定Eapi/两子块](api/song-wiki-text-subset.md)，原Song/标题/专辑/token工厂、read16/Facade关联与NotIndependentlyVerified；88/1278＋19、无自动下游；独立主SongId身份/剩余块/完整schema仍G，TC-SONG-WIKI-TEXT/R-034/Q-016，四次metadata观察不扩范围 |
| B-031 | 当前播放状态／六轨歌词本机只读接口（M2–M4，O3b2 Linux参数化组合限定I/S；发行与O4–O6 G） | [契约与证据§9.1](tasks/B-031-playback-observation.md)：Core coherent/绑定/同token隐私/六轨独立游标/窄桥已接；O2a严格codec/framer/完整golden/六轨分页已限定合成；125新/5selected216＋runtime6，当前52登记非全52；后续O2b fake连接/mailbox/订阅/增量编码/退役/背压限定I/S：43新/6selected259＋runtime6、O2b时53登记非全53；后续O3a Linux私有资源/kernel UID隔离harness32新/3selected200，O3a时54登记，后续O3b1 §9.5 owner→mailbox同步private retire/live原来源factory/单票wake组合限定I/S，20新/8selected311＋runtime6、0fail/skip；后续O3b2 §9.6已接参数化Linux真实I/O/严格发现/双向UID/hello/显式XDG，新50/Debug361＋runtime6/Release307、0fail/skip；当前Linux58登记非全58。input/retained＋native pending/八慢client/wake/deadline/thread关闭已限定，O3整体发行身份/另一OS用户负测/沙箱/minQt/部署性能与三平台adapter仍G，不拿MPRIS替代歌词API或新建播放器 |

## 4. 下一轮实际动手顺序

**当前明确选择**：账号等级与收藏计数两API先不做。原B-031设计后已按继续选择接O1 Core投影/必要观测接缝与限定合成验收，后续O2 codec/fake逻辑连接限定I/S已接，当前下一O3 Linux安全只读IPC→O4 MPRIS→O5/O6 Windows/macOS各自真实工具链与系统媒体/权限验证。设计授权不是产品实现、native依赖安装、实网或跨平台验收授权；持久化/恢复留随后独立包，UI最后。

**上包代码／维护者优先级记录**：后续明确先补发现并选择[新碟/艺人/相似9读取＋复杂百科审计](api/discovery-browse-reads.md)，已typed合成接入。106ops/49登记、新165／22affected1167＋selected runtime18，0fail/skip，15fresh0／14objects6998actualdeps禁入0／UI0；复杂百科响应结构待证不注册，原9读取strict候选非真实schema/免登录/权益保证。只本地commit/no push，无SDK/新运行依赖/实网/自动下游。不是全发现产品或44FR验收，TC-DISCOVERY-BROWSE／R-033／Q-016另记；其他paused/APK禁读不动。

[107operation／44FR覆盖账本](api/backend-coverage-audit.md)同步26families/39E63W5X、I/S/L/G/P/X；53f8f3e初始docs-only97/48仍历史，不把当前Qt证据倒填。账号等级user_level＋收藏计数user_subcount两empty Weapi读仍候选、未实现/未授权实网，需要strict synthetic schema与原account/token/read16；FM mode/feedback需真实来源与观察time，不复制25/next。该候选建议已被后续维护者选择覆盖：两个账号API先不做，先B-031设计；其他paused与UI最后不变。

**此前代码包覆盖**：[核心12 Together HTTP-only](api/together-http-core.md)已明确批准并合成接入；97ops／48登记，旧911＋runtime17/新109只属该包。NIM/SELFHOST完整同步G1–G6、v2/cloud/spoken next仍暂停，optional/完整playlist/profile另补证，APK撤权且从未检查、旧stash不自动恢复。

**此前历史日推覆盖**：第四项阶段1之后绑定／音源G1/G2仍缺证，维护者明确选择先做[历史日推两项读取](api/daily-recommendation-history.md)，第四项下一阶段暂缓，前三项不恢复。现日期列表与原列表成员／原account-token选择歌曲已合成接入，85ops/47登记、新86/17 affected839＋thread15，0fail/skip；16fresh路径clean/14objects6985deps禁入0/UI0。不推会员／window／日期完整性，candidate日期366/1MiB与songs500/4MiB，无自动下游/日期cache/反馈写；真实账号/源/SDK/音频无新授权。后续其他已纳入小包另审计确认、UI最后；本次不给暂停项自动材料等待或探测。

**此前第四项阶段1覆盖**：维护者明确前三项整歌单收藏v2／一起听／云盘后续继续暂缓，从第四项播客／声音播放开始，选择先离线接缝。[阶段1](api/spoken-media-seam.md)已新增tagged PlaybackEntity／独立AudioTrackId与identity-free MediaSource，原唯一owner先验身份／token／TTL；83ops不增／46登记，新28／15 affected373＋thread13，0fail／原audio1默认skip。下一步仍是可信消费绑定与节目v1音源／TTL／preview的G1/G2补证，再接逻辑快照／单owner spoken模式；当前不开放未证入口、偷cast ID／工作台／旧url，不自动实网或因缺证转去前三项，UI最后。

**此前优先级记录**（接续acf822e）：一起听暂停至最后，等待维护者逆向整套材料；9 HTTP草稿撤出主线且单独本地保留。整歌单收藏／取消因维护者暂无v2资料明确暂停，需求保留但不自动恢复；推进其他后端，本包[独立每日推荐歌单](api/daily-recommended-playlists.md)已合成接入，新62／affected753＋thread14，83operation／45注册，原账号／read16／同token账号-认证态屏障、无自动下游。[固定Watchman v2原生前置](api/playlist-subscription-v2-prerequisite.md)静态调查已记录：SDK桥不是原生协议、body/header token不同来源、Eapi Cookie／header携带；前置资料或架构决定仍缺，未解不注册／假完成。专用下载音源API不再加入补接口清单，未来下载以实际可听播放音源为准、不额外查询下载资格，音源自身试听／TTL／访问限制保留、文件后端另做；云盘新接口／播放／写暂缓，已有read保留。声音身份／音源链只作解释，未在这次范围调整中获准开始实现／实网；各包继续离线／UI最后。不是9 HTTP发出就完整一起听，不会自动恢复该草稿；不是明确要求收藏就授权SDK／Node／新运行库。

新接手者先完成 [HANDOFF](HANDOFF.md) 的环境／测试步骤，[扫码轮询与总时限](tasks/B-010-qr-polling.md)及 [XEAPI](tasks/B-002-xeapi.md) 已完成，[存储基础与设备 ID](tasks/B-008-storage-foundation.md)、[密钥环](tasks/B-010-secret-store.md)也已完成，[匿名 bootstrap](api/anonymous-bootstrap.md)现有定向离线回归，[账号记忆／恢复／退出](api/account-session-restore.md)也已完成后端闭环；[后台线程、非阻塞facade与启动／关闭排空](api/backend-runtime.md)也已完成；[搜索／歌曲详情／六轨歌词](api/music-queries-and-lyrics.md)也已接入；[官方URL／Qt单曲播放与静音PoC](api/stream-resolution-and-playback.md)已完成首个闭环；[队列与六轨歌词时钟](api/queue-and-lyric-clock.md)已接入；[只读目录](api/catalog-queries.md)也已接入；[每日推荐读取／FM条目供给](api/discovery-and-fm.md)已接入；[FM连续播放](api/fm-continuous-playback.md)已接通；[个人库四项读取](api/personal-library.md)也已接入；[本地观测累计／会话历史](api/local-listening-history.md)已接入；[显式听歌上报能力](api/explicit-listening-reports.md)已接入，自动时机留待播放接线；[最近听过／排行读取](api/cloud-history-reads.md)也已离线接通，[榜单列表／内容摘要及歌单选歌播放链](api/chart-reads.md)也已离线接通，[歌单发现6项读取](api/playlist-discovery-reads.md)也已成组离线接通，详细本地历史仅按分析需求再定。以下是整体后端路线，不要求在一次提交中完成。

1. 保留本轮审计成果，先处理[发现与门禁](api/gaps-and-hazards.md)，不宣称 164 个模块已经可用。
2. C++ 协议、异步传输、JSON／gzip、会话与 Cookie、捕获 token/origin 的请求链已建立；QR 取 key／本地 URL／状态映射有离线测试，见[扫码链路](api/qr-auth-request-chain.md)。
3. [认证用例、账号身份确认和精确 token 凭据交接](api/authentication-transaction.md)已有离线验证；同线程 Committing→Authenticated 发布屏障已建立，803 不直接提升为 Authenticated。[后台线程与Core-only非阻塞Facade](api/backend-runtime.md)已装配，未接模板UI。
4. XEAPI 编码与密钥流程已有离线向量；设备 ID 已入 `state.sqlite`，XEAPI 公钥已入密钥环；匿名 token 启动／恢复／重置已实现，[账号凭据恢复／退出清理](api/account-session-restore.md)已有离线回归，后台启动／关闭也已装配，业务调度随对应查询需求实现，不先造通用框架。真实验证需要授权环境，不把凭据放进对话或仓库。
5. 搜索、歌曲详情、三语言歌词、官方音源与单曲Qt播放已接入；队列／歌词时钟与有限自动跳过已接入；URL刷新／流式起播等恢复另立小包，元数据成功不代表播放权限，试听不代表整曲授权。
6. 专辑／艺人／歌单只读详情及分页、每日推荐读取／FM动态供给已接入；FM播放接线已完成；个人库四项与云端最近听过／一周和全部时间排行读取已完成离线闭环；榜单列表／内容摘要及复用歌单选歌播放链也已离线接通；推荐／普通／精品歌单及分类／热门／精品标签6项也已成组离线接入；专辑／艺人／歌单、播客与全站声音5项搜索已接入并有有限公开结构证据；[消费3项／recent4项](api/podcast-and-recent-reads.md)已合成接入，公开列表-462停止，ProgramId独立、Voice对应／节目详情／真实recent未实证；[身份／音源补证决策](api/spoken-playback-contract.md)已记录mainTrackId底层音轨路线、v1代码候选与独立媒体／历史接缝；真实binding／v1权益仍未验证，不能先开放声音播放；[资源评论五类／八operation](api/comment-reads.md)已合成接入，完整只读包不含写／账号历史／通知／Voice或艺人绑定；[原账号播客订阅／云盘列表与详情](api/account-library-reads.md)三项已合成接入，CloudSongId与catalog引用分开，无音源／播放／写；受限时继续其他已确认小包，使用CLI／测试harness贯通；本地观测累计已有独立会话包，显式报告已有独立能力包，自动时机／来源扩展仍单列，详细本地历史持久化按分析需求再定。
7. 当前三项订阅／云盘读取与[五类收藏订阅写](api/collection-writes.md)已成组离线接入；写入保留Unknown、不乐观覆盖／自动重试／刷新；整歌单收藏／取消此前因固定checkToken-v2 SDK延期，重新排入后的调查保留，维护者现明确暂无资料／暂停，未闭门禁不实现或自动恢复。[自建歌单创建／单独改名／描述](api/playlist-management-writes.md)已离线接入，候选创建ID与Unknown不混为没创建，不按同名重放／对账；[删除／完整范围排序](api/playlist-delete-and-order-writes.md)也已合成接入；连续第二包[歌曲／歌单／艺人／专辑资料与介绍](api/catalog-introduction-and-metadata-reads.md)已合成补齐，复用已有描述，独立艺人长介绍与歌曲百科基础；复杂百科／署名／真实schema／UI不伪完成；[标签编辑／隐私歌单单向公开](api/playlist-tags-and-publication-writes.md)已获本包确认并合成交付，介绍修改／清空沿用旧写链；Catalogue成员与原token／预算，不自动读／重放Unknown，无标签清空或公开转私密；[封面更新](api/playlist-cover-update.md)也已确认且合成接入，有界内存JPEG／去元数据／分配-NOS-提交与Unknown，批准Gui仅基础设施，没有真实文件／上传；格式扩展／裁剪、可靠核验及其他可见性方向仍另确认；不自动访问真实账号／发送写入；[评论五动作写](api/comment-writes.md)已获准原生v3且合成接入，账号历史／通知未交付。[搜索辅助](api/search-assistance-reads.md)也已合成接入；Together核心12 HTTP-only已按后续选择接入但完整同步仍暂停，整歌单收藏v2也明确暂缓，每日推荐歌单与history已离线补齐，后续继续其他已纳入且边界闭合的后端小包，下载音源复用播放API／云盘暂缓按本节最新覆盖，不自动扩张文件后端；遇到需要新运行依赖或协议缺失时停下询问。
8. 后端完成或受限项明确获批后，再启动全部 UI 工作。

详细门禁见 [API 移植计划](api/migration-plan.md)。

## 5. 准备好开发与真正完成

**Ready**：范围和需求 ID 清楚；输入输出、错误／取消／权限已登记；fixture 或明确的调查任务具备；依赖与数据影响可解释。

**后端任务 Done**：代码、单测、失败／取消、线程／生命周期、日志脱敏和契约文档完成；依赖真实服务的能力有受控实测证据。

**产品需求 Done**：后端门禁之外，还必须完成 M5 UI、无障碍和端到端验收。空窗口、接口清单、mock 返回或一次成功请求都不是完整功能交付。

## 6. 范围控制与评审

- 不能安全处理真实媒体来源时，先调整播放器 ADR，不堆页面或任意本地代理。
- 未确认写重试语义时，暂不开启该 operation。
- 不引入上游 Node 服务、用户 API 配置或非官方解锁；新增浏览器验证桥必须另行确认。
- 不偷偷恢复视频、云贝、动态、云小编与创作者工作台。维护者明确新增播客／声音消费端范围；[搜索包](api/entity-search-reads.md)先行，[消费／recent包](api/podcast-and-recent-reads.md)已合成接入；列表公开拒绝停止，[补证决策](api/spoken-playback-contract.md)已有传统音轨文档路线，VoiceId↔ProgramId及v1节目音源仍有实证门禁，独立声音播放未交付；不将管理接口当公共消费搜索。私人 FM 继续独立。
- 每阶段记录提交／版本、FR 技术进度与产品进度、测试报告、实际接口验证日期、风险和剩余门禁。
