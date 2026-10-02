# 队列与六轨歌词时钟：最小听歌闭环

> 已实现后端，未接入模板入口／UI。本包不增加协议、依赖或官方访问，沿用[官方来源与单曲播放器](stream-resolution-and-playback.md)和[六轨歌词查询](music-queries-and-lyrics.md)。后续[目录读取](catalog-queries.md)也已接入；[日推读取／FM动态供给](discovery-and-fm.md)也已接入，[FM连续播放](fm-continuous-playback.md)现已通过ListeningSession的独立模式接通，不继续扩大音频框架。

## 1. 装配与入口

- `PlaybackQueue`：Core-only可复制值模型，管理UUID条目、播放排列、有限历史。相同SongId可以有不同entryId。
- `LyricTimeline`：Core-only六轨区间索引，只做给定位置的定位，没有定时器／播放器／JSON。
- `ListeningSession`：调用方线程的Core QObject，通过异步 `BackendFacade` 发起播放与歌词查询。`BackendRuntime::listening()`返回其引用，Runtime拥有它并在Facade之前销毁它。账号／其他查询仍通过Facade。
- 使用队列时以ListeningSession作为**唯一播放命令生产者**；不要同时直接调用Facade.playSong／stopPlayback。外部播放意图会使本队列失去当前load所有权并清空。独立使用时Facade必须更长寿；释放服务前由所有者停止播放或关闭Runtime，普通队列析构仅取消自己的歌词请求；后续FM模式还会停止其拥有的供给源，注入的Source也须更长寿。
- 公开方法在该对象所属线程调用，返回true表示本地操作／异步命令已接纳，不代表来源可用或已出声。网络、解析协议、QMediaPlayer仍在后台；服务不拥有或跨线程触碰这些对象。

```cpp
// 后端Ready且会话已有效后；并非当前main.cpp的接线代码
// #include "application/playback/listening_session.h"
auto& listening = runtime.listening();
listening.replaceQueue({{"1"}, {"2"}}, 0); // 合成示例ID
listening.pause();
listening.next(); // 新load原子携带Pause，不能先Play再补发Pause
```

窄入口包括replaceQueue、append（可指定playNext）、select、remove、clear、next／previous、pause／resume／stop、seek、音量、模式与歌词偏移。访问器返回脱敏值，不含签名URL。信号作为变更通知，重入调用后以当前访问器为准，不继续应用已失效的旧值。

## 2. 队列规则与有界续播

- 最多10000条，输入非法／超限整次拒绝，不截断后声称“全部播放”。尚未实现大歌单分页源。
- 普通append追加；playNext插在当前之后，连续插入按提交顺序。回退后插入会取代旧的前进历史分支，保证新插入项确实下一首播放。
- 顺序或随机与不循环／列表循环／单曲循环独立组合。随机保存本轮排列，初始指定项放在本轮首位，其余各走一次；新轮多首时避免边界重复。普通追加项进轮尾；手动选曲／改变顺序会改变后续路径，不声称全局永不重复。
- 历史最多1024项。上一首在当前片段位置>3000ms且可seek时回片段开头，否则沿真实历史回退（首次选择没有历史就返回false），不是重新随机；下一首可沿前进历史返回。
- 单曲循环仅在自然结束时重播当前；手动next仍前进，循环模式末尾允许回到首项。不循环末尾保留Ended，不偷偷环绕。
- 删除当前项选择删除前的原后继并保持暂停意图；无后继则停止、取消选择，不偷偷绕回之前的条目。stop保留队列和当前选择；clear清空。
- 换曲先构造候选队列；Facade接纳新load后才提交。邮箱满时不虚构新选择。信号观察者重入的新意图优先，旧clear／remove后续不能覆盖新队列。
- 重复／旧load End不会二次前进。自然结束在下一事件循环轮次处理，避免从媒体信号栈里递归切歌。
- 直接点选失败停留原失败状态；自动续播失败可跳过，每轮最多尝试 `min(队列条数,5)` 个不同entry，不绕过单曲循环限制，不重试同一失败URL。自然结束或显式播放才开启新预算，Ready后随即失败不会刷新预算。`advanceStopped(false)`表示正常耗尽，true表示失败预算／忙而停止；最后的播放失败保留在快照，不宣称已有完整失败汇总UI。
- 队列从首次append就绑定会话，即使尚未播放。注销／换号立即使访问器与旧条目操作无效，后续会话通知清理；关闭也清队列／歌词。循环／偏移等非敏感偏好可保留。本包不持久化队列／进度。

## 3. 六轨媒体时钟

分别索引原文／翻译／罗马音的standard与enhanced。track缺失、空、纯文本或malformed时无时间游标，原始状态仍保留于SongLyrics；一次查询成功的LyricsState::Ready不代表每轨都有歌词。歌词请求失败不打断音频。

```text
原歌位置 = 后端片段positionMs + preview.startMs（非试听取0）
当前轨定位时间 = 原歌位置 - track.providerOffsetMs - userOffsetMs
```

正偏移表示延后显示。LRC正offset已由解析器转换为负providerOffset，这里不二次翻转。用户偏移限±120000ms，时钟输入限七天；seek仍提交**片段内部位置**，不把原歌位置传回播放器。

- 只消费实际媒体位置：暂停不会自建计时器继续前进；seek后等待媒体位置事件重定位，可任意向前／向后查找。没有墙钟音频替身，也未加入UI插值。
- 行／词区间为左闭右开。无显式行结束时取下一个**不同**起始时间，末行保留到媒体终止。相同／重叠时间返回多个原始行索引；词返回所属行与词索引，必须同时落在该行有效区间内。
- 不按语言行号对齐，不从逐行歌词捏造词时间，不因字段叫enhanced就假定是逐字。六轨各自缺失／偏移／语法互不覆盖。
- 按起点排序并保存前缀最大终点，二分起点后只检查仍可能覆盖的区间；限制每轨10000行／30000词。结果索引仍指向原SongLyrics，而非排序后的新行号。
- 切歌取消旧歌词查询；原requestId、会话和load身份共同约束结果，旧歌词不能覆盖新歌。Resolving／Loading／Ended／Failed／Empty无高亮；Ready／Playing／Paused才按媒体位置定位。

尚未验证设备回环延迟或逐字声学±50ms目标；Qt位置通知粒度不是采样级时钟精度承诺。

## 4. 定向证据

仅受影响Debug目标，`QT_FATAL_WARNINGS=1`；未重跑认证／协议／全量／Release，也没有新增真人账号、密钥环或网络探测：

| 目标 | 各自最后一次pass（含init/cleanup） |
| --- | ---: |
| listening | 20 |
| playback | 9 |
| backend_facade | 8 |
| 三套件合计 | **37** |
| backend_runtime仅playbackThroughThread、destructionStillDrains | **4** |

首轮三套件为34 pass，随后只重跑修改过的listening至20 pass；不要把增量次数相加成新全量成绩。该工作包时点登记22个CTest套件，最新数量见[接手指南](../HANDOFF.md)。listening为Core-only合成测试；线程用例经过实际Runtime、Facade、协议／歌词解析与注入播放器，验证暂停切歌、seek游标、挂起请求中关闭及析构排空，不等于真实声卡／长时间连续播放验证。此前公开试听与Qt适配器PoC证据仍单列于[单曲工作包](stream-resolution-and-playback.md)。

```bash
cmake --build --preset linux-dev-local --target \
  tst_listening tst_playback tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(listening|playback|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playbackThroughThread destructionStillDrains
```

本机忽略日志：`build/workspace-checks/listening-focused-test.log`、`listening-final-test.log`、`listening-final-thread.log`、`listening-final-build.log`。它们是执行证据，不是源码交付材料。

## 5. 下一步与未实现项

后续[专辑／艺人／歌单目录](catalog-queries.md)已接入并离线验证，后续[日推读取／FM条目供给](discovery-and-fm.md)也已接入，[FM连续播放接线](fm-continuous-playback.md)也已完成，接下来个人库读取。FM是独立动态来源，不能把普通列表循环当FM续批。队列持久化、听歌历史／官方播放上报、失败汇总界面、MPRIS仍未实现；流式／Range缓存、URL刷新、音质切换保留位置、设备恢复另立小包，不削弱既有CDN／TLS与缓冲上限。UI仍最后。
