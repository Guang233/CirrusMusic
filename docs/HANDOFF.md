# 开发接手指南

**维护者最新约定**：本助手只负责后端，不做UI；听歌统计／dashboard留到最后，先补上线所需后端。允许本地commit，只禁止push；此前“不提交”仅是各历史检查点状态，不是用户限制。保留“不新增测试”要求。维护者暂无Windows/macOS设备，后续Linux优先，两端原生源码保持default off／未验证，不把其环境门禁作为其他Linux后端开发的前置阻塞；不据此宣称跨平台完成。

**最新资源缓存首包**：[先设计后实现](tasks/resource-cache-backend.md)，基线cb60374；歌曲元数据／六轨歌词显式typed cache-only读写与封面显式GET／JPEG-PNG校验／尺寸化重编码。独立resource-cache.sqlite＋固定SQL账号分区（对原物理profile提案的限定偏离）、原DatabaseWorker线程、TTL/SHA256/128MiB元数据歌词＋512MiB封面payload预算、LRU/10000rows与有界淘汰。Runtime `resourceCacheRoot{}` 为空关闭；clear/remove使原writeContext permit失效，取消保留票据直到收据，旧GET／查询结果不复活cache；关闭等SQL并合并写失败。原MusicQuery不被透明拦截、无自动prefetch，元数据/歌词必须有明确剩余TTL许可才put；图库／搜索／推荐缓存与server缓存指令传递仍后续。Linux Debug/Release0warnings、actual Clang8units0、app17/cache6真实VALID依赖禁入0与no-undefined链接0、13fresh0；未新增或运行测试/SQL/图片/服务/实网，没有运行验收。UI/API107/state migrations/历史保留和原播放链不改，无新依赖/#92旧问题不闭，仅本地commit/no push。

**此前持久化接缝**：[队列恢复与长期收听历史](tasks/listening-state-persistence.md)，基线2c164a7，队列先commit55458e5、历史独立追加。Runtime `persistListeningState=false`，true才组合同owner服务／原DB线程，schema v1/v2保留＋v3明细。队列纯恢复不load/play/lyrics，原账号／session／intent与编辑屏障；每次正观测load独立record ID、UTC日历与原playedMs、单行累计upsert，数据库不设256保留上限。historyPage<=100与账号／clear epoch复验；clear不复活在途数据，退出最多265个final SQL job值移交worker、自行pending/idle排空，不依赖已销毁owner。Core计量仍保守、不把时间差／seek距离当时长；crash旧Active只是未终结历史，不还原播放／计账／报告。两包各Debug/Release0 warnings0、Clang7 units0／20 VALID对象禁入0／Core+Sql链接0，不运行产物；本包无新增或运行测试／SQL／服务／NetEase／UI。统计聚合、SQL语义／WAL／真实磁盘满和崩溃恢复尚无本轮运行证据，#92/minQt/native/部署保持；只本地commit，无push。

**此前架构整理**：[评论写入Facade职责拆分](tasks/comment-write-facade-refactor.md)。基线6191236，原queued DTO移到 `application/backend/comment_write_messages.h`，private `detail::CommentWriteRequests` 非QObject，只持pending和原评论规则；Facade仍持live状态／ID主计数／owner线程／signals和异步QTimer。public API、DTO字段、close/account屏障及其他业务方法正文保持，Gateway／Runtime／播放／IPC／UI不改。Linux Debug／Release编译0 warnings0，actual flags Clang4 units0／application17 VALID对象反向deps0／Core-only no-undefined链接0、13fresh0；无新增或运行测试／服务启动／NetEase请求／依赖安装，历史pass不复用为新验收。只本地commit，不push，#92和原生／部署门禁不关闭。

**此前SMTC／Now Playing源代码阶段**：[B-031 §9.9](tasks/B-031-playback-observation.md)新增 `native_media_service/inbox.{h,cpp}`、窄 `native_media_platform.h`、`windows_smtc_platform.cpp`、`macos_now_playing_platform.mm`，均在 `src/infrastructure/system_media/`。default-off `cirrus_system_media_native`／SMTC与NowPlaying显式平台构建开关；portable owner/main lifecycle＋≤8/32pps scalar gate、原binding/private intent stamp/消费时live复验已写，原commands加private guarded admission，不导出revision。Windows真实owned HWND/pin/STA/GetForWindow与exact event撤销，macOS main-runloop/ARC/NowPlaying playbackState与exact handler撤销代码已写入；不创建第二player／clock／query。关闭立即退役原导出，native清理异步main-loop；应用退出前必须排空ticket并核对cleanupSucceeded。Linux portable接缝与MPRIS Debug/Release0 warnings0、actual flags Clang4 units/no-undefined Core链接0，8fresh0；Runtime MOC按实际backend_autogen恢复，不删moc。**原生SDK／Qt Kit缺失，Windows/macOS代码未编译、未运行，不能称已完成三平台支持**；源码阶段I不充运行S，API/ARC/HWND/权限/版本/桌面门禁仍G。未新增或运行测试／接产品／安装依赖／访问NetEase／动stash／提交／push；#92未闭，旧证据不移作新包。

**此前Linux MPRIS代码检查点**：[B-031 §9.8](tasks/B-031-playback-observation.md)新增 `src/infrastructure/system_media/mpris_service.{h,cpp}`／`mpris_worker.{h,cpp}`。可选静态target `cirrus_system_media_mpris`／`CIRRUS_BUILD_MPRIS=OFF`，服务enabled、controls、metadata均default off。复用owner private feed与唯一命令桥；执行前live validatedExport/binding/fence复验，≤8票据，独立D-Bus I/O线程／coalesced属性发布／异步关闭。显式runtimeRoot/bus＋内核UID探测/pinned路径，拒绝ambient地址/TCP/autolaunch；路径检查不是Qt raw socket credential读取或沙箱安全证明。Qt6.11.2 Debug/Release编译0 warnings0、实际flags Clang3 units0、whole-archive no-undefined链接0；4新源fresh无C++诊断，Seeked协议名typo误报标false-positive，无源码suppress。没有新增／运行测试或启动真实bus服务，GNOME/KDE/minQt/发行身份/权限/性能仍G；SMTC／Now Playing仍未实现，原生工具链见下段。无产品入口／UI／依赖安装／NetEase请求／stash／提交／push，#92保持未闭。

**此前跨平台资源代码阶段**：[B-031 §9.7](tasks/B-031-playback-observation.md)共享LocalObservationEndpoint／Linux适配及Windows protected DACL/独占本地pipe/SID、macOS getpeereid/private filesystem/extended ACL/no-replace manifest源代码已写入。Linux适配Debug/Release与Clang通过；Windows SDK缺windows.h、macOS无xcrun，原生编译／运行仍G，inactive LSP不算验证。Windows接纳消耗的一个credential probe字节必须前置送回framer；notifier在accept/close前停用后重绑，所有资源操作/取消完成等待仅I/O线程。完整Windows/macOS服务／发现组合和系统媒体未实现，不接产品入口、不猜发行身份。**维护者最新要求：后续代码不必新增测试；本包未新增或运行测试，不删旧测试或拿历史pass填新证据。** 无新依赖安装/NetEase请求/UI/stash/提交/push。

**此前O3b2检查点（O3发行门禁仍进行中）**：[B-031 §9.6](tasks/B-031-playback-observation.md)参数化Linux owner→mailbox→单I/O线程/真实nonblocking socket＋严格发现客户端/双向UID/hello关联与显式XDG选择已限定I/S。新50，Debug11selected361＋runtime6、Release8selected307，0fail/skip；52 unique changed/adjacent fresh0，实际flags Clang11 units0；application16objects8124/611、service3objects1713/601、client2objects564/563禁入0。58登记非全58；input/retained＋native pending/八慢client/wake/deadline/隐私/thread关闭已验。default off、没有产品入口或正式namespace；发行身份/另一OS用户负测/沙箱/minQt/部署性能与平台adapter仍G。无新依赖/NetEase请求/UI/stash/commit/push，#92独立未闭。

**此前O3a工作树（历史子包）**：[B-031 §9.4](tasks/B-031-playback-observation.md)新增Linux私有资源/native SO_PEERCRED guard接缝与隔离合成harness，默认off、参数化非发行身份、private0700/0600、no-follow/pinned inode/exclusive/仅自身清理，双向UID＋跨process正测；32新/3selected200，0fail/skip。正式app ID/XDG发现reader、另一真实OS用户负测、Core私有通知/真实buffers/wakes/生命周期组合仍G，没有可消费音乐状态server或产品端点；54登记非全54，107ops不变，未装依赖/实网/UI/stash/push/commit。#92未闭；之前各包不相加。

**此前O2b工作树（历史便携子包）**：[B-031 §9.3](tasks/B-031-playback-observation.md)已接纯Core基础设施逻辑连接/fake sink、单latest/wake mailbox与显式owner beginView/private fence、分步共享encoder、基线/event/pending/rate/partial-write/慢消费者关闭；43connection、6selected259＋runtime6，0fail/skip。真实endpoint/peer/私有owner组合与bounded调度仍O3、MPRIS/SMTC/macOS仍G。107ops/26families/44FR不变，53登记非全53验收，#92独立未闭；HEAD1a36f78、全部未提交/未staged，无新依赖/实网/entry/UI/stash/push。下一O3安全本机传输，O2仅便携逻辑/fake限定I/S，不是已可连接服务。

**此前O2a工作树（历史子包，当时O2未完成）**：[B-031 §9.2](tasks/B-031-playback-observation.md)已加入纯Core基础设施codec/request/framer、完整snapshot golden/16错误编码、六轨有界预编码/分页与fake chunk consumer；125codec、5selected216＋runtime6，0fail/skip，Debug build0/warnings0、11fresh0。107ops/26families/44FR不变，登记52非全52验收。**O2仍未完成**：hello-first连接状态机、订阅基线、pending/rate、增量编码/单latest/mailbox、隐私退役与backpressure继续O2b；没有真实端点/peer认证/native/平台依赖/新业务或实网。HEAD1a36f78、全部未提交/未staged、UI/paused/APK/exact stash保持；O1以下为独立历史证据，不与本轮相加，#92未闭。

**此前O1工作树（历史子包）**：两个账号等级/收藏计数API先不做。原[B-031统一播放状态／六轨歌词本机只读IPC及三平台系统媒体](tasks/B-031-playback-observation.md)设计后要求继续，已接Core投影／唯一命令桥及volume/offset/position到达/保守seek反馈；同token phase往返退役旧load，未知metadata不补请求，六轨独立多游标。O1限定I/S：新suite37、12affected228/0fail/1音频skip、选定runtime3函数6pass、actualCore16objects含MOC8122/609禁止0、fresh30路径0；#92cpp-check解析仍未关闭，不称全51/设备/Release/minQt/平台通过。107ops/26families/44FR不变，登记51。HEAD仍1a36f78，全部本包未提交/未staged；O2–O6 IPC/MPRIS/SMTC/macOS均G，无端点、新平台依赖或入口接线/实网。下一O2 codec/fake transport；持久化后续，UI最后/paused/APK/固定stash保持。

**最新接续6f63c24：[歌曲百科受限纯文本子集](api/song-wiki-text-subset.md)**。维护者明确选仅两类文本，原请求关联但内容身份未独立验证，完整百科不开放。107ops/50登记/26families/39E63W5X/44FR；百科88／23affected1278＋16runtime函数17data/init-cleanup19，0fail/skip，18fresh all-server LSP0／13 format0／14实际application对象含MOC7004逐对象-602唯一deps禁入0/UI0。四次另行确认公开百科窗口结束2026-10-08T07:13:21Z，均HTTP/business200；原值不保存，技术结构/equality bool不证明主SongId。实现后无新live；只本地commit/no push，paused/APK/stash/媒体owner不动，#92独立未闭。以下旧工作包证据保留历史，不累加为全50验收。

> 接手入口，不需要阅读此前聊天。代码基线为认证事务 `1a60b74`，其后增加扫码调度、XEAPI、SQLite／密钥环、[匿名启动](api/anonymous-bootstrap.md)、[账号恢复](api/account-session-restore.md)与[后台装配](api/backend-runtime.md)（见 `git log`）；[验证记录](handoff-verification.md)对应的是 `1a60b74` 时点。交付方应提供**包含本文件的完整提交**，并告知 `git rev-parse HEAD`，不要只发送这个旧基线号。

## 1. 先理解现在的状态

**这是已建立后端基础和离线认证事务的项目，不是已经可用的音乐客户端。** `main.cpp`／`Main.qml` 仍是 Qt 模板，后端尚未接入入口。

| 已实现且有离线测试 | 尚未实现／尚未证明 |
| --- | --- |
| C++20／Qt 分层 targets、weapi/eapi/XEAPI、form、路由、HTTP、gzip／JSON 解码、XEAPI 公钥注册与会话密钥 | 除少量公开查询／XEAPI音源／试听播放外的广泛官方兼容性 |
| epoch 隔离、内存 Cookie、取消及晚到结果保护；SQLite 与稳定设备 ID；QtKeychain、XEAPI 公钥持久化、匿名 token 启动／恢复／重置；已登录账号凭据记忆／身份复核／恢复／退出清理 | 真实桌面密钥环／进程异常终止后的验收 |
| eapi QR key／QR check、weapi AccountIdentity、XEAPI AnonymousRegister，及搜索／歌曲详情／歌词和官方SongStream，加上专辑／艺人／歌单5项和日推／FM2项、[个人库4项读取](api/personal-library.md)，加[两个显式上报action](api/explicit-listening-reports.md)，加[云端历史两项读取](api/cloud-history-reads.md)，加[榜单两项读取](api/chart-reads.md)，加[歌单发现6项读取](api/playlist-discovery-reads.md)，加[实体／播客／声音搜索5项](api/entity-search-reads.md)，加[播客消费／4类recent读取7项](api/podcast-and-recent-reads.md)，加[资源评论8项读取](api/comment-reads.md)，加[播客订阅／云盘3项账号读取](api/account-library-reads.md)，加[五类收藏订阅8项write operation](api/collection-writes.md)，加[歌单创建／单独改名／描述3项write](api/playlist-management-writes.md)，加[歌单删除／两类排序3项write](api/playlist-delete-and-order-writes.md)，加[艺人长介绍／歌曲百科基础2项read](api/catalog-introduction-and-metadata-reads.md)，加[标签编辑／单向公开2项write](api/playlist-tags-and-publication-writes.md)，加[封面分配／提交2项write](api/playlist-cover-update.md)，加[评论点赞／取消／发布／回复／删除5项write](api/comment-writes.md)，加[搜索建议／两类热搜3项read](api/search-assistance-reads.md)，再加[每日推荐歌单独立原账号读取](api/daily-recommended-playlists.md)，再加[历史日推可用日期／原日期歌曲两项读取](api/daily-recommendation-history.md)，再加[一起听核心12 HTTP-only](api/together-http-core.md)，再加[常规发现9读取](api/discovery-browse-reads.md)，再加[百科受限文本1读取](api/song-wiki-text-subset.md)，共107个业务operation，另有独立公钥注册链与获准原生v3窄前置GET；单曲协调器、受控CDN缓冲及Qt播放后端；[队列／六轨歌词时钟](api/queue-and-lyric-clock.md)及[FM连续播放](api/fm-continuous-playback.md)、[本地观测累计／会话历史](api/local-listening-history.md) | 目录／日推／FM／个人库／云端历史／榜单／歌单发现实网验收、库缓存／同步、持久历史实际durability／统计聚合与UI、自动上报时机与来源扩展、真实报告及声学计量验收、流式起播、整歌单收藏v2 SDK／真实收藏写／对账、云盘播放／写入／下载、完整一起听实时同步／NIM |
| `QrLoginService` 身份验证、快照收据、两阶段凭据交接、可选自动轮询与认证总时限；BackendRuntime后台装配、Core-only非阻塞Facade和关闭排空 | 模板入口接线、QML注册和UI |
| 历史完整基线：13套件／329 pass；最新百科子集23affected1278 pass（百科88），另17线程数据行19 pass，0fail/skip；百科时登记50套件（当前Linux58，非全58验收）；旧发现映射修正1190＋18为历史；此前一起听HTTP-only911＋17只属该包；此前日推历史839＋15为独立证据；媒体接缝373＋13／原audio1skip为此前独立证据；每日歌单753＋14为此前独立证据；搜索辅助631＋12为此前独立证据；评论760＋11为此前独立证据；封面562＋11为此前独立证据；标签563＋11为此前独立证据；四类资料730＋11为此前独立证据；删除／排序包463＋10；歌单基础写包404＋9，收藏包335＋9，账号读取332＋8／评论328＋8／消费recent207及播放PoC是此前独立证据（含data rows及init/cleanup） | CI、发布包、跨发行版兼容性、真实账号验收 |

上游440模块（164采用候选、40待定、236排除）只是静态审计，维护者新批的播客／声音消费范围与5项搜索在增量契约中记录，不回写审计为实时支持列表；`inventory.json` 的 `not-implemented/static-only` 是审计时点标记，不作为运行注册表或实时实现进度。

上包修正：[公开发现响应窗口](api/discovery-public-response-verification.md)，原C++/Qt链11次串行metadata读（代码基线331b4fb），第11次SimilarArtists业务301即停，不重试；SimilarPlaylists未发。前10次HTTP/code200中，月榜monthData[]缺weekData及相似歌5>请求limit2被原mapper拒绝，已修为optional week/month（至少一组、missing未知／[]空）与SimilarSongs单批100／requestedPaginationUnverified=true，保完整重复顺序、不造分页。共享songFrom已支持legacy artists/album/duration，未增加alias或改共享算法。复杂百科只见data.blocks6/rnData前2块key/type，Song绑定／块类及余块语义仍G，不注册入口、套旧summary/UGC或开放raw JSON/HTML/动作。原[9发现typed合同](api/discovery-browse-reads.md)、read16／query-content-kind/token-generation/cancel-close／Cookie不采纳、显式年月／原seed，无新owner／自动下游／UI／运行库。

上包最终106ops／49show-only登记／discovery_browse1不变，发现188／22affected1190＋15选定runtime函数16data/18pass，0fail/skip；旧331b4fb新165／1167＋18与首轮123为历史、不累加。5changed C++ fresh all-server LSP0／format5，14实际application对象（含MOC）6998逐对象／601唯一Ninja依赖禁入0／UI0，真实build0/warnings0／fixed440／参考hash／Python15。首次runtime选择误函数musicThroughThread exit1未执行、未计证据，按真实-functions改musicQueriesThroughThread后重跑通过；未删负例／Q_OBJECT/.moc/fatal或压diagnostic，#92 cpp-check parse仍另记。DNS-only198.18误阻断已撤回，普通curl首页HEAD200/TLS0仅连通证据，无网络／TLS绕过。修后没有重发实网；无完整49／Release／minQt／广泛服务／账号／SDK／keyring／音频／UI或权益验收，只有该窄metadata授权覆盖此前no-live。paused／APK禁读／固定stash不动，只本地commit、不push；ignored discovery-public-*证据只保结构／计数／必要状态，无服务原值／全文／Cookie／签名URL。最终local refs/有限literal与staged diff见本包收尾记录，不挪用旧896／867或历史包计数。

[107operations／44FR覆盖对照](api/backend-coverage-audit.md)同步26families／39E63W5X，I/S/L/G/P/X分开；初始53f8f3e的docs-only审计为97／48、24groups，不把本包测试塞入历史轮。不能164adopt−106计算缺口或登记即FR完成；账号等级＋音乐收藏计数仍候选未实现，维护者明确先不做，真实schema待证、不推权益。FM mode/trash需来源与真实time，next不等于trash。Together optional/playlist/profile及G1–G6仍分开，v2/cloud/spoken next/fullTogether不恢复。

此前代码包：[一起听核心12 HTTP-only](api/together-http-core.md)。全部Eapi标准ECB／Status Eapi／窄interface.music host，保留原PC metadata（组合未实网证明）；Core typed DTO／shared8／原account-token-generation-UUID-roomGeneration相关／四结果，roomGeneration非自动房间切换fence。Queue内部playlist未知，显式unmapped，不改本地队列／播放器／报告／timer／Active。15affected911＋runtime15data/17pass，新109，0fail/skip；16fresh C++ all-server LSP0／14objects6992actualdeps禁入0／UI0；fixed440/publichash+Python15／format/diff／最终local refs与有限literal-query扫描见ignored together-http-doc-check.json，非anchors／全面安全。本包不是全48／Release／真实账号／NIM／SDK／音频／UI验收；#92旧cpp-check output parse未闭另记。只本地commit、不push；资料后用户明确批准核心12，仅HTTP暂停被覆盖，v2／云盘／spoken next／完整同步仍暂停；APK撤权且从未访问，stash9907ac4…不pop/drop/apply。

此前**资料审计**：[一起听Android摘要与固定API对照](api/together-protocol-material-audit.md)。维护者提供下载目录ListenTogether.md供参考；原文件hash／完整静态核对已记录，NIM/chatRoomId/三个type、serverSeq/ignoreUserIds与毫秒有新线索，但IM鉴权／envelope／SELFHOST、seq/version／时钟重连权限、Linux与许可未闭；CBC/host/全Eapi、command/playlistParam与check/end/queue响应差异不直接混用。资料到达不是完整协议或自动解除实施暂停；只docs，85ops／47不变，无新增C++／build／Qt／LSP／实网／SDK／依赖／UI证据。固定WIP9907ac4…仅只读，未pop/drop/apply，其他暂停项保留、UI最后。

此前代码包完成[每日推荐历史读取](api/daily-recommendation-history.md)：维护者在第四项绑定／音源资料不足后明确选择先做本项，前三项和第四项后续均暂停。2Weapi、原account/MUSIC_U、日期DTO成员private factory／原source token/read16；候选data.dates366/1MiB与data.songs500/4MiB，保序重复宽ID，无date自由空值／uid／afresh、自动读／详情／图片／播放／反馈写／cache。新86／17 affected839＋13线程数据行15 pass，0fail/skip，16fresh paths clean／14objects6985实编dep禁入0／UI0，85ops／47登记/noTogether。原wrong Cookie分类只改fixture精确expected，fatal/MOC/坏schema不弱化；日志recommendation-history-final-*独立，没有真实账号／源／audio／SDK／新库／UI或完整47/Release验收。服务schema／日期window／会员／完整性仍未实证，不能宣称全API完成；本地commit不push。后续其他已纳入小包需单独审计确认，不自动重开节目-462／第四项或前三项；固定Together stash仍不pop/drop。

此前完成[播客／声音离线共享媒体接缝阶段1](api/spoken-media-seam.md)：维护者前三项暂停、从第四项开始并明确选择离线准备。独立PlaybackEntity／AudioTrackId仅结构、MediaSource无身份，原唯一coordinator先校验Song-token-TTL后下沉到媒体port／buffer／Qt后端。83ops不增／46登记；新28／15 affected373＋11线程数据行13 pass，0fail／原桌面audio1默认skip，17fresh paths clean／14实编对象6973dep禁入0／UI0。没有可信消费binding、节目／声音resolver／活动snapshot／history模式或播放入口，不执行实网／SDK／new deps，日志spoken-seam-final-*独立。下一步仍第四项的G1/G2绑定／音源补证，再完成单owner逻辑模式；不拿类型／原歌曲测试推实际spoken可播，前三项保持暂停、WIP固定对象不动，UI最后。

此前完成[每日推荐歌单](api/daily-recommended-playlists.md)：独立原账号空payloadWeapi读取／root.recommend有界候选，复用原摘要与read16；保持与普通推荐／每日歌曲不同result。补强Facade与Service同token变账号／认证态私有读屏障，新62／16受影响753＋selected runtime14，0fail/skip，fresh14路径clean／14实编对象6963dep禁入0。没有真实账号／UI／日期cache／自动详情图片播放写报告／SDK／新库；日志daily-playlists-final-*单独有效，不使用Together草稿日志或旧二进制。维护者暂无v2资料、明确暂停整歌单收藏，后续其他后端小包先审固定来源／已纳入范围／Core边界；一起听到最后且需其全套材料，云盘仍暂缓。只本地提交、不push。

## 2. 第一次接手：按此顺序执行

### A. 确认拿到了正确代码

```bash
git status --short
git log -5 --oneline
git rev-parse HEAD
```

交付方当前只进行了本地提交，不能假设远端含最新代码。仓库地址／分支由维护者提供，不凭空填写公共地址或自行推送。若不能走远端，可由交付方在确认无敏感内容后交付 Git bundle：

```bash
# 交付方：只打包需要交接的 master 历史，不打包工作目录、缓存或其他分支
# 以下命令是交付选项，本轮没有代为发布或生成交付包。
git bundle create ../CirrusMusic-handoff.bundle master
# 接收方
git clone -b master /path/to/CirrusMusic-handoff.bundle CirrusMusic
```

目录副本也可用于构建，但正式接手应保留 Git 历史。无需获取任何账号密码或真实 Cookie。

### B. 准备工具链

- Linux；当前实测 x86_64。CMake ≥3.24、Ninja、支持 C++20 的编译器。
- Qt 开发 Kit：模板要求 ≥6.10，**实际验证的是6.11.2**；需要 Core、Quick／QML、Network、Sql（含QSQLITE插件）、Multimedia（含音频后端插件）、Test及QML lint工具；模板还使用 `QtQuick.Layouts`、`QtQuick.Controls.Basic`，不能只装 Qt Core 开发包。未证明6.10或其他发行版兼容。
- OpenSSL ≥3 Crypto、zlib、Qt6Keychain 的开发头和库（系统包名依发行版而异，如 `qtkeychain-qt6`）。仅有运行库不足以编译。系统密钥环不可用时有明确错误，不明文回退。
- Python 3：可选审计校验，不是应用构建／运行依赖。不需要 Node.js。
- GDB、clangd、clang-format、VS Code 扩展是调试／编辑工具，不是运行依赖。不要盲目复制本机编译器版本作为最低版本要求。

发行版若没有匹配 Qt，请使用已有受信任 Kit；本项目还没有自动安装依赖或发行 Runtime。不要为凑依赖悄悄降 Qt、引入 WebEngine 或服务代理。

### C. 完整构建并测试，而不是只编译空窗口

以下使用共享 preset，无需本机私有文件。将 Qt 路径替换为接收机器的实际路径；若系统能发现正确 Qt，可省略该参数。

```bash
cmake --preset linux-dev -DCMAKE_PREFIX_PATH=/absolute/path/to/Qt/gcc_64
cmake --build --preset linux-dev --parallel 4
ctest --test-dir build/linux-dev --output-on-failure --no-tests=error
cmake --build --preset linux-dev --target all_qmllint

cmake --preset linux-release -DCMAKE_PREFIX_PATH=/absolute/path/to/Qt/gcc_64
cmake --build --preset linux-release --parallel 4
ctest --test-dir build/linux-release --output-on-failure --no-tests=error
```

当前Linux CTest 登记55个套件；百科时为50、O1为51、O2a为52、O2b为53、O3a为54；少于55个先排查错误目录／`BUILD_TESTING`，不要把“无测试”当成功。13套件／329 pass为历史完整基线，此前[日推历史](api/daily-recommendation-history.md)17套件839＋13线程数据行15 pass，0fail/skip，新86；16fresh路径clean／14实编对象6985dep禁入0，85ops／47登记，原日期成员／账号／source token与read16，无实网／SDK／UI／自动下游，第四项后续亦暂缓。此前[离线媒体接缝阶段1](api/spoken-media-seam.md)15 affected套件373 pass＋11线程数据行13 pass，0fail／原audio1默认skip，新28／17fresh路径clean，14实编对象6973deps禁入0。83ops不增、无实际spoken入口／binding／实网；此前[每日推荐歌单](api/daily-recommended-playlists.md)16 affected套件753 pass＋12线程数据行14 pass，0fail/skip（新62）；14fresh paths clean、14实编application对象6963依赖禁入0／UI0，原账号／16槽与同token私有scope补强，无推荐日期或自动下游／新库／SDK／实网。此前[搜索辅助](api/search-assistance-reads.md)11 affected套件631 pass＋10线程数据行12 pass（含init/cleanup），0fail/skip；原read16／token／typed独立IDs与候选schema，无实网／SDK／新依赖／UI／自动search／cache／history。15fresh CPP（13changed＋2adjacent）clean、14应用对象6963真实依赖禁入0，详细预算／失败修正／复跑见契约；此前[评论五动作写](api/comment-writes.md)10受影响套件760 pass、9线程数据行11 pass（含init/cleanup），0fail/skip，无实网／JS／SDK／UI／新库；原生v3前置经明确批准，22fresh CPP clean／14应用对象6957实编依赖禁入0，详见新契约。此前[封面更新](api/playlist-cover-update.md)10受影响套件562 pass、另9条线程数据行11 pass，无fail/skip；此前[标签／单向公开](api/playlist-tags-and-publication-writes.md)8个受影响套件563 pass、另9条线程数据行11 pass，无fail/skip；此前[四类资料／介绍](api/catalog-introduction-and-metadata-reads.md)14个受影响套件730 pass、另9条线程数据行11 pass，无fail/skip；此前[删除／完整范围排序](api/playlist-delete-and-order-writes.md)8个受影响套件463 pass、另8条线程数据行10 pass，无fail/skip；此前[歌单基础写](api/playlist-management-writes.md)7个受影响套件404 pass、另7条线程数据行9 pass，无fail/skip；此前[收藏订阅写](api/collection-writes.md)7个受影响套件335 pass、另7条线程数据用例9 pass，无fail/skip；此前[账号补充读取](api/account-library-reads.md)7套件332＋8 pass；此前[评论只读包](api/comment-reads.md)7套件328＋8 pass；此前[消费／recent工作包](api/podcast-and-recent-reads.md)共207 pass，另6条线程数据用例共8 pass；不自动累加为新的完整验证报告。真实密钥环往返也默认跳过。首次接手可执行上述完整命令；日常小增量优先编译／测试受影响目标，不必每次重复整套 Debug/Release。

VS Code：按 [17](17-vscode-development.md) 创建私有 preset，选择匹配的 configure/build preset；`Ctrl+Shift+B` 现构建 `all`，`Cirrus: test` 先构建再跑 CTest。后端没有链接到空壳 app，**只构建 `appCirrusMusic` 不能验收后端改动**。

仅 CLI 配置后，clangd 需要刷新：

```bash
cp build/linux-dev/compile_commands.json build/compile_commands.json
```

源码入口运行仍只显示模板窗口，不能用它测试真实登录或播放。

### D. 可选审计与固定参考源码

```bash
python3 tools/validate_api_inventory.py
python3 -m unittest discover -s tools/tests -p 'test_*.py'
```

预期440模块校验及15项 Python 测试通过；不需要上游 checkout。若任务需要核对源码，另行获取锁定参考：

```bash
# 需要 GitHub 网络访问；目标目录须不存在，已有目录先检查而非覆盖
mkdir -p build/reference
git clone --no-checkout https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced.git build/reference/api-enhanced
git -C build/reference/api-enhanced checkout --detach a8c781fd64faab17fedfd46e0615a2609307f163
python3 tools/validate_api_inventory.py --upstream build/reference/api-enhanced
```

以 `docs/api/upstream-lock.json` 为准，不自动更新到 upstream HEAD；不执行 `npm install` 或运行参考服务。若固定提交无法取得，保留阻塞证据并联系维护者，不静默替换版本。`build/reference/` 中此前临时提取脚本不是产品或测试依赖，不需要复制。

## 3. 最小阅读与代码导航

顺序：本页 → [实现记录](api/native-foundation.md) → [系统架构](03-system-architecture.md) → [认证事务](api/authentication-transaction.md) → [已完成的轮询任务契约](tasks/B-010-qr-polling.md)。遇到协议疑问再查 [API 审计](api/README.md)和[门禁](api/gaps-and-hazards.md)，不必先读完440模块。

| 入口 | 看什么 |
| --- | --- |
| `src/CMakeLists.txt`、`tests/CMakeLists.txt` | 实际依赖方向、测试 target；不是架构图中的未来空模块 |
| `src/domain/account/session_types.h` | Token、phase、身份值；token 不是凭据 |
| `src/application/account/session_coordinator.*` | lifecycle 与重入安全的两阶段转换 |
| `src/application/account/qr_login_service.*` | begin/poll/cancel、自动轮询与总时限、身份确认、精确 target 提交 |
| `src/application/account/login_scheduler.*` | 单调时钟＋单次定时器窄接缝；测试替身在 `tests/support/fake_login_scheduler.h` |
| `src/application/ports/qr_login_gateway.h` | 窄 port、按值结果、脱敏错误；无 JSON／Cookie |
| `src/infrastructure/netease/api/api_operation.*` | 目前106项业务operation的封闭注册契约（together_operation.*为12项HTTP-only，discovery_browse_operation.*为9项Weapi发现）；评论read adapter为comment_operation.*，write adapter为comment_write_operation.*；动态路径只允许规范数字ID后缀 |
| `src/infrastructure/netease/api/session_api_client.*` | 捕获 token/origin、快照和一次性收据 |
| `src/infrastructure/netease/auth/` | QR 映射、身份 schema、port 实现 |
| `tests/unit/qr_login_service_test.cpp` | Core-only fake port 与虚拟时钟用例 |
| `tests/unit/auth_transaction_test.cpp` | 从实际协议代码到应用的合成网络认证链 |
| `src/infrastructure/bootstrap/anonymous_bootstrap.*`、`tests/unit/bootstrap_test.cpp` | 安装级匿名恢复／注册，重置后排空在途写入再删除的边界 |
| `src/infrastructure/netease/auth/account_credential_vault.*`、`tests/unit/account_restore_test.cpp` | 单槽位账号记录、版本隔离、身份复核与read/write/logout竞态 |
| `src/application/backend/backend_facade.*`、`src/infrastructure/runtime/backend_runtime.*` | 调用方Core-only入口、后台装配／启动／关闭；契约见[后台生命周期](api/backend-runtime.md) |
| `src/infrastructure/secrets/tracked_secret_store.*` | 原接收者销毁后仍追踪真实keyring操作完成，vault排空前不销毁后端 |
| `src/domain/music/`、`src/application/music/`、`src/infrastructure/netease/music/` | 歌曲查询、原token用例、JSON映射与六轨歌词解析；见[音乐查询契约](api/music-queries-and-lyrics.md) |
| `src/domain/playback/`、`src/application/playback/`、`src/infrastructure/media/`、`netease/music/stream_resolver.*` | [官方音源／试听／TTL、固定load身份、独立Qt媒体对象与临时缓冲](api/stream-resolution-and-playback.md) |
| `src/application/playback/listening_session.*`、`playback_queue.*`、`lyric_timeline.*` | Runtime.listening()调用方入口；队列身份、有限续播、六轨时钟索引，无网络／解码器依赖 |
| `src/application/music/personal_fm_source.*`、`netease/music/discovery_mapper.*` | [日推读取／FM条目供给](api/discovery-and-fm.md)；Runtime.fm()仅供给；[连续播放](api/fm-continuous-playback.md)由ListeningSession.startFm()独占驱动 |
| `src/domain/music/library_types.h`、`netease/music/library_mapper.*`、`tests/unit/library_test.cpp` | [当前账号个人库](api/personal-library.md)：歌单／喜欢ID／收藏专辑及艺人，认证与账号绑定，无缓存／写入 |
| `src/application/playback/observed_playback.*`、`listening_history.*` | 媒体进展计量值模型与256条会话账本；Qt采样在基础设施；无持久化／官方报告 |
| `application/ports/listening_report_gateway.h`、`netease/music/listening_report_gateway.*` | [显式上报能力](api/explicit-listening-reports.md)：每调用一个action，clientlog/osx profile；无自动触发／重试 |
| `src/domain/music/cloud_history_types.h`、`netease/music/cloud_history_mapper.*`、`tests/unit/cloud_history_test.cpp` | [最近听过／听歌排行](api/cloud-history-reads.md)：当前账号有界只读，次数／分值缺失为未知，不推导时长；响应仅合成验证 |
| `src/domain/music/chart_types.h`、`netease/music/chart_mapper.*`、`tests/unit/charts_test.cpp` | [榜单列表／内容摘要](api/chart-reads.md)：Guest／账号只读，展示预览非Song，歌曲页和显式播放复用原链 |
| `domain/music/playlist_discovery_types.h`、`netease/music/playlist_discovery_mapper.*`、`tests/unit/playlist_discovery_test.cpp` | [歌单发现6项](api/playlist-discovery-reads.md)：推荐无offset、普通offset、精品before与独立标签；原目录／显式播放复用 |
| `domain/music/search_assist_types.h`、`music/search_assist_mapper.*`、`tests/unit/search_assist_test.cpp`、`tests/support/synthetic_search_assist.h` | [搜索建议／两类热搜](api/search-assistance-reads.md)：3read、四typed ID/name预览、raw热词/score、strict order／optional与20/16/100，本地关键词256；原16槽／token及乱序query/id、close Caller Canceled，无自动search／下游／cache／持久history，117＋受影响631及线程12，仅合成 |
| `domain/music/entity_search_types.h`、`netease/music/entity_search_mapper.*`、`tests/unit/entity_search_test.cpp` | [5项实体／播客／声音搜索](api/entity-search-reads.md)：独立Podcast／Voice ID，无歌曲身份／报告适配，有限无Cookie公开结构验证 |
| `domain/music/podcast_types.h`、`recent_entity_types.h`、`netease/music/podcast_mapper.*`、`recent_entity_mapper.*`、`spoken_mapping.*` | [消费3项／recent4项](api/podcast-and-recent-reads.md)：ProgramId独立，原账号／MUSIC_U／候选schema；列表公开-462停止，Voice对应／节目详情／recent实证未完成 |
| `domain/music/comment_types.h`、`api/comment_operation.*`、`music/comment_mapper.*`、`tests/unit/comments_test.cpp` | [资源评论](api/comment-reads.md)：普通4资源＋通用5资源的热门／新版／楼层／统计，Voice／艺人无已确认绑定；读取与下述独立写隔离，账号历史／通知未交付，候选schema仅合成 |
| `domain/music/comment_write_types.h`、`application/ports/comment_write_gateway.h`、`api/comment_write_operation.*`、`music/comment_write_gateway.*`／`comment_v3_issuer.*`、`protocol/comment_v3_policy.h`、`tests/unit/comment_writes_test.cpp` | [评论五动作写](api/comment-writes.md)：4variant／5operation、原账号／独立8槽／business NotSent-Unknown；发布／回复经批准原生v3 fixed GET→XEAPI，无SDK／JS／外部Core token／推测评论ID／重试／隐式读；真实权限／UI与schema未验收 |
| `domain/music/account_library_types.h`、`music/account_library_mapper.*`、`tests/unit/account_library_test.cpp` | [账号补充读取](api/account-library-reads.md)：订阅播客／云盘列表／详情，CloudSongId与catalog引用分开、原账号／MUSIC_U；候选schema，无云盘播放／写入 |
| `domain/music/collection_types.h`、`application/ports/collection_gateway.h`、`netease/api/collection_operation.*`、`music/collection_gateway.*` | [收藏订阅写](api/collection-writes.md)：红心／收藏到歌单／专辑／艺人／播客5类10动作；8槽、原账号／Unknown、无重试／自动读，整歌单收藏v2门禁延期 |
| `tests/unit/playlist_writes_test.cpp`及上述collection_types／adapter／gateway链 | [创建／改名／描述](api/playlist-management-writes.md)：3operation、固定NORMAL、候选创建ID／Unknown、原8槽；不按同名判唯一，无batch／重试／自动读／删除／上传 |
| `domain/music/playlist_order_types.h`、`tests/unit/playlist_order_test.cpp`及collection写链 | [删除／排序](api/playlist-delete-and-order-writes.md)：单目标删除、完整索引／明确首末账号页工厂的置换、原source token／账号、预算与Unknown；无分页误当全量／CAS／自动刷新 |
| `domain/music/introduction_types.h`、`music/introduction_mapper.*`／`metadata_mapping.h`、`tests/unit/catalog_metadata_test.cpp` | [四类资料／介绍](api/catalog-introduction-and-metadata-reads.md)：复用原详情／描述，增量optional字段；独立艺人分节长介绍／歌曲四种基础百科文本，原16槽／token；候选schema，不转发JSON／生成叙事／自动下游 |
| `domain/music/playlist_tag_edit_types.h`、`tests/unit/playlist_tag_writes_test.cpp`及collection写链 | [标签编辑／单向公开](api/playlist-tags-and-publication-writes.md)：当前token的Catalogue成员工厂、1..3／128本地预算，不清空；公开只固定numeric0、原账号／8槽／Unknown，无自动读／反向隐私转换 |
| `domain/music/playlist_cover_types.h`、`music/cover_upload.*`、`tests/unit/playlist_cover_writes_test.cpp`及collection链 | [封面更新](api/playlist-cover-update.md)：JPEG字节1MiB／1024方形、经批准Gui仅基础设施，准备／分配／NOS／提交，原账号8槽及阶段Unknown／无自动读，只有合成、无文件路径或真实上传 |
| `domain/music/discovery_browse_types.h`、`api/discovery_browse_operation.*`、`music/discovery_browse_mapper.*`、`tests/unit/discovery_browse_test.cpp`、`tests/support/synthetic_discovery_browse.h` | [常规发现9读取](api/discovery-browse-reads.md)：typed action/content/query，显式年月/原Song seed/read16、strict数组/宽ID/原text/未知分页，106ops/49登记/165＋1167/thread18；complexWiki schema门禁Q-016，不装假入口，无下游/新owner |
| `tests/support/` | fake HTTP、解码测试辅助；不是产品运行组件 |

## 4. 不可破坏的设计约束

- 封面基础设施已获准PRIVATE依赖QtGui／JPEG插件做解码／重编码；不扩展到Core／UI或其他图片格式，不读取私人文件／真实上传；无SDK或第三方图片库。
- domain／application 仅 Qt Core 值与用例；不引入 Qt Network、JSON、OpenSSL、具体 NetEase 实现或 QML。Qt Multimedia已获准引入基础设施层；domain／application仍不链接Network／Multimedia。
- `803 ≠ Authenticated`。身份查询验证后 prepare 到 **Committing**；凭据交给返回的**确切 target**并确认后，才能 complete 到 Authenticated。Committing 不可分发账号私有用例。
- 身份请求前捕获 Cookie 快照，不能在身份 A 返回后复制已更新成 B 的当前 jar。普通 epoch 前进清凭据，不自动继承。
- 所有完成信号带原 token；取消只是优化，消费者必须校验上下文。重入退出／换号优先于旧成功结果。
- 当前所有认证对象在同一个后端事件循环线程构造、使用、销毁；不得直接跨线程调用同步返回 requestId 的 API。client 含值成员 CookieJar，不能构造后整体 moveToThread。
- 正常程序close保留记忆，signOut才删除账号；事件循环持续到Facade.stopped，密钥环不回调时只报告closeDelayed，不强杀或虚报擦除。Runtime只移动空Worker壳，资源在线程内创建。
- service 必须先于其引用的 SessionCoordinator 销毁；正常装配让 gateway/client 覆盖 service 生命周期。不要假设给多个对象设置同一 QObject parent 就保证正确析构顺序。
- 凭据不进入应用层、QML、普通配置或日志；UUID 收据不能持久化。大数值 ID 不安全时拒绝，不浮点舍入补救；不同作用域 MUSIC_U 冲突不猜选。
- 后端先行，UI 最后。每日推荐歌曲独立建模；私人 FM／一起听及维护者新增播客／声音消费端范围纳入；视频/MV、云贝/云豆、动态、云小编、创作者上传／审核工作台排除。PodcastId／VoiceId不强转PlaylistId／SongId。不引入第三方音源解锁。

## 5. 接下来做什么，什么时候需要询问

**此前范围记录**（接续acf822e；第四项后续离线阶段覆盖见本页最新接缝包）：维护者明确未来下载以播放接口实际可听音源为准，不新增专用下载API、不额外查询下载资格／标志／加密下载文件支路；保留音源自身试听范围／TTL／实际访问限制，不扩可听范围，文件恢复另包，临时缓冲不是下载交付。云盘后续API／音源／播放／写暂缓，原账号已有读取保留。一起听随后经维护者明确搁置至最后，表示已逆向整套接口，待收到完整材料再核对；固定参考9 HTTP草稿已本地WIP保存并撤出主线，不计API／完整一起听交付。维护者随后明确暂无v2资料，选择暂缓整歌单收藏／取消、推进其他后端；调查固定Watchman v2原生前置的结论保留，要求仍在但不自动恢复，不执行SDK／JS／Node／WebEngine、新库／实网，不以评论v3替代。需要改变这些边界再询问。声音Program/Voice与底层音轨关联、官方URL／权限／TTL／preview链本次只是解释，未默认开始实现。此前这项范围记录docs-only，当时registry82／CTest44不增；最新每日推荐歌单包单独交付83／45、新62／affected753＋thread14，一起听草稿153／affected858＋thread13只是暂停前的合成历史，不能计入主线／FR-043验收；搜索辅助631＋12仍独立已交付证据，不重宣全量或实网兼容。本地WIP stash **9907ac4a9ad83b9cefb79fea9d3337b56b91ba6f** 保存8 tracked＋8新文件的完整草稿（含取消／generation／线程测试）；只在最后明确恢复且先核对新材料时以该固定对象识别，不能按浮动stash@{0}自动pop／drop。ignored together-*日志／旧build artefacts属草稿，已重配CMake／刷新编译数据库并定向构建主线facade/runtime，实查82ops／44注册且无together；8恢复及相邻C++路径fresh全server clean，14应用对象6963实际deps禁入0。仅恢复构建／检查，未重跑Qt测试，不凭草稿缓存45suite／91ops误报。新范围本地提交、不push。

**整歌单收藏当前停止点**：[v2原生前置调查](api/playlist-subscription-v2-prerequisite.md)已完整复核固定wrapper／registrar／options／request和home／TS／config。body.checkToken与新鲜SDK header token来源不同，Eapi同时用加密header和Cookie携带；home取消2／TS取消0，空token续发不采用。未取得可信原生issuer／Linux SDK／许可／绑定资料，维护者先选提供材料、随后明确暂无资料并选择暂缓推进其他后端；需求保留，需实际可核对原生材料与明确恢复，不继续等待其本轮交件或擅自评估SDK例外；不把未解项注册为可用入口。公共GitHub固定源码fetch被198.18.0.0/15内部地址检查拒绝，未绕过／取得相关代码，不代表服务拒绝或原生不可能。此次只有静态／公共资料调查和文档，不新增C++／operation／CTest／build／LSP／Qt测试或实网证据，调查时点仍82／44；新每日歌单证据独立于该调查。上一段恢复构建／8fresh与Together草稿数据均独立历史，WIP不自动恢复。TC-PLAYLIST-SUB-V2仅待实现清单，风险R-028／Q-013保持未解。

[B-010 子任务：扫码轮询与总时限](tasks/B-010-qr-polling.md)已实现并离线验收，可作为任务契约写法的范例。

[XEAPI 编码与公钥状态](tasks/B-002-xeapi.md)已实现。[存储基础与设备 ID](tasks/B-008-storage-foundation.md)已实现。[密钥环与 XEAPI 公钥持久化](tasks/B-010-secret-store.md)已实现。[匿名 bootstrap](api/anonymous-bootstrap.md)与[账号安全持久化／恢复／退出](api/account-session-restore.md)已实现；默认新扫码登录仅本次，记住需显式选择。[后台线程／非阻塞facade与启动／关闭排空](api/backend-runtime.md)也已完成。[搜索／歌曲详情／三语言歌词](api/music-queries-and-lyrics.md)已接入，有少量官方公开只读实测。[官方URL／受控CDN／Qt单曲播放](api/stream-resolution-and-playback.md)已完成基础闭环和静音试听PoC，[队列与六轨歌词时钟](api/queue-and-lyric-clock.md)也已完成，[专辑／艺人／歌单目录](api/catalog-queries.md)也已接入并离线验证，[每日推荐读取／私人FM条目供给](api/discovery-and-fm.md)已接入；[FM播放接线](api/fm-continuous-playback.md)已完成，[个人库四项读取](api/personal-library.md)也已离线接入；[本地观测累计／会话历史](api/local-listening-history.md)已接入；[显式上报能力](api/explicit-listening-reports.md)也已接入；自动触发时机留到播放接线时讨论。[最近听过／排行读取](api/cloud-history-reads.md)已离线接通，[榜单列表／摘要读取](api/chart-reads.md)也已接入并复用现有歌单选歌播放链；[推荐／普通／精品歌单及三类标签](api/playlist-discovery-reads.md)也已成组离线接入；[音乐实体、播客及全站声音搜索](api/entity-search-reads.md)已接入并有限公开验证，[消费详情／节目与4类recent](api/podcast-and-recent-reads.md)已合成接入，公开列表遇-462已停；ProgramId／VoiceId对应和节目详情／真实recent仍待实证，[音源／身份补证](api/spoken-playback-contract.md)已记录传统mainTrackId路线、v1第三方代码候选和独立身份／媒体／历史设计，但实际binding／v1权益仍未实证，没有新增播放入口；[资源评论五类／八operation](api/comment-reads.md)已成组只读接入并合成验证，普通读取不含播客容器，Voice／艺人不强转；[评论五动作写](api/comment-writes.md)后续已获准原生v3并合成接入，历史／通知未接入，实网另授权；[播客订阅／云盘列表与详情](api/account-library-reads.md)三项原账号读取已合成接入，独立CloudSongId与可选catalogReference，不自动音源／播放／写入；[红心／收藏到歌单／专辑／艺人／播客收藏订阅](api/collection-writes.md)五类已成组离线接入，8条在途与写入Unknown、不自动读／重试；整歌单收藏／取消此前因v2 SDK门禁延期，原生调查保留、维护者现明确暂无资料暂缓，仍无绕过入口且不自动恢复；[创建／单独改名／描述写](api/playlist-management-writes.md)已合成接入，共用8槽与账号屏障，候选创建ID缺失／矛盾Unknown、不盲重试／同名对账；[删除／完整范围排序](api/playlist-delete-and-order-writes.md)已合成接入；[标签编辑／隐私歌单单向公开](api/playlist-tags-and-publication-writes.md)已合成接入，原Catalogue token／8槽／Unknown，介绍仍独立修改／清空；标签清空／公开转私密无入口；[封面更新](api/playlist-cover-update.md)已合成接入，有界JPEG重编码、分阶段Unknown、原账号／8槽，无真实文件／上传；格式扩展／裁剪／可靠核验仍另确认，连续[四类资料／介绍](api/catalog-introduction-and-metadata-reads.md)已合成交付，真实schema／复杂百科与UI仍另验收，实网另授权；受限时继续其他已确认小包，不直接当SongId；歌单内搜索／工作台仍待补证，不以本地过滤或管理接口代替；本地保留时长计量，详细历史持久化按分析需求再定；真实报告另行授权；真实认证／密钥环验证另行授权。详细顺序见[路线图](14-roadmap-and-backlog.md)。不要一次把这些全部塞进一个 Manager 或单次提交。

| 待决事项 | 安全默认／询问时机 |
| --- | --- |
| 安全存储实现与新增依赖 | QtKeychain 系统包（`qtkeychain-qt6`）已接入；无后端时只留内存，不明文 fallback |
| app ID、自身许可证、发行 Runtime | 由维护者决定；不阻碍后端单测，不擅自填公司域名或发布许可证 |
| 真实账号与音源验证 | 必须明确授权并安排脱敏流程；不在聊天或仓库提供凭据 |
| v2 checktoken SDK、一起听接收链 | v2原生前置调查保留、维护者明确暂无资料并暂停，不自行等待/装SDK或v3替代；Together后续明确批准核心12 HTTP-only已接，完整NIM/SELFHOST同步G1–G6仍暂停，metadata/Ack非Active，不暗加Node/WebEngine或从HTTP推token/transport |
| 播客／声音后续 | [补证决策](api/spoken-playback-contract.md)为docs-only，旧公开列表-462停止；传统mainTrackId文档路线已获证，v1实网／Voice关系仍未知，不换host／工作台或自动登录；账号／挑战／媒体验证另授权 |
| 播放后端 | Qt Multimedia已明确获准接入；公开试听MP3／合成WAV PoC通过，完整缓冲后起播；流式、设备切换／打包等仍有门禁 |

**发现包后的历史候选（已被B-031设计选择覆盖，两个账号API先不做）**：本包9常规发现已合成接入，complexWiki仍门禁Q-016。见[覆盖对照§7](api/backend-coverage-audit.md)，当前原账号等级/消费音乐收藏数量两只读仍候选未实现，不自动扩大VIP/跨账号资料/MV/creator/权益/cache；FM mode/write仍需原来源/真实time，文件下载/MPRIS/持久化需单独设计、不得默认加依赖。下一包由维护者另选，不自动继续实网或恢复paused。

当前没有指定新的接手人／问题负责人；由维护者分配。详见[风险与决策](15-risks-and-open-questions.md)。

## 6. 交付与接收检查

- [ ] 交付方告知完整提交、分支和传递方式；接收方确认含本页及下一任务文件。
- [ ] 从干净源码配置／构建／测试，而非复用发送者的 build 目录；结果与[验证记录](handoff-verification.md)比较。
- [ ] 接收方找到 service、port、native adapter 及其测试，能说明为何803不是登录完成。
- [ ] 领取单一任务，记录验收项；新依赖／真实访问先获授权。
- [ ] 每个增量按影响范围记录必要编译／测试结果及未复验项目，剩余风险同步更新本页和实现记录；提交、推送、发布分别授权。

不交付：`CMakeUserPresets.json`、Qt Creator 私有配置、Qt 安装目录、编译数据库、构建缓存、本机日志、真实凭据。以上接收勾选项尚待实际接手者执行，不因本机复验而代勾。

### 覆盖盘点交付记录（仅文档，代码基线7b0d762）

97row ledger名称/顺序等于当前enum、24group／38E54W5X与44FR唯一行均offline sanity-check；当前Facade声明/源体与5窄合同已有原入口，不由旧graph/查不到member猜状态。实际header SHA保持42def478…，show-only48／together_http1，固定stash是commit且不更动；8files仅Markdown，source/test/CMake/lock/inventory/entry/QML未改。fixed440（164/40/236）、21固定reference hashes（含home/HTML）mismatch0、Python15 OK、最终worktree＋cached diff0；首次stage发现新Markdown末尾多空行，仅删空行后复核（worktree-only diff本不覆盖untracked），scanner最终合并staged/worktree/untracked保证8files；**867本地reference paths missing0、有限Cookie/signed-query flags0**，anchors未查、非全面安全。结果/ledger/logs为ignored `build/workspace-checks/backend-coverage-*`；原together-http-*和其824refs等历史文件不覆盖。未编译/Qt测试/Release/minQt/fresh LSP diagnostics/actual compileddeps/服务/SDK/keyring/音频/UI，不将旧911＋17等当新回归。task92 unparsed runner输出仍单独未闭；推荐下一包不等于开始实现或恢复暂停。只本地提交，无push/包/发布。

### 常规发现初始包交付记录（331b4fb：新增9读取、复杂百科只审计）

从53f8f3e起，9typed Weapi/default reads→106unique op／49show-only登记；current ledger106名称顺序/25family/38E63W5X/44FR、closed path literal/模板/拼接与六合同源静态对应，非live/schema/参数所有分支证明。原MusicQuery/Service/Facade/read16/token-generation/cancel-close/单worker/Gateway，无新owner/自动下游/entry-QML变化，complexWiki root/blocks/原Song绑定待证不注册。

最终22affected1167（新165）／15selected runtime函数16data/18pass，0fail/skip；final真实build0/warnings0／15changed fresh all-server LSP0／clang-format15／14actual application objects含MOC的6998Ninja deps forbidden0（JSON/keyring/storage也实查0）。fixed440/21referencehash0/Python15，worktree＋cached diff分别核对；最终896本地reference paths missing0/有限Cookie-signed-query flags0、33changed files（包含staged/worktree/untracked范围）单独留档discovery-browse-*；不是anchors/全安全。首fixture漏直接include/append overload及MOC新增/改名过期如实保留，真实include/std::pair/AUTOGEN/compileDB修复，Q_OBJECT/.moc/坏schema/fatal不删不压；domain位域规则误报经GCC/LSP/实际compile核实false-positive；旧#92 parse仍pending。本包不累加first123/newthread3或旧867refs/911＋17，非全49/Release/minQt/服务/账号/SDK/keyring/audio/UI/权益验证。锁/inventory/paused/APK禁读/固定stash原样，本地commit/no push/无自动bundle或发布。

### 公开发现窗口与映射修正交付记录

[独立证据](api/discovery-public-response-verification.md)：2026-10-08一次native metadata窗口11请求／SimilarArtists业务301即停，SimilarPlaylists未发，无重试/登录/SDK/音频/写/路由TLS覆盖；complexWiki data.blocks6只部分形状、绑定／块类仍G。修正month-only optional分组与5相似歌超limit2的100有界未知分页批，106ops/49登记不增；旧331b4fb的165/1167与896refs/33files历史不回填。

本轮百科子集收尾：基于6f63c24，107 unique enum/ordered ledger、26family合计107/39E63W5X/44FR、50 registrations；fixed440/21公共reference hash与Python15通过、固定checkout clean，stash9907ac4a9ad83b9cefb79fea9d3337b56b91ba6f仍为原commit。最终33files（13C++/2CMake/18Markdown），725本地Markdown reference paths/missing0、有限Cookie-signed-query flags0，排除inline/fenced code、非anchors/全面安全；独立脚本与结果wiki-text-doc-check.json，worktree/staged diff分别核对0。百科88/23affected1278＋selected runtime19、18fresh LSP0/13format0/14实应用对象含MOC7004逐对象-602唯一deps禁入0、UI0；失败与纠因/MOC/真实compile DB均按新契约保留，#92独立未闭。无后实现实网、全50/Release/minQt/服务/账号/媒体/SDK/keyring/UI/权益验收；只本地commit/no push/publication/bundle，原paused/APK禁读/唯一媒体owner不变。

上包最终discovery188／22affected1190＋15selected runtime18，0fail/skip；5fresh all-server LSP0／format5，真实Debug build0/warnings0，14实际application objects含MOC、6998逐对象/601唯一Ninja依赖禁入0，entry-QML不变。运行函数名误写的exit1未执行／未计，按真实-functions更正重跑0，保失败日志／负例／fatal/MOC，不压diagnostic。fixed440/21referencehash0/Python15、106ledger-25family-38E63W5X-44FR/49registration核对，最终22files（5C++／17Markdown，含新证据页），919本地reference paths missing0／有限Cookie-signed-query flags0、worktree及staged diff分别0；anchors未查／非全面安全，单独保discovery-public-doc-check.json。ignored discovery-public-*只保安全结构／计数／状态，不保原服务值/全文/Cookie/签名URL；DNS-only误阻断已明确更正，无实际拒绝绕过，修后未重发实网。#92仍独立待办，其他paused/APK禁读/stash原样，无完整49/Release/minQt/账号/SDK/keyring/audio/UI/权益验收，本地commit/no push/不自动发布。
