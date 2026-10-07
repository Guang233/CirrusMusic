# 每日推荐历史：可用日期与显式日期歌曲读取

> 仅离线后端交付，不是官方兼容性／会员权益／UI验收。维护者在播客／声音阶段1之后明确选择先做本项；整歌单收藏v2、一起听、云盘后续及播客／声音下一阶段均暂缓。业务registry83→85，CTest登记46→47；原始440模块审计不变。没有新库、账号／keyring／官方业务／音源／音频访问、SDK／JS执行或反馈写入。

## 1. 固定源与证据等级

固定 `a8c781fd64faab17fedfd46e0615a2609307f163`／v4.40.1，完整核对 `module/history_recommend_songs.js`、`history_recommend_songs_detail.js`、TS对应签名、`public/docs/home.md`2419–2438、`util/option.js`／`config.json`与`request.js`请求链。

- home实际使用斜线路由 `/history/recommend/songs` 与 `/history/recommend/songs/detail`，不是module的underscore名称。完整章节明确详情date**必选、来自可用日期接口、不能任意日期**；示例2020-06-21。TS却optional，wrapper用`query.date || ''`：不照搬空值／任意日期发送。
- 两wrapper明确Weapi，recent业务空对象，detail只有date。createOption的任意crypto／cookie／host／headers／proxy／checkToken覆盖不暴露到Core；request的Weapi既有CSRF／browser UA／music.163.com／`/weapi/`路由链沿用，e_r:false。没有v2/v3前置、SDK、XEAPI或旧协议／其他host fallback。
- **home／TS／wrapper均未定义响应schema。** 本包`root.data.dates`（字符串数组）与`root.data.songs`（Song数组）是严格、未实证的合成候选，不是官方文档或真实样本。公开搜索生成回答对detail的dailySongs/songs说法冲突，不据此放行别名／fallback；没有获取第三方音乐API响应。未知schema失败关闭，不能将空结果伪装成欠权益或反之。
- 本轮先前的FeelUOwn公开源码候选用于播客补证，fetch拒绝github.com内部地址198.18.0.232；未取得源码／绕过保护，不当网易服务拒绝或本项schema证据。播客G1/G2未闭，不因切换本项标完成。

MIT归属继续见[NOTICE](../../third_party/api-enhanced/NOTICE.md)。固定锁不升级，源码可读不授予音乐／服务／商标或远程SDK权利。

## 2. Core契约与日期选择

`src/domain/music/recommendation_history_types.h`只依赖Core／标准库：

| 值／入口 | 契约 |
| --- | --- |
| DailyHistoryDatesQuery | 当前已验证accountId，canonical正signed64；账号仅本地屏障，不作为payload uid |
| DailyHistoryDates | 原query＋dates，最多366个**本地预算**；保留顺序与重复，显式空成功。不是366天服务窗口／完整历史／可永久使用权限 |
| validRecommendationDate | 恰好ASCII四位年YYYY-MM-DD，经QDate确认有效calendar且往返文本一致；不trim、归一化、时区换算、本机今天补值或接epoch |
| DailyHistorySongsQuery::fromDates | 只能从日期DTO显式选一个成员，验证全列表、account、有效原SessionToken及本地预算；private构造保存原account/token/date，不保存整份列表 |
| DailyHistorySongs | 原选择query＋最多500首Song元数据，保序／重复／宽ID；不是playlist、音源或播放／下载权证明 |
| BackendFacade::dailyRecommendationHistoryDates() | 绑定当前account，不接caller uid/date/limit/offset/afresh；未Ready／Guest／Committing／无效账号／满16返回0 |
| BackendFacade::dailyRecommendationHistorySongs(query) | 原选择account与sourceContext必须仍匹配当前快照，然后走原read16；没有自由QString日期入口、自动列表读取／日期选择／详情跟进 |

factory是可信内部DTO的**结构前置条件**，不是服务签名／不可伪造receipt／权限证明。调用者必须使用日期结果与其完成信号的原token，不能拿后来token重新标旧结果；外部QML不得构造伪DTO。日期result自身不持token，本地typed result与完成信号成对使用。相同token下账号／认证态变化由既有双层私有读屏障处理；sourceContext额外防止同账号的新epoch重用旧选择。

Service验证原token、Authenticated、query账户与当前账户，保存原authenticatedAccount；finish与session通知重核。详情的sourceContext须等于本次request token；Gateway直接调用也重核，不在错误context发业务。取消／登出／换号／close、client丢失保留原token，晚到／重复不再发新结果；公共查询不因私有scope变化被无差别取消。没有新factory装配对象、播放器owner或cache。

## 3. 两个封闭原生operation

| operation | logical path／固定Weapi wire | 业务payload | 解码预算 |
| --- | --- | --- | --- |
| DailyHistoryDates | `/api/discovery/recommend/songs/history/recent` → `/weapi/discovery/recommend/songs/history/recent` | `{}` | 1MiB |
| DailyHistorySongs | `/api/discovery/recommend/songs/history/detail` → `/weapi/discovery/recommend/songs/history/detail` | `{date:原选择标签}` | 4MiB |

JSON响应、官方music.163.com、原MUSIC_U必需；MUSIC_A不能替代。detail native payload层只校验exact一项字符串／date语法，**成员与原context由Core选择／Service／Gateway负责**，不是凭native date字符串证明来源。其他date/uid/limit/offset/afresh/header/crypto/checkToken/host/e_r等多余参数拒绝。读取响应不采纳Set-Cookie、不创建认证receipt、不刷新账号记录。

沿用MusicFailure：Guest应用门禁AuthenticationRequired；已认证但缺原MUSIC_U时SessionApiClient为ApiError::Cookie，既有MusicGateway映为ProtocolFailure，**不是服务端欠会员／无版权判定**。业务403／500／-462保留ProviderRejected与code；HTTP或业务429是RateLimited，可解析Retry-After0..86400秒→ms；网络／坏JSON／schema／取消／旧scope保持原族。无重试、自动登录、删除凭据、空成功fallback或乐观刷新。服务权益／日期window／历史保留规则未知。

## 4. 严格候选映射与显示边界

`recommendation_history_mapper.{h,cpp}`用于既有MusicGateway／mapMusicResponse，必须numeric code200、object data、显式目标数组。

- 日期只接受有效字符串calendar；missing/null/object/数字/非法日期／超过366整体InvalidResponse；explicit[]成功。没有把status、purchaseUrl、noHistoryMessage解释成权益或自动打开链接，也不生成今天／最早／最近／分页／total。
- 歌曲只接受data.songs数组≤500，逐项必须object、共享songFrom合法。沿用safe numeric≤2^53−1或canonical正signed64字符串、两套真实歌曲字段家族、optional metadata／纯文本／display cover安全策略；重复及服务顺序保留。缺数组／dailySongs别名／root.songs／坏ID／超限／非法文字／duration整体拒绝，不丢行、去重或截断。
- 本地result的query.date只是关联原请求选择，**不是验证服务返回属于该日期、推荐生成日／时区／完整性**。没有根据返回datetime猜日期或制造echo字段。
- Song为歌曲语义，历史推荐不是收听记录／ObservedDuration，不混入cloud recent、play counts、缓存／history或FM游标；不制造PlaylistId／报表来源。
- 所有名字／alias／理由等按已有纯文本策略；不是HTML、自动链接／图片或生成叙事。无自动SongDetails／歌词／图／音源／排队／play／report／收藏／日推反馈写。

## 5. 最终离线证据

Qt6.11.2本机Debug、LC_ALL=C.UTF-8、QT_FATAL_WARNINGS=1；仅下列受影响目标，不是全47／Release／实网／音频／真实keyring／SDK／UI矩阵：

| 套件 | pass |
| --- | ---: |
| recommendation_history | 86 |
| daily_playlists | 62 |
| playlist_discovery | 54 |
| discovery | 17 |
| library | 21 |
| account_library | 49 |
| cloud_history | 28 |
| podcast_reads | 55 |
| music | 26 |
| personal_fm | 11 |
| session_api_client | 24 |
| backend_facade | 8 |
| entity_search | 44 |
| search_assist | 117 |
| comments | 149 |
| catalog | 22 |
| catalog_metadata | 66 |
| **17套件合计** | **839** |
| backend_runtime：13条数据行＋init/cleanup | **15** |

零fail／skip。新86含init/cleanup2、contract-selection1、badSchema45、native-gates1、rejection18、cancellation14、clientLoss4、bounds-facade1。检验空与缺失、闰年／非法／任意／旧token日期、366/367、500/501、1/4MiB、宽重复ID／顺序／纯文本、两模式原MUSIC_U／Set-Cookie不采纳、Cookie前置0send、cross-account/context、Guest／原账号通知变化／close、shared16与直接Gateway Busy、queued/transport cancel-loss、429及拒绝；实际form／官方URL与操作payload断言，**未独立解密每个Weapi密文**，既有crypto向量另计。

线程用例recommendationHistoryThroughThread：Guest拒绝0API→合成login→日期→显式成员歌曲→歌曲empty→日期empty→dailySongs别名InvalidResponse→挂起详情closeCanceled；9 API requests／6 music results，原account/token/date／caller-thread delivery，play/report/collection/comment信号0／queue空、wrongThread0、created=destroyed、真正drain。另外选dailyPlaylists/discovery/playlistDiscovery/library/accountLibrary/cloudHistory/podcastReads/musicQueries/searchAssist/playback/destructionStillDrains；不自动跟进日期或真实媒体。

16路径（14changed C++＋2adjacent）fresh all-server LSP零diagnostic，actual14个application对象／6985条Ninja依赖，禁入JSON／Network／Gui／SQL／Multimedia／QML／crypto／infrastructure为0，UI delta0；不是仅CMake链接证明。CMake语言工具unavailable不算clean，实际configure/build通过、compile DB刷新。额外session缓存的cpp-check封装在backend_facade.cpp与music_query_service.cpp报exit1/output-unparsed（resolved cwd为src），不是可解释的代码诊断，未作为clean证据或压制；两文件再次主动LSP各0、按当前实际compile DB的GCC -fsyntax-only各exit0/无诊断，实际构建／测试也通过。该封装输出解析仍未证成功，不能把整体缓存称全clean。

首次构建无错误；首次suite85pass/1fail：nativeAndGates把已认证缺Cookie错误当AuthenticationRequired，核对原Cookie→ProtocolFailure家族后只修精确expected，Guest仍AuthenticationRequired、0send等保留，最终86。fixture日期raw JSON触发octal AST误读，改显式QJson构造保留原ISO标签，主动fresh消解；重复partial edit未套整份重放、只补唯一定位，最终enum三处switch齐全。没有压制fatal／规则、删负例／Q_OBJECT／moc或改生产错误族。

```bash
cmake --build --preset linux-dev-local --target tst_recommendation_history tst_daily_playlists \
  tst_playlist_discovery tst_discovery tst_library tst_account_library tst_cloud_history \
  tst_podcast_reads tst_music tst_personal_fm tst_session_api_client tst_backend_facade \
  tst_entity_search tst_search_assist tst_comments tst_catalog tst_catalog_metadata \
  tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(recommendation_history|daily_playlists|playlist_discovery|discovery|library|account_library|cloud_history|podcast_reads|music|personal_fm|session_api_client|backend_facade|entity_search|search_assist|comments|catalog|catalog_metadata)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  recommendationHistoryThroughThread dailyPlaylistsThroughThread discoveryThroughThread \
  playlistDiscoveryThroughThread libraryThroughThread accountLibraryThroughThread \
  cloudHistoryThroughThread podcastReadsThroughThread musicQueriesThroughThread \
  searchAssistThroughThread playbackThroughThread destructionStillDrains
```

忽略日志build/workspace-checks/recommendation-history-{final-build,final-focused,final-thread,boundaries}为本包最终证据；first/fix保留过程，不累加复跑。固定440审计／公共hash与Python15通过，format／diff-check通过；788个本地路径引用missing0（不验anchors），变更文件Cookie literal／signed-query有限扫描flags0、非全面安全审计。85ops／47实际登记/noTogether，固定WIP对象仍commit，源未在最终测试后变化。

## 6. 未完成与暂停项

日期列表／详情真实root与字段、可用窗口／会员限制／频率／拒绝、歌曲所属日期／完整性及UI仍未验收；候选不能宣称所有业务API完成。日推不感兴趣、FM不喜欢等写与账号评论历史未在本包加入；是否接新小包另定。没有日期cache／sync／persistent历史／自动report或文件下载后端。

前三项继续暂停；播客／声音G1/G2下一阶段亦经本轮选择暂停，阶段1保持，仅结构及原歌曲owner回归，不恢复-462／SDK／未知consumer端点。一起听固定WIP `9907ac4a9ad83b9cefb79fea9d3337b56b91ba6f`不pop/drop。云盘已有read保留，未来下载复用实际可听来源／原TTL／preview限制，不另查下载资格／专用音源。UI最后，未来真实服务、账号、写或音频窗口各自再批准。仅本地提交，不push／发布／bundle。
