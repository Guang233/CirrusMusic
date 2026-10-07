# 播客／声音播放准备：离线身份与共享媒体接缝（阶段1）

**后续范围覆盖**：阶段1交付后继续补核，只有旧Voice字段存在记录、无绑定／音源新样本；FeelUOwn公开源码fetch因github.com内部地址198.18.0.232拒绝，未绕过、不当服务拒绝。维护者明确选择先做[历史日推读取](daily-recommendation-history.md)，本项下一阶段G1/G2暂停、不继续等待本轮材料或自动恢复。阶段1原83/46及373＋13是历史，主线后续85/47与839＋15来自独立历史日推包，不能挪作spoken实证。前三项同样暂停，SDK／账号／metadata／媒体均无新授权。

## 1. 阶段1授权与交付范围

接续 `264c020` 每日推荐歌单包，维护者明确前三项（整歌单收藏v2、一起听、云盘后续）暂缓，从第四项播客／声音播放开始；在核对既有[播放门禁](spoken-playback-contract.md)与实际代码后，明确选择**先做离线接缝**，未选择新的官方元数据窗口或提供绑定材料。

本包只交付逻辑／音轨结构类型与无身份媒体载荷，以及现有歌曲／FM单owner的回归；**不是Program／Voice播放功能完成、可信binding、服务端权限、节目v1兼容或声学验收**。没有playProgram／playVoice入口、alwaysUnavailable假operation、新resolver／factory／coordinator、自动详情／图／音源／歌词／队列／报告、持久记录／下载、UI或新运行依赖。

没有使用账号／Cookie／真实keyring或私有文件、网易云业务／issuer／媒体／CDN、SDK／JS执行；只有合成替身与既有安装SQL临时测试。83个business operation不变，CTest **45→46**；一起听固定stash `9907ac4a9ad83b9cefb79fea9d3337b56b91ba6f` 不pop／drop／恢复，云盘已有read保留，未来文件下载仍复用实际可听播放源、不额外查资格。

## 2. 三种职责不能混同

### 逻辑实体与底层音轨

新 `domain/playback/playback_entity.h`：

- `PlaybackEntity = variant<SongId,ProgramId,VoiceId>`，保留kind标签；`validPlaybackEntity`只检查规范positive signed64文本；`samePlaybackEntity`同时比较kind／文本，不做跨namespace转换。
- `AudioTrackId{value}` 独立结构类型、同规范ID校验，与Song／Program／Voice不隐式转换。没有创建“任意实体＋任意音轨”的binding工厂或可信性标记。
- 两命名空间数值相同仍保持不同tag；这不是“官方ID一定不同”的断言。结构相等也不证明有效性、同一远端资源、请求关联、父播客、原account／context、服务权限或可播；比较与校验独立。

类型当前**尚未进入活动PlaybackSnapshot／StreamQuery／ListeningSession／history／report／跨线程消息**，不把歌曲快照临时贴Voice标签。下一阶段可信消费binding和逻辑播放模式必须另完成，不能因为头文件存在就宣称支持。

### 物理媒体载荷

新 `domain/playback/media_source.h` 的 `MediaSource`：url、actualQuality、format、size、validUntil、optional PreviewRange；PreviewRange从旧playback_types移到这里，语义不变。**不包含SongId、ProgramId、VoiceId、AudioTrackId、PodcastId或账号**，URL只在内部暂存，不进UI snapshot／日志或持久库。

MediaSource是内部未授予权限的值，不是经过验证的URL／binding类型；构造它不能替代原resolver、受控CDN／deadline／size／format检查。复制／移动／cancel释放不是安全擦除所有隐式共享／网络／排队副本的保证。

### 现有歌曲包络与单owner

`StreamSource : MediaSource`仍额外保留SongId；显式构造函数保持旧字段访问及六／七参数初始化，默认构造／随后设字段仍可用。**有意采用非多态值继承**保持既有歌曲mapper／fake fixture调用方式：媒体按base值接收、丢弃Song包络，不新增虚拟函数／继承层管理器，反向MediaSource→StreamSource不隐式允许。不能依赖旧aggregate性／对象布局／二进制ABI。

唯一 `PlaybackCoordinator` 仍校验original LoadToken／session／request、source.song等于snapshot.song、有限未过期TTL后发布Loading；再次检查current(token)，再显式move成MediaSource交给后台。已在Loading通知中重入stop则不load。身份留在原歌曲snapshot／history／报告，不随物理值丢弃；这不是把AudioTrackId转换SongId。

## 3. 生产接缝改动与保留边界

| 接缝 | 本包变化 | 不变／未交付 |
| --- | --- | --- |
| IPlaybackBackend::load | `(MediaSource,LoadToken)`，编译期签名检查；不接StreamSource身份包络 | token／事件／play/pause/stop/seek/volume契约不变 |
| MediaBuffer::start／m_source | identity-free MediaSource | 受控HTTPS／逐跳检查、无账号Cookie／auth、临时0600、64MiB／45s／15s无进展／3redirect、有限TTL／精确length／cancel／device寿命 |
| QtPlaybackBackend::load | physical source／preview／format交给原buffer | 每load新player/audio、先销毁player再buffer、15sdecode、试听边界／观测计量／stop屏障 |
| Song StreamResolver | 原Song StreamQuery与严格matching row.id，继续返回Song StreamSource | 固定XEAPI／key失败关闭、原TTL减elapsed、MP3/FLAC／level／size／preview，不退旧URL／SDK／解锁 |
| ListeningSession／Runtime | 使用原唯一协调器／media factory，override测试改用MediaSource | Queue／FM与generation、六歌词／history／显式报告身份不泛化，未切入spoken模式 |

MediaSource不含identity也不代表匿名源可用；未来spoken不能直接拼物理URL跳过G1/G2，不能再启动另一播放器owner。实际声学输出／长节目、流式起播／过期恢复／完整播放／付费访问仍未证明，64MiB／45s不会偷偷扩张。

## 4. 本包离线测试与实际依赖

本机Qt6.11.2 Debug，`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`，只取最终选择的成功结果，不累计first／fix：

| suite | pass | suite | pass |
| --- | ---: | --- | ---: |
| media_seam（新、Core/Test） | 28 | stream | 16 |
| playback | 9 | media | 17 |
| listening | 20 | fm_listening | 15 |
| listening_history | 13 | listening_report | 18 |
| backend_facade | 8 | music | 26 |
| catalog | 22 | charts | 28 |
| playlist_discovery | 54 | entity_search | 44 |
| podcast_reads | 55 | 总计 | **373** |

0fail，**1项原MediaTest::silentQtSmoke默认skip**（CIRRUS_AUDIO_SMOKE未启用），不是跳过新接缝测试、不冒称静音／设备实测。selected Runtime的10函数含Queue／FM两数据行＝11条线程数据行＋init/cleanup，**13pass／0fail／0skip**。它回归的是已有歌曲／FM跨线程和关闭，不是spoken三模式转换实测。

新28：14规范ID数据行（11非法／3合法含wide），9种kind交叉（同数值不跨tag、异文本不等），3物理载荷／单owner／验证前置用例＋init/cleanup。编译期验证MediaSource无song、load参数为physical、不能由MediaSource转歌曲包络、AudioTrack不隐式转Song、Program／Voice不互转。完整保留URL／quality／format／size／absolute deadline／preview，单owner A→B→C取消late、旧媒体事件／Ended一次、加载pause、原Song snapshot、错误identity／expired／切会话在媒体前拒绝。

首次新CapturingBackend误继承final FakePlaybackBackend造成编译失败；改为独立IPlaybackBackend测试实现，**没有移除原final／fatal／失败断言／moc或弱化生产验证**。头同步的7个声明／override连锁LSP错误在配对实现与主动9路径0后消失；实际configure／AUTOMOC／构建与compile DB刷新后，**13改变C++＋4邻接共17路径fresh all-server，0诊断**。已有media_test的3条seeked拼写信息改为seekAcknowledged，语义／audio opt-in不变。CMake分析服务不可用不是clean证据，实际配置／构建另验证。

14已编译application对象的 **6973条实际Ninja依赖**，基础设施／JSON／非Core Qt／OpenSSL禁入0；UI delta0。没有用CMake链接声明替代实际依赖证据，未增加应用手写TU／运行库。AST CLI本地缺失，工具npx也拒绝缺包，未安装；限定已由源码／LSP确定路径的文本检索作为fallback，不称AST成功。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_media_seam tst_stream tst_playback tst_media \
  tst_listening tst_fm_listening tst_listening_history tst_listening_report tst_backend_facade \
  tst_music tst_catalog tst_charts tst_playlist_discovery tst_entity_search tst_podcast_reads \
  tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(media_seam|stream|playback|media|listening|fm_listening|listening_history|listening_report|backend_facade|music|catalog|charts|playlist_discovery|entity_search|podcast_reads)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playbackThroughThread chartsToPlaylistPlaybackThroughThread playlistDiscoveryThroughThread \
  catalogMetadataThroughThread listeningReportThroughThread musicQueriesThroughThread \
  discoveryThroughThread libraryThroughThread podcastReadsThroughThread destructionStillDrains
```

ignored `build/workspace-checks/spoken-seam-*` 的final-build／final-focused／final-thread及boundaries为此阶段证据；不挪旧日推753＋14、Together草稿或之前歌曲试听作本包／spoken验收。非全46／Release／账号／keyring／SDK／跨发行版／声学／UI矩阵，仅本地提交、不push。

固定440模块／公共文件hash与15项Python审计工具测试通过；format／diff-check通过，765个本地路径引用missing0（不核对锚点）。变更文件Cookie literal／signed-query有限扫描flags0，非全面安全／秘密审计；实际83ops／46注册且无together，固定WIP对象存在。文档原`SearchSuggestion<ID>`补代码标记修复MD033，不压制诊断。

## 5. 下一阶段仍需的契约

[原G1–G6](spoken-playback-contract.md)保留：G1消费Program／Voice与AudioTrack关联、mainTrackId／mainSong冲突、父播客与原context；G2节目v1实际ID／格式／size／level／TTL／preview候选及权限；G3合成／另授权媒体；G4可信binding及完整逻辑身份隔离；G5 Song↔FM↔spoken唯一owner／历史／计量；G6新报告namespace另授权（安全默认不发送）。

本包只完成结构tag与物理接缝、原歌曲／FM屏障回归；G4/G5只补部分准备，**不把整门禁标完成**。没有消费binding字段实际材料／新公开窗口授权，不能把Program.id／Voice.id／mainSong猜为SongId，不调用workbench、旧url、SDK或自动实网验证；不伪造报告来源或Podcast→Playlist。阶段1交付时下一步仍在第四项、需先取得可核对绑定／音源材料或另明确验证授权，再完成resolver／逻辑快照／单owner spoken模式；后续维护者明确选择历史日推，本项下一阶段现亦暂停，不自动恢复等待／探测。前三项继续暂停，不跳过门禁直接UI。
