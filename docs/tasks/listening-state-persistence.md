# 队列持久化与安全恢复

基线 `2c164a7`。Runtime显式 `BackendConfig::persistListeningState=true` 才组合队列存储服务，默认false；不接产品入口／UI，不激活系统媒体、服务或NetEase请求。后续长期历史另包追加，不以256条内存快照作为数据库保留上限。

## 队列第一包

- 复用现有DatabaseWorker与SQL专用线程；`ListeningStateStore::migrations()` 保留installation v1原语句，追加v2队列表。Runtime无论开关均认识当前schema，关闭持久化不能伪装降版打开旧数据库；关闭开关不自动删除已存数据。
- 队列按account_id隔离，Guest是安装内空账号独立桶，不挪给登录账号。保存最多10000个有序条目、独立entry UUID（允许重复song）、active、顺序／随机及循环模式；不保存播放token、private revision、URL、Cookie、歌词或音频。
- 参数绑定、事务原子替换一个账号队列；读取最多10001行，校验序号连续、canonical UUID／宽song ID／唯一entry／active属于列表和枚举。Corrupt／写入失败明确回报，不静默截断／修补／推播放权限，不影响内存播放。
- owner-thread服务500ms合并最新队列编辑，最多8个未完成DB消息（load预留关闭槽）。忙时保留dirty并报错，不能宣称已保存；切号先保存旧账号捕获的编辑，不把reset当用户清空；手动clear另发明确信号，立即失效迟到restore并落盘空队列。
- 纯Core `PlaybackQueue::restore`／`ListeningSession::restoreQueue` 不调用Facade播放、resolver或lyrics。消费时复验Ready／原SessionSnapshot／原intent／空队列与Queue模式，正常queue编辑也失效晚到读取。恢复只还原选择，不复原媒体、音质权益、进度、FM或随机遍历历史。
- 同token换account／认证phase也同步reset队列；append捕获原完整session，防旧队列被新账号持久化。signal重入后重新采样，新的用户队列胜过恢复，QObject生命周期用QPointer护住新接缝。
- 关闭先发最后dirty队列，再排队backend.close；后台pending计数／idle参与原排空，先等DB结果与收据、后销毁SQL executor，仅backend线程join。owner不等待，退出event loop前仍必须等Facade.stopped()；关闭完成不等于保存成功，失败合入原finishClose结果。

## 检查范围

只编译／静态／结构检查；不新增或运行测试，不把旧suite pass作为本包证据，不执行生成的链接产物或native端点。Qt6.11.2 Linux Debug／Release `cirrus_backend/cirrus_listening_state/MPRIS/native共用` 编译及后续实际flags／边界检查记录在ignored `build/workspace-checks/queue-persistence-*`，最低Qt／两端SDK／实际崩溃恢复／磁盘耗尽／发行权限与#92仍需另证。

`ListeningStateResult` 的两个bit-field提示经实际Clang AST确认 `isBitfield=false`，作精确false-positive标记，不加源码suppress。首次AST分析把多JSON root当单对象导致JSONDecodeError，改stream解码后取实际FieldDecl；源文件编译0与分析器失败分开记录。cpp-check输出解析旧问题未关闭。
