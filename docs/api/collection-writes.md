# 收藏到歌单、红心与收藏／订阅写入

## 1. 本包范围、术语与门禁

维护者确认：**歌曲加入指定歌单沿用官方“收藏到歌单”文案**；红心是另一种方式，不把二者混成同一操作。单曲从歌单移除不删除歌曲本身；收藏整个歌单也不是编辑其歌曲。

本包时点接入五类十种正反动作：红心／取消红心、收藏歌曲到指定歌单／从该歌单移除、收藏／取消收藏专辑、收藏／取消收藏艺人、订阅／退订消费端传统播客。只有离线合成验证，**没有真实账号或服务端写入验收**。

完整核对固定 `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `like.js`、`playlist_tracks.js`、`album_sub.js`、`artist_sub.js`、`dj_sub.js` 与对应home.md章节／TS签名，完整options、request和config；也核对 `playlist_subscribe.js`、`register_checktoken_v2.js`、`like_v1.js`、`song_like.js` 的不同门禁／路径。TS generic Response不是完整响应schema；参考不是官方文档或实网支持证明，440模块及164／40／236审计保持历史，不回写为运行注册表。

**本包时点整歌单收藏／取消收藏明确延期，维护者先完成其余五类；接续搜索辅助包acf822e，维护者现重新明确要求补齐，两动作已排回后端待办。一起听随后经维护者明确暂停至最后，现优先本项原生v2调查。** 需求恢复不等于前置链已实现／SDK执行、新运行依赖或真实写入已获准；优先调查C++原生v2等价链可行性，未证前不假注册可用入口。 固定playlist_subscribe包装器两种动作均强制query.checkToken=v2，request每次先调用Watchman SDK，再把token置入eapi header；收藏动作另有body.checkToken的严格数字t分支。现有原生后端没有该SDK能力／许可和环境验收。本包未开放对应入口、注册对应operation、接收外部token、复制固定config token或空token假成功，不改为Weapi／旧接口、v3、Node／WebEngine／转发服务绕过。这是固定参考链的门禁，尚不是已证官方服务要求。最新[原生v2前置调查](playlist-subscription-v2-prerequisite.md)核实body/header token来源分离、Eapi加密header及Cookie双携带、home取消2与TS取消0、空token续发问题；原生issuer／许可／绑定资料未闭合，需要材料或明确架构决定，未新增入口或operation。

旧红心like是Weapi `/api/radio/like`，其radio路径不等于私人FM反馈；新版like_v1是XEAPI `/api/v1/radio/like`加v3门禁，另song_like是带userid的不同默认接口，**都不作fallback／双发**。没有FM垃圾桶、评论点赞／写、账号评论历史／通知、歌单创建／删除／命名／排序、云盘上传／删除、声音播放或UI。

本轮没有读取真实记忆凭据／密钥环、登录真实账号、请求业务服务／音源／头像／私人文件、执行SDK或继续此前−462窗口。只有fake HTTP／fake secrets和临时安装SQL，产品入口仍模板。

## 2. Core接口与有界输入

`domain/music/collection_types.h`：

- `CollectionMutation{eventId, CollectionChange}`；非空UUID由调用方显式提供，仅本地关联，不是服务端幂等键。
- 该增量时点`CollectionChange`是独立SongHeart、PlaylistCollection、AlbumCollection、ArtistCollection、PodcastSubscription五variant。各自使用SongId／PlaylistId／AlbumId／ArtistId／PodcastId，不接受ProgramId、VoiceId、CloudSongId、任意resourceType／URL／JSON或uid。类型独立不证明服务端数字值一定不同。
- `SongHeart`明确bool liked，false保留；其他四项使用仅Collect／Remove有效的枚举，不采用上游“任意其他t→取消”或JS真假值默认。
- 所有ID为规范正signed64十进制文本。PlaylistCollection显式playlist与1..100个唯一SongId，保持次序／wide文本；空、重复、非规范／溢出ID或未知action拒绝，不静默去重、全歌单抓取或自动拆批。100是Cirrus预算，不是已验证服务上限。

`BackendFacade::mutateCollection(mutation)`捕获当前已验证accountId／token／generation；未Ready、Guest、Committing、无效输入、8槽满或同eventId在途返回0，已知该次未派发且无完成信号。受理非零ID后通过 `collectionCompleted(id, originalToken, CollectionResult)`完成，取消用cancelCollection。播放／读取／登录流程不自动调用它。

独立Core port `ICollectionGateway`，不是MusicQuery／MusicResult读变体或通用Manager。后台复用SessionCoordinator／SessionApiClient／现有线程；gateway再次检查Authenticated、原token、规范原账号相等，当前token＋异账号→NotSent／InvalidRequest；旧token→NotSent／StaleContext；8槽与在途eventId重复再次拒绝。Native client要求非空MUSIC_U，匿名MUSIC_A不足；响应Set-Cookie不采用，不登录／退出／更新记忆。

仅凭歌单详情／creator／公开元数据不能证明可编辑权限，本包不接收伪可用的isOwner／editable开关，不替服务端承诺权限；调用方需明确选择目标，服务端仍可拒绝。历史DTO副本／列表不是实时权限证明，UI权限展示另验收。Voice／workbench容器不代替传统PodcastId。

## 3. 封闭路径、载荷与协议

所有目标固定Default官方目的地，响应JSON／64KiB；没有任意host、crypto、profile、csrf、e_r、header、token覆盖。新增**8 operation，registry54→62**。

| operation | 官方逻辑路径 | 固定业务payload | crypto |
| --- | --- | --- | --- |
| SongHeart | `/api/radio/like` | trackId文本、like严格bool、alg:"itembased"、time:"3" | 显式Weapi |
| PlaylistCollectSongs | `/api/playlist/manipulate/tracks` | pid文本、op:"add"或"del"、trackIds字符串数组的紧凑JSON文本、imme:**文本"true"** | 默认encrypt:true→Eapi |
| AlbumSubscribe／AlbumUnsubscribe | `/api/album/sub`／`/api/album/unsub` | id文本 | 显式Weapi |
| ArtistSubscribe／ArtistUnsubscribe | `/api/artist/sub`／`/api/artist/unsub` | artistId文本、artistIds:"[<规范id>]" | 显式Weapi |
| PodcastSubscribe／PodcastUnsubscribe | `/api/djradio/sub`／`/api/djradio/unsub` | id文本（wrapper rid） | 显式Weapi |

逻辑路径不是包装服务的 `/like`、`/playlist/tracks`、`/album/sub`等路由。歌单歌曲Eapi保持原Pc envelope／e_r:false，Weapi保持原浏览器／csrf链；不改变既有profile、响应加密或域。artistIds按固定源码为含**数字token的JSON文本**，直接拼接已校验id，不把wide ID转C++ double／JS Number，也不冒充引用提供的是字符串数组；宽值服务兼容性未实证。

内部adapter参数仅id／liked、或pid／op／ids，严格字段数与JSON类型；其他值固定生成，拒绝多余和覆写键，包括uid、crypto、checkToken、timestamp、alg、time、imme、header、e_r。eventId／accountId不放入业务payload。

home.md对playlist/tracks另有包装请求timestamp警告；wrapper与TS没有将timestamp写入该逻辑payload，参考server还带2分钟包装响应cache。原生不走该服务／cache，每次Eapi有既有新requestId；**不据此证明警告已满足或实网写入成功，也不猜测添加业务timestamp字段**。新的真实验证需确认原生请求／当前服务兼容性，不能以此重放或绕过拒绝。

固定playlist_tracks遇512会复制trackIds再发一次，catch还包装HTTP200。本包**不复制这个重试／重复ID／总成功包装**：每次显式调用只发一次，512／−462／403等保留拒绝，不代替用户换协议、扩大请求或隐式下游读。

## 4. 写结果、取消与不确定性

该包`CollectionResult`含eventId、outcome、error、可选providerCode／retryAfterMs，没有raw JSON／provider msg、Cookie／URL／权限数据。后续[歌单基础写](playlist-management-writes.md)新增三个typed variant及创建专属可选createdPlaylist，其他动作／非Ack／失效不发布该ID；旧五类语义不变。

| outcome | 语义 |
| --- | --- |
| NotSent | 本地／账号／Cookie／参数／容量等检查已知未交给transport，或native发送前取消 |
| Acknowledged | 原账号／上下文下合法解码得到业务码200；只确认响应，不证明库已可见、整批原子提交／逐项成功、权限或所有客户端一致 |
| Rejected | 收到合法非200业务码；不保证服务端绝无部分副作用 |
| Unknown | 交给transport后网络／HTTP／超时／解码失败／取消／失效，或调用线程无法确定发送阶段；不能当成成功或“已撤销” |

合法code沿用既有有界int解码，JSON整数及十进制字符串可解析；bool、科学记数文本、缺码、坏JSON／root不是200成功，HTTP200也不是成功。HTTP429通常Unknown／RateLimited，业务429为Rejected／RateLimited；合法Retry-After0..86400整数秒转ms，仅信息，不安排重试。

`submittedToTransport`是“可能已发出”的保守边界，不是服务端收件证明。Facade对已受理的取消、close、非Poll账号意图、同token的账号／认证态变化统一Unknown；原token／eventId保留，完成为异步，晚到Ack不会复活／归给新账号。后台能确认尚未发送时才NotSent。client丢失保守Unknown，随后新请求NotSent／Network。

同UUID在途去重只是本地容量保护，完成后相同UUID再次提交允许；跨进程／设备没有exactly-once保证。不同UUID同时改同资源没有服务端顺序／最终状态保证，不用最后完成的响应覆盖当前库。Batch没有逐项结果契约，不猜测部分失败列表，不靠“目标状态似乎幂等”盲目重试。

没有乐观库状态、缓存／outbox／SQL持久化、自动对账、回滚远端、重试、SDK／登录、读刷新、排队／播放／报告。需要查看当前结果时由调用方**另发显式有界读取**（喜欢IDs、账号收藏列表、播客订阅、指定歌单详情／歌曲页）；它也不能证明全批次原子效果或跨页一致性。已交付旧DTO副本不会自动擦除，消费者清理仍按原账号策略。close先销毁CollectionGateway再client/session，线程及资源真实排空不等于撤销远端写。

## 5. 离线验证与交接

该收藏包时点Qt6.11.2 Debug，`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`。七个受影响套件：collections189、listening_report18、library21、account_library49、music26、session_api_client24、backend_facade8，**335 pass**；另collections／accountLibrary／library／listeningReport／playback（queue/FM）／destruction七条线程数据行，**9 pass**（含init/cleanup），无fail/skip。登记36→37套件，不是全37／Release／真实账号／SDK／UI验收。

新suite包括10种正反动作的独立路径／固定payload／响应限额断言、20个native显式写请求及原Cookie不轮换；歌单歌曲实际Eapi wire解密并核对原Pc e_r:false，其他Weapi核对实际固定路由／form，payload另有独立固定期望；不声称独立解密每条Weapi业务payload，原密码向量仍是既有证据。140行响应矩阵覆盖Ack／validated字符串码／201／512／−462／403、坏码／JSON／root、HTTP与业务429、502无重试；10组Guest／MUSIC_A不足／异账号／旧token、10组发送前后cancel／失效，100项／重复／wide／多余字段、8槽／在途UUID、client销毁、同token Guest／Committing／异账号、晚到／伪generation、无自动读／播放／报告。

线程新用例使用fake secrets／完整合成登录，五类十种动作、原token／eventId和调用线程返回、512不重试、挂起订阅close为Unknown并排空；无真实业务／凭据／媒体。既有报告与Queue/FM回归不因新收藏而自动发写。

首轮166 pass／11 fail不是交付结果：10行误把既有parseBusinessCode允许的"200"字符串当坏schema，1行Crypto测试复用ApiResult遗留Timeout。读完整解码器／classifier后只修测试：显式字符串Ack，保留"2e2"坏码，Crypto样本清除旧transport状态；**生产协议／分类器不改、不削弱坏schema测试**。其后加client销毁／同token屏障，最终189。本机clangd旧enum preamble与未生成moc经实际构建／DB刷新和active复验解决；域内scoped enum／inline方法bitfield提示为已核实启发式误报，不删includes／Q_OBJECT／moc、不加suppression。

13个修改C++路径fresh active LSP clean，另Runtime公开头、FM供给／ListeningSession邻接头复验clean。实际Ninja检查14个application对象／6924 dependency entries，无基础设施／JSON／Network／Multimedia／Sql／Quick／Qml／Gui／OpenSSL穿透；UI差异0。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_collections tst_listening_report tst_library \
  tst_account_library tst_music tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(collections|listening_report|library|account_library|music|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  collectionsThroughThread accountLibraryThroughThread libraryThroughThread listeningReportThroughThread \
  playbackThroughThread destructionStillDrains
```

clang-format dry-run、git diff --check及固定440模块hash校验通过；545个本地Markdown文件引用有效（排除fenced／inline code，不验证外链／anchor）。live-looking Cookie模式扫描0 findings，仅有限模式检查而非全面秘密保证。

ignored日志 `build/workspace-checks/collections-final-{build,focused,thread}.log`及collections-{boundaries,format,diff-check,fixed-hash,links}.log，首轮／修复／中间结果单列、不累加；本机cache／凭据不交付，只本地提交、不推送。整歌单收藏checkToken-v2、真实服务兼容／权限／副作用／timestamp行为、自动／明确对账流程、歌单排序、库同步及UI仍未交付。后续[创建／单独改名／描述写](playlist-management-writes.md)已合成接入，候选创建身份缺失／矛盾仍Unknown，不以同名猜ID；共享原8槽，不扩大到真实写入或绕过门禁，可靠创建核验仍未交付。后续[封面更新](playlist-cover-update.md)也共享8槽／原账号／Unknown，显式JPEG字节／获准Gui仅基础设施，分配-NOS-提交及阶段值，不自动读／重试；本页335＋9仍原收藏包时点，不代表真实上传或全量。
