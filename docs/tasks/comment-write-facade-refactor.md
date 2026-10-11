# 评论写入 Facade 内部职责拆分

基线 `6191236`；仅架构整理，不扩业务、官方协议或播放能力。维护者批准以评论写入为试点，允许本地commit、禁止push，不新增或运行测试。

## 职责边界

- [`CommentWriteRequests`](../../src/application/backend/comment_write_requests.h) 是 Application 内部、非QObject的同步请求组件，持有评论pending表及接纳／取消结果／回包校验；不持Facade、gateway、线程、timer、缓存或第二套账号状态。
- Facade每次传入当前readiness、原generation与session，不把组件的历史值作为授权。原共享请求ID计数仍归Facade，仅接纳成功才递增；8槽与重复event UUID拒绝规则不变。
- Facade保留原public API／signals、owner-thread断言和Qt调度。请求先入表再emit；取消先移除pending再emit底层取消，随后仍用Facade为context的`QTimer::singleShot(0, ...)`发Unknown完成，不将取消解释为撤销服务端效果。
- close、新账号意图、token／account／认证态屏障在原位置调用失效逻辑，逐项取消前先取ID快照，保留原同步取消／异步完成顺序。回包先消费pending，再检查原generation／token／account／Ready／Authenticated，校验step与Acknowledged，eventId和完成context恢复为原请求来源。
- [`comment_write_messages.h`](../../src/application/backend/comment_write_messages.h) 只承载原两个queued DTO与metatype声明，字段／namespace不改；Facade头继续引入，现有调用方无需换include。组件不反向include庞大的Facade头；Facade只前置声明组件并持私有unique_ptr，析构在cpp默认定义，不额外执行close。

## 验证与限制

- Qt6.11.2 Linux Debug／Release编译 `cirrus_backend`、`cirrus_system_media_mpris`、`cirrus_system_media_native` 均exit0／warnings0；actual-flags Clang四个translation units（Facade、组件、Application MOC、Runtime）exit0／diagnostic0。
- Application 17个实际Ninja VALID对象未见infrastructure／非Core Qt／OpenSSL反向包含，whole-archive Qt Core-only no-undefined链接通过，未执行产物；13个changed／adjacent文件fresh LSP无诊断。
- 静态对照：原两个消息struct定义及Facade现有public业务方法／signal声明不变，非评论方法正文和账号／关闭屏障调用位置不变。Facade评论路由块62→39行，全文件826→809行；这是职责隔离，不靠总行数宣称质量或性能收益。
- 原测试／fixture／Gateway／Runtime／MusicGateway／播放链／IPC／native源码／API注册表／UI不改，107 operations不增；没有测试执行、服务启动、依赖安装、NetEase请求或push。编译与源码对照**不是运行行为等价证明**，不套历史760＋11或B-031通过数作为本轮验收；#92及原生／部署门禁未因此关闭。
- ignored证据位于 `build/workspace-checks/comment-facade-refactor-*`。后续是否拆收藏等业务应另行确认，不把此试点扩成通用Manager或全面重写。

业务合同与历史独立验证见[评论写入](../api/comment-writes.md)。
