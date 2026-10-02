# 每日推荐读取与私人FM条目供给

本读取包（`7794bd1`）仅交付账号态读取和动态条目供给，后续已另包接入[FM连续播放／普通队列模式切换与失败预算](fm-continuous-playback.md)。后续[个人库四项读取](personal-library.md)也已接入；UI、日推持久缓存及真实已听累计／scrobble仍待后续。

## 1. 封闭请求与权限边界

固定参考版本仍为 `a8c781fd64faab17fedfd46e0615a2609307f163`，只读取 `module/recommend_songs.js`、`personal_fm.js` 及 `public/docs/home.md`；不运行上游服务，不增加依赖。

| operation | 官方逻辑路径 | 原生固定参数 | 响应解码上限 |
| --- | --- | --- | --- |
| DailyRecommendations | `/api/v3/discovery/recommend/songs` | `afresh:false` | 4MiB |
| PersonalFm | `/api/v1/radio/get` | 空对象 | 1MiB |

两者均为weapi／JSON，沿用受控网络与原会话Cookie。该读取包完成时注册表有15项业务operation，最新数量见[个人库包](personal-library.md)，公钥注册仍独立。路径中的radio不改变私人FM属于产品范围的约定。

- `BackendFacade::dailyRecommendations()`／`personalFmBatch()`仅接受本地已验证的Authenticated状态；Guest、认证中、未Ready及满16槽返回0，不发送请求。
- `MusicQueryService`再次校验原token／认证态；Guest得到异步`AuthenticationRequired`。Native weapi路径也要求非空MUSIC_U，不能用匿名token代替。基础API类不负责宣布账号已验证，仍以应用认证事务为准。
- 不接受调用方日期、afresh、模式、任意URL或额外参数。重新读取不表示强制刷新服务端推荐。读取响应不采纳Cookie、不生成身份收据、不修改记忆凭据；服务端拒绝不自动删除账号记录或发起登录。
- 沿用MusicQuery／MusicResult及取消、原会话、账户generation保护。HTTP429保留可解析的Retry-After秒数；不自动重试，也不把本地Authenticated当作服务端一定接受的证明。
- 本包没有官方访问，没有读取真实账号或本地密钥环。参数路径已静态核对；响应映射只有合成数据验证，不能声称真实账号兼容。

## 2. 日推不是歌单

Core-only DTO位于 `src/domain/music/discovery_types.h`。

`DailyRecommendations`读取`data.dailySongs`，最多500首，保留顺序；每项是Song和可缺失的行内reason。`data.recommendReasons`可缺失，最多500条，以SongId与文字独立保存，不按数组下标强行对齐，也不丢弃未匹配的理由。文字仅为不可信纯文本，不能作为链接或富文本执行。共享歌曲映射支持ar/al/dt与artists/album/duration两种字段结构、规范宽ID和缺失元数据。

空数组是成功的空结果；缺少数组、错误类型、超限、非法ID或理由整体拒绝，不静默截断。没有虚构PlaylistId、trackCount、分页或“完整曲库”。

Gateway成功收到日推时附加本机UTC `receivedAtUtc`；它**不是服务端日推日期或生成时间**。不按本机零点／猜测的六点换日、不凭日期认定“今日已更新”。本包不缓存日推；调用方若保存返回值，必须同时处理原会话和本地接收时间，不能伪称服务端当天内容。

## 3. FM是有界动态批次

`PersonalFmBatch`只含本次`data`歌曲数组，最多20首。空数组有效；不假定固定三首，不虚构total／offset／hasMore。原批次保留重复；去重属于动态供给层，而不是把服务端列表改造成普通歌单。

`src/application/music/personal_fm_source.*`是调用线程Core-only QObject，Runtime通过`fm()`持有它。Facade必须活得更久；接入连续播放后，Runtime析构先销毁ListeningSession，再销毁其引用的FM供给，最后关闭Facade。

| 方法／状态 | 语义 |
| --- | --- |
| `start()` | 验证Ready＋Authenticated并启动一个内存供给会话；返回true是激活受理，不是已有歌曲或正在播放 |
| `takeNext()` | FIFO取一首；低水位≤2首时补充一次；空时返回nullopt，不忙等或自行循环 |
| `retry()` | 当前会话、无请求且余项≤2时显式再取一批；绝不并发补充 |
| `stop()` | 清空余项、近期ID、错误和上下文，取消自己的请求；不停止或操纵播放器 |
| `snapshot()`／`changed` | active、fetching、refillBlocked、upcoming、failure；没有音源URL、Cookie或账号凭据，歌曲元数据可含封面URL |

- 同时最多一个本供给层请求；提交前设置fetching，堵住同步命令观察者的重入窗口。stop／重启后，旧提交的返回ID只会被取消，不能覆盖新供给状态。
- 最多22个余项（2个存量＋20个新批次）；保存最近256个**接纳**的SongId并排除当前已排队ID。它不是全局永不重复保证，更不是“已听历史”。停止／换号清除这些信息。
- 补充失败保留现有余项；耗尽后不回绕。失败、空批次或全部重复都会设置refillBlocked，只有显式retry解除，没有后台计时重试／重复批次自旋。Busy同样可观察并等待显式重试。
- logout／换号／close后，getter和takeNext立即拒绝旧内容，即便通知还未排到。请求ID、原token与局部revision共同拒绝迟到结果；重入退出优先于返回旧歌曲。
- changed携带的是发出时的值，只作变更通知；同线程重入／排队转发后以当前snapshot()为准，不能将旧通知当成当前账号缓存提交。访问器也只能在所属线程调用。
- 使用此供给层时不要同时直接调用Facade的personalFmBatch，也不要另造一个供给实例争用同一消费序列。直接Facade入口是底层批次查询，不替多个调用者协调FM游标。

它**不接管播放命令**：没有自动将条目塞进PlaybackQueue、没有设置RepeatAll、没有发送“不喜欢”。后续[ListeningSession的FM模式](fm-continuous-playback.md)已接通媒体结束、暂停意图、错误预算和队列切换时stop供给；播放时不得再直接操作本Source。这里保留的是底层读取包证据，不将其冒充连续播放验证。

## 4. 听歌上报仍未实现

没有加入scrobble、startplay、fm_trash、日推不感兴趣或任何业务写operation。FM条目的接纳／取出、日推读取、媒体位置都不是实际收听量。后续须按真实播放区间累计，排除暂停、缓冲和seek跳跃，保留播放来源与原账号并处理重复／未知写结果。真实账号上报仍需独立授权，不能承诺计入排行。

## 5. 定向验证

Qt6.11.2 Debug，`QT_FATAL_WARNINGS=1`，没有全量／Release或新的实网、keyring、音频设备测试。

| 受影响套件的最新结果 | pass（含init/cleanup及数据行） |
| --- | ---: |
| discovery | 17 |
| personal_fm（仅Core/Test） | 11 |
| music | 26 |
| catalog | 22 |
| session_api_client | 24 |
| backend_facade | 8 |
| 合计 | **108** |
| backend_runtime仅discoveryThroughThread、musicQueriesThroughThread、destructionStillDrains | **5** |

首次组合运行发现新用例重复追加了默认已有的Retry-After:3，合成为不可解析的`3, 3`；修正fixture后只重建／复跑discovery（17 pass），未放松产品解析器。随后补强匿名token不得代替账号凭据、两项读取的Cookie隔离、理由数组上限及同步stop→重启用例，仅复跑discovery／personal_fm（17／11 pass）。上表取各套件最新成功结果，不累加重跑次数，不是全量结果；该读取包完成时登记25个CTest套件，最新数量见[HANDOFF](../HANDOFF.md)。

覆盖封闭载荷／无强制刷新、认证前无网络、宽ID／新旧歌曲结构、理由按ID独立、缺失／空／错误类型与大小边界、Cookie不回写、取消／换号／429，以及FM的低水位、单请求、有界队列、重复／空批次、保留余项、显式重试、立即隐私屏障、重入与析构取消。新Runtime用例采用全合成登录事务及HTTP，验证日推与FM跨线程传值、挂起补充时关闭排空；绝不使用真实凭据。

```bash
cmake --build --preset linux-dev-local --target \
  tst_discovery tst_personal_fm tst_music tst_catalog tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(discovery|personal_fm|music|catalog|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  discoveryThroughThread musicQueriesThroughThread destructionStillDrains
```

本机忽略日志：`build/workspace-checks/discovery-{configure,build,focused,thread,fix-build,fix-test,final-build,final-test}.log`。产品入口仍是模板，以上是后端验收，不是可用UI验收。
