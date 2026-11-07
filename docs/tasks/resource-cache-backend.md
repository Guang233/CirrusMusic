# 资源缓存后端：先设计、后实现

状态：设计先落盘，随后完成首包源码与Linux编译／静态核查；运行验收未执行。基线 `cb60374`。维护者选择首包同时包含歌曲元数据、六轨歌词和封面；本助手只负责后端，不做UI；听歌统计留最后。不新增／运行测试、不安装依赖、不访问真实NetEase或音源；允许本地commit，不push。

## 1. 范围与现状

- 设计时已有 `Song/SongLyrics` typed值、Facade音乐查询与原账号／取消屏障；原先没有封面读取服务，本包新增独立fetcher。`cover_upload` 是上传，不能复用为下载。
- `DatabaseWorker` 已有单SQL线程，安装状态与队列／长期历史在state.sqlite；缓存不能参与用户历史的保留／删除政策。
- `docs/09-storage-cache-and-sync.md` 和ADR-0006还是提案，缓存策略不等于实现。当前Song.cover mapper可接HTTP／任意host作为元数据，这不是下载许可或安全URL验证，封面fetch必须独立收窄。

首包提供：typed歌曲／歌词cache-only读写、显式封面cache-only或缓存优先下载、按当前账号清理／显式全部清理／占用查询、TTL／SHA256／预算／LRU、Runtime opt-in组合。**不透明拦截既有MusicQuery、不自动预取／批量扫目录、不因数据库错误自动发HTTP**。调用方cache miss后可显式走原Facade；写缓存需显式给出获准存储的TTL，不能从规范化DTO猜服务器允许持久化。现有API不携带Cache-Control，所以本包不自动监听musicCompleted落盘。

不包含：搜索／歌单／个人库／推荐快照缓存、音频缓存、离线下载、Cookie／媒体URL、通用blob／JSON存储接口、统计、UI/QML、自动报告／同步／后台刷新。后续接透明查询装饰器前必须打通服务端缓存指令来源，不能忽略no-store。

## 2. 组合与依赖

- `domain/music/resource_cache_types.h`：Core typed类别／操作／结果／统计；不含SQL、JSON、Network、QImage。
- `infrastructure/cache/resource_cache_codec`：以固定版本的长度前缀UTF-8／大端整数编码Song、SongLyrics，PNG仅由cover normalizer生成、不可通过公开put任意写入；有版本、完整性校验与严格上限；不序列化任意MusicResult/provider reply。
- `resource_cache_store`：通过**已有DatabaseWorker.run**在现有SQL线程开闭独立cache连接，连接和查询从不跨线程；初版按job短连接，避免扩成多库调度框架。连接开销／性能需后续实测，不声称已优化。SQL任务只触碰cache.sqlite，原主库用于执行线程接纳，不修改其schema／备份／队列／历史。
- `cover_fetcher`：后台线程的独立无凭据Qt Network manager，显式GET；QtGui仅用于有界图像校验、缩放和重新编码。
- `resource_cache_worker`：后台线程store/fetch组合，限制未完成任务，原session变更取消封面并拒绝旧scope请求。
- `resource_cache_service`：调用线程窄入口，捕获原SessionSnapshot／本地clear revision，结果消费重新检查live Ready/session/epoch。Runtime暴露可选service，配置cacheRoot为空即关闭；不修改main.cpp/Main.qml，不加第二播放器或数据库线程。

## 3. 磁盘布局与隔离选择

调用方显式提供已存在、当前用户拥有、canonical且0700的独立cacheRoot，祖先限root或当前uid拥有且无group/other写权限（不放/tmp）；初版Linux-first。disabled不读环境、不猜HOME/temp/AppID，不自动选择目录。固定受控文件名 `resource-cache.sqlite`，文件0600；拒绝symlink／非普通文件／不安全目录／与主数据库同路径；普通TLS与文件系统错误不旁路。

首包采用**与state物理分离的一个可重建cache库＋固定SQL账号分区**，不实现提案中的每profile独立库。理由：安装级128/512MiB预算可事务内统一淘汰，不引入跨profile多库遍历及文件／索引双提交。账号scope只由原会话生成，所有读写／单账号清理都由store集中绑定scope；无调用方自选账号入口。Guest独立，不并入登录账号。scope摘要是存储命名，不是认证或匿名保证；数据库没有加密，同用户恶意程序不在权限保护范围内。公共跨账号复用暂不开启。此处是首包明确设计偏离，不把物理profile隔离宣称为已实现；未来若扩私人库，另设计物理分离，不复用这个缓存表作权威库。

使用独立cache schema v1／application tag；识别更高版本／损坏／外来库，报错并停用该次缓存，不自动删除或降级重建。单独锁文件，连接close后释放；原state lock／migrations不变。短连接使用已确认DELETE journal、synchronous=FULL与100ms busy timeout，缓存不启用WAL；SQL故障与未知schema统一Storage失败，record元数据／hash／codec不符返回Corrupt，不宣称做过全库quick_check。SQLite BLOB同时存索引和数据，避免封面文件与DB提交不一致；不返回磁盘文件名／可失效路径。返回值是独立字节／typed副本，淘汰不破坏消费者已持值，因此本包无需文件pin租约。

## 4. Keys、TTL与资源界限

有效命名空间包含cache schema/codec v1、scope、类别和规范化SongId；封面key为含v1／完整规范化URL／目标尺寸的SHA256。封面不以URL明文作文件名，不接带query／fragment／userinfo的URL；音源URL、signed URL、Cookie、token、load/private revision不入缓存。

| 类别 | TTL上限 | 单项编码上限 | 安装级有效payload预算 |
| --- | ---: | ---: | ---: |
| 单曲元数据 | 24h | 512KiB | 与歌词合计128MiB |
| 六轨完整歌词 | 7d | 8MiB | 与元数据合计128MiB |
| 重新编码封面PNG | 7d | 4MiB | 512MiB |

元数据／歌词写入必须显式传正TTL，并确认原来源准许缓存；超过上限拒绝，不静默延长。未来可缓存数据必须保持server no-store／no-cache与最大年龄更严格的政策。接纳put时固定notAfter期限，SQL排队不得延长；排队期间到期回Expired且不写。磁盘written记录实际SQL写入时刻，读时拒绝已过期／时钟早于写入／非法时间范围。初版不提供过期内容、不stale-while-revalidate、不后台刷新，不把离线缓存当授权证据。

SQL索引记录scope/kind/key、written/expires/touched、payload bytes/SHA256。先查长度和元数据，再取BLOB；解析后仍逐项验证，不信任磁盘。损坏项返回Corrupt，不作为命中。歌词保留六轨availability/timing/offset/lines/words/credits/provider flags，无猜测对齐。为限制资源，大到不满足cache单项上限的合法服务结果可不缓存，不截断后冒充完整数据。

最多10000个cache rows；满时可淘汰缓存，但绝不影响长期听歌历史。PUT事务内优先过期／再最少最近访问，单次最多128个victim；仍放不下则明确容量错误并回滚，不能无限扫描／阻塞。预算为payload bytes，不是整个SQLite／WAL／journal／索引／进程内存上限；删除不保证立即归还文件空间，不能向UI谎报磁盘已释放。占用同时区分逻辑字节与DB文件大小，不自动VACUUM或清理外部文件。

服务普通请求最多7个，预留第8个给clear/remove屏障；取消／切号仍占接纳票据直到后台收据，不通过反复cancel释放名额制造无界Qt队列。每份编码payload≤8MiB，因此单请求阶段≤64MiB编码payload；SQL pending另限8，后台保留阶段分别有界，**不是整个进程64MiB保证**，不包括decoded DTO、Qt内部buffer／返回值消费者。封面最多2个并发网络请求／4MiB原响应。owner不等SQL／网络。关闭先拒绝新请求、取消网络，再等待已接纳SQL任务并走原backend drain；完成和成功写入不同：SQL写／删失败sticky并合入原finishClose失败位，期间仍按cache result报告，不把缓存故障升级为账号启动失败。

## 5. 封面下载

显式 `cover(url, size, CacheOnly|PreferCache)`。CacheOnly不发HTTP；PreferCache只有miss/expired才启动一次GET，损坏／存储错误不自动降级网络。Refresh为明确绕过缓存的单次请求；不扫描元数据触发下载。

- 仅HTTPS且无userinfo/fragment/query、无非443端口、无IP字面量；首版精确allowlist `p1.music.126.net` 至 `p4.music.126.net`，不是任意后缀匹配；其他host在service接纳前返回0拒绝，不切代理／DNS／IP、不自动HTTP升级，不扩大下载音乐权限。
- 独立无凭据manager，不复用API Cookie／Referer／Authorization，不自动Cookie保存／加载或HTTP认证；禁止重定向，不处理Set-Cookie，不自动重试。
- response只接受200，头部有界，原body≤4MiB，总deadline15s／无进展期限；TLS错误直接失败，压缩编码拒绝，不调用ignoreSslErrors。
- 仅JPEG/PNG（Content-Type接受image/jpeg、image/png，以及图片主机实际发送的非标准image/jpg，2026-11-07 UI接入实测），验证实际格式、非多帧、原尺寸≤2048×2048且总像素≤4M，再缩到128/256/512/1024之一；用新图像重绘／重新编码PNG以去掉EXIF／文本等原metadata，不保存任意响应原字节或SVG。未知／越界／插件失败明确拒绝；这些是输入限制，不声称图像解码器无漏洞。
- 封面ttl≤7d且尊重Cache-Control/Age/Date/Expires，扣除保守网络耗时；剩余新鲜度＝min(7d, lifetime−age−耗时)（2026-11-07修正：原先先把lifetime截到7d再减age，CDN上age超过7d的封面——实测多数——永远不落盘；同日修正Date／Expires解析：HTTP日期以GMT结尾，Qt的RFC2822Date不接受，原先所有真实响应都解析失败而不落盘；两处与API响应共用`cacheRetention`）；no-store/no-cache、Vary/Pragma或策略解析不确定时仅返回本次网络结果，不落盘。重编码hash为内容fingerprint，不以ETag当完整性证明。

allowlist为安全接纳范围，不是这四个host都已实测可用的承诺；普通fetch失败保留，不绕过。

## 6. clear、取消、账号和重入

账号切换／同token的account或phase变化／Stopping同步失效owner请求；后台和结果消费都用原scope复验，不能把旧请求重新贴上新token。旧已接纳SQL写仍只触碰旧账号分区；它不授权向新账号交付数据。

clear当前scope或显式clearAll：**先抬升owner epoch并取消当前网络／待交付读写，再把DELETE排入原FIFO SQL通道**；在其收据前拒绝新的put/fetch，避免clear内重入复活旧项。已发SQL写在DELETE前排空；未发write／晚到HTTP／旧read不能重新提交。clear失败报告，不声称已删除。取消不是撤销已经提交的SQL；已交付值副本不自动擦除。清缓存不退出登录、不删密钥环／设备ID／队列／听歌历史／下载或用户文件。

外部歌曲／歌词查询回灌前必须在发起原查询前捕获 `writeContext()`，put要求同service实例、原SessionSnapshot与原clear revision；仅传一个新token不足以绕过clear。clear/remove都会使旧permit失效；原查询并非本组件拥有，不擅自取消它，但它的迟到结果无法用旧permit落盘。单项remove同样走全接纳退役屏障（保守取消其他在途cache请求），避免旧封面GET在remove后重新落盘。

每个accepted请求最多一份终态回包；容量拒绝返回0，不伪造成功；callback前移出pending、signal后QPointer/live检查，不靠历史信号作当前授权。

## 7. 实施顺序与验收边界

1. 本设计先落盘；补Core typed DTO与严格codec／独立cache store，确认main DB bytes/migrations不改。
2. 封面fetch与有限PNG重编码、owner/backend service／Runtime显式配置和关闭排空。
3. Linux Debug/Release、实际compile flags Clang、fresh changed-path LSP、Core依赖／no-undefined链接、format／diff／文档链接和本地commit核查。

按维护者要求**不新增或运行测试**，不执行SQL／真实图片／服务／账户／网络访问；结构与编译不替代SQL语义、LRU实际行为、磁盘满、缓存清空竞态、图片插件、DNS/TLS、真实运行／性能验收。最小Qt6.10、非Linux环境、#92 cpp-check解析、正式发行身份／许可仍另记。每阶段必须如实标明源码/编译与运行证据，不能把计划表算成完成表。

## 8. 已写入口与核查记录

后续已接[显式缓存查询组合与服务器缓存许可](resource-cache-query-integration.md)：原Facade网络语义保留，新resourceQueries入口只组合详情／歌词。封面新响应没有明确缓存许可时不再缺省7d落盘；以下首包编译数是历史，不挪作接入包证据。

- `BackendConfig::resourceCacheRoot{}` 空时不构造cache组件；显式非空时 `BackendRuntime::resourceCache()` 返回owner服务。必须等原Facade Ready、同owner线程调用；非法参数／关闭／容量不足返回0。非Linux worker明确Unsupported，不宣称跨平台支持。
- `song(SongId)`、`lyrics(SongId)` 为cache-only；`writeContext()` 捕获私有写permit，`putSong(permit, Song, allowedTtlMs)`／`putLyrics(...)` 仅接受显式获准的剩余TTL。permit是本机顺序屏障，不是服务端缓存授权。遇到没有Cache-Control来源的原Facade结果，不能自动按最大TTL落盘。
- `cover(url,size,mode)` 默认CacheOnly；PreferCache只在Miss/Expired发一次GET，Refresh为单独明确网络意图。返回canonical PNG字节，不给磁盘路径。`status=Network,error!=None,value=PNG,persisted=false` 表示图片获得但cache写失败，不是成功保存；无存储许可时Network/None/persisted=false是预期行为。persisted表示返回值来自持久化cache（Hit）或本次成功保存（Stored/Network），不是本次写入次数；旧文本误写Hit不置true，已按现有Store实现更正。先检查error与status，再按类别消费value。
- `remove`／`removeCover`、`clear(false)`当前scope、`clear(true)`显式所有scope；都只删resource-cache.sqlite的资源表。clear/remove收据前拒绝新cache任务。`usage()`提供全缓存逻辑分类字节／rows与当前主DB文件大小，不返回其他账号内容；不把删除行数当物理回收空间。
- 首次与后续Linux Qt6.11.2 Debug/Release `cirrus_backend/cirrus_resource_cache/MPRIS/native共用` 编译exit0、warning0；actual flags Clang8units（cache5cpp＋MOC、Runtime、application MOC）exit0诊断0；application17＋cache6实际VALID对象无反向／不准依赖；Core/Sql/Network/Gui whole-archive no-undefined链接0，产物未运行。13个changed C++ fresh all-server LSP0，后续5个修改源复验0；最终格式／文档／提交核查另存同前缀。
- 证据在ignored `build/workspace-checks/resource-cache-*`，不是SQL／图片／账号／真实网络行为证明。原tests/fixtures、API107 registry、数据库v1/v2/v3 migrations、播放权限链、IPC/native协议、模板入口均不改。旧#92未解决；CMake LSP曾Unavailable，不以之称clean，实际CMake configure/build结果单列。
- 保留工具失败：首个AST分析把多根JSON当单根导致Extra data，Clang本身syntax0；改stream解析取真实FieldDecl。两个CacheResult字段、Pending.forced与既有BackendFactories字段的bit-field提示均有actual AST isBitfield=false、精确false-positive，无源码suppress。调整clear票据时cpp与header短暂不同步诊断已通过声明同步消除；未压警告或弱化容量。缓存原本就未运行，因此没有编造首次运行成功／SQL fixture pass。

## 9. 提交前最终复核

- 最终13个changed C++路径fresh all-server LSP均clean，无unsupported/unavailable；最后的Debug/Release与8个actual-flags Clang单元再次0诊断。23个VALID对象分层依赖与whole-archive链接重做通过，产物不执行；13个代码文件format、git diff --check均0；5份改动文档的202个本地文件链接存在（不含锚点语义验证）。HANDOFF编辑时出现marksman silent覆盖警告，不声称Markdown LSP完整通过。
- 收尾再整理AST时曾复现多根JSON的Extra data解析错误；以raw_decode逐根解析修正，三份证据中的实际字段均非bit-field。该脚本错误不算编译／运行失败，也不隐藏为成功检查。
- 提交范围19文件（11个新C++文件、2个Runtime文件、src/CMakeLists.txt及5份文档）；API header固定SHA256不变，原DatabaseWorker/installation/listening migrations与main.cpp/Main.qml逐字对照HEAD不变，tests/fixtures不改。固定stash `9907ac4a9ad83b9cefb79fea9d3337b56b91ba6f` 保留。精确文件／hash、提交前后tree核查保存在同前缀ignored证据；仅本地commit，不push。
- 首个staged diff检查发现新文档EOF多余空行（此前unstaged检查不覆盖untracked新文件）；保留exit2日志，移除空行后重新冻结hash并复验staged，而非沿用旧pass。
