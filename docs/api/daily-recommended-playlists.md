# 每日推荐歌单：独立原账号读取

## 1. 范围与证据

维护者暂缓整歌单收藏v2、要求推进其他后端后，本包补齐已纳入FR-015的每日推荐歌单。它**不是**已有 `personalized` 普通推荐歌单，也不是每日推荐歌曲；不创建一个虚构日推歌单ID，不用旧读取冒充新来源。一起听／整歌单收藏／云盘后续仍暂停，专用下载音源API不加入待办，UI仍最后。

完整核对固定 `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `module/recommend_resource.js`、home每日推荐章节、TS签名及options／config／request默认链。wrapper显式Weapi、空业务data；home要求登录，TS只有generic Response。下述root.recommend和字段是**严格有界合成候选**，没有真实响应／账号／服务兼容或推荐日期证据。固定440／164-40-236审计不改写为当前注册表。

没有执行JS／SDK、新增运行库、读取真实凭据／密钥环／私人文件、访问网易云业务／图片／媒体／CDN或发送写入。只有fake HTTP、内存合成数据与测试临时安装SQL。入口模板不接线。

## 2. Core与生命周期

`domain/music/playlist_discovery_types.h` 新增：

- `DailyRecommendedPlaylistsQuery{accountId}`：原已验证账号，规范正signed64十进制文本；没有date、limit、offset、afresh或uid透传。
- `DailyRecommendedPlaylists{query,playlists}`：保留原账号query和有序 `DiscoveryPlaylist` 摘要、重复ID；不包含歌曲／权限／owner／订阅状态或provider JSON。

`BackendFacade::dailyRecommendedPlaylists()` 显式读取，沿用 `musicCompleted(id,originalToken,MusicResult)`／`cancelMusic(id)`。未Ready、Guest／Committing、非法账号或16槽满返回0，无完成信号／隐式登录。独立每日query加入原MusicQuery／Result、playlist discovery分派及账号校验，不新造gateway／线程／通用Manager。普通recommendedPlaylists仍可Guest，不受本项私有policy替代。

MusicQueryService验证原token／Authenticated与query账号相同；pending额外捕获私有读取的原verified account。快照通知及finish均检查token、账号和认证态；Facade对**同token但账号／认证态变化**取消在途私有读取，deliver再次核对认证态和已带账号的query。公共读取不因同token的私有scope通知被一律取消。这是对已有私有音乐读取的共同屏障补强，不只是本项测试特判。

取消／换号／close、旧generation、client销毁保留原上下文并隔离晚到结果，不重标到新账号。Facade close为Canceled；其完成不证明远端撤销／共享内存或已交付副本安全擦除。没有cache、持久推荐历史、后台刷新／跨日时钟、UI或自动登录恢复。

## 3. 单一封闭原生请求

| operation | 官方逻辑路径 | 协议／目标 | 业务payload | JSON body预算 |
| --- | --- | --- | --- | --- |
| DailyRecommendedPlaylists | `/api/v1/discovery/recommend/resource` | 显式Weapi／music.163.com | `{}` | 1MiB |

实际POST为固定 `/weapi/v1/discovery/recommend/resource`，使用原Weapi浏览器／csrf／e_r:false链。原账号MUSIC_U必需，MUSIC_A不能替代；accountId仅做本地身份屏障，不生成uid业务字段。拒绝多余参数，包括limit／date／afresh／header／checkToken／crypto／host／e_r，不启用v2/v3 token、XEAPI、加密根猜测、旧接口或自动fallback。响应Set-Cookie不采用，不改变认证／账号记忆。

registry **82→83**；注册CTest **44→45**。非零requestId只表示受理，不证明登录服务支持／完整返回／推荐更新或曲目权益。

## 4. 候选schema与显示边界

- root.code必须为数值200，root.recommend必须显式array，至多100；[]成功，缺失／null／对象或错放root.result／data.recommend是InvalidResponse，不空成功或跨来源兜底。1MiB／100是本地安全预算，不是服务schema或真实固定数量保证。
- 每项必须object，id为规范positive signed64字符串；数值ID仅安全整数≤2^53−1，宽ID用字符串。name必需string（显式空允许），合法UTF16／无NUL／≤4096；不把缺失或null名字伪成空。保留Unicode、空格、HTML样式文本但仅当纯文本，不自动富文本／linkify。
- copywriter映射optional summary；缺失／null未知，显式空仍empty。文本4096／UTF16／无NUL预算，超限／错类型整个结果失败，不跳行／截断／本地排序／去重。
- picUrl仅display cover，沿用既有2048／HTTP(S)／host／无凭据／fragment规则；非法／非URL降为空QUrl，**不允许据此自动下载图片**。
- 每日来源只映射**小写playcount**，不以普通推荐的大写playCount兜底；optional有限非负安全范围JSON数，保留0／小数，仅聚合显示原值，不推定当前账号次数、已听时长、排行／推荐效果。
- 可选trackCount、updateTime须安全非负整数，保留原值和0；后者不推算本机日期／墙钟。摘要不证明trackIds完整、可编辑／已收藏、播放或下载范围。
- 不转发creator、alg／fromAlg、action URL等未采用字段。结果没有offset／total／hasMore、刷新指令、provider推荐日或缓存有效期；不由API名称、接收时间、午夜或updateTime生成推荐日期。

调用方明确选择摘要PlaylistId后，才可独立使用既有详情／歌曲页链；该读取不会自动详情、补Song、抓整个库、加载图片／音源、入队／播放／报告、收藏或刷新库。

业务403／429／-462等拒绝按原错误族保留；HTTP200不覆盖业务错误，HTTP429保留合法Retry-After0..86400秒转ms，仅信息、不重试。坏JSON／schema、oversize与客户端失联不是空推荐。一次显式调用最多一个业务请求。

## 5. 实际离线验证

Qt6.11.2 Debug，`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`，仅各选择最终成功结果，不累计中间重跑：

| suite | pass | suite | pass |
| --- | ---: | --- | ---: |
| daily_playlists（新） | 62 | playlist_discovery | 54 |
| discovery | 17 | library | 21 |
| account_library | 49 | cloud_history | 28 |
| podcast_reads | 55 | music | 26 |
| personal_fm | 11 | session_api_client | 24 |
| backend_facade | 8 | entity_search | 44 |
| search_assist | 117 | comments | 149 |
| catalog | 22 | catalog_metadata | 66 |
| 合计 | **753** | fail／skip | **0／0** |

新62覆盖39坏schema数据行、9拒绝／HTTP／429、4发送前后cancel／logout、3同token scope通知（异账号／Guest／Committing）、2 client loss、精确空payload／path／预算、纯文本／宽ID／重复与optional、Cookie不轮换、缺MUSIC_U／异账号／Guest／超限、Facade和实际Service16槽／late／close。Weapi实际网络form／URL／原Cookie有断言、payload独立直接核对，**没有独立解密每条Weapi业务ciphertext**；密码向量仍是既有证据。

`dailyPlaylistsThroughThread`：Guest入口拒绝→合成登录→每日摘要→另发普通推荐（不同typed result）→每日显式空→错用result字段失败→挂起close；**8 API请求／5读取结果**、原账号／token／caller thread与创建=销毁／wrongThread0／真实排空，播放／报告／收藏／评论请求0、队列空。和11个其他线程数据行一起选择，共 **14 pass**（12数据行＋init/cleanup）、0fail/skip。不是全45／Release／账号／keyring／SDK／声学或UI验收。

首轮测试编译QCOMPARE直接比较无operator==的PlaylistId，改为比较其.value；不改变生产ID语义。随后56pass／1fail暴露同token账号变化Facade未取消私有音乐读，**修生产屏障并保留失败断言**，Service也捕获原authenticatedAccount并检查finish，最终新62。没有放宽坏schema、关闭fatal或删Q_OBJECT／moc。

新类型同步修改时clangd产生No member／MusicQuery／visit连锁诊断；必要include保留，相关路径主动复查及3实际GCC TU语法检查先通过，实际AUTOMOC／构建／编译DB刷新后**12改变C++＋2相邻路径全server fresh clean**，无suppression。14实际application对象／**6963 Ninja依赖**，基础设施／JSON／非Core Qt／OpenSSL禁入0，UI差异0；不是仅看CMake链接声明。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_daily_playlists tst_playlist_discovery \
  tst_discovery tst_library tst_account_library tst_cloud_history tst_podcast_reads tst_music \
  tst_personal_fm tst_session_api_client tst_backend_facade tst_entity_search tst_search_assist \
  tst_comments tst_catalog tst_catalog_metadata tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(daily_playlists|playlist_discovery|discovery|library|account_library|cloud_history|podcast_reads|music|personal_fm|session_api_client|backend_facade|entity_search|search_assist|comments|catalog|catalog_metadata)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  dailyPlaylistsThroughThread discoveryThroughThread playlistDiscoveryThroughThread libraryThroughThread \
  accountLibraryThroughThread cloudHistoryThroughThread podcastReadsThroughThread musicQueriesThroughThread \
  searchAssistThroughThread playbackThroughThread destructionStillDrains
```

固定440模块及公共文件hash核对／15项Python开发工具测试通过，format／diff-check通过，745个本地路径引用missing0（不核对锚点）；变更文件Cookie literal／signed-query URL有限扫描flags0，不代表全面秘密／安全审计。Together固定stash对象存在，实际登记无together，未pop／drop／恢复。

ignored日志 `build/workspace-checks/daily-playlists-*`；final-build／final-focused／final-thread与boundaries为本包最终证据，first／fix／review／syntax另留开发历史。真实schema／频率／推荐日／服务支持／权限、UI／cache／自动下游未验收；原生v2、一起听与云盘后续保持用户暂停，不因此读取完成而恢复。保留[NOTICE](../../third_party/api-enhanced/NOTICE.md)，仅本地提交、不push。
