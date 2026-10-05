# 榜单只读查询与现有歌单播放链

## 1. 范围与入口

```cpp
quint64 BackendFacade::charts(ChartListKind kind = ChartListKind::Overview);
```

`Overview`读取榜单列表，`WithPreviews`读取内容摘要；一次调用只发一项请求，不顺序串发两个接口、不合并两个不同时点的列表。非零ID是受理，结果仍通过 `musicCompleted(id, originalContext, MusicResult)`取得 `Charts` 或 `MusicFailure`，取消仍为 `cancelMusic(id)`。

复用MusicQuery／MusicResult、16槽邮箱、MusicQueryService与Runtime后台装配。当前Ready的Guest／Authenticated上下文可读；不需要任意uid，不自动认证或恢复账号。新domain值只依赖Core；没有新线程／运行依赖、SQL表、缓存、刷新任务或UI。模板main.cpp／Main.qml保持不变。

## 2. 固定来源与闭合协议

固定api-enhanced `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `module/toplist.js`、`toplist_detail.js`及 `public/docs/home.md`；模块SHA-256匹配inventory。`toplist`默认createOption配合APP_CONF.encrypt=true为eapi，`toplist_detail`显式weapi；不是两个weapi接口，也不运行上游Node或开放crypto／host参数。

新增两项封闭operation，业务registry从23到25：

| operation | 逻辑路径 | 协议／响应 | 业务payload |
| --- | --- | --- | --- |
| Charts | `/api/toplist` | eapi、JSON、4MiB | 空对象 |
| ChartsWithPreviews | `/api/toplist/detail` | weapi、JSON、4MiB | 空对象 |

只用原固定官方目标（eapi interfacepc.music.163.com、weapi music.163.com），通用协议profile保持原契约；不引入log目标、XEAPI回退或特殊OS伪装。不接受uid／分页／地区／任意参数，不复用QR的type=3载荷。只读响应不采纳Set-Cookie，无身份收据／账号提升。Guest可用表示本客户端允许发起，不是已经实测服务端在所有地区或匿名状态都成功。

取消、切号／退出、close沿用原token／generation和同步abort屏障，迟到结果不能重新附到新上下文。429提供合法Retry-After但不自动重试、登录或删除凭据。已取走的DTO副本不会自动擦除；消费者仍须在上下文失效时丢弃。

## 3. DTO、摘要与防护界限

`domain/music/chart_types.h`：

- `Charts { query, charts }`，保存请求种类及服务端列表顺序／重复项；无猜测的total／hasMore／本地分类或排序。
- `Chart`：PlaylistId、标题、简介、封面、可选trackCount／原updateTime／updateFrequency以及可选预览列表。
- `ChartPreview { primaryText, optional secondaryText }`：提供方first／second文本，**不是Song／SongId，也不是完整歌曲页**。

当前decoder要求root code200与root `list`数组，至多200项，每项规范正signed64歌单ID；JSON数值ID只接受≤2^53−1，wide ID必须是规范字符串。200是产品防护上限，不是服务端已确认的榜单数量。超限／坏行整体失败，不截断或跳过后冒充完整列表。

标题、更新标签、预览文本遵循4096 UTF-16、合法字符／无NUL规则，简介上限65536。trackCount与updateTime缺失／null保持未知，存在时为安全非负整数；更新标签缺失／null未知，显式空字符串与未知不同。updateTime保留为 `providerUpdateTime` 原字段，单位与具体时刻语义尚未实测，不推算本地刷新时间；updateFrequency只是提供方标签，不是定时刷新规则。封面复用现有目录图片元数据策略，非法／不支持URL降为不可用；不抓取图片，也不将URL当授权。

`tracks`缺失／null为未知，空数组为已知空；有值时最多10项，每项要求first文本，second缺失／null为未知。预览并不保证固定3条或覆盖全部榜单歌曲；不拆分字符串猜曲名／艺人ID，不将摘要条数换算为trackCount。

只映射root.list的歌单榜单分支，不把artistToplist／rewardToplist等独立响应分支拼成歌单或可播放歌曲；不新增歌手榜、MV／视频／播客等业务，不按名称猜类型或推广为所有服务分支兼容。也没有toplist_detail_v2适配／回退。

**响应证据限制：** 固定模块与文档确认路径、协议选择、列表／内容摘要用途及“排行榜也是歌单”的兼容依据；没有完整响应schema或本轮真实样本。root.list、coverImgUrl、updateFrequency、updateTime、tracks.first／second等是当前客户端有界decoder契约，仅合成fixture验证，不称为官方确认字段或当前广泛兼容性。真实只读榜单／账号验收另授权，不将真实库内容或原始响应存入仓库／日志。

## 4. 歌曲与播放：复用，不隐式扩展

拿到Chart.playlist后，调用现有 `playlistDetails()`／`playlistSongs()`，遵循[目录索引及两阶段页补详情](catalog-queries.md)：

1. 歌单详情仍区分完整trackIds与部分嵌入tracks，不把Chart预览当索引。
2. 页查询重新确认完整ID索引，再只补该页的SongDetails；恢复原次序／重复项，显式missing，不为整个榜单抓详情或自动翻页。
3. 调用方显式挑选成功补齐的SongId，通过既有ListeningSession.append()与select()进入队列／播放。查询／补详情本身不会入队、自动播放或发报告。

没有新“榜单歌曲”端点、任意idx映射或专属播放协调器；不复活上游已划除的 `/top/list` 包装。不同榜单／歌单页不保证跨请求原子快照。Song元数据不证明权益；真实音源仍由原官方StreamResolver校验。榜单ID和预览字符串都不是SongId，也不会被伪造为FM／日推／队列的报告来源。未增加队列来源模型或自动报告时机。

## 5. 定向离线验证

本机Qt6.11.2 Debug，`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`；各套件最新结果（含数据行、init/cleanup，不叠加重复运行）：

| suite | pass |
| --- | ---: |
| charts | 28 |
| catalog | 22 |
| music | 26 |
| session_api_client | 24 |
| backend_facade | 8 |
| listening | 20 |
| 合计 | **128** |

后台线程选择 `chartsToPlaylistPlaybackThroughThread`、`musicQueriesThroughThread`、`cloudHistoryThroughThread`、`playbackThroughThread`的Queue／FM两行、`destructionStillDrains`，共**8 pass**（含init/cleanup）。没有全量认证／Release、真实keyring或声卡矩阵。

新线程用例从合成Guest会话读取两种榜单值，再复用原歌单详情与两阶段分页；无序详情恢复为1／2／1，明确挑选SongId2入队并达到假后端Playing／歌词Ready，最后在挂起榜单请求中close并排空资源。播放使用合成XEAPI源及自定义IPlaybackBackend，没有真实CDN／音频数据／声卡或声学证明。只选择一首测试播放，不声明自动整榜入队。

charts覆盖空业务payload／固定路径／协议与4MiB上限、两种query及非法enum、wide ID与未知／空预览、重复顺序与无关分支、17条坏schema数据行、200榜单／10预览及文本界限、Guest／Authenticated两类原生链、Cookie不回写、取消／退出旧token、429不重试、Facade16槽／晚到隔离及读取不播放／不报告。

```bash
cmake --build --preset linux-dev-local --target \
  tst_charts tst_catalog tst_music tst_session_api_client \
  tst_backend_facade tst_listening tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(charts|catalog|music|session_api_client|backend_facade|listening)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  chartsToPlaylistPlaybackThroughThread musicQueriesThroughThread cloudHistoryThroughThread \
  playbackThroughThread destructionStillDrains
```

12个变更C++文件的active LSP无诊断，format／diff检查通过；变更文档264个本地链接无断链；13个人工application编译单元加1个AUTOMOC的实际依赖无基础设施／Network／Multimedia／Sql／Quick／Qml／Gui／JSON穿透。main.cpp／Main.qml无改动。

当前登记31个CTest套件，不是31套全量验证；忽略日志 `build/workspace-checks/charts-*.log`，本轮最新configure／build／focused／thread。成对头文件更新引起的clangd旧enum／PCH和级联未使用include误报，经active刷新消除并标记，不删除必要头文件或放宽产品验证。

后续可继续推荐／精品歌单等发现读取；收藏、编辑、评论、真实写入与自动报告策略仍分别立包。本地详细历史持久化和UI不在本包。
