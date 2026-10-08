# Cirrus 开发技术文档

| 项目 | 内容 |
| --- | --- |
| 文档基线 | 0.2，2026-09-26；后端先行／UI 最后，API 静态审计已完成 |
| 产品／工程名 | Cirrus／CirrusMusic |
| 状态 | 产品规格／ADR 按各页状态理解；HANDOFF 和 api 实现记录描述当前已落地边界，不能混为整体完成 |
| 适用阶段 | 从空项目到 Linux 首个稳定版本，再到歌曲相关能力补齐 |
| 读者 | 产品维护者、C++/QML 开发者、设计者、测试及打包维护者 |
| 唯一事实来源 | 当前代码决定“已实现”；需求决定“要做”；ADR 记录“为什么这样做” |

## 1. 使用方式

这些文档是一套相互约束的开发契约，不是功能愿望清单。维护者确认后，可将某一文档或 ADR 标记为“已采纳”，但**不得因文档写了某项能力就将其标记为已交付**。

**新接手者先读 [HANDOFF](HANDOFF.md)**，完成构建／测试，并参照[路线图](14-roadmap-and-backlog.md)领取下一项任务，不需要先读完所有规格。完整规格阅读顺序：项目基线 → 产品需求 → 技术栈 → 架构 → 负责模块 → 测试 → 路线图。

| 文档 | 回答的问题 |
| --- | --- |
| [开发接手指南](HANDOFF.md) | 当前做到哪里、怎样构建、哪些设计不能破坏、下一步做什么？ |
| [交接验证记录](handoff-verification.md) | 干净源码复验用了什么环境，有哪些不能证明的边界？ |
| [已完成任务：扫码轮询与总时限](tasks/B-010-qr-polling.md) | 轮询／总时限的默认策略和验收结果；新任务契约的写法范例 |
| [00 项目现状与文档约定](00-project-baseline.md) | 当前有什么、哪些只是提案、术语和边界是什么？ |
| [01 产品范围与需求规格](01-product-requirements.md) | 做什么、不做什么、何时做、如何判定完成？ |
| [02 技术栈与依赖策略](02-technology-stack.md) | Qt、C++、音频、数据库、工具怎样选？ |
| [03 系统架构与模块契约](03-system-architecture.md) | 模块如何拆分、依赖如何约束、线程如何划分？ |
| [04 领域与数据模型](04-domain-and-data-model.md) | 歌曲、队列、账号、音质和本地资源如何表达？ |
| [05 API 适配与客户端后端](05-api-and-backend.md) | 如何接入现有接口并抵御接口变化？ |
| [一起听核心12 HTTP-only](api/together-http-core.md) | 后续明确授权：全Eapi-ECB／Status／窄host／shared8／原account-token／四结果及部分metadata；97ops／48登记，新109／affected911＋runtime17；playlist内部unmapped、非NIM／完整同步／实网／播放器queue-report-timer |
| [一起听Android补充材料审计](api/together-protocol-material-audit.md) | NIM/独立chatRoomId/type/毫秒/seq线索、与固定9 HTTP crypto/profile/schema差异；G1–G6未闭，不恢复WIP或自动SDK/实网，f384b7d历史docs-only、当时85ops/47；后续HTTP-only选择另页，不自动放开完整同步 |
| [固定版本 API 审计](api/README.md) | 哪些模块采用／待定／排除，协议依赖、许可与缺口是什么？ |
| [原生后端实现记录](api/native-foundation.md) | 当前哪些组件和离线测试已落地？ |
| [扫码请求链路](api/qr-auth-request-chain.md) | 请求上下文怎样隔离，为什么803还不等于完整登录？ |
| [认证事务](api/authentication-transaction.md) | 如何验证身份、绑定 Cookie 快照并安全发布已登录状态？ |
| [独立每日推荐歌单](api/daily-recommended-playlists.md) | 原账号／read16与同token私有scope，Weapi空业务／recommend100／1MiB，62＋affected753／thread14；83ops／45登记，仅合成、无日期cache／自动下游 |
| [每日推荐历史读取](api/daily-recommendation-history.md) | 2Weapi／原日期DTO成员-source token-account private选择/read16；候选dates366/1MiB与songs500/4MiB，86／17 affected839＋thread15、0fail/skip，85ops／47登记，无会员/window实证／cache／自动下游／写 |
| [日推读取与私人FM供给](api/discovery-and-fm.md) | 独立日推、认证态读取、动态批次及失败／换号边界 |
| [收藏到歌单／红心／收藏订阅写](api/collection-writes.md) | 五类10动作、原账号／8槽／写结果Unknown，仅合成、不重试；整歌单收藏需求保留、维护者暂无v2资料明确暂缓 |
| [整歌单收藏v2原生前置调查](api/playlist-subscription-v2-prerequisite.md) | 固定body／header token与Cookie分层、SDK／原生／许可和待实现测试门禁，要求仍在、前置未解，无新增接口 |
| [歌单创建／改名／描述写](api/playlist-management-writes.md) | 3封闭operation、创建ID候选／Unknown、独立资料动作，原8槽／账号屏障；仅合成，无删除／排序／上传／对账 |
| [歌单删除／完整范围排序](api/playlist-delete-and-order-writes.md) | 3operation、完整索引／明确首末账号页置换、原来源token与Unknown；仅合成，不自动读／重试 |
| [歌单封面更新](api/playlist-cover-update.md) | 显式内存JPEG／1MiB／1024方形／去元数据，批准Gui仅基础设施，NOS三阶段／原8槽／Unknown；合成57＋受影响562与线程11，无真实文件／上传／UI |
| [歌单标签编辑／隐私歌单单向公开](api/playlist-tags-and-publication-writes.md) | Catalogue成员工厂／原token与预算、固定numeric0／8槽／Unknown，仅合成，无标签清空／反向转换；介绍沿用已有修改／清空 |
| [歌曲／歌单／艺人／专辑资料与介绍](api/catalog-introduction-and-metadata-reads.md) | 复用旧描述、增量optional字段、独立艺人长介绍／歌曲百科基础，纯文本／原16槽，仅合成、不自动下游 |
| [账号补充读取](api/account-library-reads.md) | 当前账号播客订阅／云盘列表与详情；CloudSongId和catalog引用分离，仅合成候选，无云盘音源／播放／写入 |
| [个人库只读查询](api/personal-library.md) | 当前账号歌单、喜欢ID、收藏专辑及艺人的有界读取；不包含缓存／写入／播放历史 |
| [云端最近听过／听歌排行](api/cloud-history-reads.md) | 当前账号记录、一周／全部时间窗口；保留未知次数／分值，不推算时长，响应仅合成验证 |
| [榜单列表／内容摘要](api/chart-reads.md) | 有界Guest／账号读取；展示预览不是Song，歌曲分页与显式选歌播放复用现有链，响应仅合成验证 |
| [六项歌单发现读取](api/playlist-discovery-reads.md) | 推荐／普通／精品歌单与分类／热门／精品标签；不同分页／来源及原歌单／显式播放复用，仅合成验证 |
| [搜索建议／简略及详细热搜](api/search-assistance-reads.md) | 3封闭read、typed四组ID/name预览／optional／原16槽-token及乱序query/id；raw热搜文本、strict候选schema，仅合成117＋受影响631与线程12，无自动search／图／下游／cache／持久history或UI |
| [音乐实体／播客／声音搜索](api/entity-search-reads.md) | 五类有界读、独立Podcast／Voice身份、公开结构证据、固定e_r单路径门禁；不含新播放或工作台 |
| [消费端播客／节目与四类recent](api/podcast-and-recent-reads.md) | 七项有界读／原账号；ProgramId独立、公开列表-462停止，Voice对应／节目详情／真实recent待实证，不含声音播放 |
| [评论点赞／取消／发布／回复／删除写](api/comment-writes.md) | 5封闭operation与获准原生v3前置GET，无SDK／JS／新依赖／实网／UI；独立8槽／原账号／Unknown、无评论ID猜测或自动重试／读，合成134＋受影响760与线程11 |
| [资源评论只读](api/comment-reads.md) | 普通／热门／新版／楼层／统计五类入口、八operation及严格资源身份；仅合成验证，无写入／账号评论历史／通知 |
| [播客／声音离线媒体接缝阶段1](api/spoken-media-seam.md) | tagged逻辑结构／AudioTrackId与无身份MediaSource，原唯一owner／媒体限额，28／affected373＋thread13、0fail／原audio1skip；83ops／46登记，无可信binding／实际播放入口 |
| [播客／声音身份与音源补证](api/spoken-playback-contract.md) | 传统底层音轨路线、Voice对应／v1实证门禁、媒体共享和历史／报告隔离；仅研究决策，没有新播放实现 |
| [FM连续播放](api/fm-continuous-playback.md) | ListeningSession的FM模式、暂停续播、失败预算及队列切换；仍无听歌上报 |
| [专辑／艺人／歌单目录](api/catalog-queries.md) | 只读元数据、分页与完整性、两阶段歌单补详情；听歌上报的未实现边界 |
| [队列与六轨歌词时钟](api/queue-and-lyric-clock.md) | 最小听歌闭环：队列策略、暂停切歌、媒体位置与独立歌词游标的实现边界 |
| [06 播放与音频系统](06-playback-and-audio.md) | 播放状态、并发切歌、错误恢复和后端如何实现？ |
| [07 QML 与设计系统工程](07-qml-and-design-system.md) | 如何自绘现代控件而不丢掉交互、性能和可访问性？ |
| [08 功能流程规格](08-feature-specifications.md) | 登录、搜索、歌词、下载、云盘等具体怎样工作？ |
| [09 存储、缓存与同步](09-storage-cache-and-sync.md) | 数据存哪里、如何迁移、冲突和退出账号怎样处理？ |
| [10 Linux 桌面集成](10-linux-integration.md) | Wayland、MPRIS、托盘、快捷键和 Flatpak 有何限制？ |
| [11 安全、隐私与合规](11-security-privacy-and-compliance.md) | 凭据、远程内容、授权和依赖许可如何控制？ |
| [12 质量与测试策略](12-quality-and-testing.md) | 怎样证明需求真正完成？ |
| [13 工程规范与交付](13-engineering-and-delivery.md) | 开发、构建、CI、打包和发布怎样运作？ |
| [14 实施路线与任务拆解](14-roadmap-and-backlog.md) | 从哪一步开始，每个阶段的退出条件是什么？ |
| [15 风险与待确认事项](15-risks-and-open-questions.md) | 哪些因素可能阻塞，谁来决定，默认如何处理？ |
| [16 外部规范与参考资料](16-references.md) | 技术依据来自哪里，哪些结论仍需实测？ |
| [17 VS Code 开发环境](17-vscode-development.md) | 已提供的 presets、构建／调试、C++/QML 工具怎样使用？ |
| [ADR 索引](adr/README.md) | 关键选择、备选方案和重新决策条件 |
| [API 交接清单](checklists/api-handoff.md) | 接入真实 API 前需要哪些材料？ |
| [发布检查表](checklists/release.md) | 哪些条件不可跳过？ |

**最新后端覆盖入口**：[97-operation／44-FR静态盘点](api/backend-coverage-audit.md)。24组已接Core入口、38E/54W/5X与逐项ledger，区分代码／合成／有限live／缺证／暂停／排除；不能164−97计缺口。推荐下一独立小包当前账号等级＋音乐收藏计数两Weapi只读，未开始实现；Together可选发送／playlist／profile与完整同步、其他paused仍分开；97ops／48注册不增，无本轮Qt/build/LSP/deps/实网/SDK证据。

## 2. 规范用词

- **必须／不得（MUST／MUST NOT）**：采纳基线后的强制约束，例外需记录理由、负责人及到期时间。
- **应该（SHOULD）**：默认实施方式，偏离需要说明。
- **可以（MAY）**：可选实现，不构成发布承诺。
- **建议／提案**：尚未由维护者批准的决策。
- **待验证**：有明确验证任务，不能据此向用户宣称支持。

方案中的示例代码、JSON、YAML 和结构图是**设计材料**，不表示对应架构或字段映射已存在。HANDOFF、api 下的 native-foundation／session-isolation／qr-auth-request-chain／authentication-transaction，以及根 README、00、17、13 中明确标记“已提供／已实现”的部分描述当前代码或已执行验证。其余产品规格按目标理解；tasks 下的待实现契约也不能当实现报告。

## 3. 标识与追踪

| 前缀 | 含义 | 维护位置 |
| --- | --- | --- |
| `FR-xxx` | 功能需求 | 01 |
| `NFR-xxx` | 非功能需求 | 01、12 |
| `TC-xxx` | 验收场景／测试族 | 12 |
| `M0`～`M6` | 里程碑；M0～M4 后端，M5 UI／整体验收，M6 增强 | 14 |
| `B-xxx` | 可转成 issue 的实施任务 | 14 |
| `R-xxx`／`Q-xxx` | 风险／待确认问题 | 15 |
| `ADR-xxxx` | 架构决策 | adr/ |

每个实现 PR 应关联至少一个需求或任务；每个需求必须有验收场景。修改跨层接口时，同一 PR 更新契约、fixture、测试和相关 ADR。变更优先级不是删除需求，必须保留推迟原因。

## 4. 文档治理

- 文档负责人默认为项目维护者；模块负责人和 QA 负责人可由同一人担任，不假定存在大型团队。
- 草案 → 评审 → 采纳 → 被新版本替代；ADR 被替代后保留历史，不回写成“当时已经知道”。
- 每个里程碑结束检查一次需求覆盖表、API 能力表和风险清单。
- 发生 API 破坏性变化、Qt 升级、数据库迁移、权限增加或播放后端变更，必须同步更新文档。
- 文档有冲突时：已批准的新 ADR 优先于旧 ADR；需求变更需维护者确认；外部实际权限优先于任何产品预期。不得靠“后端应该支持”消解矛盾。
- 产品文案默认中文，代码标识和日志事件使用英文；后续使用 Qt Linguist 国际化，禁止拼接需要翻译的句子。

## 5. 可复用模板

- [功能设计模板](templates/feature-spec.md)
- [API 操作登记模板](templates/api-operation.yaml)
- [ADR 模板](templates/adr.md)

新增工作先填设计和验收条件，再实现。对于只改一处样式的微小任务可简化模板，但不能省略回归证据。
