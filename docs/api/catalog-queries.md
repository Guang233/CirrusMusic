# 专辑／艺人／歌单只读目录

> 已接入原生后端与异步Facade；本包只有合成离线与真实线程测试，没有新增官方实测。响应结构兼容性仍待小样本核对，不以通过mock宣称整个目录可用。UI／模板入口未变。

## 1. 范围与入口

沿用Core-only `IMusicGateway`、`MusicQueryService`、`BackendFacade` 与原会话／generation屏障，不新增通用接口转发器或网络依赖。Facade提供：

- `albumDetails(AlbumId)`：专辑元数据、嵌入歌曲及明确完整性。
- `artistDetails(ArtistId)`：艺人摘要、简介、图片、已知歌曲／专辑数量。
- `artistSongs(ArtistSongsQuery)`：hot／time排序及limit／offset。
- `artistAlbums(ArtistAlbumsQuery)`：专辑分页。
- `playlistDetails(PlaylistId)`：歌单摘要、原始曲目ID序列与部分嵌入歌曲。
- `playlistSongs(PlaylistSongsQuery)`：完整ID索引确认后，只补指定页的歌曲详情。

完成事件仍为 `musicCompleted(id, originalContext, MusicResult)`，目录DTO加入原有variant；共享16条查询容量与cancelMusic。只接纳当前Guest／Authenticated会话；不自动记住目录内容，不自动建队列／播放。拿到某页不等于“全部播放”，需要上层明确处理完整索引、缺失条目和队列10000条上限。

## 2. 封闭协议

参考固定api-enhanced `a8c781fd64faab17fedfd46e0615a2609307f163` 的album、artist_detail、artist_songs、artist_album、playlist_detail、playlist_track_all模块及其文档；默认createOption结合request/config解析为eapi，显式weapi保持weapi。这是兼容依据，不是官方规范；不运行Node或接受调用方crypto／domain／headers参数。

| 新增operation | 内置逻辑路径 | 协议／业务载荷 | 响应上限 |
| --- | --- | --- | --- |
| AlbumDetails | `/api/v1/album/<id>` | weapi；空业务载荷 | 8MiB |
| ArtistDetails | `/api/artist/head/info/get` | eapi；id | 2MiB |
| ArtistSongs | `/api/v1/artist/songs` | eapi；id、limit、offset、order、private_cloud字符串true、work_type=1 | 4MiB |
| ArtistAlbums | `/api/artist/albums/<id>` | weapi；limit、offset、total=true | 8MiB |
| PlaylistDetails | `/api/v6/playlist/detail` | eapi；id、n=100000、s=8 | 16MiB |

路径后缀只由规范正signed64十进制ID构造，不能注入路径、查询或主机。API载荷按字段精确白名单验证；分页limit为1–100、offset为0–100000。读响应不采纳Cookie，也不发布原始JSON／用户profile／收藏者等无关数据。现在共**13项业务operation**，另有独立公钥注册；playlistSongs不是第14项远端接口，而是已有两项请求的原生编排。静态440模块清单保持审计时点状态。

## 3. 映射与完整性

- DTO位于 `domain/music/catalog_types.h`；JSON实现位于 `infrastructure/netease/music/catalog_mapper.*`。歌曲和目录共享基础设施私有 `music_mapping.h` 的ID／数字／歌曲映射规则，不把JSON带入应用层。
- 根code必须为200；详情必须返回对应实体ID。数字ID仅接受不超过2^53−1的正整数，更大的ID须是规范字符串。请求ID不经过double。
- 专辑读取 `album`＋`songs`，艺人详情读取 `data.artist`，艺人歌曲读取 `songs`，艺人专辑读取 `hotAlbums`，歌单读取 `playlist`；不做任意包装层猜测。
- 数量缺失／null保持unknown，more缺失不按“满页”推断；已给more必须是bool。艺人专辑total可来自明确的root.total或artist.albumSize；同页artist若存在必须匹配请求ID。
- 专辑size与返回歌曲数量相等才标complete；缺失size不假定完整。允许真实的1970年前publishTime；未知日期不合成。元数据、计数和图片URL都不是播放／下载权利证明；图片URL仅作元数据，不在此发起图片下载。
- 保留歌单trackIds的顺序与重复。trackIds缺失与空列表分开，且必须与明确trackCount相等才标完整；tracks始终只是嵌入详情，不能替代ID序列。数量小于ID数量、嵌入未被该索引包含的ID等矛盾拒绝。
- 边界：每个歌单最多100000个索引ID，专辑／歌单嵌入歌曲最多10000条，艺人页不超过请求limit；名字≤4096 UTF-16单元，简介≤65536，参与艺人≤100。超限拒绝，不默默截断后伪称完整。大响应仍可能触及HTTP上限，不能保证任意大型歌单都能分页。

## 4. 歌单歌曲页的两阶段请求

1. 在原上下文读取PlaylistDetails；索引缺失或不能证明完整时返回 `MusicError::IncompleteCatalog`，不退回部分tracks冒充全部。
2. 截取指定页（最多100条），仅对该页去重ID调用现有SongDetails；不逐首请求，不后台抓取整个曲库。
3. 返回 `PlaylistSongsPage.ids`（原切片）、songs（按原次序恢复重复）、missing（缺失ID也保留顺序）、total／hasMore及可选indexUpdatedAtMs。分页基于ID数量，而不是成功补齐的歌曲数量。额外歌曲ID仍被拒绝，缺失不直接判为无版权。
4. 空页／越界页只需第一个请求；第二阶段失败整体返回失败，不把第一阶段成功包装为页面成功。两阶段只占一个应用查询槽，取消先记录状态再abort，避免同步abort或晚到回包启动后继请求。

两阶段均保留原token和应用请求ID；内部请求映射只指向当前阶段，旧完成不能复用新身份。换号、关闭取消两阶段。每次页查询重新读取索引，**未提供跨页原子快照／缓存**：歌单可能被远端修改，上层需比较版本／重开浏览，不能据数次页面拼接声称得到了同一时点全集。未知更新时间也不伪造版本。

## 5. 本目录读取包不生成听歌上报

本目录包交付时`scrobble`只有审计参考；后续[显式startplay／play报告](explicit-listening-reports.md)已另包接入，但未接自动时机。播放位置／歌词时钟并不是“实际已听时长”。本轮没有向 `/api/feedback/weblog` 发送startplay或play事件。

后续播放历史包需先保存条目的真实播放来源，统计实际播放区间（暂停／缓冲和seek跳跃不计，重复事件去重），并处理原账号、退出／换号、未知写结果及用户授权。不能把进度条位置直接当已听秒数，也不能统一伪造完整播放结束。服务端是否计入排行／统计有独立规则，不作保证；真实账号写入须另获明确授权。每日推荐／私人FM接入也不等于已实现该上报。

## 6. 定向验证

本机Qt6.11.2 Debug，`QT_FATAL_WARNINGS=1`；只验证受影响查询与播放消费端：

| 套件 | pass（含数据行、init/cleanup） |
| --- | ---: |
| catalog | 22 |
| music | 26 |
| session_api_client | 24 |
| backend_facade | 8 |
| listening | 20 |
| 合计 | **100** |
| backend_runtime仅musicQueriesThroughThread、playbackThroughThread | **4** |

目录用例覆盖五项原生编码／合成传输链与封闭路由、宽ID、元数据不完整／矛盾、两阶段切片／重复／缺失、取消／换号与429、只读Cookie隔离。线程用例覆盖新的artistDetails与playlistSongs跨线程值传递，再回归播放器／歌词消费者。未重跑完整认证矩阵、全量／Release、真实账号／keyring／声卡；本包无新的官方访问。本目录工作包完成时登记23个CTest套件，旧工作包结果不叠加成全量成绩。

```bash
cmake --build --preset linux-dev-local --target \
  tst_catalog tst_music tst_session_api_client tst_backend_facade tst_listening tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(catalog|music|session_api_client|backend_facade|listening)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  musicQueriesThroughThread playbackThroughThread
```

忽略的本机日志：`build/workspace-checks/catalog-build.log`、`catalog-focused.log`、`catalog-thread.log`。调试中发现moc对内联测试原始字符串里的file URL误判注释；fixture使用JSON转义斜杠，运行时仍测试同一个file URL，没有减弱非法图片URL检查。

后续[每日推荐读取／FM条目供给](discovery-and-fm.md)已接入，[FM连续播放](fm-continuous-playback.md)也已接入，[个人库四项读取](personal-library.md)也已接入；[本地观测累计／会话历史](local-listening-history.md)也已另包接入，持久历史与scrobble仍单列。后续[榜单列表／内容摘要](chart-reads.md)已另包离线接入，榜单歌曲复用本歌单详情／两阶段分页，显式选歌入队播放由线程替身验证。其他歌单发现、收藏／编辑、缓存、UI、自动整歌单入队并未因本包而全部完成。
