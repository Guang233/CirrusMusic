# 歌曲／歌单／艺人／专辑资料与独立介绍读取

**后续独立增量**：[SongWikiText受限纯文本](song-wiki-text-subset.md)走另一个固定Eapi路径、独立query/mapper，不套本页SongWikiSummary结构或UGC。仅两类显示文本/描述匹配/原请求关联，内容身份未独立验证，完整百科仍G。以下70ops/40登记等为原包历史；当前107ops/50登记见覆盖账本。

## 1. 连续第二包与来源证据

维护者要求删除／排序后连续补齐资料，并明确包含**歌曲、歌单、艺人、专辑**。第一包见[删除／排序](playlist-delete-and-order-writes.md)，本包不做UI、不重写既有目录／歌词／播放／库生命周期、不生成或拼凑介绍。

盘点[歌曲](music-queries-and-lyrics.md)／[目录](catalog-queries.md)：Song已有标题、艺人署名、专辑／封面、时长；Album已有描述、封面、署名、数量／发行时间；ArtistDetails已有briefDesc／头像／数量；PlaylistDetails已有description／封面／完整索引与部分songs／数量／updateTime。**歌单／专辑基础介绍不是此前没写**；缺的是附加资料、艺人分节长介绍、独立歌曲百科基础显示文本。旧description是QString，缺失与空串不能区分，仍不把它当“已知没有介绍”；新增optional字段明确区分unknown与显式空值。

完整核对固定`a8c781fd64faab17fedfd46e0615a2609307f163`的song_detail、playlist_detail、artist_detail、album、artist_desc、song_wiki_summary，以及ugc_{song,artist,album}_get wrappers；相关home.md／TS，沿用完整核对的options／request／config默认。artist_desc显式Weapi；song_wiki_summary默认Eapi，普通JSON，没有启用SDK、XEAPI或特殊e_r。TS generic Response／包装文档不提供完整schema；新增字段／两类response是明确的合成候选，不证明当前服务兼容性／账号权限。

另只读取公开SPlayer固定`40d9ec606cc1e323c964ffea7bf0ab02f137e93b`的[src/views/Song/wiki.vue](https://github.com/imsyy/SPlayer/blob/40d9ec606cc1e323c964ffea7bf0ab02f137e93b/src/views/Song/wiki.vue)、[song.ts](https://github.com/imsyy/SPlayer/blob/40d9ec606cc1e323c964ffea7bf0ab02f137e93b/src/api/song.ts)、[artist.ts](https://github.com/imsyy/SPlayer/blob/40d9ec606cc1e323c964ffea7bf0ab02f137e93b/src/api/artist.ts)，及公开Git树：basic block／creativeType／uiElement标题／resources标签／textLinks文本是代码候选证据，不是官方或实网schema。**没有运行其客户端、代理、自动联合请求、解锁或外链**；不复制Vue／TS代码到产品，不升级上游lock。公开types.ts的raw fetch被工具SSRF／Fake-IP限制阻止，未绕过、改白名单或以另一协议继续；这不是网易云业务拒绝。无需该文件即可限定已有候选字段；所有网易云／CDN／账号／媒体访问仍未获授权，旧−462窗口不恢复。

## 2. 已有模型的增量，不另造四套详情

`Song`／`Album`／`ArtistDetails`／`PlaylistDetails`追加Core optional值，缺失或null保持unknown；数组显式[]是已知空，保留提供方次序和重复，不去重或翻译：

| 类型 | 新字段与固定候选取值 |
| --- | --- |
| Song | aliases←alia；translatedTitles←tns；publishedAtMs←publishTime |
| Album | aliases←alias；company／type／subType为提供方显示文本 |
| ArtistDetails | aliases←alias；translatedNames←transNames |
| PlaylistDetails | tags；可选creator仅userId／nickname；createdAtMs←createTime；subscriberCount←subscribedCount／commentCount／shareCount；playCount原数值 |

每个数组≤100、每项／短文本≤4096 UTF16，严格字符串／Unicode／无NUL，错类型／过预算失败而非截断。Song日期为安全JSON整数±(2^53−1)，保留负值和0；Album已有历史发行日期规则不变；Playlist时间／三个计数为安全非负JSON整数，playCount为有限0..2^53−1数值，可小数。都是提供方原值，不推导timezone、收藏时间、本地实听时长、播放声学、权益、满库完整性或最终写状态。

creator是公开展示身份：userId规范正signed64，数字仅≤2^53−1；缺失／null／0保持未知。没有AccountIdentity、Cookie、头像、creator私人内容、owner／edit／subscribed权限推断或自动资料加载。type／subType／company是原文本，不是闭合全服务枚举、商业权益或播放凭据。详情扩充不改变完整索引工厂、歌词六轨或播放／历史／报告身份。

JSON辅助`metadata_mapping.h`只在基础设施。共享songFrom的已选读链可保留实际出现的新歌曲字段；albumFrom仍为目录／库等复用，没有自动发新请求。Artist搜索／其他摘要不因此变成完整详情，不自动补artistIntroduction或wiki；不为未确认的legacy字段名添加猜测fallback。

## 3. 两个独立显式介绍查询

新`introduction_types.h`：ArtistIntroductionQuery／ArtistIntroduction、IntroductionSection；SongWikiQuery／SongWikiSummary／SongWikiCard／SongWikiKind。接入原IMusicGateway、MusicQueryService、Facade、musicCompleted／cancelMusic／原token与generation／共享16槽，Runtime不用新线程或服务。Facade入口artistIntroduction(ArtistId)、songWikiSummary(SongId)，仅规范ID；Guest／Authenticated本地允许不等于当前服务无Cookie可用。

| operation | 官方逻辑路径 | 唯一业务payload | crypto／response |
| --- | --- | --- | --- |
| ArtistIntroduction | `/api/artist/introduction` | id规范文本 | Weapi／JSON2MiB |
| SongWikiSummary | `/api/song/play/about/block/page` | songId规范文本 | 默认Eapi／JSON2MiB |

包装路由/artist/desc、/song/wiki/summary不是官方逻辑路径。内部参数仅id字符串，拒绝数值、额外header／SDK／host／e_r／crypto／timestamp／account字段。Default官方目的地、原Pc Eapi header／e_r:false、读响应Cookie不采用，注册68→70，公钥链仍单列；请求码／HTTP200／Ready不等于业务成功或完整介绍。

### 艺人长介绍候选

root.introduction**必需array**≤64，每行object；ti可选短字符串（缺失unknown），txt必需字符串≤65536；root.briefDesc可选≤65536。纯文本顺序分节，所有保留title／txt／brief合计≤256Ki UTF16；不合并已有ArtistDetails.briefDesc，不把ti当固定类别／作者权益，不执行HTML、链接或递归。若出现root.id必须是正确规范ArtistId（null仍未知）；缺身份依靠原请求上下文，不用别的响应找ID。显式[]合法；缺array不伪装空成功，合法200但坏schema为InvalidResponse。

### 歌曲百科基础候选

仅接受root.data object及**必需data.blocks array**≤32，可选data.songId回显必须匹配；不复制第三方data-or-root／缺失blocks→[]的宽松fallback。每block为object／必需code短文本；只解析`SONG_PLAY_ABOUT_SONG_BASIC`。其creatives必需array≤100，creativeType短文本；只接四种：songTag、songBizTag、language、bpm。card标题取uiElement.mainTitle.title必需短文本；前两类resources必需array≤100，各取uiElement.mainTitle.title；后两类uiElement.textLinks必需array≤16，各取text，不打开url。每段≤4096、总cards≤100、保留文本合计≤256Ki UTF16；不从BPM字符串推导数值tempo或补语言。

其他block／creativeType不转发JSON，设unsupportedContent=true；相似歌曲、奖励、影视／综艺、乐谱、图片、外链、私人首听等均不加载或编造。支持字段错类型／缺失／超预算拒绝整结果；支持资源显式[]保留空卡，blocks[]合法，**cards为空／unsupportedContent=false不是“歌曲完整百科／简介为空”的证明**。没有通用song.description、生成的叙事、词曲作者或完整制作署名承诺。已存在六轨歌词credits仍是歌词元数据，不能当权威完整制作名单。

## 4. 失败、权限与剩余范围

沿用原单次读、取消／失效／异号／signOut／close屏障；结果保留originalToken，不自动翻页、补下游／图片／作者、读百科另一接口、缓存／落SQL、写、入队／播放／报告或重试。429只返回合法Retry-After；−462／拒绝不判定版权／登录原因，不绕开挑战。内容不是可信富文本；UI未来需显式plain text／安全链接策略，不因文字含URL就访问它。

/song/wiki/info与登录后的ugc_song／artist／album_get已区分路径和条件，**本包不接猜测的全百科／UGC响应、编辑工作台或通用Raw JSON通道**；相关长叙事／完整制作署名／复杂百科sections仍待schema补证。基础field的真实出现率／单位／编辑权限／统计解释、两类介绍真实schema／权限、UI及图片加载仍未验收；不能将FR-008／009／010全标完成或宣称任意实体所有资料已经支持。

## 5. 合成验证、编译边界与交接

Qt6.11.2 Debug／LC_ALL=C.UTF-8／QT_FATAL_WARNINGS=1。最新14套件：catalog_metadata66、catalog22、music26、library21、entity_search44、podcast_reads55、account_library49、discovery17、charts28、playlist_order59、playlist_writes122、collections189、session_api_client24、backend_facade8，**730 pass**；选定9条线程数据行**11 pass**（含init/cleanup），0fail/skip。CTest39→40，不是全40／Release／真实服务／UI；旧463＋10／404＋9不累加。

新suite含53行有效JSON形状坏资料（数组／item／NUL／长度／源shape／回显／timestamp／creator／count等）、四实体共存、缺与显式空、重复／fraction／旧完整索引不变、64节／65536单段／256Ki总预算；两条closed route／payload／预算／wideID，4 Guest/account行各两次native读、Set-Cookie不轮换、百科实际Eapi解密／e_r:false，Weapi只路由／form与独立载荷期待、非逐请求解密保证；取消／旧token／429／−462与16槽／Facade晚到屏障，无隐藏play/report/write。

线程为合成登录→六次显式详情／介绍读，公共creator7≠账号101、源码DTO文本／统计／歌曲字段跨线程保真；200坏百科schema为InvalidResponse，挂起介绍close Canceled／原token，资源真实排空，队列空／无自动级联读／图片／写／播放／报告。没有真实业务探测。

15个改动C++ fresh all-server LSP 15000ms确认clean；实际Ninja14个application对象／6935依赖，无基础设施／JSON／Qt Network、Multimedia、Sql、Quick、Qml、Gui／OpenSSL穿透，UI0。旧music_types PCH／type_traits提示经fresh主动复查与真实构建解决，直接decay_t／is_same_v需要该include，未删除／suppression；初次QTest creator标签冲突仅修test标签，fatal warnings保持，最后全组通过。Ninja绝对output目标错误改为build-relative后才获得依赖证据，不用失败输出冒充通过。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_catalog_metadata tst_catalog tst_music tst_library \
  tst_entity_search tst_podcast_reads tst_account_library tst_discovery tst_charts tst_playlist_order \
  tst_playlist_writes tst_collections tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(catalog_metadata|catalog|music|library|entity_search|podcast_reads|account_library|discovery|charts|playlist_order|playlist_writes|collections|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  catalogMetadataThroughThread musicQueriesThroughThread entitySearchThroughThread podcastReadsThroughThread \
  accountLibraryThroughThread playlistOrderThroughThread playbackThroughThread destructionStillDrains
```

clang-format dry-run／git diff --check／固定440模块hash通过；614个本地Markdown文件引用有效（排除inline／fenced code，不验证外链／anchor），有限live-looking Cookie模式扫描0 findings而非全面秘密保证。

ignored日志catalog-metadata-final-{build,focused,thread}.log、catalog-metadata-{boundaries,format,diff-check,fixed-hash,links}.log及初轮／标签修复单列。完整契约／需求／域／测试／风险／路线／交接／索引／NOTICE同步，只本地提交不推送；不访问真实账号／业务／媒体／凭据或继续旧公开窗口。本机cache不交付，许可／服务／音乐／SDK权利边界仍不变。
