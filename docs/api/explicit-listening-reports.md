# 显式听歌上报能力（不接自动时机）

## 1. 本包范围

维护者确认：先做上报能力，具体播放生命周期的触发时机稍后确定；不顺带实现最近听过／听歌排行读取。网易云记录作为未来历史展示的主要数据源，本地保留[时长计量](local-listening-history.md)，详细本地历史持久化／分析不是默认下一项。

`BackendFacade::reportListening(ListeningReport)`返回非零本地命令ID，后续 `listeningReportCompleted(id, originalContext, result)`返回明确结果；0表示本地拒绝、未派发该调用。不从UI／调用方接收uid、URL、日志JSON、Cookie、os或UA。Facade从当前已验证快照捕获accountId与token，后台 `ListeningReportGateway`再核对SessionCoordinator的Authenticated状态、原token与原账号，然后使用现有SessionApiClient。

报告是独立写port `IListeningReportGateway`，不是 `MusicQuery`／只读查询的新变体。后台复用原线程／client，8条在途上限；关闭先销毁gateway再client，挂起报告会被取消而不阻塞已建立的资源排空。domain/application仅Qt Core，产品入口／UI不变。

**本包没有新官方访问、真实账号／keyring访问、实际听歌报告或自动上报。** 只做合成离线验证。功能已可由显式调用驱动，但尚未用于实际播放；真实报告需要独立授权。用户关于切歌等低频上报的实测输入用于后续时机设计，不据此宣称已经验证服务端阈值。

## 2. 固定请求与来源约束

参考固定 `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `module/scrobble.js`、`util/request.js`、`util/config.json`。scrobble模块SHA-256与inventory匹配。上游是第三方兼容参考，不是官方文档，静态inventory仍保留审计时点状态。

新增两个封闭operation（19→21），同一逻辑路径 `/api/feedback/weblog`、eapi、明文JSON响应、64KiB上限：

| operation／action | 入口参数 | 日志内容 |
| --- | --- | --- |
| ListeningStart／StartPlay | song、playlist、eventId；playedSeconds必须0 | `action:startplay`，json含id、type:song、mainsite/mainsiteWeb:"1"、`content:"id=<playlist>"` |
| ListeningPlay／Play | 同上，playedSeconds为1～604800整数秒 | `action:play`，增加sourceId、time、source:list、download:0、wifi:0、end:playend |

`logs`是单元素数组的紧凑JSON字符串，歌曲／来源ID使用规范正signed64十进制字符串，不把wide ID转double。时间上限只是输入／溢出防护，不是网易云的计数门槛。时间由调用方提供，客户端不能证明显式调用方传入值的真实性；未来接线应使用真实累计而非最终进度／歌曲总长，整数秒换算与何时达标另定。

当前**仅支持明确的歌单来源**；playlist不能为空、0或伪造值。没有证据时不把搜索、日推、专辑、普通队列或FM硬写成list来源。ListeningRecord的queueEntry UUID不是远程playlistId。来源扩展需先补正确日志字段；本包不能声称任意播放来源都已能正确上报。固定 `end:playend`仅是兼容payload，不把未知的切歌原因冒充已验证的终止语义；自动切歌／错误结算接线前再核对。

### clientlog与兼容profile

实际目标固定为 `https://clientlog.music.163.com/eapi/feedback/weblog`。增加窄 `OfficialDestination::ListeningLog`：仅Eapi与这个确切逻辑路径可用，其他组合拒绝；不提供任意domain覆盖、备用域名或重定向。默认eapi仍为interfacepc，其他业务不变。

仅报告operation选用 `ListeningLogOsx` profile：os=osx、osver=15.5、appver=3.1.10.5100，以及固定Mac浏览器UA，来源为固定参考。它不是实际运行平台检测。签名路径仍为逻辑 `/api/feedback/weblog`。

SessionApiClient依旧从原规范eapi凭据作用域捕获当前Cookie值；**只有封闭的ListeningLog分支**将这些已验证凭据按兼容header送往固定官方clientlog主机，不放宽CookieJar的接收／恢复域白名单，也不跟随服务端URL。weblog必须有非空MUSIC_U，匿名MUSIC_A不足以发送。业务报告响应不回写Cookie或提升身份；临时header与日志JSON不进入application／QML、仓库或日志。

## 3. 两个动作分开，不隐藏部分结果

不照抄上游包装器“顺序两次请求后包装code200”的流程。一次显式调用只产生一个动作／一个HTTP请求；StartPlay成功不自动发送Play，Play失败不覆盖已获得的StartPlay确认。调用方可以使用相同eventId关联两个不同action，分别保留结果，这就是部分成功的处理，不伪造两动作事务或总成功。

| outcome | 含义 |
| --- | --- |
| NotSent | 账号／参数／上下文／容量／凭据等检查未通过，已知尚未交给transport |
| Acknowledged | 当前原账号上下文下收到合法code200；只代表服务端响应确认，不保证已进入最近记录／排行或最终计数 |
| Rejected | 收到合法的非200业务码；保留providerCode，不承诺所有服务端状态都无副作用 |
| Unknown | 交给transport后取消、网络／超时／HTTP错误、解码失败、账号失效等，无法证明服务端是否已处理 |

`ApiResult::submittedToTransport`是保守的“可能已发送”边界，不是收到服务端的证明。排队至transport但尚未真正发出时也可能报Unknown；宁可不承诺撤销成功。Facade取消／切号／退出／关闭时因调用线程不知道发送阶段，对已接受命令统一以Unknown完成，携带原token；晚到成功不能重新归属于新账号。临时值副本仍需调用方按账号失效清理。

429保留可校验的Retry-After信息，但**没有任何自动重试、离线重放、退出flush、定时批量上报或持久outbox**。eventId只是本地关联值，不发送到服务端，不是幂等键；Facade只拒绝同eventId+action的在途重复调用，不承诺完成后的重复调用、跨进程或多设备exactly-once。今后的播放接线需按原load结算去重，并明确未知结果策略；不能为了“补成功”盲目重试刷次数。scrobble_v1与其他变体保持未实现，不同时发送多个版本。

## 4. 离线验证

本机Qt6.11.2 Debug，`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`；最新受影响结果（不累加重复运行，含init/cleanup与数据行）：

| suite | pass |
| --- | ---: |
| listening_report | 18 |
| netease_protocol | 40 |
| http_transport | 22 |
| session_api_client | 24 |
| backend_facade | 8 |
| listening_history | 13 |
| 合计 | **125** |

线程回归 `listeningReportThroughThread`、`playbackThroughThread`的Queue/FM两行、`destructionStillDrains`共**6 pass**（含init/cleanup）。报告线程用例走合成完整登录／原生eapi请求链，Start确认→Play拒绝分别保留、挂起报告中close为Unknown并排空，线程检查无违例；startup／login／playback／close不自动生成报告。

其他覆盖：精确路径／主机／osx profile、wide ID／logs schema、参数／时长／来源上限、两action不自动连发、账号错配／guest／缺MUSIC_U不发网络、响应Cookie不采用、业务拒绝／HTTP失败／坏schema、429无重试、发送前／后取消、多个在途报告同步失效、8条上限、原context和晚到隔离。超时分类用合成ApiResult注入，不声称进行了真实长时网络验收。

```bash
cmake --build --preset linux-dev-local --target \
  tst_listening_report tst_netease_protocol tst_http_transport tst_session_api_client \
  tst_backend_facade tst_listening_history tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(listening_report|netease_protocol|http_transport|session_api_client|backend_facade|listening_history)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  listeningReportThroughThread playbackThroughThread destructionStillDrains
```

19个变更C++文件active LSP无诊断，format／diff检查通过；变更文档232个本地链接无断链，13个application编译单元无基础设施／Network／Multimedia／Sql／Quick／Qml／Gui／JSON依赖穿透；main.cpp／Main.qml无改动。本包登记29个CTest套件，不是29套全量通过；没有自动重复全量认证／Release／真实声卡／keyring矩阵。忽略日志 `build/workspace-checks/report-*.log`，最终为final-build、final-focused、final-thread。开发期修正了QList append测试fixture歧义；新test的clangd在编译数据库刷新前误用旧Qt头，刷新数据库并重新解析后active检查恢复，不删除必要include。

## 5. 未决事项

自动时机、最低有效收听阈值、试听资格、来源扩展、自然End／手动切歌／失败／关闭的终止字段、跨设备行为及结果对账都未在本包决定。最近听过／听歌排行读取另包；详细本地历史持久化／分析按维护者后续需求。UI仍最后，不以接口接通宣称用户的播放已进入云端记录。
