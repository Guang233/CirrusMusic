# 播客消费端与四类最近播放读取

## 1. 本包边界

维护者在5项搜索之后批准第二读取包；只交付有界metadata能力，不自动翻页、读完整库、入队、抓音频／封面、播放或上报。Facade入口：

```cpp
quint64 podcastDetails(PodcastId podcast);
quint64 podcastPrograms(PodcastProgramsQuery query);
quint64 programDetails(ProgramId program);
quint64 recentPlaylists(int limit = 100);
quint64 recentAlbums(int limit = 100);
quint64 recentPodcasts(int limit = 100);
quint64 recentVoices(int limit = 100);
```

复用MusicQuery／MusicResult、MusicQueryService、Runtime／16槽Facade邮箱／原token及取消屏障。registry36→43，CTest33→34；domain／application仅Qt Core，模板入口／QML不变。本包不接创作者voice/workbench、不实现播客内检索、歌单内搜索、视频或自动报告。

**实现接缝＋合成验证不等于完整实网交付。** 播客详情有一次公开成功包装结构；节目列表被拒、节目详情没有发出；四种最近播放没有真实账号请求。后续播放仍需确认节目／声音ID、音源权益及来源／历史／报告策略；不转换成当前SongId来冒充已播放。

## 2. 完整固定模块与请求证据

固定 `a8c781fd64faab17fedfd46e0615a2609307f163`；下列7个完整module与 `public/docs/home.md` 对应章节已读取，SHA-256均与inventory.source匹配。inventory仍为历史440模块164／40／236决定；新消费范围没有回写成“已支持全部电台”。

| module／wrapper | native operation／逻辑路径 | 业务payload／限制 |
| --- | --- | --- |
| dj_detail／`/dj/detail` | PodcastDetails `/api/djradio/v2/get` | id；JSON≤2MiB |
| dj_program／`/dj/program` | PodcastPrograms `/api/dj/program/byradio` | radioId、limit、offset、asc；JSON≤4MiB |
| dj_program_detail／`/dj/program/detail` | ProgramDetails `/api/dj/program/detail` | id；JSON≤2MiB |
| record_recent_playlist／`/record/recent/playlist` | RecentPlaylists `/api/play-record/playlist/list` | limit；JSON≤4MiB |
| record_recent_album／`/record/recent/album` | RecentAlbums `/api/play-record/album/list` | limit；JSON≤4MiB |
| record_recent_dj／`/record/recent/dj` | RecentPodcasts `/api/play-record/djradio/list` | limit；JSON≤4MiB |
| record_recent_voice／`/record/recent/voice` | RecentVoices `/api/play-record/voice/list` | limit；JSON≤4MiB |

均明确weapi、固定music.163.com官方目标、JSON解码、不采纳Set-Cookie。id／radioId必须canonical正signed64十进制字符串；不能传host、crypto、URL、uid、csrf_token、e_r或私人状态。原生参数名不是JS wrapper的rid，不混淆doc／wrapper／实际wire。只允许对应固定键数量及值类型；limit1～100，不沿用JS `||`替非法0兜底。

PodcastProgramsQuery使用现有CatalogPage默认30／0、offset0～100000；ProgramOrder闭合NewestFirst→asc:false、OldestFirst→asc:true，wire必须bool，不能把字符串／1经JS truthiness解释。列表返回原query，不承诺稳定跨页快照。

完整docs称dj/detail、dj/program“登录后调用”，program/detail未写登录要求；这不是无Cookie兼容保证。消费读取允许原Guest／Authenticated上下文提交，保留ProviderRejected业务码，不尝试登录、换目标、workbench fallback或重试。四类recent则明确以当前已验证账号作为本地隐私边界：Facade捕获accountId；Service先验证原token、Authenticated和该账号；SessionApiClient要求该上下文MUSIC_U，匿名MUSIC_A不能满足。wire仅limit、不接受任意uid；账号回显若出现必须匹配。当前token但外来accountId为InvalidRequest，旧token为StaleContext。错误不删除账号、恢复／登录或发布成功空列表。

## 3. 独立身份与严格响应候选

`domain/music/podcast_types.h`引入独立ProgramId、Program、PodcastDetails／PodcastProgramsPage／ProgramDetails。它不是VoiceId或SongId；programDetails只接受ProgramId。**当前未证明search VoiceId可用于program/detail**，列表取得的ProgramId可以供caller显式查询节目详情，不能从mainSong.id／mainTrackId生成ProgramId／VoiceId或播放器SongId。

共享 `spoken_mapping.*` 在基础设施复用Podcast／Voice及歌单摘要解析，与先前[实体搜索](entity-search-reads.md)字段、顺序／重复、未知值保持一致；专辑复用albumFrom。图片只是展示metadata，不抓取；creator、账号头像／资料、action URLs、签名音源、mainSong音轨、付费／解锁字段不会发布给应用。

- PodcastDetails：**当前公开观察为root.data**，只接受data对象，podcast.id必须匹配请求；不把root.djRadio当自动兼容fallback。映射name／picUrl、可选desc／programCount，不将订阅／所有权／付费计数当权限。
- PodcastProgramsPage：候选root.programs数组、可选count／more；各Program取id／name／coverUrl、可选description、providerDuration／serialNumber、radio播客摘要。radio若出现必须合法且匹配请求的PodcastId，不编造缺失父播客摘要。
- ProgramDetails：候选root.program对象，id匹配请求ProgramId；其radio只能作为可选metadata，不能推断声音命名空间／节目可播性。
- Four CloudRecentEntities：候选root.data.list，每行data对象、可选resourceId／resourceType／playTime，root.data.total可选。输出分别CloudRecentPlaylists／Albums／Podcasts／Voices，不混为歌曲记录。resourceId若有须等于该entity.id；类型若有须分别PLAYLIST／ALBUM／DJRADIO／VOICE。root.uid或data.uid若有须等于捕获的accountId。**这些包装与资源类型为合成候选契约，不是经过真实账号验证的完整schema**；不猜额外包装／大小写alias或从mainSong救回缺失身份。
- 所有ID按共享安全边界：数字≤2^53−1，宽ID字符串；可选计数／duration／serialNum／playTime为安全非负JSON整数，缺失／null未知。providerDuration、providerPlayTime只保留原值，不声明单位、时钟、实际收听量或与歌曲本地账本可比较。
- name／摘要使用共享合法UTF-16／无NUL／≤4096；专辑描述沿用原目录限制。非法／不支持图片地址降不可用，不猜图片或rights。
- 数组必需，合法空数组为空；**recent/list或programs缺失／null不是成功空列表**，不复制声音搜索已有的“零total/null resources”特殊规则。单页≤请求limit，显式total／count≥当页数、more若有必须bool；坏行／超限／矛盾身份或父播客全部失败，不跳过／截断。
- 保留服务端顺序和重复，不从短页推完整库／hasMore；recent没有offset／nextCursor或本地时钟兜底，programs也不自动继续请求。

原上下文完成值；切号／退出／close隔离晚到结果，返回的DTO副本需caller自行丢弃。429仅保留合法Retry-After，无自动重试。查询不会更新本地听歌时长、详细历史／队列持久化或触发任何官方写入。

## 4. 新授权公开探测：成功与拒绝分开

**2026-10-06 UTC**维护者另行选择“允许公开核对”：只用无Cookie元数据尝试dj/detail、dj/program、dj/program/detail，遇到认证／服务拒绝停止；不读账号或本地凭据、不登录、不请求recent或音频。

忽略的 `build/workspace-checks/podcast-shape-probe.cpp` 使用现有C++ Eapi／Weapi加密与HttpTransport，HTTP Cookie为空，无MUSIC_U／MUSIC_A／NMTID或密钥环／数据库。首先一次已获授权的全站声音搜索limit1，只在内存取得radio.id／voice.id／mainSong.id，随后两次消费读取：

1. voice search：HTTP200／code200，voice.id与内嵌mainSong.id不同。
2. djradio/v2/get：HTTP200／code200，实际root为code／data／msg，data为radio形状（id、name、desc、picUrl、programCount及大量未发布的账号／权益字段）。这是包装与字段类型证据，**未完成新production mapper实测或ID关系证明**。开发probe原比较器误取root.djRadio，所以输出radioMatchesSearch=0不能证明真实data.id不同；没有保留原始JSON可供补算，不为了补算而绕开下一步拒绝。
3. dj/program/byradio：HTTP200但业务code **-462**，立即停止，不重试／切host／换协议／获取Cookie。没有解析／落盘挑战详情或URL，不能凭码断言精确权限或永久需要登录。

共3次metadata请求；program/detail没有发出，列表schema、program.id与voice.id关系、节目播放全部尚未实证。仅字段结构、计数／长度和比较boolean进结构日志 `podcast-public-schema.log`；真实ID、标题／描述、creator资料、音频URL、原始响应不落盘或进入fixture／聊天。此probe不是认证许可；不得用真实账号recent来补这个空缺。将来若要继续，需要明确安排新的授权与脱敏测试，不把-462当作自动重试／接WebEngine或校验SDK的理由。

## 5. 最新定向验证与交接

Qt6.11.2／Debug、LC_ALL=C.UTF-8、QT_FATAL_WARNINGS=1。最新7套件：podcast_reads55、entity_search44、cloud_history28、catalog22、music26、session_api_client24、backend_facade8，**207 pass／0 fail／0 skip**。线程选择podcastReadsThroughThread、entitySearchThroughThread、cloudHistoryThroughThread、playbackThroughThread Queue/FM两行、destructionStillDrains，**8 pass**（含init／cleanup）；不累加失败前或之前包的运行结果。

新55项包含30条合法JSON但坏schema用例、7个operation严格payload／边界、独立namespace／wide ID／缺失元数据／重复／空数组／父播客／账号回显、10条Guest或Authenticated原生读取链、7条operation取消／切号／429、无MUSIC_U／guest／外来账号拒绝、-462无fallback／重试、Facade16槽／晚到隔离。新线程从Guest播客详情→显式列表→列表ProgramId详情，再假账号登录→4recent，原token分段回调用方线程，挂起recent close取消排空，无播放／报告／队列自动副作用；所有账号资料均合成。

初始accountGates错误断言期望StaleContext，经读取既有服务契约纠正为InvalidRequest（不改变生产拒绝规则），复验55项及最终7套件全部成功。新测试创建早于编译DB刷新产生的QHttpHeaders／variant constexpr级联经DB＋file-version／preamble刷新、真实编译与active LSP验证解决；没有移除必要include、降类型检查或添加压制注释。CMake lens“不可用”不算清洁，真实configure／build成功。scoped enum／inline方法no-bit-fields误报作false-positive记录。

```bash
cmake --build --preset linux-dev-local --target tst_podcast_reads tst_entity_search \
  tst_cloud_history tst_catalog tst_music tst_session_api_client tst_backend_facade \
  tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(podcast_reads|entity_search|cloud_history|catalog|music|session_api_client|backend_facade)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  podcastReadsThroughThread entitySearchThroughThread cloudHistoryThroughThread \
  playbackThroughThread destructionStillDrains
```

现登记34套件；不声明全量Debug／Release或真实认证／keyring矩阵。18个改动C++路径最终主动LSP清洁，format／diff通过，473个本地Markdown链接无断链（跳过代码标记），固定440模块hash核验通过且历史164／40／236决定不变；实际14个application编译条目（13手写＋AUTOMOC）的Ninja依赖无infrastructure／Network／Multimedia／Sql／Quick／Qml／Gui／JSON／OpenSSL泄漏，UI diff=0。忽略的podcast-reads-final-focused／final-thread日志为最新结果；先前失败记录不累加为通过数。

后续[身份／音源补证决策](spoken-playback-contract.md)已确认传统mainTrackId→旧song/url文档路线，并发现v1第三方代码候选；实际绑定、v1节目音源、VoiceId／ProgramId关系尚未实证，未新增播放代码。不能直接“把voice当song播”：底层音轨必须显式绑定且不替代逻辑身份，权益／TTL／来源／历史／报告门禁仍保留，再独立建模播放。若服务拒绝需要授权账号／挑战环境，停下确认；仍不接管理工作台／视频、自动报告、真实写入或UI。
