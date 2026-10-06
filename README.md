# Cirrus

**Cirrus** 是面向 Linux 桌面的第三方网易云音乐客户端项目，仓库／工程名称为 **CirrusMusic**。计划使用 C++ 与 Qt Quick/QML，构建以歌曲播放、音乐发现和音乐库管理为中心的桌面体验。

> 当前状态：Qt 模板入口 + 分层原生后端（认证／查询／队列／FM播放／歌词）+ 开发文档、固定版本 API 审计及定向 Qt Test，尚未实现可用的音乐客户端功能。本文中的产品目标不是已交付功能；本项目不是网易云音乐官方客户端。

## 开发文档

**新接手者从 [开发接手指南](docs/HANDOFF.md) 开始**：包含当前状态、从零构建、代码导航、设计边界和待确认事项。[匿名启动](docs/api/anonymous-bootstrap.md)与[账号记忆／恢复／退出](docs/api/account-session-restore.md)已完成后端闭环；[后台线程与非阻塞启动／关闭装配](docs/api/backend-runtime.md)也已完成；[搜索／歌曲详情／三语言歌词](docs/api/music-queries-and-lyrics.md)已接入并完成少量官方公开只读实测；[官方URL与无界面播放PoC](docs/api/stream-resolution-and-playback.md)已接入并通过公开试听静音验证；[队列与六轨歌词时钟](docs/api/queue-and-lyric-clock.md)也已完成后端最小闭环；[专辑／艺人／歌单只读目录](docs/api/catalog-queries.md)已接入并离线验证；[每日推荐读取与私人FM条目供给](docs/api/discovery-and-fm.md)也已接入，[FM连续播放接线](docs/api/fm-continuous-playback.md)已完成离线后端闭环，[个人库四项只读查询](docs/api/personal-library.md)也已离线接入；[本地可观测收听累计／有界会话历史](docs/api/local-listening-history.md)已接入，[显式听歌上报能力](docs/api/explicit-listening-reports.md)也已离线接入，[云端最近听过／一周与全部时间排行](docs/api/cloud-history-reads.md)已离线接入，[榜单列表／内容摘要与歌单选歌播放链](docs/api/chart-reads.md)也已离线接通，[六项歌单发现读取](docs/api/playlist-discovery-reads.md)也已成组离线接入；自动上报时机／来源扩展尚未接线，本地历史持久化仍未实现（见[路线图](docs/14-roadmap-and-backlog.md)）；已完成的[扫码轮询与总时限](docs/tasks/B-010-qr-polling.md)可作为任务契约范例；[干净源码验证记录](docs/handoff-verification.md)说明已跑过的流程和局限。完整规格见[技术文档总览](docs/README.md)。

- [产品范围与需求](docs/01-product-requirements.md)
- [技术栈](docs/02-technology-stack.md) · [系统架构](docs/03-system-architecture.md)
- [网易云 API 与客户端后端](docs/05-api-and-backend.md)
- [API 审计与移植清单](docs/api/README.md)：440 个上游模块，164 个采用候选、40 个待定、236 个排除；仅少量原生查询有公开实测，清单不是实现进度
- [已实现的 C++ 协议基础与测试](docs/api/native-foundation.md)
- [扫码请求链](docs/api/qr-auth-request-chain.md) · [身份验证与凭据交接](docs/api/authentication-transaction.md)：离线认证事务、扫码自动轮询与总时限已实现，未装配到产品入口／UI
- [播放引擎](docs/06-playback-and-audio.md) · [自定义 QML 控件体系](docs/07-qml-and-design-system.md)
- [实施路线与任务拆解](docs/14-roadmap-and-backlog.md)
- [待确认问题](docs/15-risks-and-open-questions.md) · [API 交接清单](docs/checklists/api-handoff.md)

## VS Code 与当前骨架构建

完整说明见 **[VS Code 开发指南](docs/17-vscode-development.md)**。需要 CMake ≥ 3.24、Ninja、C++20 编译器、OpenSSL ≥ 3、zlib、Qt6Keychain 开发包及 Qt 开发 Kit（含 Network、Sql／QSQLITE、Multimedia及音频后端插件、封面基础设施的Gui／JPEG插件，测试另需 Test）；模板要求 Qt 6.10+，当前已验证 Qt 6.11.2。

首次克隆时复制 `CMakeUserPresets.json.example` 为 `CMakeUserPresets.json`，填写其中两处 Qt Kit 路径。本次已为当前机器配置好私有文件，不必重复覆盖；个人路径不会提交。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --parallel
ctest --test-dir build/linux-dev-local --output-on-failure --no-tests=error
./build/linux-dev-local/appCirrusMusic
```

在 VS Code 中安装推荐扩展，选择 **Linux Debug (local Qt)** configure preset 和对应 build preset；`Ctrl+Shift+B` 完整构建 app／后端／已启用测试，`Cirrus: test` 先构建再运行 CTest，`F5` 使用 **Cirrus: Debug (GDB)** 调试。另有 QML lint 与 clean 任务；只运行应用仍是模板窗口。

Debug/Release 构建、qmllint、clangd 检查、GDB main 断点和 offscreen 启动冒烟已验证。C++ 协议（含 XEAPI）、网络、会话隔离、认证事务和 SQLite 存储基础、系统密钥环的历史完整基线为13套件／329项 Qt Test pass（含数据行／初始化／清理），通过了当时的 Debug/Release CTest。当前共45套件；最新[独立每日推荐歌单](docs/api/daily-recommended-playlists.md)16受影响Debug套件753 pass＋12线程数据行14 pass，0fail/skip（新62）；原账号／MUSIC_U／空Weapi业务、root.recommend有界候选，与普通推荐／每日歌曲不同。补强同token账号-认证态私有读取屏障，14fresh路径clean／14应用对象6963真实dep禁入0／UI0，registry83；无日期cache／自动下游／新库／SDK／实网。整歌单收藏因维护者暂无v2资料明确暂缓，需求与调查保留；一起听至最后、云盘后续继续暂停。此前[搜索建议／简略及详细热搜](docs/api/search-assistance-reads.md)11受影响Debug套件631 pass、另10条线程数据行12 pass，0fail/skip（新suite117）；四类独立ID预览、严格候选schema／原16槽／token、乱序query绑定，无自动搜索／详情／图／play／报告／缓存或持久历史；13changed CPP＋2邻接fresh clean，14应用对象6963实编依赖禁入0／UI0，registry82，仅合成、无新库／SDK／实网。此前[评论五动作写](docs/api/comment-writes.md)10受影响Debug套件760 pass、另9条线程数据行11 pass，0fail/skip；新增评论suite134，发布／回复经明确批准采用封闭原生v3前置GET、无JS／SDK／新依赖／实网／UI。原账号／token／8槽与Unknown，不推断评论ID、可见性、权限或自动重试；22fresh C++ clean／Core实编依赖6957无泄漏，业务registry79。此前[歌单封面更新](docs/api/playlist-cover-update.md)10个受影响Debug套件562项、另9条线程数据行11项通过，无fail/skip；有界JPEG／去元数据／NOS三阶段与Unknown，Gui仅基础设施、无真实文件／上传／UI。此前[标签编辑／隐私歌单单向公开](docs/api/playlist-tags-and-publication-writes.md)8个受影响Debug套件563项、另9条线程数据行11项通过，无fail/skip；原账号／Catalogue工厂与Unknown，仅合成，无标签清空／反向隐私转换／UI／真实写入；歌单介绍已有修改／清空且继续回归。此前[歌曲／歌单／艺人／专辑资料与介绍](docs/api/catalog-introduction-and-metadata-reads.md)14个受影响Debug套件730项、另9条线程数据行11项通过，无fail/skip；复用已有描述，增补optional元数据／艺人分节介绍／歌曲百科基础文本，仅合成，无UI或自动级联。此前[歌单删除／两类完整范围排序](docs/api/playlist-delete-and-order-writes.md)8个受影响Debug套件463项通过、另8条线程数据行10项通过，无fail/skip；不发送分页当全量，原token绑定／Unknown，无真实写入；四类介绍资料已另包连续合成接入。此前[歌单创建／改名／描述写](docs/api/playlist-management-writes.md)7个受影响Debug套件404项通过、另7条线程数据用例9项通过，无fail/skip；创建身份仅候选schema，Unknown不盲重试／同名对账，没有真实写入。此前[五类收藏／订阅写](docs/api/collection-writes.md)7个受影响Debug套件335项通过、另7条线程数据用例9项通过，无fail/skip；红心与收藏到歌单独立，原账号／明确Unknown，无真实写入／自动重试；整歌单收藏因v2 SDK门禁经确认延期。此前[账号播客订阅／云盘读取](docs/api/account-library-reads.md)7个受影响Debug套件332项通过、另6条线程数据用例8项通过，无fail/skip；独立CloudSongId与原账号，仅合成候选，无云盘音源／播放／写入。此前[资源评论只读包](docs/api/comment-reads.md)7个受影响Debug套件共328项通过、另6条后台线程数据用例共8项通过，无fail/skip；只有合成候选schema，无实网／评论写／账号历史／通知。此前[播客消费／四类recent读取](docs/api/podcast-and-recent-reads.md)增量的7个受影响 Debug 套件共207项通过，另6条后台线程数据用例共8项通过（含init/cleanup）；该消费读取包的3次无Cookie公开metadata探测在节目列表code-462即停，节目详情／真实recent未实证；此前[实体搜索](docs/api/entity-search-reads.md)有限公开证据单列，不声明重新完成全量／Release验证；已有公开查询及单个官方试听片段的静音播放实测，仍没有 CI、真实账号／完整业务测试或正式发布包；未来规范见[工程与交付](docs/13-engineering-and-delivery.md)。

## 边界

- Cirrus 内置对接网易云官方 API，用户无需配置 API 地址或部署服务。客户端 C++ 层负责协议适配、请求签名／加密与会话管理；参考 api-enhanced 源码但不运行 Node.js 服务。
- 产品代码使用 C++/Qt；`tools/` 中 Python 仅用于离线接口审计校验，不是客户端运行依赖。
- 后端优先、UI 最后；包括每日推荐歌曲、私人 FM 和一起听。维护者明确新增播客／声音消费端范围，搜索与消费／recent读取已分包接入，后续播放独立建模并保留音源／身份实证门禁；仍排除视频、云贝、动态、云小编与创作者工作台。
- 所有音质、下载、离线和账号操作受服务端能力、账号权限及适用条款约束，不提供付费／版权限制绕过功能。
- 上游参考材料的 MIT 通知见 [third_party/api-enhanced](third_party/api-enhanced/NOTICE.md)。项目自身许可证尚待维护者确认；未添加许可证不等于允许自由复制分发。依赖许可与发布门禁见[安全、隐私与合规](docs/11-security-privacy-and-compliance.md)。
