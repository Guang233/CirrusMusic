# 12 · 质量保障与测试策略

> 状态：产品测试策略提案。API 清单已有离线 Python 工具测试，与 C++ 协议／业务测试分开统计；下列测试族不是已通过的产品验收报告。

## 1. 测试分层

| 层 | 工具／方式 | 范围 | 是否依赖真实 API |
| --- | --- | --- | --- |
| 领域单元 | Qt Test／纯 C++ 测试 | 队列、随机历史、权限、模型、歌词时间线 | 否 |
| 应用单元 | Qt Test + fake ports/clock | 异步流程、取消、epoch、重试、未知写结果 | 否 |
| API 契约 | fixture + 本地 mock transport/HTTP | 编码签名、DTO、错误映射、分页 | 否 |
| 存储集成 | 临时目录 + 真实 SQLite driver | 事务、迁移、恢复、隔离 | 否 |
| 媒体集成 | 合法本地音频 + 受控 HTTP 服务 | 解码、Range、过期、卡顿、设备 | 不必；另设真实来源冒烟 |
| QML 组件 | Qt Quick Test | Gallery、键盘、状态、绑定和 model 更新 | 否 |
| 端到端 | fake API 的完整应用 + 系统交互 | 主要用户旅程、跨页面状态、重启恢复 | 默认否 |
| 真实服务冒烟 | 专用受控账号、显式触发 | 契约仍有效、官方流和权限 | 是，不作为普通 PR 必需网络条件 |
| 真机验收 | GNOME/KDE、音频设备、屏幕阅读器 | Wayland、MPRIS、蓝牙、GPU、包 | 按场景 |

真实服务故障不能被当成“所有单元测试失败”；反过来 mock 通过也不证明真实协议可用。两类证据分开展示。

## 2. 需求追踪矩阵

| 测试族 | 关联需求 | 至少覆盖的验收场景 |
| --- | --- | --- |
| TC-API | FR-001；NFR-011/014 | 内置官方 registry、签名向量、无用户端点配置；200 业务失败、坏 JSON、429、5xx、分页与取消 |
| TC-AUTH | FR-002～004；NFR-008 | 游客、二维码过期／取消晚到、密钥环失败、会话恢复、登出后响应、A→B 切号 |
| TC-RIGHTS | FR-005/018 | 非会员、不同权益、试听、不可播、目标与实际质量不一致、权限变动 |
| TC-SEARCH | FR-006/007 | IME、防抖、A/B 请求逆序、分类切换、未知总数、空态、超大 ID |
| TC-PODCAST-VOICE | FR-044 | 独立ID、voice≠mainSong、资源类型和父播客核对、当前context取消／切号；搜索44项合成Qt Test及有限公开结构验证见[实体搜索](api/entity-search-reads.md)，后续[消费／recent包](api/podcast-and-recent-reads.md)55项合成Qt Test及线程流通过，公开列表-462拒绝；ProgramId／VoiceId对应、节目详情／真实recent、播放及UI验收未完成 |
| TC-CATALOG | FR-008/009 | 缺图、未知时长、多碟专辑、多艺人、下架、详情分页 |
| TC-LIBRARY | FR-010 | 超大歌单、完整 ID 与首屏区分、失败续页、只读权限 |
| TC-SYNC | FR-011～014；NFR-007/014 | 双击喜欢、写超时已成功、批次部分成功、排序冲突、无 revision 对账 |
| TC-DISCOVERY | FR-015/016 | 每日跨时区、旧推荐标记、FM 单次预取、断网耗尽、反馈语义 |
| TC-PLAY | FR-017；NFR-004/006 | load/pause/seek、A→B→C 乱序、重复结束事件、坏源、停止时晚到事件 |
| TC-QUEUE | FR-019/020 | 重复歌曲、删除当前项、拖拽版本冲突、随机历史、单曲循环手动 next、全部失败有限终止 |
| TC-LYRICS | FR-021/022 | 多时间标签、翻译错位、正负偏移、重叠、坏格式、逐字降级、无歌词、seek 后同步 |
| TC-UI | FR-023/040；NFR-002/003/013 | 迷你栏／沉浸页状态相同、切页不断播、加载／空／错、长标题和大量行 |
| TC-HISTORY | FR-024 | 真实收听阈值、seek 不增加已听量、短歌、远程历史分开、清理范围 |
| TC-DOWNLOAD | FR-025/026；NFR-007 | Range 200/206/416、URL 过期换源、暂停、磁盘满、崩溃于 rename 前后、权限失效 |
| TC-LOCAL | FR-027 | 目录授权、坏标签、巨型封面、符号链接环、同名不同文件、文件移动与目录离线 |
| TC-CLOUD | FR-028/029 | 私有元数据隔离、列表分页、上传阶段失败、传完未入库、删除确认 |
| TC-COMMENT | FR-030/031 | [读取包](api/comment-reads.md)：严格资源、cursor／showInner:false、楼层／missing、坏schema／纯文本、取消／换号／429；点赞／审核／发帖未知结果属后续写验证 |
| TC-VIDEO | FR-032（撤销） | 视频明确排除，此测试族不进入验收集合 |
| TC-LINUX | FR-033～036；NFR-010 | MPRIS 单位与事件、无托盘退出、portal 版本／拒绝、媒体键不双触发 |
| TC-A11Y | FR-037；NFR-009/013 | 键盘完整主路径、Orca、名称／状态、焦点归还、主题和 200% 缩放 |
| TC-SETTINGS | FR-038 | 值校验、设置迁移、目录拒绝、设备不存在、重置不删库 |
| TC-RECOVERY | FR-039；NFR-006/007 | 队列恢复默认暂停、休眠、设备丢失、断网、升级／降级数据库 |
| TC-FAILURE | FR-040；NFR-014 | 一致错误分类、部分数据可用、取消静默、无重试风暴 |
| TC-IMPORT | FR-041 | 不可信 URI、重复项、超限文件、导出脱敏、预览后取消 |
| TC-AUDIO-PLUS | FR-042 | 定时器、倍速、DSP、gapless、crossfade 各自的开关与能力测试 |
| TC-TOGETHER | FR-043 | 双账号建房／加入／退出、心跳、事件乱序、seq/version、权限差异、重连与命令回环 |
| TC-SECURITY | NFR-008/012 | 凭据搜漏、跨域头剥离、路径穿越、诊断包预览、TLS 错误不可绕过 |
| TC-PERF | NFR-001～006 | 固定设备上的启动、帧、首声延迟、内存、长期播放 |
| TC-PACKAGE | NFR-010/011 | 干净系统安装、运行时依赖、QML 插件、许可证、升级／卸载 |

P2 测试族在对应功能实现时进入必测集合，不把未实现 P2 标记为失败，也不把它们当已支持功能。

## 3. 必须自动化的关键用例

### TC-PLAY-RACE-001：乱序切歌

Given A/B/C 三个条目，来源响应可控；When 快速选择 A→B→C，响应 C→A→B，并注入 A 的旧 error/endOfMedia；Then 只有 C 被加载、元数据／歌词／MPRIS 对应 C、generation 不回退。

### TC-AUTH-ISOLATION-001：换号

Given A 的歌单请求和下载解析未完成；When 登出并登录 B，再送达 A 的回包；Then B 的模型、DB、CookieJar 和下载队列都不发生 A 数据写入，A 的临时 URL被销毁。

### TC-SYNC-UNKNOWN-001：写结果不确定

Given 创建歌单在服务端成功；When 响应连接断开；Then UI 显示待确认，对账先于重试，最终只有一个远程歌单。若无法对账则保留不确定提示，不编造成功／失败。

### TC-DOWNLOAD-RECOVERY-001：文件与 DB 崩溃窗口

分别在临时写入、校验后、rename 后、DB 提交后终止进程。重启后每个资源只处于一个可解释状态，不拼接不同源、不误报 Completed、不覆盖用户文件。

### TC-DB-MIGRATION-001：升级中断

从支持的每个旧 schema 启动，注入空间不足与中断；原库或一致备份可恢复。高版本库在旧客户端中不得被降级写坏。

### TC-QUEUE-PROPERTY-001：随机操作性质测试

固定随机种子生成至少 10,000 次增删、播放、重排、切换随机／循环和结束事件；每步验证 entry 唯一、current 存在、shuffle 为排列、无越界、无无限循环。失败输出最小可复现序列，不输出用户数据。

### TC-COMMENT-READ：当前只读验证

该评论包当时受影响Debug七套件：comments149、podcast_reads55、entity_search44、catalog22、music26、session_api_client24、backend_facade8，共328 pass。另选comments／podcast／entity／playback（两数据行）／destruction共6条后台线程数据用例、8 pass；均无fail/skip，不是全35套件／Release／真实账号矩阵。构建／复跑命令及32个有效JSON坏schema、68条Guest／账号native替身行见[评论契约](api/comment-reads.md)；日志位于ignored build/workspace-checks/comments-final-{build,focused,thread}.log。初轮31个fixture预期失败已复核修正，不删除production profile或同步化完成通知。

### TC-ACCOUNT-LIBRARY-READ：当前补充读取验证

该账号读取包当时受影响Debug七套件：account_library49、library21、podcast_reads55、comments149、music26、session_api_client24、backend_facade8，共332 pass。另accountLibrary／library／podcast／playback（queue/FM）／destruction六条线程数据行8 pass，无fail/skip；不是全36套件／Release／真实账号矩阵。[完整契约与命令](api/account-library-reads.md)含31个有效JSON坏schema行、独立CloudSongId／catalog引用、quota精度／未知／missing、MUSIC_U／异账号门禁、取消／晚到／429、16槽及后台close排空；无业务／账号／文件／媒体访问。最新ignored日志account-library-final-{build,focused,thread}.log。

### TC-COLLECTION-WRITE：当前收藏订阅写验证

该收藏包时点受影响Debug七套件collections189、listening_report18、library21、account_library49、music26、session_api_client24、backend_facade8，共335 pass；另collections／accountLibrary／library／listeningReport／playback（queue/FM）／destruction七条线程数据行9 pass，无fail/skip，不是全37／Release／真实账号／SDK验收。[契约与命令](api/collection-writes.md)含10动作固定路径／payload、140行响应矩阵、20次native显式写、原Cookie／实际Eapi解密、8槽／UUID、Guest／异账号／旧token、client销毁／同token认证失效、发送前后cancel与Unknown、512／429不重试、close排空。只有合成，整歌单收藏v2门禁延期。初轮11项fixture差异经既有decoder/classifier复核，只修预期／样本而非生产协议；日志collections-final-{build,focused,thread}.log单列，不累加。

### TC-PLAYLIST-WRITE：歌单创建与资料编辑

[契约](api/playlist-management-writes.md)：该包时点受影响7套件playlist_writes122、collections189、catalog22、library21、listening_report18、session_api_client24、backend_facade8，共404 pass；选定7条线程数据行9 pass，0fail/skip，当前38套件而非全量／Release／真实写验收。覆盖固定NORMAL与privacy文本、名称／描述UTF16预算／显式清空／escaping、独立name与descEapi解密、10 native单次动作／Cookie不轮换、70响应矩阵／22有效JSON坏创建schema、宽ID／回显、Guest／异账号／旧token、取消／同token变化／晚到ID／缺失与坏delivery、8槽／UUID、无自动读／播放／报告。线程合成登录／两个同名不同ID、不猜创建关联、码200缺身份Unknown、403拒绝、close挂起创建排空。fresh9 C++ LSP clean／14 application对象6924实际依赖、UI0；保留moc与Q_OBJECT，旧诊断由构建／DB刷新／主动复查解决。日志playlist-writes-final-{build,focused,thread}.log单列，不累加历史结果。

### TC-PLAYLIST-ORDER：删除与完整范围排序

[契约](api/playlist-delete-and-order-writes.md)：最新8套件463 pass（playlist_order59），8条线程数据行10 pass，0fail/skip，当前39非全量／Release。工厂源完整性／置换／重复／未知／预算、固定3path/payload/crypto/64KiB、6 native单次动作／Cookie不轮换、排序实际Eapi解密、42响应矩阵、Guest／错来源token／账号、发送前后cancel、8槽／UUID／晚到、无自动读／播放／报告覆盖。线程生产mapper两次显式读取→工厂→显式写与close挂起排空；完整性／权限／范围仅合成。9 C++ fresh LSP clean、14 application对象6929实际依赖无层泄漏、UI0；历史463+10与此前404+9不累加。

## 4. API fixture 规范

目录建议 `tests/fixtures/netease/<operation>/<case>.json`，配套描述版本、采集方式和清理规则。必须使用合成或充分脱敏数据，保留字段形状／类型／边界，不提交 Cookie、手机号、真实私人歌单和有效签名 URL。

- 原始协议 fixture 与规范化期望值分开，避免测试“mapper 自己生成的输入”。
- 加密／签名向量包含固定时钟和随机输入，只允许在测试注入。
- schema 校验不能替代语义断言：ID、单位、试听范围、空字段、未知枚举都必须断言。
- 契约破坏时提供 diff，影响面映射到 operation 和 FR，不把解析异常吞成空列表。
- 普通 CI 禁止调用真实官方接口，避免限流、账号风险及不可重复测试。

## 5. 性能测量方法

NFR 指标见 01。M0 记录固定设备：CPU/GPU/RAM、磁盘、系统、Qt 构建、音频后端、窗口大小、DPR、刷新率、数据集和网络条件。

- Release 构建测性能，Debug/sanitizer 数据不可直接比较。
- 启动至少 30 次；明确“冷”是进程冷启还是操作系统页缓存清空，分别报告，不混合计算。
- p95 采用排序后的 nearest-rank，保留样本数、分布和失败次数；不能只报告最优值。
- 首声延迟用受控音频回环／采样检测或经验证探针，不把 `PlayingState` 信号时间当成真实可听首声。
- 10,000 行列表、固定封面集合、60 s 滚动，记录关键帧耗时、掉帧和主线程阻塞。
- 8 h 长播放／反复切页／封面替换测 RSS 和资源句柄；区分可回收缓存与持续增长。
- 真实互联网请求延迟单列，不能让外部网络波动掩盖本地 UI 卡顿。

如预算不合理，先保留测量证据并提交调整，不为让 CI 变绿直接改阈值。

## 6. 工具与覆盖门槛

- 每次 PR：编译、clang-format、qmllint、单元／契约／存储／基础 QML 测试。
- 初始代码覆盖目标：领域／应用语句 ≥ 85%，分支 ≥ 75%；安全和状态机关键路径按用例覆盖，不只看百分比。
- Qt／自动生成代码不计入业务覆盖分母；排除项记录原因。
- ASan/UBSan 在支持的 Linux 构建运行；TSan 对自有并发核心设专门 job，识别 Qt 外部噪声，不能无审查全部忽略。
- 歌词、DTO、分享链接和本地标签边界引入 fuzz／性质测试，限制资源预算。
- QML offscreen／软件渲染用于基本逻辑测试，不能代替真实 GPU、输入法、Wayland 和屏幕阅读器测试。
- screenshot golden 固定字体、DPR、渲染后端，允许明确差异阈值；人工视觉评审仍必需。

## 7. CI 与真实账号隔离

测试使用临时 HOME/XDG 目录、临时 SQLite、fake 密钥环，不触碰开发者真实库。真实服务冒烟单独显式触发，凭据从受控秘密存储注入，不出现在命令行／日志／artifact；外部 PR 不授予凭据。

直播流量故障、官方维护和不支持的桌面能力记录为环境／契约证据；不能通过删测试悄悄回避。Mock 模式必须在窗口／日志明确标记，不进入正式发行配置。

## 8. 缺陷等级与发布门禁

- **S0**：凭据泄露、数据破坏、越权跨号；立即阻止发布。
- **S1**：核心播放／登录不可用、频繁崩溃、无法退出；稳定版必须清零。
- **S2**：重要功能受损且有可接受替代；需明确影响面和批准。
- **S3**：轻微视觉／非核心问题；可记录已知问题。

稳定版：P0 全通过；目标 P1 通过或有具名降级说明；无 S0/S1；真实 API 核心冒烟、长期播放、两类 Wayland 桌面和安装包回归有证据。功能完成定义还包含文档、错误态、可访问性和迁移测试，见[发布检查表](checklists/release.md)。
