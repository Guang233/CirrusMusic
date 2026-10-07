# 播客／声音身份与官方音源：补证结论和播放门禁

> 2026-10-06 UTC，接续 `a3c67c1` 的[消费／recent读取](podcast-and-recent-reads.md)。本包只调查契约、确定后续接缝；**没有新增operation、C++类型／播放器入口、缓存或声音播放实现**。现有43项business operation／34套件不变。没有继续已在-462停止的公开探测，没有账号／keyring／Cookie／网易云业务API／CDN／音频请求或写入。本轮网络仅公共代码仓库与资料检索。

**最新增量**：维护者现明确前三项（整歌单收藏／一起听／云盘后续）暂缓，从播客／声音播放开始，并选择[离线媒体接缝阶段1](spoken-media-seam.md)。已新增PlaybackEntity／AudioTrackId结构类型和无identity MediaSource，媒体端口／buffer／Qt后端不再接Song包络；原唯一coordinator先验证歌曲身份／TTL／token再下沉物理值。新28／15受影响373＋线程13，0fail／原audio默认1skip，17fresh路径clean／Core实编6973deps禁入0，83ops不增／46登记；**仍没有可信消费binding、节目／声音resolver／活动快照／播放入口或实网**，G1/G2待证，G4/G5仅部分准备。首段43／34及文末485links等是旧调查时点证据，不是当前注册／本包验证；未恢复-462窗口或前三项。

## 1. 决策摘要

| 问题 | 结论／证据等级 | 对实现的约束 |
| --- | --- | --- |
| 传统节目以哪个ID取音源？ | 固定home.md完整dj/program章节明确 `mainTrackId`→`/song/url`，不是program.id；旧mp3Url无效／null。**文档级兼容证据**，不是本机节目实网验收 | 保留ProgramId；底层音轨必须另建显式绑定，不能把ProgramId.value作为音源id兜底 |
| 现有v1音源能用于节目音轨吗？ | 固定song_url_v1.js没有节目特别分支；SPlayer对应代码使用radio底层音轨调用song/url/v1，属**第三方调用实践** | 可作为下一包优先核对的候选，不宣称本机XEAPI／字段／权益／TTL兼容；未获样本前不自动放行 |
| VoiceId是否等于传统ProgramId？ | 尚未证明相同或不同；独立C++类型是一项安全设计，不是“服务ID一定不同”的实证 | 不做转换；不能因整数相同、programId参数名、Voice包含mainSong或名字近似就宣告对应 |
| mainTrackId与mainSong.id是否必定相等？ | 本轮文档只明确mainTrackId；此前公开搜索观察Voice own id≠mainSong.id。没有验证传统节目两字段的关系 | 将来只有实际消费响应足以证明关联才接绑定；两个有效音轨声明相互矛盾应失败，不任选一个 |
| voice/detail是否已确认公共消费详情？ | 固定及另行观察的当前Node／Rust wrapper都指向voice/workbench/voice/detail，未补上公共可见性证据 | 不接管理详情、不凭名称推权限；不猜 `/api/voice/detail` 等端点 |
| 可否复用歌曲播放器／报告？ | 媒体缓冲／Qt backend／位置计量可共享机制；当前query／source／snapshot／history含SongId，报告要求SongId＋PlaylistId | 先分离逻辑身份与传输来源，再共享媒体；不能填假SongId／PlaylistId、自动歌曲歌词或歌曲报告 |

**本包结论是“底层音轨路线获得补证，实际节目／声音播放仍受门禁”**，不是“已支持播客播放”或“所有消费接口已补齐”。旧-462只表示该次被拒，不能据此断言永远需要登录／具体账号权限；也不能为消除门禁自动登录、切协议、换host、调用workbench／挑战SDK或重试。

## 2. 固定材料与精确请求证据

依旧锁定 `a8c781fd64faab17fedfd46e0615a2609307f163`，没有更新upstream-lock／inventory决定。以下9个完整module、相关home章节以及dj_program_detail／voice_detail／song_url_v1的TS签名已复核；原440模块／公共文件hash validator通过（164／40／236仍历史审计，不是当前支持列表）。

| 固定文件 | 实际内容与能证明什么 |
| --- | --- |
| module/dj_program.js | wrapper rid→radioId、limit／offset、toBoolean(asc)，明确weapi `/api/dj/program/byradio`；不解析mainTrackId或响应身份 |
| module/dj_program_detail.js | id，明确weapi `/api/dj/program/detail`；没有VoiceId alias／response schema |
| module/song_url.js | ids为字符串列表JSON、br默认999000；`/api/song/enhance/player/url`，createOption无显式crypto；默认配置encrypt:true，经request.js选择eapi |
| module/song_url_v1.js | ids整数列表文本、level、encodeType:flac，明确XEAPI `/api/song/enhance/player/url/v1`；sky／vivid特殊profile与解锁代码均不移植 |
| module/search.js | type2000专门 `/api/search/voice/get`；不能因此把Voice ID用于song/url或传统program/detail |
| module/voice_detail.js | id→`/api/voice/workbench/voice/detail`，默认eapi兼容profile；名称不是消费权限证明 |
| module/voicelist_detail.js | id→`/api/voice/workbench/voicelist/detail`；不能替换已接入的djradio/v2/get |
| module/voicelist_trans.js | programId／radioId等→workbench/radio/program/trans；这是管理排序写，不执行、不移植 |
| module/voice_lyric.js | wrapper id→programId、`/api/voice/lyric/get`；参数名programId本身不证明legacy program namespace，也不在本包接声音歌词 |

util/option.js／config.json与request.js的crypto选择接缝也已核对：doc地址是JS wrapper，不是逻辑wire path；默认profile是固定源码＋配置的结论，不从接口名推加密。interface.d.ts的dj_program_detail／voice_detail／song_url_v1只有`Promise<Response>`，不能补出具体binding、TTL或权益schema。

**重要差异：旧song_url.js整理响应并将返回根code固定200。** 本客户端不能照抄这层包装来遮住业务拒绝；也不能把br版返回当成现有v1的level／expi／freeTrialInfo契约，或在XEAPI失败时自动退旧eapi。当前native只实现SongStream v1，没有增加旧版本operation。

### 完整文档上下文

- 固定 `public/docs/home.md`：dj/detail～dj/program/detail完整章节（3298～3338），dj/program明确mainTrackId→旧song/url；获取音乐url／新版完整章节说明br与level差异。它没有保证现有native v1的所有节目格式／试听／期限。
- 播客声音列表／搜索／详情／排序／播客列表详情完整相关章节（4307～4413）：排序文档写“programId即voiceId”，**只能说明该管理操作的wrapper命名**，不证明 `/api/dj/program/detail` 接受search VoiceId；也不证明任意账号能访问工作台。
- `mp3Url`、fee／payed、programCount、metadata duration和Voice own ID都不等于音源授权；不能由有URL字段／元数据成功／200推可播或完整付费权益。

## 3. 外部公开代码交叉核对（不是上游锁升级）

检索回答只用来找来源；生成的示例、把Voice／Program断言为不同服务ID、将null URL笼统解释为版权、或把mainSong.id与mainTrackId宣告等价等说法，**没有据此采纳**。没有访问搜索命中的第三方音乐API服务／响应样本。下面均为另行固定的公开代码快照，不运行／安装／复制它们的产品代码；第三方实践不能授予服务、曲库或SDK权利。

- [api-enhanced voice_detail，2db6684453297ee86dec0492842f2320b08182d9](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/2db6684453297ee86dec0492842f2320b08182d9/module/voice_detail.js)：完整wrapper仍workbench；同快照完整dj_program_detail仍weapi旧消费路径。[该program wrapper](https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced/blob/2db6684453297ee86dec0492842f2320b08182d9/module/dj_program_detail.js)。不更新项目固定锁。
- [ncm-api-rs voice_detail，133b65bfe482e41ebccf018870d3fce07bf58eb3](https://github.com/SPlayer-Dev/ncm-api-rs/blob/133b65bfe482e41ebccf018870d3fce07bf58eb3/src/api/voice_detail.rs)：完整方法仍workbench＋Eapi，只是相同兼容路线的移植，不是额外公共消费权限证据。
- SPlayer `40d9ec606cc1e323c964ffea7bf0ab02f137e93b` 的完整[radio.ts](https://github.com/imsyy/SPlayer/blob/40d9ec606cc1e323c964ffea7bf0ab02f137e93b/src/api/radio.ts)支持dj/program／detail；[format.ts](https://github.com/imsyy/SPlayer/blob/40d9ec606cc1e323c964ffea7bf0ab02f137e93b/src/utils/format.ts)的完整formatSongsList（49～110）在radio条目保留item.id而把mainTrackId置于dj.id；[SongManager.ts](https://github.com/imsyy/SPlayer/blob/40d9ec606cc1e323c964ffea7bf0ab02f137e93b/src/core/player/SongManager.ts)所核对的radio选择／官方调用片段（514～557）与完整getOnlineUrl（184～258），再经[song.ts](https://github.com/imsyy/SPlayer/blob/40d9ec606cc1e323c964ffea7bf0ab02f137e93b/src/api/song.ts)完整songUrl函数，非dolby路径调用song/url/v1。**这是code-level调用证据，不是本机实测**；没有全面审查该播放器其余路径。

SPlayer的mainTrackId缺失→item.id fallback、通用SongType／JS number、API原响应console.log、URL域名文本替换、自动缓存／降级／解锁等逻辑均不采纳。即使radio的解锁条件被关闭，也不能把其他代码中的解锁或缓存策略带进Cirrus。它没证明Cirrus的XEAPI profile、响应ID／期限、付费音频或长节目能通过当前有界buffer。

扫描固定440个wrapper没有发现它们自己解析mainTrackId／mainSong或声明独立voice/program音源url path；最新公开模块文件名扫描也没有提供这种候选。**这是参考源码范围内的有限观察，不是证明官方不存在其他消费端点。**

## 4. 后续独立播放接缝（设计约束，未新增API）

### 逻辑身份与音轨分开

后续候选是逻辑实体variant `SongId | ProgramId | VoiceId`，container PodcastId只作为来源；内部另建底层 `AudioTrackId` 与原context／实体的可信绑定。原调查时点只有这些设计方向；后续阶段1已添加PlaybackEntity／AudioTrackId结构类型，**尚未泛化活动逻辑snapshot／可信binding或添加任何playProgram／playVoice入口**。

- 从已核对且可验证的消费响应取得底层音轨，校验请求／resource／父播客身份、canonical ID、原token。Programme列表／详情与声音search若将来用于binding，必须各自获得实际字段及关联证据；当前Program／Voice展示mapper继续忽略mainSong／mainTrackId，不自动抓全库补绑定。
- caller只选择逻辑实体，不允许自行提交“任意VoiceId＋任意音轨”拼接。音轨ID不进入歌曲收藏、歌词查询、Song queue、歌曲最近记录或报告。返回数据若只有节目ID、mp3Url、无绑定或两个音轨声明冲突，失败关闭；不得fallback为节目／声音自身ID。
- resolver回包验证底层响应ID，但完成值仍保留**原逻辑实体＋原context＋load generation**；不能用响应的音轨ID覆盖显示身份／历史，或重新标记旧结果。换号／close后丢弃binding，不落盘／打日志，不扩充详细本地recent库。
- ProgramId／VoiceId值恰好相等也不自动合并。将来若有对应证据，建立明确映射，不使用静态cast／QString构造作为验证。

### 可复用与不能原样复用

已读实际 `playback_types.h`、ports/playback.h、stream_resolver.cpp、playback_coordinator.cpp、media_buffer.cpp、qt_playback_backend.cpp、listening_history.cpp、listening_report.h：

| 现有接缝 | 实际约束／后续处理 |
| --- | --- |
| StreamQuery／StreamSource／PlaybackSnapshot | 当前有SongId；mapStreamResponse逐行匹配query.song；coordinator又检查source.song。不能给它们塞音轨SongId后把快照贴Voice标签 |
| MediaBuffer／QtPlaybackBackend | 原调查时点签名StreamSource；最新阶段1已改MediaSource，只有URL／size／validUntil／format／preview及固定LoadToken，不含identity。物理分离已完成，不证明节目／声音消费binding、泛化owner／活动snapshot或实际可播 |
| PlaybackCoordinator／ListeningSession | 当前歌曲生产者与generation边界必须保留；未来整个歌曲／FM／spoken模式只有一个活动媒体owner和单调generation，切模式取消解析、销毁旧player／buffer。不能再建两套同时控制同一backend而互相覆盖的coordinator |
| observed_playback机制 | 可复用同单调时间＋位置条件，不把duration／seek／静音／buffering当收听时长；不代表扬声器／用户注意力 |
| ListeningHistory | 当前字段song且按snapshot.song匹配，ListeningMode只有歌曲Queue／PersonalFm；需要逻辑实体／来源模型后再记录spoken时长。不能把音轨插为新歌曲历史，也不持久化另一套完整recent |
| ListeningReport | 当前SongId＋PlaylistId、固定source:list。Programme／Voice／Podcast没有获证同等报告契约；不自动或显式伪填playlist，不将父Podcast当PlaylistId |

试听时钟仍是媒体片段位置；只有已验证的preview start/end才能转换到节目原始坐标。providerDuration仍raw元数据，不能替代媒体duration或证明preview偏移。歌曲六轨歌词不会随音轨自动加载；voice_lyric另属未来证据和明确范围。

### 音源／权益／期限／传输保持失败关闭

若后续核实了v1节目音轨，优先保持当前固定XEAPI、原账号／匿名上下文、公钥缺失拒绝，不复制Node解锁／配置host／SDK或自动旧url fallback。

- 必须有根／条目业务成功、匹配的音轨、支持的MP3／FLAC、合法实际level／size／expi；expi减请求耗时建单调期限，0／过期／无期限不能放行。null URL是不可用结果，不断言唯一原因。
- freeTrialInfo合法时限制试听；缺失是未知，不授予整段／下载／付费访问权。fee／payed／订阅状态不能单独补出权益。作品级权限验证另受服务约束，必要时停止。
- 复用可信CDN HTTPS／逐跳检查、无Cookie鉴权／缓存、临时0600、固定load、新实例／先销毁player再删device。保留64MiB／45s完整缓冲、15s无进展、至多3次重定向／15s解码准备；**长节目可能超过限制，不据此悄悄扩上限、做流式或持久下载**。
- 解析／取消／暂停意图／seek合并／结束单次与close排空继续原屏障；没有自动重试、降级、过期重解析、翻页续播、播放报告或官方recent写后对账。

## 5. 放行前的最小证据与测试清单

| 门禁 | 下一步所需证据／验证 | 当前状态 |
| --- | --- | --- |
| G1 原节目／声音与音轨绑定 | 脱敏结构＋同请求／原context的ID比较boolean，分别证明字段来源／关联；不要保存真实ID／文本／creator／原JSON | 传统文档mainTrackId路线已获证；当前消费schema仅合成，Voice关联未验证 |
| G2 v1音源 | 明确授权后最少量native metadata请求：成功／拒绝区分、row.id相等boolean、类型／size／level／expi／preview结构。不请求音频就不宣称播放 | SPlayer代码补强候选，但本机节目／声音XEAPI无实证；此前歌曲试听PoC不能替代 |
| G3 媒体样本 | 另行授权的短静音试听／合成音频、暂停／seek／stop／close；不保留签名URL或版权音频 | 本轮未请求；没有声学证明 |
| G4 身份隔离 | 合成宽ID、Program／Voice／audio ID各不同或数值相同、缺失／冲突binding、无任何ID fallback、原context／A→B→C取消／late回包 | 阶段1有规范ID／同值不同tag与原Song验证屏障；可信消费binding／冲突／spoken snapshot尚未实现，不能整门禁标完成 |
| G5 共用player与计量 | 歌曲↔FM↔spoken独占转换、generation、失败／Ended去重、暂停加载、试听坐标、seek／mute不增加时长、clear抑制／切号／close | 阶段1已分离无身份媒体端口并回归原Song／FM单owner；spoken模式／三模式切换与逻辑计量未实现，不能整门禁标完成 |
| G6 报告／云端记录 | 新服务端来源／行为契约另核对／授权；4recent实测另授权，不根据播放推记录必出现或自动发报告 | 无新报告能力／实测；安全默认不发送 |

需要真实账号／挑战环境时另问维护者，不从现有记忆记录／keyring取凭据，不把泛泛“继续”当账号或媒体授权。旧-462 probe不补发；G1／G2可以在未来明确批准的新验证窗口进行，拒绝则再次停下。即使后来确认旧eapi可用，也应作为独立、明确批准的契约，而非当前v1失败的偷偷fallback。

## 6. 本包交接与不阻塞其他能力

本包为docs-only契约决策，没有新业务请求／生产代码／运行依赖／UI改动。485个本地Markdown链接有效（跳过代码标记），git diff --check通过，固定440模块／公共文件hash校验通过；改动全部为Markdown，C++／UI delta=0，因此没有新增编译／LSP覆盖需求；**不重新宣称207＋8是本轮测试结果**，该数字仍为a3c67c1读取包的历史最新定向证据。没有重跑全量Debug／Release／认证／设备矩阵，也没有新增spoken测试／虚构native实测。

如果G1／G2受限，不为“补接口”而开放未知音源或管理端点。原调查时点提出可继续已确认其他后端；最新维护者指定从第四项继续，阶段1已交付，下一步仍需闭合本项绑定／音源门禁，前三项继续暂停、不自动转去云盘／一起听；每包重新确认范围／权限／错误语义，写入与账号／音频实测单独授权。不会本轮自动开始这些包，不因缺证就把FR-044标完成；UI仍最后。
