# 私人FM连续播放：接入现有听歌会话

已交付离线后端闭环，未接UI或真实账号。复用[日推／FM供给](discovery-and-fm.md)、[官方单曲播放器](stream-resolution-and-playback.md)与[队列／六轨歌词时钟](queue-and-lyric-clock.md)；本包不新增operation、依赖、官方访问或账号写操作。业务registry仍为15项。

## 1. 一个播放命令生产者

`ListeningSession`新增注入`PersonalFmSource&`的构造方式，并在 `listening_fm.cpp` 实现FM编排。旧的仅Facade构造仍支持普通队列，调用startFm返回false。Source必须绑定同一个Facade，否则该构造保持普通队列能力而拒绝startFm，不能串用另一个后端的账号推荐。Facade与注入的Source必须活得更久。

Runtime构造顺序为Facade→FM供给→ListeningSession，销毁顺序相反；后台媒体对象的构造／销毁线程与关闭排空保持不变。FM控制仍通过 `runtime.listening()`，不再另造播放器或竞争播放命令的控制器。

```cpp
// 仅示例；模板main.cpp未接线。Ready且Authenticated后在调用方线程使用。
auto& session = runtime.listening();
session.startFm();
session.pause();       // 等批次时也能保留下一次load的Pause意图
session.next();        // 下一条FM歌曲，不是“不喜欢”
session.retryFm();     // 仅等待／阻塞态有效，显式重试供给或保留的待播项
session.replaceQueue({{"1"}, {"2"}}); // 普通队列接纳后才退出FM，示例ID
```

- `mode()`区分Queue／PersonalFm；`waitingForFm()`和`fmBlocked()`提供等待／需显式重试状态；`fmChanged()`只是通知，观察者重读当前getter。
- startFm可带音质与初始Play／Pause。必须本地Authenticated、依赖存在且参数有效；先接纳Stop退役旧load，才清原队列、建立FM会话并获取新批次。true不保证已有歌曲、来源可用或出声。
- FM期间不得直接调用Source的start/stop/retry/takeNext或Facade播放命令。`runtime.fm().snapshot()`仍可用于读取供给失败信息；播放快照保留媒体失败。直接Facade的新load/stop会让ListeningSession放弃所有权并清理FM，不得混用生产者。
- 原普通队列不自动保存／恢复。FM对外的queue()为空，不伪装成可循环歌单；内部单条候选仅复用现有load、歌词及快照路径。

## 2. 播放与等待规则

| 操作／事件 | 行为 |
| --- | --- |
| 批次可用 | 下一事件循环轮次取一项，提交官方来源解析／新load；不会在Source通知栈内递归播放 |
| 自然End | 每个load只处理一次，按FM供给FIFO续播，不受普通列表循环／单曲循环／随机模式影响 |
| 手动next | Stop被接纳后再等待下一项，保留Pause意图；已在等待时返回false，使用retryFm处理阻塞 |
| 等待时pause | 仅改变下一load意图，不虚报旧媒体继续播放；同一Pause幂等，支持End通知中的暂停 |
| resume | 非终态恢复当前load；等待态恢复Play意图并可显式重试；终态当前歌曲可重新解析重播 |
| previous | 不提供FM回退历史；当前位置>3秒且可seek时仅重启当前片段 |
| stop／clear | 接纳Stop后退出FM、清理供给／待播项／歌词；普通队列的stop保留队列语义不变 |
| replaceQueue | 候选Play接纳后才切回普通队列并停止FM供给；非法输入／本地满载拒绝时不提前销毁FM |
| append／select／remove／setModes | FM模式拒绝，不把FM当作普通队列编辑 |

复用六轨原文／翻译／罗马音歌词、逐行／逐字游标、试听偏移和媒体位置，不引入第二套歌词时钟。新load携带初始Pause，不能先Play再发Pause；旧歌词和旧媒体事件仍由原会话／request／load token隔离。

## 3. 有界失败与事务边界

- Source仍只有一个补充请求、低水位2、最多22个余项和256个近期接纳ID。控制器最多另外持有一个已取出但尚未被本地Play接纳的候选；满邮箱时保留它，不偷偷跳到下一首。
- 补充失败不打断当前音频，也不丢弃已有余项。余项耗尽后进入blocked，发出一次advanceStopped(true)，等待显式retryFm／resume；空批次／重复批次不自动请求自旋。
- startFm的首曲、手动next和显式重试属于手动链，媒体失败停住；自然结束开启自动链，后续媒体失败最多尝试5首。Ready本身不刷新预算；新的自然End或显式用户意图才重置。预算耗尽后保留剩余供给供用户决策，不回绕或重试同一个过期URL。
- 底层仍保留严格CDN／TLS／试听／权限／有效期约束；换到另一官方歌曲不表示破解当前歌曲权限。此处不承诺错误汇总UI或无缝切歌。
- 本地Play／Stop返回0时回退本次未提交的revision，不能使旧的已接纳调度任务失效而永久等待；若同步观察者已发出新意图，则不回退新revision。
- token检查也覆盖同步的直接Facade新命令；不能把别人的load收为自己的。停止供给和取消旧歌词可能触发重入，因此先提交状态、按原ID退役旧请求，再检查revision，后续旧通知不能覆盖新播放选择。
- 切号、注销和close立即隐藏旧访问器，迟到批次、歌词或媒体结果不能恢复旧会话。QObject信号只作通知；重入／转发排队后以当前getter为准。

析构ListeningSession会停止其拥有的FM供给并取消歌词，但不替独立所有者停止音频；独立使用时仍先stop或关闭Facade。Runtime随后close并排空后台；事件循环必须活到stopped，不把析构当作同步退出或持久擦除。

## 4. 定向证据

本机Qt6.11.2 Debug，`QT_FATAL_WARNINGS=1`：

| 受影响套件最新结果 | pass（含init/cleanup） |
| --- | ---: |
| fm_listening | 15 |
| listening | 20 |
| personal_fm | 11 |
| playback | 9 |
| backend_facade | 8 |
| 合计 | **63** |
| backend_runtime仅playbackThroughThread（queue／fm两行）、discoveryThroughThread、destructionStillDrains | **6** |

首轮5套件通过后，补强等待态幂等暂停、拒绝命令的调度保留和同步Facade竞争，仅重建／重跑两个Listening套件及上述线程用例。随后又补上Source／Facade实例绑定校验，仅复跑FM套件（15 pass）及上述线程用例（6 pass）。上表使用各套件最新结果，不叠加重复运行，不是全量／Release结果。当前登记26个CTest套件。

覆盖初始／等待／切歌暂停、自然End去重、普通循环不影响FM、供给失败保留余项／耗尽／空批次、手动失败停止、自动失败最多5首且Ready不刷新、满邮箱保留候选、模式切换提交边界、取消／换号／关闭、Source回调中新队列意图、停止旧FM时重入新FM以及直接Facade抢占。线程用例经过合成认证事务、真实原生音源与歌词解析、注入播放器和挂起补充请求，验证FM自然结束续播、终态观察者暂停下一曲以及关闭排空；没有真实网络、声卡或账号数据。

```bash
cmake --build --preset linux-dev-local --target \
  tst_fm_listening tst_listening tst_personal_fm tst_playback tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(fm_listening|listening|personal_fm|playback|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playbackThroughThread discoveryThroughThread destructionStillDrains
```

忽略的本机日志在 `build/workspace-checks/fm-playback-*.log`，普通队列末次证据为final-core，FM与线程末次为binding-build、binding-core、binding-thread。没有全量认证矩阵、Release、实网、真实keyring／设备或广泛官方兼容性验收。

## 5. 仍未实现

UI、个人库读取、持久播放历史／队列、实际收听区间累计、scrobble／startplay／不喜欢反馈及FM模式偏好均未因本包完成。FM批次取出、媒体位置或自然End都不是“实际已听时长”；本包没有任何上报，真实写入另行授权。下一业务包转个人库读取，实际已听累计／播放历史与上报另立契约。
