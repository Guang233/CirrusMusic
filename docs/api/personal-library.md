# 个人库只读查询：当前账号、有界结果

已接入后端，未接UI、真实账号或新的官方实测。复用MusicQuery／MusicResult、16槽查询邮箱、取消／原token／账户generation与Runtime后台装配；不添加缓存、写操作或运行依赖。

## 1. 范围与固定参考

固定参考 `a8c781fd64faab17fedfd46e0615a2609307f163` 的四个模块SHA-256与审计清单一致；参考的是第三方兼容实现和 `public/docs/home.md`，不是官方保证。静态inventory保持审计时点状态，不充当实现登记表。封闭业务registry由15扩展至**19**项。

| Facade入口／operation | 上游模块 | 官方逻辑路径／协议 | 固定行为／解码上限 |
| --- | --- | --- | --- |
| myPlaylists(page)／MyPlaylists | user_playlist.js | /api/user/playlist，weapi | uid来自当前账号；limit/offset；includeVideo:false；4MiB |
| likedSongIds()／LikedSongIds | likelist.js | /api/song/like/get，默认eapi | 仅uid；4MiB |
| savedAlbums(page)／SavedAlbums | album_sublist.js | /api/album/sublist，weapi | limit/offset、total:true；4MiB |
| savedArtists(page)／SavedArtists | artist_sublist.js | /api/artist/sublist，weapi | limit/offset、total:true；2MiB |

歌单默认30条，专辑／艺人默认25条，offset默认0；页大小1–100，offset0–100000。拒绝小数、超限、任意URL和多余参数，不让调用方覆写total/includeVideo。上游user_playlist固定includeVideo:true，本项目按排除视频的范围明确改为false；这一差异尚无实网证明，不宣传已验证所有账号兼容。

喜欢ID接口没有分页契约；最多100000个规范ID，超过上限整次InvalidResponse，不截断后声称完整。不会自动进行歌曲详情补齐、递归翻页、把歌单整库加入队列或自动播放。

## 2. 账号边界

- Facade只接收分页参数，没有任意uid入口；从已验证SessionSnapshot捕获accountId，非Ready／非Authenticated／无效输入／满邮箱返回0。受理ID不等于查询成功。
- Core-only查询DTO保留accountId。MusicQueryService除检查原token和Authenticated外，还要求其与当前账号相等；内部端口误传另一个账号也在发网前拒绝InvalidRequest。
- 原生weapi **及eapi**统一通过requiresAccountCookie检查非空MUSIC_U。匿名MUSIC_A不能替代；这只是底层防御，Native client不负责宣布账号验证成功，禁止绕开应用service拼装个人业务。
- 四种响应均不采纳Set-Cookie、不产生身份收据或改变记忆凭据。失败只返回脱敏MusicFailure；429保留合法Retry-After，不自动重试、签出或删除凭据。
- 结果保留请求账号（query或accountId），完成事件带原SessionToken。注销／切号／close先使旧请求失效，迟到成功不会作为当前账号结果送出。
- 这是一次性读取，没有全局库缓存。调用方若保存结果，仍须监听会话失效、清理旧数据，转发排队或重入后重新核对当前上下文，不能认为已返回的值会自动被擦除。

## 3. 值模型与映射

`src/domain/music/library_types.h`仅依赖Core值类型；JSON、路径和Cookie均留在基础设施。

- **MyPlaylistsPage**：读取playlist数组，返回歌单ID、标题、封面、可缺失creatorId／trackCount／subscribed；保留more及可缺失total。不假定首条就是“我喜欢”，不将缺失subscribed当false，也不靠缺失作者推断创建／收藏分类。摘要不是PlaylistDetails，不带完整trackIds或自动补歌曲。
- **LikedSongIds**：读取ids数组，仅保留这次返回的ID及原账号。保留提供方顺序但不赋予播放、收藏时间或历史含义；重复规范ID视为无效响应，不静默修复。没有分页／全局缓存完整性承诺，也没有喜欢／取消喜欢操作。
- **SavedAlbumsPage**：读取data、count和hasMore；复用目录Album的严格映射（艺人署名、封面、可缺失数量／发行日期），不再建立另一套专辑schema。发行日期允许负的历史毫秒时间。
- **SavedArtistsPage**：读取data、count和hasMore；仅音乐相关艺人ID、名称、头像和可缺失albumCount，不扩展MV字段或视频业务。
- 页数组最多请求limit项；空数组成功，缺少／null／错类型数组失败。缺少或null计数／more仍未知，不依据短页或offset捏造结束；出现错误类型、负计数或计数小于当页条数则拒绝。
- 所有ID复用规范正signed64字符串规则，JSON数字只接收到2^53−1；超宽ID必须是字符串。若响应带uid则必须匹配原账号。文本沿用4096 UTF-16与合法字符检查，专辑描述沿用目录上限；封面／头像复用元数据URL校验，返回URL不授权任意图片网络访问。
- 不保证跨页原子快照，不将不同时间请求拼成“服务端当前完整库”；同步／持久化／增量对账另立契约。

具体映射和载荷构造位于 `src/infrastructure/netease/music/library_mapper.*`；`catalog_mapper.cpp`只导出已有专辑／图片映射供复用。Native API保留封闭参数白名单。

后续[五类收藏订阅写](collection-writes.md)独立接入红心／收藏到歌单及移除／专辑／艺人／播客，非本只读port变体，无自动读刷新／库缓存。整歌单收藏／取消的v2 SDK链经确认延期；写后明确读取只能查看当前有界结果，不推定全批原子提交，Unknown不得盲目重试。本文保持旧读取包证据。

## 4. 离线验证

本机Qt6.11.2 Debug，`LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1`。只运行受影响测试，不重跑全量认证／Release／真实keyring／声卡。

| 套件最新结果 | Qt Test pass（含init/cleanup及数据行） |
| --- | ---: |
| library | 21 |
| discovery | 17 |
| catalog | 22 |
| music | 26 |
| session_api_client | 24 |
| backend_facade | 8 |
| 合计 | **118** |
| backend_runtime仅libraryThroughThread、musicQueriesThroughThread、playbackThroughThread（queue/FM）、destructionStillDrains | **7** |

library覆盖固定路径／加密／参数／响应大小、wide uid、认证前无网络、四种内部错账号查询拒绝、匿名凭据不能替代MUSIC_U、Cookie不回写、原生查询往返、不自动补详情、未知元数据／空集合／坏schema、页上限／100000喜欢ID边界、取消／换号／429和Facade满邮箱及注销丢旧结果。线程用例经过完整合成登录／身份提升，验证四种值跨线程返回、账号／页参数保留和挂起查询中关闭排空；既有队列/FM及歌词查询线程路径一起定向回归。

开发中修正了测试初始化器的闭合括号；随后遇到Qt moc对内联raw JSON中的URL误判（No relevant classes／vtable缺失），fixture改为JSON转义斜杠，解析后的值未变。没有放松产品校验。首轮六套件通过后，补默认页大小／响应上限断言与独立线程用例，仅复跑library和所列线程用例；不叠加重复次数。clangd在头文件更新时的PCH陈旧级联通过刷新编译数据库与active检查解决，不移除必要include。

```bash
cmake --build --preset linux-dev-local --target \
  tst_library tst_discovery tst_catalog tst_music tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(library|discovery|catalog|music|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  libraryThroughThread musicQueriesThroughThread playbackThroughThread destructionStillDrains
```

本包完成时登记**27个CTest套件**，不是27套全量验收；最新数量见[接手指南](../HANDOFF.md)。忽略日志：`build/workspace-checks/library-*.log`，最新为focused、final-library、final-thread及对应构建日志。入口main.cpp／Main.qml仍为模板。

## 5. 未实现项与下一步

后续[账号补充读取](account-library-reads.md)已另包接入播客订阅／云盘列表与详情，独立CloudSongId、原账号／MUSIC_U；仅合成候选，无云盘音源／播放／写。本文19项registry／27套件／118＋7是当时的四读取包证据，最新数量／定向结果见HANDOFF，不回写成实时支持名单。

这四项读取不是完整个人中心：其他独立创建／收藏歌单接口、关注／喜欢／收藏写入、库缓存／同步、云盘／下载／评论／榜单仍未因本包完成。

个人库读取本身不提供计量、持久播放历史或scrobble。后续[本地观测累计／有界会话历史](local-listening-history.md)已另包接入，[显式报告能力](explicit-listening-reports.md)也已接入，自动报告时机与持久历史仍未实现；后续[云端最近听过／一周和全部时间排行](cloud-history-reads.md)已另包离线接入，仍无本地历史持久化，不与个人库schema混用；不能以曲目时长或进度条代替实听，更不能从喜欢列表推断播放记录。官方上报及真实账号写入仍需单独授权，UI继续最后。
