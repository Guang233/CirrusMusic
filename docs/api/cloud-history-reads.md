# 云端最近听过与听歌排行读取

## 1. 范围与入口

按维护者确定的方向：历史展示优先使用网易云记录，本地主要保留[时长计量](local-listening-history.md)。本包只接入当前账号的两项云端读取：

```cpp
quint64 BackendFacade::recentSongs(int limit = 100);
quint64 BackendFacade::listeningRanking(RankingPeriod period = RankingPeriod::AllTime);
```

返回非零ID是有界查询受理，不是成功；继续通过 `musicCompleted(id, originalContext, MusicResult)`取得值或失败，`cancelMusic(id)`取消。复用原MusicQuery／MusicResult、16查询槽和后台装配；不新增线程、运行依赖、SQL表、缓存或独立同步器。domain／application仍只依赖Qt Core，模板入口／UI不变。

Facade不接受任意uid，从已验证快照捕获accountId；MusicQueryService通过 `musicQueryAccount()`再次核对原账号、token和Authenticated状态。该helper兼容既有个人库查询；library／history mapper的路由仍各自独立，历史不会误送入发现或个人库schema解析。guest／认证中／未Ready／非法参数／满邮箱拒绝，不自动登录／恢复账号。

没有真实账号／密钥环读取或官方请求；没有自动报告、喜欢／订阅等写入，也不通过上报或补详情来“修复”历史。已存在的[显式报告能力](explicit-listening-reports.md)保持独立，时机和来源扩展仍留待播放接线。

## 2. 固定请求与界限

固定参考 `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `module/record_recent_song.js`、`module/user_record.js` 与 `public/docs/home.md`；两个模块的SHA-256与inventory匹配，未运行上游代码。该参考是第三方兼容证据，不是官方文档或实时端点注册表。

新增两个封闭operation，21→23；均为weapi／明文JSON、4MiB上限、默认固定music.163.com主机：

| operation | 逻辑路径 | payload |
| --- | --- | --- |
| RecentSongs | `/api/play-record/song/list` | 仅limit，默认100，产品限制1～100，无offset或uid |
| ListeningRanking | `/api/v1/play/record` | 当前账号uid的规范十进制字符串；type=0全部时间／type=1最近一周 |

拒绝额外参数、分数limit、任意scope／type及不规范ID；不照抄JS的 `limit || 100` 把无效0悄悄变为默认。最近记录请求本身不携带uid，但内部query仍绑定原账号，避免跨账号命令错用。两项Native weapi操作都必须有非空MUSIC_U，匿名MUSIC_A不能替代；Native凭据检查本身不宣布身份已验证，认证权威仍为应用事务。

响应不采纳Set-Cookie，不改变认证／记忆凭据，不生成身份收据。429保留可校验Retry-After信息，但没有自动重试／自动分页；取消、切号／退出、关闭均保留原token并拒绝旧结果。调用方保留的值副本仍需在账号失效时丢弃，不会自动擦除。

## 3. 响应值与不可推断事项

### 最近听过

`CloudRecentSongs { query, songs, optional total }`，`RecentSong { Song, optional providerPlayTime }`。

当前decoder契约为root code200、`data.list`为数组，每项含歌曲对象 `data`，至多请求limit条；`data.total`缺失／null为未知，若存在必须是安全非负整数且不小于返回条目数。歌曲沿用已有映射，wide ID用规范字符串、未知标题／艺人／专辑／曲目时长保持已有语义。

- 保留服务端顺序和重复歌曲，不重新排序、不按SongId去重，也不承诺列表每项必定代表独立一次播放。
- `resourceId`如果存在必须匹配内嵌歌曲ID；`resourceType`如果存在必须为SONG。缺失标记保持未知，不能将显式VIDEO等记录混入音乐结果。
- `playTime`如果存在，只保留为安全非负整数的**原服务端字段**；单位、时区和具体时刻语义尚未实测，不转换成本地日期，也不叫听过秒数。缺失时不填本机时间或歌曲时长。
- 没有虚构offset／hasMore、短列表完成标记或整个账号的完整历史。total不是本地实听次数或时长汇总。

### 听歌排行

`ListeningRanking { query, songs }`，`RankedSong { Song, optional playCount, optional score }`。

当前契约选择**请求窗口对应**的root `weekData`或`allData`数组，每项含 `song`对象；不会在周数据缺失时悄悄用allData兜底。最多1000项，超过整体失败，不静默截断；1000是本产品防护上限，不是已验证服务端固定返回数量，接口没有新增limit／offset参数。

- playCount缺失／null保持未知，存在时必须是安全非负整数；显式0与未知不同。
- score缺失／null保持未知，存在时接受有界有限非负JSON数（允许小数）；保留原值，不把它换算为次数、百分比、秒数或本地排名。
- 保留服务端数组顺序与重复项，不重新排序、合并或补歌曲详情；不凭列表长度猜总量／覆盖率。
- “最近一周”是服务端type=1窗口，不按本地星期一重筛；“全部时间”是服务端type=0标签，不承诺覆盖全部历史或终身总次数。
- Song.durationMs仍只是曲目元数据，不是该账号实听时长。云端排行与当前Cirrus计量不可直接合并或相减。

两项root若带uid，必须匹配捕获账号。空数组是合法空结果；缺失／null／错误列表、坏歌曲、错误数值／资源标记／账号、超限或矛盾total整体失败，不忽略坏行后冒充完整结果。

### 响应证据限制

固定模块／文档能确认路径、请求参数与weekData／allData选择，**没有给出完整最近列表、playTime、playCount、score的响应schema或当前真实账号样本**。这些是当前客户端的有界decoder契约，只有合成fixture验证，不能把它们说成已经官方确认的字段／单位或广泛兼容性。真实账号验收另授权；若实际schema不同，应据脱敏证据修订，而不是读取原始日志／保存真实库fixture或猜测成功。

报告获得code200与读取到历史／排行数据仍是两种证据：本包不会保证已有显式报告已入库、计数必涨或即时可见，也没有写后轮询对账。

## 4. 定向离线验证

本机Qt6.11.2 Debug、`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`；各套件最新结果（含init/cleanup与数据行，不叠加重复运行）：

| suite | pass |
| --- | ---: |
| cloud_history | 28 |
| library | 21 |
| music | 26 |
| discovery | 17 |
| session_api_client | 24 |
| backend_facade | 8 |
| 合计 | **124** |

线程回归 `cloudHistoryThroughThread`、`libraryThroughThread`、`listeningReportThroughThread`、`playbackThroughThread`的Queue／FM两行、`destructionStillDrains`共**8 pass**（含init/cleanup）。新用例经过合成完整账号提升及原生查询链，验证最近重复歌曲／未知字段与两个排行窗口跨线程传值、账号／参数／原token保留、挂起读取中关闭排空，且查询不会发出报告。既有个人库、显式报告、队列/FM线程路径一起定向回归。

```bash
cmake --build --preset linux-dev-local --target \
  tst_cloud_history tst_library tst_music tst_discovery tst_session_api_client \
  tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(cloud_history|library|music|discovery|session_api_client|backend_facade)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  cloudHistoryThroughThread libraryThroughThread listeningReportThroughThread \
  playbackThroughThread destructionStillDrains
```

覆盖固定payload／协议／响应上限、规范wide ID、空集合／未知字段／重复顺序、选窗与1000项边界、18条坏schema数据行、guest／错账号／匿名凭据拒绝、读取Cookie不回写、取消／切号／429及Facade16槽／注销晚到隔离。首轮六套件通过后，将playTime字段明确为无单位原值，重建受影响目标，仅复跑cloud_history和所列线程用例；不累计重复次数。没有全量认证／Release／真实keyring／声卡或账号矩阵。

13个变更C++文件的active LSP无诊断，format／diff检查通过；变更文档255个本地链接无断链；13个人工application编译单元加1个AUTOMOC的实际依赖均无基础设施／Network／Multimedia／Sql／Quick／Qml／Gui／JSON穿透。main.cpp／Main.qml无改动。

本包完成时登记30个CTest套件，不是30套全量验证；最新数量见[接手指南](../HANDOFF.md)。忽略日志 `build/workspace-checks/cloud-history-*.log`，最终为focused、final-cloud及final-thread与对应构建日志。新DTO头文件更新时的clangd旧member诊断，经active刷新消除，不删除必要include。

## 5. 后续

历史读取本包已闭环；后续[榜单列表／内容摘要](chart-reads.md)已另包离线接入，并复用歌单页与显式选歌播放链，仍无真实业务请求。自动报告的来源、有效收听门槛、时机／终止字段、load去重与未知结果策略仍留待具体播放接线；本地详细历史／分析持久化按需求再定，UI继续最后。
