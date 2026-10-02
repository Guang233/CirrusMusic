# 官方音源解析与无界面播放后端

> 已实现首个单曲闭环，尚不是完整播放器。维护者已明确批准新增 **Qt6::Multimedia** 和静音短样本验证。`main.cpp`／`Main.qml` 不变，不引入 libmpv、自建 FFmpeg 管线、Node、代理服务或第三方解锁。

## 1. 实现与调用边界

| 入口 | 实际职责 |
| --- | --- |
| `domain/playback/playback_types.h` | 音质、带期限的内部来源、试听范围、固定LoadToken、无URL的播放快照 |
| `application/ports/playback.h` | IStreamResolver／IPlaybackBackend；应用只依赖Core，不接触JSON／Cookie／媒体对象 |
| `application/playback/playback_coordinator.*` | 单曲解析→加载→播放，暂停意图、合并seek、epoch／generation隔离、停止和加载时限 |
| `netease/music/stream_resolver.*`（infrastructure） | 官方XEAPI调用、安全ID／权限／音质／TTL映射；没有降级解锁 |
| `infrastructure/media/`／`cirrus_media` | CDN策略、Qt Network有界临时缓冲、Qt Multimedia设备播放 |
| BackendFacade／BackendRuntime | 原后台线程装配；playSong、pausePlayback、resumePlayback、stopPlayback、seekPlayback、setPlaybackVolume及playbackChanged |

Facade命令与账号命令共用32条未确认邮箱；0表示未接收。音质参数默认Standard；Play／Stop立刻推进调用方播放generation，旧快照不能覆盖新意图。命令ack只是接受，不是已经出声。播放快照不含签名URL、Cookie、原始provider正文或错误字符串。

正常关闭仍保留记忆账号。先退役PlaybackCoordinator、resolver、媒体实例和临时缓冲，再继续原vault／密钥环／SQL排空；创建、取消、媒体销毁发生在后台线程，不在GUI线程等待解码器。必须保持进程事件循环直到stopped；底层媒体库或keyring永久不返回时不会强制终止或虚报完成。

## 2. 封闭音源契约

参考固定上游 `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `module/song_url_v1.js`、既有XEAPI公共层；这是兼容实现参考，不是官方稳定性承诺。MIT归属见[NOTICE](../../third_party/api-enhanced/NOTICE.md)。

- 新增SongStream：`/api/song/enhance/player/url/v1`，**XEAPI**，响应按既有AES／可选gzip流程解码；registry现共8项业务operation，公钥获取仍是独立链。
- 一次一个canonical正signed64歌曲ID；payload为`ids: "[整数文本]"`、`level`、`encodeType: "flac"`。不经过double，也不接收任意参数／路径。
- 本轮音质白名单：standard、exhigh、lossless、hires。vivid／sky等涉及其他profile或参数的变体暂不开放。
- 无安装公钥时返回ProtocolKeyMissing，不切换weapi/eapi；原有当前账号／匿名凭据选择仍由SessionApiClient负责。媒体请求另用无账号凭据的manager。
- 根code、单条data、歌曲ID、条目code分别验证。null／空URL或条目拒绝为Unavailable；不能笼统把全部失败解释为“无版权”。不按fee/payed独自判断授权，也不因为元数据存在而授予播放／下载权。
- 保留**返回的实际level**，不把请求hires显示成已经得到hires。只接受本轮实现的独立MP3／FLAC格式。
- freeTrialInfo非空时严格解析start/end（秒→毫秒）；缺失／null保留未知，不标记“已获整曲／下载授权”。类型异常、反向范围等拒绝。
- expi必须是0～86400秒的整数；减去本次请求已耗时间，再转换为单调期限。缺失不能当永久有效。取消／切会话结果携带原token；不缓存、持久化或输出签名URL。
- 暂无自动重试、失效自动重解析或自动降音质。失败明确结束；显式playSong创建新generation、重新解析，而不是无限重放旧URL。

## 3. 媒体传输与临时缓冲

Qt媒体引擎的内部网络不复用业务manager，也没有本实现可用的逐跳策略钩子，因此**不把远程URL直接交给QMediaPlayer**。

1. 仅接受`*.music.126.net`的来源；拒绝凭据、显式端口、fragment、IP／本机／外部主机及非媒体路径。provider初始HTTP URL仅在此可信CDN域升级为HTTPS，不尝试明文fallback。
2. TLS使用Qt默认校验，不忽略证书错误；手动处理最多3次重定向，每跳重新验证HTTPS／主机。重定向不得降级HTTP或离开白名单。
3. 不加载／保存Cookie、不复用HTTP鉴权、不发送账号头；不配置媒体磁盘缓存。请求Accept-Encoding: identity，拒绝额外内容编码。
4. 总缓冲上限**64MiB**、最长**45秒**且不超过来源剩余期限；传输无进展时限15秒。只接受完整200，按块写入0600、自动删除的QTemporaryFile；检查累计大小与provider声明相等，拒绝超限／截断。
5. 完成后清除签名URL，仅把可seek的临时QIODevice交给QMediaPlayer。验证独立MP3／FLAC文件头，拒绝播放列表／外部引用容器；WAV仅供合成开发夹具。媒体实例先销毁，再关闭／删除它借用的设备。

这是**完整临时缓冲后起播**，不是低延迟流式播放／Range缓存／离线下载库：长曲或高解析文件可能等待较久，超过限额返回TooLarge。停止、换曲、结束、失败、退出时释放缓冲，不作为授权离线资源。播放器只支持默认输出和线性音量；设备切换／设备恢复、速度、无缝、DSP尚未实现。

## 4. 播放与试听时间

- 每次load使用独立QMediaPlayer对象，信号连接捕获固定LoadToken；Qt事件queued后也检查原token。Coordinator拒绝重复或倒退generation，Stop／登出不会重置此约束。
- A→B→C乱序回包或旧实例error／position／end不能覆盖C。End只消费一次，终止后释放临时来源；再次播放需要新generation。
- Resolving／Loading阶段暂停只改变用户意图；Ready后保持暂停，不能突然出声。尚未可seek时只保存最后一个位置；duration／seekable具备后夹取应用。
- 整体解析／加载时限75秒、媒体解码准备时限15秒；定时器为可取消的单实例，不随快速切歌堆积旧长时定时任务。
- positionMs和durationMs为**本次媒体片段内部时间**。试听对应原歌位置需要加preview.startMs；以后歌词同步器负责该转换，不把试听片段的0误认为整曲的0。
- 显式试听时，解码时长超出范围250ms以上拒绝（给MP3封装／padding留余量）；seek限制到片段，播放位置达到试听长度时结束。不宣称采样级DRM执行或已授权播放完整歌曲。
- 本轮没有队列／循环／FM续批、自动故障恢复、音质切换保留位置、歌词时钟或MPRIS。End／Failed后resume不偷偷重播旧来源，调用方可明确重新playSong。

## 5. 本轮验证

### 定向离线与静音设备验证

Qt6.11.2本机Debug，`QT_FATAL_WARNINGS=1`：

| 范围 | 结果 |
| --- | --- |
| stream | 16 pass：参数／宽ID／试听／实际音质／坏响应／过期／无公钥不降级 |
| playback（只链接Core/Test） | 8 pass：暂停、合并seek、乱序／旧实例事件、stop／会话变化、过期与拒绝、重入、禁止复用load身份 |
| media | 17 pass、1默认skip：主机策略、头隔离、重定向／降级／循环、超限／截断／TLS／TTL、取消、拒绝播放列表 |
| backend_facade | 8 pass：既有入口回归及播放意图／会话／关闭屏障 |
| backend_runtime仅选4条用例 | 6 pass（含init/cleanup）：线程内播放／暂停／seek／关闭，登录恢复、匿名写入关闭、析构排空 |
| 显式启用silentQtSmoke | 3 pass（含init/cleanup）：合成3秒静音WAV，真实Qt适配器在独立QThread中创建／加载／暂停／seek／stop／销毁 |

前四套件最后一次运行结果合计**49 pass、1默认skip**（末次只重跑stream确认补充的超时／HTTP错误映射）；该音频skip随后单独显式启用通过，不把多次init/cleanup相加成“全量通过”。当前登记**21套件**。本轮没有全量／Release／真实账号／真实keyring／跨桌面／打包矩阵验证。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target \
  tst_stream tst_playback tst_media tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(stream|playback|media|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playbackThroughThread startLoginCloseRestore closeDuringAnonymousWrite destructionStillDrains
# 显式选择当前桌面设备；合成样本与播放器均静音。普通离线测试不触碰真实音频设备。
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 CIRRUS_AUDIO_SMOKE=1 \
  build/linux-dev-local/tests/tst_media silentQtSmoke
```

### 官方公开小样本

2026-10-02（执行环境UTC），按既有公开只读授权及本轮静音PoC确认，使用ignored build目录中的原生临时探测器。两轮各获取一次公开XEAPI公钥及歌曲`1824020871`的standard来源；不恢复本机记录、不使用账号／密钥环、不注册匿名账号、不登录／写业务、不保存Cookie。第二轮额外做一次1024字节Range GET和一次完整试听缓冲GET；未发生重定向。

- 公开公钥获取和音源接口均HTTP200；第一次还确认根code200、expi=1200、fee=1／payed=0，但freeTrialInfo与可用URL同时存在。
- 实际来源为MP3／standard，**480813字节、试听0～30000ms**，初始HTTP URL安全升级为HTTPS。CDN主机两次为m802／m702.music.126.net，签名路径／参数不记录。
- 第二轮Range请求得到206、读取1024字节；当前实现仍采用完整临时缓冲，不因此宣称已实现流式Range缓存。
- 原生MediaBuffer→QMediaPlayer无界面静音加载成功：解码时长30024ms、可seek、暂停与seek到约2秒、恢复及stop成功。Qt报告FFmpeg 7.1.5（LGPL标识）；正式分发仍须审核实际构建与传递许可。
- 只保留结构／状态摘要；不保留完整provider响应、签名URL、音频或解码器曲目元数据。临时缓冲在stop后释放。

这证明少量当前环境下的XEAPI／TLS/CDN／试听和基础播放链可行，不证明会员整曲、FLAC／hires实源、多地区、实际重定向／失效URL恢复、蓝牙／无设备恢复、长期播放或发行Runtime兼容。ADR-0004仍保留这些门禁，不升级为产品级全面验收。

## 6. 下一步

在现有单曲协调器上接**队列与歌词时钟同步**（独立原文／翻译／罗马音轨、逐行／逐字定位、seek／试听偏移）；之后继续专辑／艺人／歌单、每日推荐、私人FM与个人库。流式起播、受控Range缓冲及失效恢复应另立小工作包，不用放开CDN策略或任意播放器网络访问来掩盖当前限制。UI仍最后。
