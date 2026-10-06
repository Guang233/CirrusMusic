# 开发接手指南

> 接手入口，不需要阅读此前聊天。代码基线为认证事务 `1a60b74`，其后增加扫码调度、XEAPI、SQLite／密钥环、[匿名启动](api/anonymous-bootstrap.md)、[账号恢复](api/account-session-restore.md)与[后台装配](api/backend-runtime.md)（见 `git log`）；[验证记录](handoff-verification.md)对应的是 `1a60b74` 时点。交付方应提供**包含本文件的完整提交**，并告知 `git rev-parse HEAD`，不要只发送这个旧基线号。

## 1. 先理解现在的状态

**这是已建立后端基础和离线认证事务的项目，不是已经可用的音乐客户端。** `main.cpp`／`Main.qml` 仍是 Qt 模板，后端尚未接入入口。

| 已实现且有离线测试 | 尚未实现／尚未证明 |
| --- | --- |
| C++20／Qt 分层 targets、weapi/eapi/XEAPI、form、路由、HTTP、gzip／JSON 解码、XEAPI 公钥注册与会话密钥 | 除少量公开查询／XEAPI音源／试听播放外的广泛官方兼容性 |
| epoch 隔离、内存 Cookie、取消及晚到结果保护；SQLite 与稳定设备 ID；QtKeychain、XEAPI 公钥持久化、匿名 token 启动／恢复／重置；已登录账号凭据记忆／身份复核／恢复／退出清理 | 真实桌面密钥环／进程异常终止后的验收 |
| eapi QR key／QR check、weapi AccountIdentity、XEAPI AnonymousRegister，及搜索／歌曲详情／歌词和官方SongStream，加上专辑／艺人／歌单5项和日推／FM2项、[个人库4项读取](api/personal-library.md)，加[两个显式上报action](api/explicit-listening-reports.md)，加[云端历史两项读取](api/cloud-history-reads.md)，加[榜单两项读取](api/chart-reads.md)，加[歌单发现6项读取](api/playlist-discovery-reads.md)，加[实体／播客／声音搜索5项](api/entity-search-reads.md)，加[播客消费／4类recent读取7项](api/podcast-and-recent-reads.md)，加[资源评论8项读取](api/comment-reads.md)，加[播客订阅／云盘3项账号读取](api/account-library-reads.md)，加[五类收藏订阅8项write operation](api/collection-writes.md)，加[歌单创建／单独改名／描述3项write](api/playlist-management-writes.md)，加[歌单删除／两类排序3项write](api/playlist-delete-and-order-writes.md)，加[艺人长介绍／歌曲百科基础2项read](api/catalog-introduction-and-metadata-reads.md)，加[标签编辑／单向公开2项write](api/playlist-tags-and-publication-writes.md)，加[封面分配／提交2项write](api/playlist-cover-update.md)，加[评论点赞／取消／发布／回复／删除5项write](api/comment-writes.md)，加[搜索建议／两类热搜3项read](api/search-assistance-reads.md)，共82个业务operation，另有独立公钥注册链与获准原生v3窄前置GET；单曲协调器、受控CDN缓冲及Qt播放后端；[队列／六轨歌词时钟](api/queue-and-lyric-clock.md)及[FM连续播放](api/fm-continuous-playback.md)、[本地观测累计／会话历史](api/local-listening-history.md) | 目录／日推／FM／个人库／云端历史／榜单／歌单发现实网验收、库缓存／同步、持久历史／自动上报时机与来源扩展、真实报告及声学计量验收、流式起播、整歌单收藏v2 SDK／真实收藏写／对账、云盘播放／写入／下载、一起听 |
| `QrLoginService` 身份验证、快照收据、两阶段凭据交接、可选自动轮询与认证总时限；BackendRuntime后台装配、Core-only非阻塞Facade和关闭排空 | 模板入口接线、QML注册和UI |
| 历史完整基线：13套件／329 pass；最新搜索辅助11套件631 pass（search_assist117），另10条线程数据行12 pass，0fail/skip；现登记44套件；评论760＋11为此前独立证据；封面562＋11为此前独立证据；标签563＋11为此前独立证据；四类资料730＋11为此前独立证据；删除／排序包463＋10；歌单基础写包404＋9，收藏包335＋9，账号读取332＋8／评论328＋8／消费recent207及播放PoC是此前独立证据（含data rows及init/cleanup） | CI、发布包、跨发行版兼容性、真实账号验收 |

上游440模块（164采用候选、40待定、236排除）只是静态审计，维护者新批的播客／声音消费范围与5项搜索在增量契约中记录，不回写审计为实时支持列表；`inventory.json` 的 `not-implemented/static-only` 是审计时点标记，不作为运行注册表或实时实现进度。

## 2. 第一次接手：按此顺序执行

### A. 确认拿到了正确代码

```bash
git status --short
git log -5 --oneline
git rev-parse HEAD
```

交付方当前只进行了本地提交，不能假设远端含最新代码。仓库地址／分支由维护者提供，不凭空填写公共地址或自行推送。若不能走远端，可由交付方在确认无敏感内容后交付 Git bundle：

```bash
# 交付方：只打包需要交接的 master 历史，不打包工作目录、缓存或其他分支
# 以下命令是交付选项，本轮没有代为发布或生成交付包。
git bundle create ../CirrusMusic-handoff.bundle master
# 接收方
git clone -b master /path/to/CirrusMusic-handoff.bundle CirrusMusic
```

目录副本也可用于构建，但正式接手应保留 Git 历史。无需获取任何账号密码或真实 Cookie。

### B. 准备工具链

- Linux；当前实测 x86_64。CMake ≥3.24、Ninja、支持 C++20 的编译器。
- Qt 开发 Kit：模板要求 ≥6.10，**实际验证的是6.11.2**；需要 Core、Quick／QML、Network、Sql（含QSQLITE插件）、Multimedia（含音频后端插件）、Test及QML lint工具；模板还使用 `QtQuick.Layouts`、`QtQuick.Controls.Basic`，不能只装 Qt Core 开发包。未证明6.10或其他发行版兼容。
- OpenSSL ≥3 Crypto、zlib、Qt6Keychain 的开发头和库（系统包名依发行版而异，如 `qtkeychain-qt6`）。仅有运行库不足以编译。系统密钥环不可用时有明确错误，不明文回退。
- Python 3：可选审计校验，不是应用构建／运行依赖。不需要 Node.js。
- GDB、clangd、clang-format、VS Code 扩展是调试／编辑工具，不是运行依赖。不要盲目复制本机编译器版本作为最低版本要求。

发行版若没有匹配 Qt，请使用已有受信任 Kit；本项目还没有自动安装依赖或发行 Runtime。不要为凑依赖悄悄降 Qt、引入 WebEngine 或服务代理。

### C. 完整构建并测试，而不是只编译空窗口

以下使用共享 preset，无需本机私有文件。将 Qt 路径替换为接收机器的实际路径；若系统能发现正确 Qt，可省略该参数。

```bash
cmake --preset linux-dev -DCMAKE_PREFIX_PATH=/absolute/path/to/Qt/gcc_64
cmake --build --preset linux-dev --parallel 4
ctest --test-dir build/linux-dev --output-on-failure --no-tests=error
cmake --build --preset linux-dev --target all_qmllint

cmake --preset linux-release -DCMAKE_PREFIX_PATH=/absolute/path/to/Qt/gcc_64
cmake --build --preset linux-release --parallel 4
ctest --test-dir build/linux-release --output-on-failure --no-tests=error
```

当前 CTest 登记44个套件；少于44个先排查错误目录／`BUILD_TESTING`，不要把“无测试”当成功。13套件／329 pass为历史完整基线，最新[搜索辅助](api/search-assistance-reads.md)11 affected套件631 pass＋10线程数据行12 pass（含init/cleanup），0fail/skip；原read16／token／typed独立IDs与候选schema，无实网／SDK／新依赖／UI／自动search／cache／history。15fresh CPP（13changed＋2adjacent）clean、14应用对象6963真实依赖禁入0，详细预算／失败修正／复跑见契约；此前[评论五动作写](api/comment-writes.md)10受影响套件760 pass、9线程数据行11 pass（含init/cleanup），0fail/skip，无实网／JS／SDK／UI／新库；原生v3前置经明确批准，22fresh CPP clean／14应用对象6957实编依赖禁入0，详见新契约。此前[封面更新](api/playlist-cover-update.md)10受影响套件562 pass、另9条线程数据行11 pass，无fail/skip；此前[标签／单向公开](api/playlist-tags-and-publication-writes.md)8个受影响套件563 pass、另9条线程数据行11 pass，无fail/skip；此前[四类资料／介绍](api/catalog-introduction-and-metadata-reads.md)14个受影响套件730 pass、另9条线程数据行11 pass，无fail/skip；此前[删除／完整范围排序](api/playlist-delete-and-order-writes.md)8个受影响套件463 pass、另8条线程数据行10 pass，无fail/skip；此前[歌单基础写](api/playlist-management-writes.md)7个受影响套件404 pass、另7条线程数据行9 pass，无fail/skip；此前[收藏订阅写](api/collection-writes.md)7个受影响套件335 pass、另7条线程数据用例9 pass，无fail/skip；此前[账号补充读取](api/account-library-reads.md)7套件332＋8 pass；此前[评论只读包](api/comment-reads.md)7套件328＋8 pass；此前[消费／recent工作包](api/podcast-and-recent-reads.md)共207 pass，另6条线程数据用例共8 pass；不自动累加为新的完整验证报告。真实密钥环往返也默认跳过。首次接手可执行上述完整命令；日常小增量优先编译／测试受影响目标，不必每次重复整套 Debug/Release。

VS Code：按 [17](17-vscode-development.md) 创建私有 preset，选择匹配的 configure/build preset；`Ctrl+Shift+B` 现构建 `all`，`Cirrus: test` 先构建再跑 CTest。后端没有链接到空壳 app，**只构建 `appCirrusMusic` 不能验收后端改动**。

仅 CLI 配置后，clangd 需要刷新：

```bash
cp build/linux-dev/compile_commands.json build/compile_commands.json
```

源码入口运行仍只显示模板窗口，不能用它测试真实登录或播放。

### D. 可选审计与固定参考源码

```bash
python3 tools/validate_api_inventory.py
python3 -m unittest discover -s tools/tests -p 'test_*.py'
```

预期440模块校验及15项 Python 测试通过；不需要上游 checkout。若任务需要核对源码，另行获取锁定参考：

```bash
# 需要 GitHub 网络访问；目标目录须不存在，已有目录先检查而非覆盖
mkdir -p build/reference
git clone --no-checkout https://github.com/NeteaseCloudMusicApiEnhanced/api-enhanced.git build/reference/api-enhanced
git -C build/reference/api-enhanced checkout --detach a8c781fd64faab17fedfd46e0615a2609307f163
python3 tools/validate_api_inventory.py --upstream build/reference/api-enhanced
```

以 `docs/api/upstream-lock.json` 为准，不自动更新到 upstream HEAD；不执行 `npm install` 或运行参考服务。若固定提交无法取得，保留阻塞证据并联系维护者，不静默替换版本。`build/reference/` 中此前临时提取脚本不是产品或测试依赖，不需要复制。

## 3. 最小阅读与代码导航

顺序：本页 → [实现记录](api/native-foundation.md) → [系统架构](03-system-architecture.md) → [认证事务](api/authentication-transaction.md) → [已完成的轮询任务契约](tasks/B-010-qr-polling.md)。遇到协议疑问再查 [API 审计](api/README.md)和[门禁](api/gaps-and-hazards.md)，不必先读完440模块。

| 入口 | 看什么 |
| --- | --- |
| `src/CMakeLists.txt`、`tests/CMakeLists.txt` | 实际依赖方向、测试 target；不是架构图中的未来空模块 |
| `src/domain/account/session_types.h` | Token、phase、身份值；token 不是凭据 |
| `src/application/account/session_coordinator.*` | lifecycle 与重入安全的两阶段转换 |
| `src/application/account/qr_login_service.*` | begin/poll/cancel、自动轮询与总时限、身份确认、精确 target 提交 |
| `src/application/account/login_scheduler.*` | 单调时钟＋单次定时器窄接缝；测试替身在 `tests/support/fake_login_scheduler.h` |
| `src/application/ports/qr_login_gateway.h` | 窄 port、按值结果、脱敏错误；无 JSON／Cookie |
| `src/infrastructure/netease/api/api_operation.*` | 目前82项业务operation的封闭注册契约；评论read adapter为comment_operation.*，write adapter为comment_write_operation.*；动态路径只允许规范数字ID后缀 |
| `src/infrastructure/netease/api/session_api_client.*` | 捕获 token/origin、快照和一次性收据 |
| `src/infrastructure/netease/auth/` | QR 映射、身份 schema、port 实现 |
| `tests/unit/qr_login_service_test.cpp` | Core-only fake port 与虚拟时钟用例 |
| `tests/unit/auth_transaction_test.cpp` | 从实际协议代码到应用的合成网络认证链 |
| `src/infrastructure/bootstrap/anonymous_bootstrap.*`、`tests/unit/bootstrap_test.cpp` | 安装级匿名恢复／注册，重置后排空在途写入再删除的边界 |
| `src/infrastructure/netease/auth/account_credential_vault.*`、`tests/unit/account_restore_test.cpp` | 单槽位账号记录、版本隔离、身份复核与read/write/logout竞态 |
| `src/application/backend/backend_facade.*`、`src/infrastructure/runtime/backend_runtime.*` | 调用方Core-only入口、后台装配／启动／关闭；契约见[后台生命周期](api/backend-runtime.md) |
| `src/infrastructure/secrets/tracked_secret_store.*` | 原接收者销毁后仍追踪真实keyring操作完成，vault排空前不销毁后端 |
| `src/domain/music/`、`src/application/music/`、`src/infrastructure/netease/music/` | 歌曲查询、原token用例、JSON映射与六轨歌词解析；见[音乐查询契约](api/music-queries-and-lyrics.md) |
| `src/domain/playback/`、`src/application/playback/`、`src/infrastructure/media/`、`netease/music/stream_resolver.*` | [官方音源／试听／TTL、固定load身份、独立Qt媒体对象与临时缓冲](api/stream-resolution-and-playback.md) |
| `src/application/playback/listening_session.*`、`playback_queue.*`、`lyric_timeline.*` | Runtime.listening()调用方入口；队列身份、有限续播、六轨时钟索引，无网络／解码器依赖 |
| `src/application/music/personal_fm_source.*`、`netease/music/discovery_mapper.*` | [日推读取／FM条目供给](api/discovery-and-fm.md)；Runtime.fm()仅供给；[连续播放](api/fm-continuous-playback.md)由ListeningSession.startFm()独占驱动 |
| `src/domain/music/library_types.h`、`netease/music/library_mapper.*`、`tests/unit/library_test.cpp` | [当前账号个人库](api/personal-library.md)：歌单／喜欢ID／收藏专辑及艺人，认证与账号绑定，无缓存／写入 |
| `src/application/playback/observed_playback.*`、`listening_history.*` | 媒体进展计量值模型与256条会话账本；Qt采样在基础设施；无持久化／官方报告 |
| `application/ports/listening_report_gateway.h`、`netease/music/listening_report_gateway.*` | [显式上报能力](api/explicit-listening-reports.md)：每调用一个action，clientlog/osx profile；无自动触发／重试 |
| `src/domain/music/cloud_history_types.h`、`netease/music/cloud_history_mapper.*`、`tests/unit/cloud_history_test.cpp` | [最近听过／听歌排行](api/cloud-history-reads.md)：当前账号有界只读，次数／分值缺失为未知，不推导时长；响应仅合成验证 |
| `src/domain/music/chart_types.h`、`netease/music/chart_mapper.*`、`tests/unit/charts_test.cpp` | [榜单列表／内容摘要](api/chart-reads.md)：Guest／账号只读，展示预览非Song，歌曲页和显式播放复用原链 |
| `domain/music/playlist_discovery_types.h`、`netease/music/playlist_discovery_mapper.*`、`tests/unit/playlist_discovery_test.cpp` | [歌单发现6项](api/playlist-discovery-reads.md)：推荐无offset、普通offset、精品before与独立标签；原目录／显式播放复用 |
| `domain/music/search_assist_types.h`、`music/search_assist_mapper.*`、`tests/unit/search_assist_test.cpp`、`tests/support/synthetic_search_assist.h` | [搜索建议／两类热搜](api/search-assistance-reads.md)：3read、四typed ID/name预览、raw热词/score、strict order／optional与20/16/100，本地关键词256；原16槽／token及乱序query/id、close Caller Canceled，无自动search／下游／cache／持久history，117＋受影响631及线程12，仅合成 |
| `domain/music/entity_search_types.h`、`netease/music/entity_search_mapper.*`、`tests/unit/entity_search_test.cpp` | [5项实体／播客／声音搜索](api/entity-search-reads.md)：独立Podcast／Voice ID，无歌曲身份／报告适配，有限无Cookie公开结构验证 |
| `domain/music/podcast_types.h`、`recent_entity_types.h`、`netease/music/podcast_mapper.*`、`recent_entity_mapper.*`、`spoken_mapping.*` | [消费3项／recent4项](api/podcast-and-recent-reads.md)：ProgramId独立，原账号／MUSIC_U／候选schema；列表公开-462停止，Voice对应／节目详情／recent实证未完成 |
| `domain/music/comment_types.h`、`api/comment_operation.*`、`music/comment_mapper.*`、`tests/unit/comments_test.cpp` | [资源评论](api/comment-reads.md)：普通4资源＋通用5资源的热门／新版／楼层／统计，Voice／艺人无已确认绑定；读取与下述独立写隔离，账号历史／通知未交付，候选schema仅合成 |
| `domain/music/comment_write_types.h`、`application/ports/comment_write_gateway.h`、`api/comment_write_operation.*`、`music/comment_write_gateway.*`／`comment_v3_issuer.*`、`protocol/comment_v3_policy.h`、`tests/unit/comment_writes_test.cpp` | [评论五动作写](api/comment-writes.md)：4variant／5operation、原账号／独立8槽／business NotSent-Unknown；发布／回复经批准原生v3 fixed GET→XEAPI，无SDK／JS／外部Core token／推测评论ID／重试／隐式读；真实权限／UI与schema未验收 |
| `domain/music/account_library_types.h`、`music/account_library_mapper.*`、`tests/unit/account_library_test.cpp` | [账号补充读取](api/account-library-reads.md)：订阅播客／云盘列表／详情，CloudSongId与catalog引用分开、原账号／MUSIC_U；候选schema，无云盘播放／写入 |
| `domain/music/collection_types.h`、`application/ports/collection_gateway.h`、`netease/api/collection_operation.*`、`music/collection_gateway.*` | [收藏订阅写](api/collection-writes.md)：红心／收藏到歌单／专辑／艺人／播客5类10动作；8槽、原账号／Unknown、无重试／自动读，整歌单收藏v2门禁延期 |
| `tests/unit/playlist_writes_test.cpp`及上述collection_types／adapter／gateway链 | [创建／改名／描述](api/playlist-management-writes.md)：3operation、固定NORMAL、候选创建ID／Unknown、原8槽；不按同名判唯一，无batch／重试／自动读／删除／上传 |
| `domain/music/playlist_order_types.h`、`tests/unit/playlist_order_test.cpp`及collection写链 | [删除／排序](api/playlist-delete-and-order-writes.md)：单目标删除、完整索引／明确首末账号页工厂的置换、原source token／账号、预算与Unknown；无分页误当全量／CAS／自动刷新 |
| `domain/music/introduction_types.h`、`music/introduction_mapper.*`／`metadata_mapping.h`、`tests/unit/catalog_metadata_test.cpp` | [四类资料／介绍](api/catalog-introduction-and-metadata-reads.md)：复用原详情／描述，增量optional字段；独立艺人分节长介绍／歌曲四种基础百科文本，原16槽／token；候选schema，不转发JSON／生成叙事／自动下游 |
| `domain/music/playlist_tag_edit_types.h`、`tests/unit/playlist_tag_writes_test.cpp`及collection写链 | [标签编辑／单向公开](api/playlist-tags-and-publication-writes.md)：当前token的Catalogue成员工厂、1..3／128本地预算，不清空；公开只固定numeric0、原账号／8槽／Unknown，无自动读／反向隐私转换 |
| `domain/music/playlist_cover_types.h`、`music/cover_upload.*`、`tests/unit/playlist_cover_writes_test.cpp`及collection链 | [封面更新](api/playlist-cover-update.md)：JPEG字节1MiB／1024方形、经批准Gui仅基础设施，准备／分配／NOS／提交，原账号8槽及阶段Unknown／无自动读，只有合成、无文件路径或真实上传 |
| `tests/support/` | fake HTTP、解码测试辅助；不是产品运行组件 |

## 4. 不可破坏的设计约束

- 封面基础设施已获准PRIVATE依赖QtGui／JPEG插件做解码／重编码；不扩展到Core／UI或其他图片格式，不读取私人文件／真实上传；无SDK或第三方图片库。
- domain／application 仅 Qt Core 值与用例；不引入 Qt Network、JSON、OpenSSL、具体 NetEase 实现或 QML。Qt Multimedia已获准引入基础设施层；domain／application仍不链接Network／Multimedia。
- `803 ≠ Authenticated`。身份查询验证后 prepare 到 **Committing**；凭据交给返回的**确切 target**并确认后，才能 complete 到 Authenticated。Committing 不可分发账号私有用例。
- 身份请求前捕获 Cookie 快照，不能在身份 A 返回后复制已更新成 B 的当前 jar。普通 epoch 前进清凭据，不自动继承。
- 所有完成信号带原 token；取消只是优化，消费者必须校验上下文。重入退出／换号优先于旧成功结果。
- 当前所有认证对象在同一个后端事件循环线程构造、使用、销毁；不得直接跨线程调用同步返回 requestId 的 API。client 含值成员 CookieJar，不能构造后整体 moveToThread。
- 正常程序close保留记忆，signOut才删除账号；事件循环持续到Facade.stopped，密钥环不回调时只报告closeDelayed，不强杀或虚报擦除。Runtime只移动空Worker壳，资源在线程内创建。
- service 必须先于其引用的 SessionCoordinator 销毁；正常装配让 gateway/client 覆盖 service 生命周期。不要假设给多个对象设置同一 QObject parent 就保证正确析构顺序。
- 凭据不进入应用层、QML、普通配置或日志；UUID 收据不能持久化。大数值 ID 不安全时拒绝，不浮点舍入补救；不同作用域 MUSIC_U 冲突不猜选。
- 后端先行，UI 最后。每日推荐歌曲独立建模；私人 FM／一起听及维护者新增播客／声音消费端范围纳入；视频/MV、云贝/云豆、动态、云小编、创作者上传／审核工作台排除。PodcastId／VoiceId不强转PlaylistId／SongId。不引入第三方音源解锁。

## 5. 接下来做什么，什么时候需要询问

**最新范围覆盖旧剩余清单**（接续acf822e）：维护者不要求专用下载API，未来下载优先复用播放音源接口；下载权利／试听限制／过期与文件恢复另验，临时缓冲不是下载交付。云盘后续API／音源／播放／写暂缓，原账号已有读取保留。一起听和整歌单收藏／取消明确加入待办：先一起听9 HTTP契约／接入，接收／seq／version／时钟／恢复未证不称完整同步；收藏另调查固定Watchman v2原生前置，不执行SDK／JS／Node／WebEngine、新库／实网，不以评论v3替代。需要改变这些边界再询问。声音Program/Voice与底层音轨关联、官方URL／权限／TTL／preview链本次只是解释，未默认开始实现。此次范围记录docs-only，registry82／CTest44不变，无新代码／build／LSP／测试或实网证据；搜索辅助631＋12仍独立历史，本地提交、不push。

[B-010 子任务：扫码轮询与总时限](tasks/B-010-qr-polling.md)已实现并离线验收，可作为任务契约写法的范例。

[XEAPI 编码与公钥状态](tasks/B-002-xeapi.md)已实现。[存储基础与设备 ID](tasks/B-008-storage-foundation.md)已实现。[密钥环与 XEAPI 公钥持久化](tasks/B-010-secret-store.md)已实现。[匿名 bootstrap](api/anonymous-bootstrap.md)与[账号安全持久化／恢复／退出](api/account-session-restore.md)已实现；默认新扫码登录仅本次，记住需显式选择。[后台线程／非阻塞facade与启动／关闭排空](api/backend-runtime.md)也已完成。[搜索／歌曲详情／三语言歌词](api/music-queries-and-lyrics.md)已接入，有少量官方公开只读实测。[官方URL／受控CDN／Qt单曲播放](api/stream-resolution-and-playback.md)已完成基础闭环和静音试听PoC，[队列与六轨歌词时钟](api/queue-and-lyric-clock.md)也已完成，[专辑／艺人／歌单目录](api/catalog-queries.md)也已接入并离线验证，[每日推荐读取／私人FM条目供给](api/discovery-and-fm.md)已接入；[FM播放接线](api/fm-continuous-playback.md)已完成，[个人库四项读取](api/personal-library.md)也已离线接入；[本地观测累计／会话历史](api/local-listening-history.md)已接入；[显式上报能力](api/explicit-listening-reports.md)也已接入；自动触发时机留到播放接线时讨论。[最近听过／排行读取](api/cloud-history-reads.md)已离线接通，[榜单列表／摘要读取](api/chart-reads.md)也已接入并复用现有歌单选歌播放链；[推荐／普通／精品歌单及三类标签](api/playlist-discovery-reads.md)也已成组离线接入；[音乐实体、播客及全站声音搜索](api/entity-search-reads.md)已接入并有限公开验证，[消费详情／节目与4类recent](api/podcast-and-recent-reads.md)已合成接入，公开列表遇-462已停；ProgramId／VoiceId对应和节目详情／真实recent仍待实证，[音源／身份补证](api/spoken-playback-contract.md)已记录传统mainTrackId路线、v1第三方代码候选和独立身份／媒体／历史设计，但实际binding／v1权益仍未实证，没有新增播放入口；[资源评论五类／八operation](api/comment-reads.md)已成组只读接入并合成验证，普通读取不含播客容器，Voice／艺人不强转；[评论五动作写](api/comment-writes.md)后续已获准原生v3并合成接入，历史／通知未接入，实网另授权；[播客订阅／云盘列表与详情](api/account-library-reads.md)三项原账号读取已合成接入，独立CloudSongId与可选catalogReference，不自动音源／播放／写入；[红心／收藏到歌单／专辑／艺人／播客收藏订阅](api/collection-writes.md)五类已成组离线接入，8条在途与写入Unknown、不自动读／重试；整歌单收藏／取消此前因v2 SDK门禁延期，现重新明确要求补齐，原生前置待证，仍无绕过入口；[创建／单独改名／描述写](api/playlist-management-writes.md)已合成接入，共用8槽与账号屏障，候选创建ID缺失／矛盾Unknown、不盲重试／同名对账；[删除／完整范围排序](api/playlist-delete-and-order-writes.md)已合成接入；[标签编辑／隐私歌单单向公开](api/playlist-tags-and-publication-writes.md)已合成接入，原Catalogue token／8槽／Unknown，介绍仍独立修改／清空；标签清空／公开转私密无入口；[封面更新](api/playlist-cover-update.md)已合成接入，有界JPEG重编码、分阶段Unknown、原账号／8槽，无真实文件／上传；格式扩展／裁剪／可靠核验仍另确认，连续[四类资料／介绍](api/catalog-introduction-and-metadata-reads.md)已合成交付，真实schema／复杂百科与UI仍另验收，实网另授权；受限时继续其他已确认小包，不直接当SongId；歌单内搜索／工作台仍待补证，不以本地过滤或管理接口代替；本地保留时长计量，详细历史持久化按分析需求再定；真实报告另行授权；真实认证／密钥环验证另行授权。详细顺序见[路线图](14-roadmap-and-backlog.md)。不要一次把这些全部塞进一个 Manager 或单次提交。

| 待决事项 | 安全默认／询问时机 |
| --- | --- |
| 安全存储实现与新增依赖 | QtKeychain 系统包（`qtkeychain-qt6`）已接入；无后端时只留内存，不明文 fallback |
| app ID、自身许可证、发行 Runtime | 由维护者决定；不阻碍后端单测，不擅自填公司域名或发布许可证 |
| 真实账号与音源验证 | 必须明确授权并安排脱敏流程；不在聊天或仓库提供凭据 |
| v2 checktoken SDK、一起听接收链 | 整歌单收藏／取消固定链每次需v2 SDK，原生未接，经确认延期；SDK与一起听仍有证据／许可门禁，不暗加Node/WebEngine、不以v3／改协议替代v2 |
| 播客／声音后续 | [补证决策](api/spoken-playback-contract.md)为docs-only，旧公开列表-462停止；传统mainTrackId文档路线已获证，v1实网／Voice关系仍未知，不换host／工作台或自动登录；账号／挑战／媒体验证另授权 |
| 播放后端 | Qt Multimedia已明确获准接入；公开试听MP3／合成WAV PoC通过，完整缓冲后起播；流式、设备切换／打包等仍有门禁 |

当前没有指定新的接手人／问题负责人；由维护者分配。详见[风险与决策](15-risks-and-open-questions.md)。

## 6. 交付与接收检查

- [ ] 交付方告知完整提交、分支和传递方式；接收方确认含本页及下一任务文件。
- [ ] 从干净源码配置／构建／测试，而非复用发送者的 build 目录；结果与[验证记录](handoff-verification.md)比较。
- [ ] 接收方找到 service、port、native adapter 及其测试，能说明为何803不是登录完成。
- [ ] 领取单一任务，记录验收项；新依赖／真实访问先获授权。
- [ ] 每个增量按影响范围记录必要编译／测试结果及未复验项目，剩余风险同步更新本页和实现记录；提交、推送、发布分别授权。

不交付：`CMakeUserPresets.json`、Qt Creator 私有配置、Qt 安装目录、编译数据库、构建缓存、本机日志、真实凭据。以上接收勾选项尚待实际接手者执行，不因本机复验而代勾。
