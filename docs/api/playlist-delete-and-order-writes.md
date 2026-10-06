# 歌单删除与两类完整范围排序

## 1. 本包与固定证据

维护者确认连续任务第一包：删除自建歌单、调整歌单歌曲顺序、调整当前账号歌单列表顺序。仅离线合成接入，无真实账号／写入／当前服务权限或效果验证；第二包[歌曲／歌单／艺人／专辑资料与介绍](catalog-introduction-and-metadata-reads.md)已另行合成接入（本页验证仍为第一包时点），UI最后。

完整核对固定 `a8c781fd64faab17fedfd46e0615a2609307f163` 的playlist_delete／song_order_update／playlist_order_update wrappers、home.md与TS，复用此前完整核对的options／request／config：删除与账号顺序显式Weapi，歌曲顺序默认Eapi，没有固定SDK启用。TS generic Response／路径存在不证明当前服务支持、权限、排序范围／原子性。原440／164／40／236静态审计不改为实时支持表。整歌单收藏v2门禁仍延期，不执行SDK、换协议或注入token。

## 2. Core动作、范围与原上下文

新 `playlist_order_types.h`：PlaylistDelete、PlaylistSongOrder、AccountPlaylistOrder。在原CollectionMutation封闭variant／ICollectionGateway／8槽链增加3类（合计11类），无新Manager、读变体、线程或运行依赖。

- PlaylistDelete只接收一个规范PlaylistId，由显式mutateCollection触发；不自动删除／批删、从收藏列表移除或清空歌曲，不推断isOwner权限。删除后不改本地库，不把Ack当所有客户端已消失。UI确认另验收。
- PlaylistSongOrder只能经 `fromDetails(originalToken, PlaylistDetails, orderedIds)` 工厂创建：源有规范playlist ID、trackIds、显式trackIdsComplete且trackCount等于全部索引长度；原集合与目标集合均1..1000唯一规范SongId且是完全置换。只拿已加载songs或PlaylistSongsPage、缺／未知count、缺索引、截断、重复来源／目标、遗漏／新增ID、过预算拒绝；不静默去重或发送当前页。不需要加载所有歌曲详情。
- AccountPlaylistOrder只能经 `fromPage(originalToken, MyPlaylistsPage, orderedIds)` 工厂创建：原查询账号规范／分页有效、offset=0、rows≤limit、明确hasMore=false及total恰等于rows；原集合与目标集合均1..100唯一规范PlaylistId且完全置换。未知total／more、未完页、多页拼接、重复／缺／新增ID、过预算拒绝；不自动抓全库或拼不同时间页。

1000／100是Cirrus预算，不是服务端最大值。缺完整性证据时能力受限，不能用布尔“用户确认全量”跳过工厂。工厂是结构验证，不是密码学来源／权限证明；普通读DTO仍可被调用者自行构造。只把剥离后的ID／原token／账号放入写邮箱，不带完整介绍／封面／曲目DTO。

排序来源token必须等于当前已验证写token，账号排序来源accountId也必须等于Facade捕获账号；跨登录／换号需另显式读取，不能重新标记旧结果。Facade不受理错来源（返回0）；gateway错来源token为NotSent／StaleContext、错账号为NotSent／InvalidRequest。读取完整性不证明实时库未改变／可编辑权；源updateTime不是已确认服务CAS，不添加timestamp／版本条件或声称无并发覆盖。服务端部分／完整顺序语义仍未实证。

## 3. 封闭协议与单次发送

新增3operation，registry65→68；Default官方目标、JSON响应64KiB、MUSIC_U、Set-Cookie忽略。内部参数只id，或pid／ids，或ids；ID为规范正signed64文本数组（不是caller任意JSON字符串），拒绝数值ID／额外op、imme、uid、SDK、timestamp、header、crypto等覆盖。

| operation | 官方逻辑路径 | 固定wire payload | crypto |
| --- | --- | --- | --- |
| PlaylistDelete | `/api/playlist/remove` | ids为`[<规范ID>]`数字JSON文本 | Weapi |
| PlaylistSongOrder | `/api/playlist/manipulate/tracks` | pid文本、trackIds数字JSON文本、固定op:"update"；无imme | Eapi |
| AccountPlaylistOrder | `/api/playlist/order/update` | ids数字JSON文本 | Weapi |

参考文档示例／删除wrapper是数字JSON文本；直接拼接已校验规范数字token，不转double，也不声称字符串数组是相同wire。wide值当前服务兼容性未验证。排序与收藏到歌单add／del共享路径但有独立operation／载荷，不接受自由op、借删除／排序路径绕开上层范围。Eapi保留Pc header／e_r:false，Weapi保留原浏览器／csrf。包装路由 `/playlist/delete`、`/song/order/update`不是官方逻辑路径。

沿用NotSent／Acknowledged／Rejected／Unknown四结果、原账号／token／generation／UUID在途去重及共享8槽。每次显式动作只发一次，不复制512重复IDs重试、batch、HTTP200错误包装或隐式刷新。Ack只确认合法码200响应，不证明删除／排序最终效果、全部原子更新、权限或强一致性；Rejected也不证明没有部分效果。发送后取消／失效／close不能推断远端撤销；Unknown不盲重试，429仅附合法Retry-After。三项不携带createdPlaylist；没有乐观库状态、SQL／outbox、自动读取／播放／报告或可靠排序对账协议。

## 4. 合成验证与交接

Qt6.11.2 Debug，LC_ALL=C.UTF-8／QT_FATAL_WARNINGS=1。最新八套件：playlist_order59、playlist_writes122、collections189、catalog22、library21、listening_report18、session_api_client24、backend_facade8，**463 pass**；选定8条线程数据行**10 pass**（含init/cleanup），0fail/skip。CTest38→39，非全39／Release／真实账号／SDK／UI验收，旧404＋9不累加。

新suite覆盖完整源工厂／置换／未知／重复／分页／1000与100预算、3固定路径／payload／crypto／限额、6次native显式写与Cookie不轮换、歌曲排序实际Eapi解密、Weapi路由／form与独立payload期望、42行响应矩阵、Guest／MUSIC_A／异账号／旧源token、容量／UUID、发送前后cancel／原token及Unknown／晚到结果。Weapi不声称每次独立解密业务body，原密码向量仍是既有证据。

线程用例经完整合成登录／生产mapper的两次**显式**目录／账号首且末页读取→工厂→三个显式写，原token／eventId／调用线程验证，删除403保留Rejected；无隐式刷新／播放／报告，挂起排序close为Unknown且资源真实排空。完整性结构候选不是实网或权限证明。

9个改动C++ fresh active LSP确认clean；真实Ninja 14 application对象／6929 dependency entries，无基础设施／JSON／非Core Qt／OpenSSL穿透，UI0。新枚举初期遗漏已补齐并真实构建通过；旧preamble诊断经active复查确认过期，无删除必要include／Q_OBJECT／moc或源码suppression。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_playlist_order tst_playlist_writes tst_collections \
  tst_catalog tst_library tst_listening_report tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(playlist_order|playlist_writes|collections|catalog|library|listening_report|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playlistOrderThroughThread playlistWritesThroughThread collectionsThroughThread libraryThroughThread \
  listeningReportThroughThread playbackThroughThread destructionStillDrains
```

clang-format dry-run、git diff --check、固定440模块hash通过；589个本地Markdown文件引用有效（排除inline／fenced code，不验证外链／anchor）；有限live-looking Cookie模式扫描0 findings，不是全面秘密保证。

ignored日志playlist-order-{configure,build,first-focused,first-thread,boundaries,format,diff-check,fixed-hash,links}.log；本机cache不交付，本地完整提交不推送。没有真实业务／凭据／媒体／私人文件访问或继续−462窗口。真实schema／权限／范围／并发／删除保护、可靠对账／封面／标签清空／公开转私密／整歌单SDK和UI仍另验收；后续[标签编辑／隐私歌单单向公开](playlist-tags-and-publication-writes.md)已另包合成接入；第二包四类介绍资料独立记录。
