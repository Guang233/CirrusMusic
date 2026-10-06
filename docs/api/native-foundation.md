# 已落地的 C++ 协议与传输基础

## 实现范围

产品代码使用 C++20/Qt；Python 仅用于开发期接口清单校验，不参与客户端构建或运行。

### 协议库：cirrus_netease_protocol

源码：`src/infrastructure/netease/protocol/`，依赖 Qt Core、OpenSSL 3 Crypto 和 zlib。

- `form_encoding.*`：UTF-8 form 编码，保留字段顺序／重复名／空值，正确处理空格、百分号与孤立 UTF-16 surrogate。
- `official_route.*`：api/weapi/eapi/xeapi 默认官方 HTTPS 主机和路径改写；拒绝外部 URL、查询串、片段、路径穿越和控制字符。
- `protocol_crypto.*`：weapi 双层 AES-128-CBC + raw RSA；eapi 的 MD5／分隔符／AES-128-ECB 编码，以及二进制 eapi 响应解密。
- `response_decoder.*`：有界 gzip、UTF-8／JSON object 校验和可选业务码提取；解码成功不等于业务成功，不吞掉损坏／超限数据。
- `eapi_request_profile.*`：固定 pc 请求 profile、显式 Cookie／User-Agent、`e_r=false`；设备 ID 由装配方提供；匿名 bootstrap 见[安装级启动记录](anonymous-bootstrap.md)。
- `xeapi_crypto.*`：XEAPI B/S/R 编码（AES-256-ECB／随机中间变换／X25519＋HMAC 派生＋AES-128-GCM）、注册签名与常数时间校验、公钥响应解密校验、匿名注册用户名；拒绝小阶点和非法会话密钥。
- `xeapi_request_profile.*`：固定 android xeapi 兼容 profile（`x-*` 头、Cookie 子集，不发送追踪类随机 Cookie）；`openssl_primitives.*` 为库内共享 AES／HMAC 辅助。
- 生产随机数使用 OpenSSL RAND_bytes，base62 用拒绝采样避免偏差；测试可注入随机源，但没有用户可配置的固定 seed/key。
- 编码输入有大小上限；随机失败、无效输入、加解密失败返回明确错误，不使用固定密钥 fallback，不把解密失败当明文成功。

API 层另有 `xeapi_key_ring.*`（安装级公钥状态，整体原子替换，`changed()` 为持久化接缝）和 `xeapi_key_registrar.*`（单飞公钥注册，签名必须匹配本次 nonce，失败不改旧状态）；`SessionApiClient` 的 xeapi 路径无公钥时以 `MissingProtocolKey` 结束、不降级协议，`x-encr-ssid/sskey` 只被当前 epoch 采纳并在 epoch 推进时清除。

form 编码遵循 [WHATWG URL Standard](https://url.spec.whatwg.org/#application/x-www-form-urlencoded)。JSON 使用 QJsonDocument Compact；各 operation 仍须明确数组、布尔、大 ID 和空字段的序列化，不能照搬 JS 隐式转换。

协议常量和流程参考固定上游；版权与许可见 [NOTICE](../../third_party/api-enhanced/NOTICE.md)。这些算法是服务兼容要求，不是推荐用于新协议的现代密码设计。

### 传输库：cirrus_netease_network

源码：`src/infrastructure/netease/network/http_transport.*`，依赖协议库及 Qt Network。

- 异步 POST，返回独立 requestId；结果区分 HTTP 状态、网络错误、超时、取消、响应超限和重定向拒绝。
- 仅使用内置官方默认主机／路径；请求正文上限 2 MiB，响应默认上限 4 MiB、最大 32 MiB。
- 分发后的总时限定时器不随 readyRead 重置；主动取消和超限会 abort；完成信号最多一次，支持启动前取消和对象销毁。
- 禁止自动重定向、自动 Cookie 读写、缓存保存和认证复用；不忽略 TLS 错误，不做隐式重试。
- 保留受限正文、HTTP status、network error 和 Retry-After；不打印正文、Cookie 或密码材料。
- 显式 Cookie ≤16 KiB、User-Agent ≤1 KiB，拒绝控制字符；成功响应的 Set-Cookie 按原边界返回，每条 ≤4 KiB、最多64条／合计64 KiB。使用 QHttpHeaders，避免 legacy rawHeaderPairs 合并重复项。

传输层不负责 operation registry、业务 code 判断、重试调度、账号 epoch、认证头构造、Cookie 保存或响应 JSON 映射。其安全默认并不表示完整登录链已经可用。

### 领域／应用与凭据基础

`cirrus_domain` 提供会话／扫码值类型；`cirrus_application` 包含 SessionCoordinator、QrLoginService 和窄 IQrLoginGateway port；`cirrus_netease_session` 保管当前 epoch 的内存 Cookie。应用层不依赖 Qt Network 或具体协议；凭据留在基础设施。见[会话隔离说明](session-isolation.md)。

### 请求与扫码适配

`cirrus_netease_api` 连接 protocol/network/session，捕获每项请求的 token 与可信 origin，提供封闭 QR／AccountIdentity operation 契约、编码／解码、Cookie 暂存、失效和取消；最多32个在途 HTTP 请求。

`cirrus_netease_auth` 实现应用 port：取 key、本地扫码 URL、800～803 状态映射、同一尝试的 key 绑定和单远程请求限制。原始 JSON／Cookie 不进入应用层。**803 不会直接认证 SessionCoordinator**。weapi 账号身份确认、请求时 Cookie 快照、一次性收据、Committing→Authenticated 精确交接与同线程发布屏障已实现；大数值 ID 和跨作用域凭据冲突保守拒绝。详见[扫码请求链路](qr-auth-request-chain.md)及[认证事务](authentication-transaction.md)。[账号记忆／恢复／退出](account-session-restore.md)也已实现，仍必须复核身份后使用原有交接，不从本地记录直接认证。

`cirrus_backend` 已装配后台线程、Core-only非阻塞Facade、匿名启动与账号服务，关闭时排空vault队列和真实keyring回调后再退出线程；不把已构造的CookieJar移动线程，不在GUI线程等待SQL。见[后台生命周期](backend-runtime.md)。

`cirrus_netease_music` 已实现歌曲搜索／详情／六轨歌词映射及LRC／YRC解析，通过MusicQueryService和原Facade跨线程接入；仅当前Guest／Authenticated上下文可查询。见[契约与少量官方实测](music-queries-and-lyrics.md)。

`cirrus_netease_music`新增官方XEAPI音源解析，`cirrus_media`新增经批准的Qt Multimedia依赖、受控HTTPS/CDN临时缓冲和独立load媒体实例；Core-only单曲协调器已跨线程接入Facade。见[音源／播放契约与静音实测](stream-resolution-and-playback.md)。

Core-only `PlaybackQueue`／`LyricTimeline`／`ListeningSession`已组成[队列与六轨歌词时钟](queue-and-lyric-clock.md)，Runtime提供调用方listening()入口，不把网络或媒体依赖带入应用层。

[专辑／艺人／歌单目录](catalog-queries.md)已扩展原查询port／Facade；含完整歌单索引检查与至多两次请求的分页补详情，无新运行依赖。

[每日推荐读取与私人FM条目供给](discovery-and-fm.md)新增2项认证态weapi读取；Core-only PersonalFmSource处理单请求、低水位、去重、保留余项及显式重试，Runtime提供fm()；[FM连续播放／模式切换](fm-continuous-playback.md)现由ListeningSession接通；Source仍不产生播放命令。

[当前账号个人库](personal-library.md)新增歌单、喜欢ID、收藏专辑和艺人4项读取；Facade捕获账号、service核对账号，weapi/eapi均要求MUSIC_U，读取不回写Cookie、不缓存或写入。

[本地观测累计／会话历史](local-listening-history.md)复用原load身份：Qt媒体线程做单调时钟与位置交叉采样，Core保留256条有界记录；切号／关闭清空，不落盘／不上报，也不证明声学出声。

[显式听歌上报](explicit-listening-reports.md)新增StartPlay／Play两个独立action、固定clientlog/osx profile与写结果状态；8条在途、原账号校验、不重试，不接播放自动触发。

[云端最近听过／听歌排行](cloud-history-reads.md)新增两项weapi原账号读取；保留重复与服务端顺序、未知计数／分值与原playTime字段，不推算实听时长／完整历史，仅合成响应验证。

[榜单列表／内容摘要](chart-reads.md)新增eapi／weapi两项Guest或账号只读，保留未知元数据和无ID展示文本；歌曲分页和显式选歌播放复用已有歌单／队列链，仅合成响应与假后端验证。

[六项歌单发现](playlist-discovery-reads.md)成组接入推荐、普通／精品分页、分类／热门／精品标签；保留未知元数据、不同分页与标签来源，仍复用原歌单／显式选歌播放链，无自动翻页／入队／报告或实网证明。

[实体／播客／声音搜索](entity-search-reads.md)已接入5个固定operation，复用原query生命周期，PodcastId／VoiceId保持独立；该搜索包20次无Cookie公开metadata探测只记录结构，5次最终样本通过新mapper。后续[消费端3项／recent4项](podcast-and-recent-reads.md)已合成接入，ProgramId独立、原账号有界读取；公开播客详情观察root.data，列表-462即停，节目详情／Voice对应／真实recent仍未实证，声音播放未交付；随后[身份／音源补证](spoken-playback-contract.md)仅记录传统mainTrackId路线、v1代码候选、媒体共享接缝与未获证门禁，未新增C++／operation／测试，207＋8仍是读取包结果；原静态inventory不冒充当前范围或registry。

[资源评论](comment-reads.md)新增8个封闭operation／五类读取，四类普通资源和五类通用资源身份严格分开；limit／cursor／floor／统计missing有界，showInner:false保留。仅合成候选schema，无实网、写入、用户评论历史／通知、Voice／艺人强转或UI；Pc profile不变，原token／16槽／线程取消与排空复用。

[账号补充读取](account-library-reads.md)新增3项Weapi：当前账号播客订阅／云盘列表／按ID详情，独立CloudSongId、可选catalog引用、规范配额原值与missing；MUSIC_U、原账号／token／线程屏障复用，仅合成候选。无真实账号／业务访问、音源／播放／写入或UI。

[五类收藏订阅写](collection-writes.md)新增8项封闭operation：红心／收藏到歌单及移除／专辑／艺人／播客；独立Core port与8槽，原账号／token／generation、NotSent／Ack／Rejected／Unknown，不自动读／重试／播放／报告。整歌单收藏／取消的v2 SDK门禁经维护者确认延期，未注册／降级；本包仅合成，不包含真实写或新运行依赖。

[歌单基础写](playlist-management-writes.md)再增3operation，复用上述8槽与Core port、原账号屏障；创建固定NORMAL、单独改名／描述／显式清空。候选创建ID缺失／矛盾即Unknown，不以同名对账、自动重试或更新库；无batch／SDK／新运行依赖。

[删除／完整范围排序](playlist-delete-and-order-writes.md)再增3operation，共享原写port／8槽。完整索引／明确首末账号页的置换工厂与source token／账号屏障拒绝分页／未知／跨号，单目标显式删除；结构不是权限／CAS证据，无自动读／重试／库更新。

[四类资料／介绍](catalog-introduction-and-metadata-reads.md)复用原详情／描述，追加optional元数据；独立艺人分节长介绍与歌曲百科基础2operation、原16槽／token／线程屏障。内容纯文本／预算／白名单，候选schema与unsupported标记，不生成叙事或自动级联／图片／写／播放／报告。

[标签编辑／隐私歌单单向公开](playlist-tags-and-publication-writes.md)再增2operation、原8槽／账号／Unknown；Catalogue成员工厂／source token／1..3与128本地预算，不清空、无自动读。公开仅固定numeric0、无反向privacy；原介绍修改／清空继续回归，无SDK／新依赖／UI或真实写。

[封面更新](playlist-cover-update.md)增加2Weapi operation与原写variant，显式JPEG内存输入，经批准Gui仅PRIVATE基础设施，去元数据／1MiB／1024方形；固定NOS原始POST与strict回执、分阶段Unknown／原账号／8槽，无自动文件读／重试／刷新／UI或真实上传。

[评论点赞／取消／发布／回复／删除](comment-writes.md)增加5operation／4Core variant与独立8槽port，原account/token/generation及business NotSent-Unknown；publish/reply经明确批准新增固定原生v3 GET→XEAPI，严格候选JSONP／token双重白名单，无SDK／JS／外部Core token／猜创建ID／重试／隐式读。真实issuer格式／TTL／绑定、权限／审核／效果另授权，既有whole-playlist v2延期不变。

## 测试与构建

下表是密钥环增量时的13套件完整验证基线；随后新增bootstrap、account_restore和两个backend套件，再加music及stream/playback/media、listening、catalog、discovery、personal_fm、fm_listening、library、listening_history、listening_report、cloud_history、charts及playlist_discovery／entity_search／podcast_reads及comments／account_library／collections／playlist_writes／playlist_order／catalog_metadata／playlist_tag_writes／playlist_cover_writes／comment_writes，当前登记43套件。最新[评论五动作写](comment-writes.md)10套件760 pass、9线程数据行11 pass，0fail/skip（新134），22fresh CPP clean／14 application对象6957实编依赖禁入0；无实网／SDK／JS／UI／新库，不累加历史。此前[封面更新](playlist-cover-update.md)10套件562 pass、另9条线程数据行11 pass，无fail/skip；此前[标签／单向公开](playlist-tags-and-publication-writes.md)8套件563 pass、另9条线程数据行11 pass，无fail/skip；此前[四类资料／介绍](catalog-introduction-and-metadata-reads.md)14套件730 pass、另9条线程数据行11 pass，无fail/skip；此前[删除／排序](playlist-delete-and-order-writes.md)8套件463 pass、另8条线程数据行10 pass，无fail/skip；此前[歌单基础写](playlist-management-writes.md)7套件404 pass、另7条线程数据行9 pass，无fail/skip；此前[收藏订阅写](collection-writes.md)7套件335 pass、另7条线程数据行9 pass，无fail/skip；此前[账号补充读取](account-library-reads.md)7套件332 pass、另6条线程数据用例8 pass，无fail/skip；此前[评论验证](comment-reads.md)7套件共328 pass（comments149），另6条线程数据用例共8 pass，无fail/skip；此前[消费／recent验证](podcast-and-recent-reads.md)7套件最新共207 pass，另6条线程数据用例共8 pass；此前[实体搜索验证](entity-search-reads.md)7套件最新共196 pass，另6条线程数据用例共8 pass；此前[歌单发现验证](playlist-discovery-reads.md)6套件各自最新结果共162 pass，另6条线程数据用例共8 pass；此前[榜单验证](chart-reads.md)6套件各自最新结果共128 pass，另6条线程数据用例共8 pass；此前[云端历史验证](cloud-history-reads.md)6套件各自最新结果共124 pass，另6条线程数据用例共8 pass；此前[显式上报验证](explicit-listening-reports.md)6套件最新结果共125 pass，另4条线程数据用例共6 pass；此前[本地收听验证](local-listening-history.md)6套件最新结果共82 pass、1默认音频skip，另3条线程数据用例共5 pass；此前[个人库验证](personal-library.md)6套件最新成功结果共118 pass、另5条线程数据用例共7 pass；此前[FM播放验证](fm-continuous-playback.md)5套件最新成功结果共63 pass、另4条线程数据用例共6 pass；此前[发现验证](discovery-and-fm.md)6套件最新成功结果共108 pass、另3条线程用例共5 pass；此前[目录验证](catalog-queries.md)5套件共100 pass、另2条线程用例共4 pass；此前[队列／歌词验证](queue-and-lyric-clock.md)仅3套件各自最后一次结果共37 pass，另2条线程用例共4 pass；此前[播放验证](stream-resolution-and-playback.md)仅4套件最后一次结果共49 pass、1默认skip，另4条线程用例和显式音频冒烟通过；此前音乐查询4套件／95 pass，另1条后台线程用例通过；此前后台装配7套件／158 pass、1默认skip，结果见[后台生命周期](backend-runtime.md)；此前账号恢复6套件／191 pass见[账号恢复记录](account-session-restore.md)；匿名启动的较早定向结果见[匿名启动记录](anonymous-bootstrap.md)，不声明重新完成完整矩阵：

| 套件 | 范围 | Qt Test 汇总 |
| --- | --- | --- |
| netease_protocol | form、Unicode、大 ID 字符串、路由和非法路径 | 40 passed |
| netease_crypto | 合成向量、无效密文／输入、随机失败与有界拒绝采样；XEAPI B/S/R、签名、公钥解密、小阶点与非法会话密钥 | 18 passed |
| http_transport | fake reply、429／TLS／重定向、取消／乱序／销毁、显式头、重复 Cookie 和限额 | 22 passed |
| response_decoder | 明文／加密／gzip、CRC、超限、坏 UTF-8／JSON 和业务码边界 | 19 passed |
| session_coordinator | epoch、两阶段认证、取消、换号、过期和同步重入 | 12 passed |
| session_cookie_store | 域／路径／属性、原子更新、限额、旧账号响应及冲突凭据查询 | 21 passed |
| session_api_client | profile、Cookie 链与失效、HTTP 已成功后的切号、取消／并发／封闭 registry | 24 passed |
| xeapi | 匿名用户名向量、签名校验、传输层固定头白名单与会话头捕获、公钥注册（nonce 重放／错误拒绝且不改旧状态）、xeapi 请求路径与按 epoch 的会话密钥 | 23 passed |
| storage | 目录／文件权限、进程锁、pragma／WAL、迁移校验和、更高版本拒绝、升级备份与失败回滚、损坏文件、线程与回调生命周期、设备 ID 生成与稳定性 | 11 passed |
| secrets | XEAPI 公钥整条密钥环持久化：恢复不回写、不覆盖更新的公钥、损坏／不可用上报、变化合并无重叠；无 D-Bus 时真实适配器报错且无明文回退文件（另有默认跳过的真实密钥环往返） | 12 passed, 1 skipped |
| qr_login_gateway | 官方与包装 envelope、QR 状态、错误、key 绑定、queued 值结果和销毁 | 28 passed |
| qr_login_service | Core-only 应用事务、发布屏障、失败回滚、取消／晚到／重入／析构；自动轮询节奏与总时限（虚拟时钟＋Qt 定时器冒烟） | 61 passed |
| auth_transaction | 身份 schema、快照隔离、精确交接、错误作用域／收据、登出／到期／重入、总时限跨 Committing | 38 passed |

该历史完整基线合计 329 passed（包含数据行及初始化／清理）。XEAPI 向量由锁定上游函数在固定随机输入下生成（开发期 Node 脚本，不随产品分发），签名与 R 字段另经 OpenSSL CLI 交叉验证。加密向量由合成 JSON、固定测试密钥和公开协议常量构成；AES/MD5 通过 OpenSSL CLI 独立组装，raw RSA 另以大整数模幂计算。它们不是官方响应样本，不证明当前服务可用。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --parallel
ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error
```

密钥环增量记录的开发机 Qt 6.11.2、OpenSSL 3.6.4、zlib 1.3.2：当时 Debug/Release 构建和十三个 CTest 套件通过，C++ 源文件主动 LSP 检查无诊断，QML lint 通过。匿名启动增量只进行了受影响 Debug 目标编译／3套件回归；账号恢复增量只编译／执行受影响6套件；后台装配增量仅7个受影响套件（QT_FATAL_WARNINGS=1）及变更文件诊断。`BUILD_TESTING=OFF` 不构建测试；测试不安装。

另有 15 项 Python 清单工具测试和 440 个模块／公共文件 hash 核对通过；这是开发工具证据，不计为产品协议测试。自动化网络测试仍使用替身；历史按授权另做[3项官方公开查询的少量真实socket/TLS探测](music-queries-and-lyrics.md)，之后另做[公开XEAPI音源与静音试听PoC](stream-resolution-and-playback.md)，未做真实账号集成验证。

## 未实现与下一步

[匿名 bootstrap](anonymous-bootstrap.md)与[账号凭据持久化／恢复／退出](account-session-restore.md)已实现并有定向离线回归；后台线程与非视觉Facade也已实现；现有音乐查询已接入取消／上下文调度，已有单曲Qt播放后端和队列／歌词时钟，尚未实现模板入口／QML接线或真实账号验收。registry 当前79项业务operation（原8项认证／歌曲／播放加5项目录读取、2项日推/FM、4项个人库、2个显式报告action、2项云端历史、2项榜单及6项歌单发现读取、5项实体／播客／声音搜索、3项消费节目／播客与4项recent、8项资源评论只读、3项账号播客订阅／云盘读取、8项收藏订阅写及3项歌单基础写、3项删除／排序、2项独立介绍、2项标签／单向公开、2项封面分配／提交及5项评论write），原生v3 issuer另计，并非完整目录／播放业务；应用空窗口不变，后端尚未装配到产品入口。

扫码自动轮询与认证总时限已实现（[B-010 子任务](../tasks/B-010-qr-polling.md)）。XEAPI 编码、公钥注册与会话密钥已实现（[B-002 子任务](../tasks/B-002-xeapi.md)）。官方音源与静音试听PoC、队列和歌词时钟已完成最小闭环，目录读取也已接入并离线验证，日推读取与FM条目供给也已接入；FM连续播放已离线接通，个人库四项读取也已离线接入；本地观测累计／会话历史已另包接入，显式上报已接入，但自动时机／来源扩展留待播放接线；[最近记录与排行读取](cloud-history-reads.md)已离线接入，[榜单列表／摘要](chart-reads.md)也已离线接入；[推荐／普通／精品歌单与三类标签](playlist-discovery-reads.md)也已成组离线接入；[实体／播客／声音搜索](entity-search-reads.md)已经接入并有有限公开结构证据；[消费3项／recent4项](podcast-and-recent-reads.md)已合成接入，公开列表-462停止；ProgramId／VoiceId对应、节目详情／真实recent待实证，消费音源与独立声音播放另包，工作台与歌单内搜索待补证；[资源评论普通／热门／新版／楼层／统计](comment-reads.md)已候选schema合成接入，[评论五动作写](comment-writes.md)已明确批准原生v3前置HTTP并合成接入，原账号／独立8槽／Unknown，不执行SDK／JS、无新库／外部Core token／创建ID猜测／重试／隐式读；真实issuer格式／权限／审核／UI及账号评论历史／通知未交付；[播客订阅／云盘列表与详情](account-library-reads.md)已仅候选schema合成接入，云盘音源／播放／写入仍未交付；[五类收藏订阅写](collection-writes.md)、[创建／改名／描述写](playlist-management-writes.md)与[删除／排序](playlist-delete-and-order-writes.md)已仅合成接入，整歌单收藏v2 SDK／自动对账／库同步及UI未交付；[四类资料／独立介绍](catalog-introduction-and-metadata-reads.md)已候选schema合成补齐；[标签编辑／隐私歌单单向公开](playlist-tags-and-publication-writes.md)已合成接入，[封面更新](playlist-cover-update.md)已合成接入，Gui仅基础设施；标签清空／反向privacy／格式扩展／对账仍未交付。本地详细历史持久化按分析需求再定，真实写入另授权；公开只读小样本验证已有维护者授权，真实账号／写入操作不在该授权范围内。浏览器 SDK 和一起听仍遵守[审计门禁](gaps-and-hazards.md)，不得以局部协议测试通过宣称这些业务已支持。
