# 队列恢复与长期本地收听历史

基线 `2c164a7`。Runtime显式 `BackendConfig::persistListeningState=true` 才组合队列存储服务，默认false；不接产品入口／UI，不激活系统媒体、服务或NetEase请求。第一包队列已本地提交 `55458e5`，第二包在其上追加长期历史；256仅是近期内存窗口，不是数据库保留上限。没有push。

## 队列第一包

- 复用现有DatabaseWorker与SQL专用线程；`ListeningStateStore::migrations()` 保留installation v1原语句，追加v2队列表。Runtime无论开关均认识当前schema，关闭持久化不能伪装降版打开旧数据库；关闭开关不自动删除已存数据。
- 队列按account_id隔离，Guest是安装内空账号独立桶，不挪给登录账号。保存最多10000个有序条目、独立entry UUID（允许重复song）、active、顺序／随机及循环模式；不保存播放token、private revision、URL、Cookie、歌词或音频。
- 参数绑定、事务原子替换一个账号队列；读取最多10001行，校验序号连续、canonical UUID／宽song ID／唯一entry／active属于列表和枚举。Corrupt／写入失败明确回报，不静默截断／修补／推播放权限，不影响内存播放。
- owner-thread服务500ms合并最新队列编辑，最多8个未完成DB消息（load预留关闭槽）。忙时保留dirty并报错，不能宣称已保存；切号先保存旧账号捕获的编辑，不把reset当用户清空；手动clear另发明确信号，立即失效迟到restore并落盘空队列。
- 纯Core `PlaybackQueue::restore`／`ListeningSession::restoreQueue` 不调用Facade播放、resolver或lyrics。消费时复验Ready／原SessionSnapshot／原intent／空队列与Queue模式，正常queue编辑也失效晚到读取。恢复只还原选择，不复原媒体、音质权益、进度、FM或随机遍历历史。
- 同token换account／认证phase也同步reset队列；append捕获原完整session，防旧队列被新账号持久化。signal重入后重新采样，新的用户队列胜过恢复，QObject生命周期用QPointer护住新接缝。
- 关闭先发最后dirty队列，再排队backend.close；后台pending计数／idle参与原排空，先等DB结果与收据、后销毁SQL executor，仅backend线程join。owner不等待，退出event loop前仍必须等Facade.stopped()；关闭完成不等于保存成功，失败合入原finishClose结果。

## 长期历史第二包

- 保留installation v1／queue v2语句，追加v3 `listening_history` 与 `(account_id, started_at_ms DESC, record_id DESC)` 分页索引。长期数据库**不按条数删除或截断**；仅显式 `ListeningSession::clearHistory()` 清当前owner桶，关闭开关不删已有历史。不把近期256行反序列化成档案。
- 每个Core接受的新load生成独立record UUID与UTC开始时间；只在原 `PlaybackSnapshot::observed` 有正进展后形成记录。保存账号／歌曲／Queue或FM／queue entry／起止UTC／actual playedMs／capped／end／preview／actual quality；同曲重放不同ID。独立 `StoredListeningRecord` 没有token、URL、Cookie、generation或provider reply，旧live `ListeningRecord` 不直接序列化。
- **时长仅来自原250ms计量链的累计值**，数据库按单记录upsert／MAX，不把重复快照相加，也不拿位置、seek距离或UTC差值算时长。暂停／seek／缓冲／静音及尾部保守丢弃仍沿原契约；每load最多7天、capped保留。UTC只作日历元数据，结束墙钟倒退夹到开始时间，不补时长；无正进展的点击／失败不造收听记录。未来dashboard的“次数”须明确正观测load与有效收听阈值，不能把这些行叫全部播放点击或官方计数。
- Core每次变更生成独立fact（最多64个瞬时待交接；溢出显式报告），ListeningSession同步发给唯一local service；不依赖256行getter中的历史仍在、不恢复active load、不自动调用报告。终结／切号／关闭标记原账号记录，正常退出析构在删除Core owner前结算最后已接纳累计值，不能估算漏掉的采样尾部。
- service只合并最多256个**未送出fact**；普通active更新5s合并，终结立即尝试提交，同record更新不重复计账，64facts／事务、8个DB pending。容量／ID／输入或SQL错误显式报告且原播放继续，不静默删档、不自动重试失败写。SQLITE_FULL的exec／commit错误映射InsufficientSpace，其他SQL错误Query；这不是实际磁盘满验收。
- `historyPage(1..100, optional cursor)` 只查询live原账号，最多2个在途page／100+1行探测next。read消费复验原session与history epoch；切号、clear、close只回NotOpen空page，不泄漏旧数据；同桶clear未送出或等收据期间拒绝新page。keyset不加载全档；分页不是跨多次调用的SQL一致性快照，不能承诺并发更新下的统计快照。
- clear先去掉该桶未送facts／抬升读epoch，再串行排队DELETE；clear任务优先于后来新load。Core clear终止记录当前load剩余部分、private clear epoch阻断在栈旧fact循环；旧已发save→同一backend→同一DB executor FIFO先于erase，ack不重入旧payload、无retry/outbox，因此不会凭旧快照重建被清记录。删除失败明确报告，不能宣称旧DB已删；账号临时reset不发DELETE。
- 关闭新增**有界值移交**：service close停止timer／读与恢复，把≤256facts／≤8桶erase／1队列构成≤265个最终job移交原worker。worker按原8-slot/pending/idle逐批排空，独立于owner对象寿命；只在最终handoff与DB及密钥收据全清后destroy executor／finishClose。解决owner已关／销毁时不能靠其后续ack发残余dirty的问题；owner不阻塞／不另开线程，完成≠保存成功。
- 异常kill／断电可能丢失尚未flush的进展；旧磁盘行可保留`Active`（意为未终结摘要，不是当前播放）。页面值不进入ListeningHistory活动账本／source resolver／report系统，不伪造关闭时间；崩溃恢复、WAL／SQL语义和实际磁盘满行为尚无本包运行证据。统计聚合查询与dashboard UI暂未实现，schema与长期明细／分页为其准备。UTC开始是Core接纳load时、结束是Core结算时的日历值，不保证精确出声起止；本版无逐采样UTC／跨日duration桶，因此按开始日期group的统计只能叫“该日开始的记录累计”，不能声称精确按日切分的收听时间。后续若要跨日精确归属须另加采样来源证据，不从UTC差推时长。

## 检查范围

只编译／静态／结构检查；不新增或运行测试，不把旧suite pass作为本包证据，不执行生成的链接产物或native端点。Qt6.11.2 Linux Debug／Release `cirrus_backend/cirrus_listening_state/MPRIS/native共用` 编译及后续实际flags／边界检查记录在ignored `build/workspace-checks/queue-persistence-*`，第一包Debug／Release0 warnings0、actual-flags Clang7 units0、application17+state3 VALID对象禁入0、Core+Sql no-undefined link0（产物不运行）、9fresh LSP0、format/diff/links0。第二包同样仅编译／静态：Debug／Release0 warnings0、Clang7 units0、20 VALID对象禁入0／Core+Sql链接0，22个changed+neighbor fresh LSP0、17个最终changed+neighbor复验0和单头复验0；最后SQL错误映射修改后Debug／Release、Clang store／链接再次0。忽略日志 `listening-history-*` 与源码／migration检查；不是SQL语义、实际durability／清空重入／WAL／崩溃播放运行证明。最低Qt／两端SDK／实际崩溃恢复／磁盘耗尽／发行权限与#92仍需另证。

`ListeningStateResult` 的两个bit-field提示经实际Clang AST确认 `isBitfield=false`，作精确false-positive标记，不加源码suppress。首次AST分析把多JSON root当单对象导致JSONDecodeError，改stream解码后取实际FieldDecl；源文件编译0与分析器失败分开记录。编辑Runtime的一次expired read写入被工具拒绝，未改文件；重读受影响正文后按原协议重试，无改用旁路写入。第二包`records/cursor/history`三个同类bit-field误报，实际flags Clang头syntax0、FieldDecl均isBitfield=false，fresh LSP0，精确false-positive，无suppress。后续单头unused-include提示复验当前LSP0；`listening_history.h`直接提供使用中的ListeningRecord／ListeningEnd，不按过期提示删掉。cpp-check输出解析旧问题未关闭。
