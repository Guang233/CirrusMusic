# 资源缓存接入歌曲／歌词查询链

状态：设计先落盘，随后完成源码／Linux编译与静态核查；未做运行验收。基线60e89cc；只后端，不UI／统计，不新增或运行测试、不访问真实API／图片／SQL，不安装依赖，只本地commit、禁止push。

## 1. 边界

保留原Facade songDetails/lyrics的网络语义与musicCompleted三参数通知，不自动监听所有音乐结果落盘。Runtime在显式resourceCacheRoot启用时新增窄CachedMusicQueryService；只有调用新入口且明确选择CacheOnly/PreferCache/Refresh才进入此组合。使用原Facade查询、原ResourceCacheService/SQL线程，无第二网关／数据库／播放器。

首包只接DetailQuery（1..100唯一歌曲）和LyricQuery（一首六轨）。封面仍显式请求，不由Song.cover触发下载。搜索／歌单／推荐／私人库／权限与媒体URL不接缓存。

## 2. 服务器缓存许可

新增Core值CacheRetention：仅包含可复制的单调时钟deadline，默认已过期，Forever不能作为许可。网络基础设施有界解析Cache-Control/Date/Age/Expires，未知或不确定一律0许可；必须有明确max-age或Expires及可解析Date。no-store/no-cache、Vary/Pragma、Set-Cookie、重复／未知directive、非法／未来Date等均拒绝自动保存。不从成功码／DTO／本地最大TTL推断服务器同意。

只提取正常HTTP200响应；SessionApiClient仅向SongDetails/SongLyrics成功解码业务200交付；MusicGateway只对这两个独立查询保留许可，playlist补详情等聚合不继承。内部gateway/service completed通知增加默认空许可尾参数（旧三参数emit与少参数slot保持源码可用；没有声称旧QSignalSpy参数行数不变）。原MusicDelivery附带许可；Facade保留原musicCompleted，并向新的缓存组合发布关联原ID/context的资源结果。取消／失败／stale一律清空许可；新通知前再做重入后的live复验。

网络耗时与HTTP年龄先扣减，随后跨线程排队／映射／批量写入消耗同一单调期限，每个put只用剩余TTL并受类别24h/7d上限约束。原单调期限随临时CacheJob继续传入SQL，编码后接纳、SQL执行再次取剩余时间，且不超过原接纳时notAfter；不因编码或排队延长。deadline不是数据库字段，schema/codec仍v1。原查询前捕获CacheWriteContext，许可只限制时间，不能替代该私有会话／clear屏障。

共享保守HTTP策略用于封面：修正旧首包在无缓存指令时缺省7d的行为，未知只返回当次图片，不保存。该收紧约束新响应，不自动重建／擦除旧cache；旧行仍受既有TTL和显式清理约束。此次不是HTTP响应代理缓存，不复用POST原始响应，不发送条件请求／304或自动重试。

## 3. 读取／写入状态机

- CacheOnly：按请求顺序串行查cache，不发HTTP。歌曲可返回部分命中，明确列出cacheMisses；缺失不是提供方否定或版权结论。歌词miss没有伪造空歌词。
- PreferCache：全部命中直接返回；歌曲只向原Facade请求miss集合，再按原完整顺序合并；歌词miss请求原歌词接口。只有Miss/Expired触发网络，Corrupt/Storage/Canceled/Busy不自动降级。
- Refresh：跳过cache读取，对原完整请求发一次网络查询；不自动删除旧缓存。新响应禁止保存时旧的已获准快照保留到原TTL／显式清理；不把新响应写入或延长旧TTL，不宣称服务端主动失效／版本排序。
- 有许可的成功网络结果逐项串行回灌；仅缓存存在的typed歌曲／歌词，不缓存missing／失败。返回网络结果与缓存保存错误分开，写失败不抹去已获得的正常网络值。不把部分保存说成全部成功。

## 4. 资源与生命周期

最多4个组合请求；每个最多一个在途cache或Facade请求，每批最多100歌曲。沿用cache 7+1/SQL8和原Facade16限额，容量不足显式失败，不重试或轮询。取消保留组合票据到下游终态；无下游时只排一个owner单步wake，避免取消／重入制造无界队列。

ResourceCacheService提供原permit有效性检查与无数据的失效通知。clear/remove/account/phase/close先使permit失效，再取消组合；每个继续点及最终交付重新检查原permit，旧结果不能借新permit回灌。同步失效通知只通知取消，不携带可复用revision、也不作为权限来源。关闭先退役组合、取消原网络与cache，再由原Runtime等待已接纳SQL；销毁不join owner线程，不复活数据。

## 5. 验证计划

Linux Debug/Release、changed-path fresh LSP、实际编译flags Clang、Core依赖与no-undefined链接、格式／文档／提交hash核查。按维护者要求不新增或运行测试、SQL／真实图片／网络／服务；编译不能证明HTTP策略实际兼容、组合命中／清理竞态／性能／磁盘故障或Qt最低版本通过。原#92与跨平台／发行门禁不闭。

## 6. 已写入口与语义

- `BackendRuntime::resourceQueries()`：只有显式启用resourceCacheRoot时非空。声明在 `infrastructure/cache/cached_music_query_service.h`。同owner线程、原Facade Ready后调用 `details(DetailQuery, CacheReadMode)`／`lyrics(SongId, CacheReadMode)`；mode必填，无默认网络意图。返回0表示未接纳，否则以自己的请求ID关联 `completed(CachedMusicResult)`。原ListeningSession／FM等既有Facade调用未被偷偷改为此入口，不自动预取或切换播放来源。
- `CachedMusicResult.value` 只含SongDetails/SongLyrics/MusicFailure或缺省，不开放通用MusicResult缓存。CacheOnly部分歌曲命中时value可含部分songs，`SongDetails.missing`仍只表示提供方遗漏，cache本地缺失／过期单列cacheMisses；歌词miss不造空六轨。PreferCache的cacheMisses记录本次查询的cache未命中集合，网络可补齐其中条目。
- cache命中是各次SQL读取时的有效快照；批量读取不保证原子版本或最后交付瞬间全体仍在TTL内，返回的值副本也不会因随后TTL到期自动撤销。clear／切号／close则仍在最后交付点用原permit拒绝旧结果。没有SWR或自动重试来掩盖这些区别。
- `status=Network`不等于保存成功。检查MusicFailure与cacheError，`storageCandidates/storedEntries`区分没有许可、期间到期、部分保存及写入拒绝；写入首个错误后停止剩余回灌，但保留正常网络value。put本地拒绝（例如资源／codec限制）报告InvalidRequest；只读接纳不足报告Busy，不伪装磁盘或网络故障。cancel不保证撤销已提交SQL。
- Facade新 `musicResourceCompleted` 是独立详情／歌词的关联结果通知（包括取消）；原 `musicCompleted` 保持三参数且一次终态。组合只订阅前者，不把两个通知当成两次请求；内部gateway/service完成信号的默认空第四参数是明确的内部契约增量，未拿未运行的旧测试作兼容证明。
- 审查修正：下游对象在接纳调用中销毁时，把销毁作为失败收据，不再登记一个永远无法回来的ID；原单调期限在编码后／SQL阶段继续扣减；重复策略header明确拒绝，不依赖Qt合并重复字段的行为。旧缓存文档对Hit.persisted=false的描述错误已更正，现有Store命中本来就返回true；此字段不是“本次执行写入”计数，新组合只在Stored收据累加storedEntries。

## 7. 证据与保留失败

- Linux Qt6.11.2 Debug/Release：backend/resource_cache/MPRIS/native共享target均exit0，compiler诊断0；实际flags Clang16单元（10cpp＋6MOC）exit0／stderr0。
- Application17＋cache7个实际Ninja VALID对象分层依赖禁入0；Core-only与Core/Sql/Network/Gui whole-archive no-undefined链接均0，产物未执行。22个changed C++路径fresh all-server LSP全部clean；最后策略helper及两个使用方／组合源4路径再次clean。不是运行验收，不与历史测试计数相加。
- 保留：新头初次尚未关联实际TU时QDeadlineTimer找不到，以及并行更新TransportResult声明时短暂成员缺失，后续完整fresh/build消除；22路径首次probe有1个timeout，未算clean，增大等待后22/22确认。AST首轮把多根JSON当单根导致Extra data（compiler本身0），改raw_decode逐根处理。CacheRetention、CachedMusicResult/Pending及既有TransportRequest bit-field误报以实际AST isBitfield=false排除，精确false-positive、无源码suppress。首轮MOC有C locale提示，最终build进程显式LC_ALL=C.UTF-8，不改全局环境或安装依赖。
- ignored证据前缀 `build/workspace-checks/cache-query-*`。提交范围28文件（22个C++路径、src/CMakeLists及5份文档）；format22、git diff --check均0，204个本地文件链接存在（非锚点语义证明）。API header固定SHA256、state存储源码／迁移、cache schema/helper前缀、模板入口逐字不变，tests/fixtures未改，固定stash保留。提交前后文件hash核查保存在同前缀证据；没有新增或执行测试、SQL、图片、账号、网络或backend服务；官方107 operations登记、state migrations、播放权限链、IPC/native协议和模板入口保持。
