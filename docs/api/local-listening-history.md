# 本地可观测收听累计与会话历史

## 1. 范围与入口

在[官方播放](stream-resolution-and-playback.md)、[普通队列](queue-and-lyric-clock.md)与[FM连续播放](fm-continuous-playback.md)之上增加：

- `PlaybackSnapshot::observed`：可选的 `ObservedPlayback { playedMs, capped }`，固定load内的累计观测值；缺失表示没有具备计量能力的后端证据，不是零秒。
- `ListeningSession::history()`／`historyChanged()`／`clearHistory()`：调用线程上、仅当前会话、最多256条的播放摘要。Runtime仍通过 `listening()` 暴露，不新增线程或服务。
- Core-only `ObservedPlaybackMeter` 与 `ListeningHistory`；Qt播放器／单调时钟／采样定时器接线留在基础设施层。未新增依赖、数据库表、业务operation或UI接线。

**这是保守的媒体播放进展观测，不是“用户确实听到了声音”的证明，也不是持久历史或官方听歌报告。** 不从FM取出、喜欢ID、曲目时长、seek后的进度或自然End推导已听时长。不新增官方访问、真实账号／密钥环访问或写操作。

## 2. 计量契约

`QtPlaybackBackend`在原媒体线程每250ms采样，计时使用该load的 `QElapsedTimer`；Facade／GUI收消息的延迟不参与累计。

| 条件／事件 | 行为 |
| --- | --- |
| ready、实际Playing、BufferedMedia、1x、非静音且本播放器volume>0、无待确认seek | 才允许建立／延续采样区间 |
| 暂停、缓冲／状态变化、速率变化、静音边界、错误／结束／stop／新load | 断开区间；不补算未知尾部 |
| 第一份有效样本 | 只建锚点，不凭空计入之前的时间 |
| 媒体位置没有前进 | 不增加累计；短期保留锚点，兼容较粗的位置通知 |
| 时钟倒退／相等、位置倒退、非法位置 | 不计该间隔并重建／丢弃锚点 |
| 间隔>2000ms，或位置前进>单调时间差+250ms | 视为观测空档／跳跃，不计该间隔 |
| 正常连续样本 | 增加 `min(媒体位置前进, 单调时间差)`，不超过这段实际经过的时间 |

采样坐标是片段内位置，受已知Qt时长／试听片段长度上限约束；**不加试听起点**，也不拿整曲元数据时长充当累计。重新听过的片段可以再次贡献时间，不宣称统计了不重复音频覆盖率。每load上限7天，到顶 `capped=true`，不溢出或静默截断；新load从零开始。

seek在调用播放器之前先断区间，并等待 `positionChanged` 的目标位置值确认；确认后第一份样本仍只建锚点。当前坐标相同且无在途seek时不再发跳转。Qt没有统一的解码／出声seek完成收据：若后端不给出精确目标位置通知，计量保持关闭，直到后续可确认的seek或新load，**不按超时猜测恢复计量**。位置通知不是声学完成保证。

状态／状态码变化在媒体对象信号到达时立即断区间，避免漏掉两个采样tick之间的短暂缓冲；向上发进度仍由定时器完成，错误销毁仍延后到安全调用栈。累加值经 `PlaybackEvent::Observation` 传递，Coordinator只接受当前ready load的非负、有界、不回退值；旧load、重复通知和终态后的计量不能重复增加。

### 证据限制

- Qt的Playing／Buffered与位置是可用的客户端证据，但不是扬声器实际输出、耳机佩戴或人的注意力证据。系统级静音、外部混音器、硬件断连及音频管线延迟不一定能由这些属性证明。
- 有采样量化、seek确认及暂停／结束尾部丢弃；忙碌／休眠空档不回填。快速替换load还可能丢弃已在途的旧进度快照。因此不承诺精确实听秒数、完整覆盖率或精确自然结束时刻。
- 本包未做新的真实Qt音频计量端到端验收；以前静音试听PoC不是本包计量正确性的证明。本播放器静音时应为零，系统外部静音的可观测性另论。

## 3. 会话历史与隐私

每个接受的队列／FM Play先绑定原 `SessionToken + load generation`、原账号（guest为空）、SongId与Queue／PersonalFm来源；普通队列保留entry UUID，FM不伪造普通队列entry或歌单ID。仅在收到正累计证据后入列表，最新load在前，超过256条逐出最旧条目。

- 保存实际音质、可选试听范围、累计毫秒与到顶标记；同曲重放是不同load，不按SongId错误去重。值是累计替换，不把重复快照再相加。
- `Active`表示load摘要尚未终结，**不等于正在出声**。End、Failed、Stopped、Replaced与外部生产者接管后的Detached只终结当前匹配记录；从未观测到正进展的失败／未播load不造记录。
- Play／Stop被本地邮箱拒绝时不改历史；重入的新意图优先于旧通知。普通队列与FM共用同一账本，但来源标签不跟随之后的模式切换修改。
- 单曲计量也适用于直接使用Facade的调用方；但本会话历史只记录ListeningSession拥有的load，不把外部Facade播放冒充队列／FM播放。
- 切号、退出、启动失败、关闭立即在getter隔离，并在失效处理时清空内存；相同账号重新登录也不继承旧epoch。仅停止／清空队列保留当前会话已有历史。
- `clearHistory()`清空列表并停止记录**当前load的剩余部分**；直到新load才重新入历史。这样晚到的、可能在清空前采样的累计值不能复活旧记录。它不停止音频，也不重置底层单曲计量。重复begin／旧generation不能重新开启已清空load。
- 信号只是通知，消费者应读当前getter；已经取走的值副本不会自动擦除，消费者仍需在账号变化时丢弃。没有写盘、缓存恢复、日期分组或历史同步，也未保存墙钟起止时间。

## 4. 离线验证

最新受影响Debug结果（含init/cleanup；前次运行不累计）：

| 套件 | pass |
| --- | ---: |
| listening_history | 13 |
| playback | 9 |
| listening | 20 |
| fm_listening | 15 |
| media | 17 |
| backend_facade | 8 |
| 合计 | **82** |

`media`另有1项默认真实音频smoke skip。线程回归 `playbackThroughThread` 的Queue／FM两行与 `destructionStillDrains` 共**5 pass**（含init/cleanup）。线程替身显式注入计量证据，证明值／身份／来源跨线程接通，而不是在静音假播放器里声称真实出声。

```bash
cmake --build --preset linux-dev-local --target \
  tst_listening_history tst_playback tst_listening tst_fm_listening \
  tst_media tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(listening_history|playback|listening|fm_listening|media|backend_facade)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playbackThroughThread destructionStillDrains
```

覆盖虚拟单调时钟、不可计量间隔、粗粒度位置、时间／位置跳跃、计量到顶、重复／倒退／旧load、无证据不造历史、重复同曲、容量／账号隔离、清空后晚到、邮箱拒绝、重入切歌与外部接管。未重跑全量认证／Release矩阵、真实设备或账号验收。

15个变更C++文件的active LSP无诊断；format／diff检查通过，变更文档229个本地链接无断链；13个application编译单元的实际依赖无基础设施／Network／Multimedia／Sql／Quick／Qml／Gui／JSON穿透，history测试只链接Qt Core／Test。登记28个CTest套件，不是28套全量验收；main.cpp／Main.qml无改动。忽略日志在 `build/workspace-checks/history-*.log`，最终结果为final-build、final-focused与final-thread；不叠加开发期重复运行次数。

## 5. 下一步

本地历史持久化需另定保留上限、guest与账号归属、显式清除及在途写入隔离；不得直接把本内存结构或token序列化当完成。`scrobble`／`startplay`／不喜欢反馈仍未实现，需独立的原来源归因、资格／阈值、幂等与未知写入结果契约，真实报告另行授权。UI、MPRIS、队列恢复及先进音频仍另包处理。
