# 音乐实体、播客与全站声音搜索

## 1. 本包与范围变更

维护者明确将播客／声音的消费端读取与后续播放纳入产品范围，撤销此前对这些内容的整体排除；仍不包含视频／MV、云贝／云豆、动态、创作者上传／审核／编辑工作台或解锁。本包只交付5项搜索：

```cpp
quint64 BackendFacade::searchAlbums(SearchPage query);
quint64 BackendFacade::searchArtists(SearchPage query);
quint64 BackendFacade::searchPlaylists(SearchPage query);
quint64 BackendFacade::searchPodcasts(SearchPage query);
quint64 BackendFacade::searchVoices(SearchPage query);
```

原歌曲搜索保持不变。SearchPage为不含身份的参数值：关键词trim后非空、原长度≤256、合法UTF-16／无NUL，limit1～100、offset0～10000；共用SearchPage默认30／0，直接PodcastSearchQuery的默认页为10／0。Facade调用方可显式选10；不复制上游JS offset=0被 `||30`改为30的行为。查询保留原参数，wire只trim关键词，不本地过滤／排序／自动翻页或去重。

沿用MusicQuery／MusicResult、16槽邮箱、原token／generation、取消、Guest／Authenticated与Runtime后台装配。ID非零仅受理；结果走musicCompleted，取消用cancelMusic；请求／响应不采纳Set-Cookie、认证／恢复、不抓图片。API registry由31到36；QML／模板入口仍不变。

## 2. 固定路径与加密

固定参考 `a8c781fd64faab17fedfd46e0615a2609307f163` 的 `search.js` 和 `voicelist_search.js`，hash与inventory一致，完整 `public/docs/home.md` 搜索／播客章节已核对：

| operation | 网易云逻辑路径 | 业务payload／编码 |
| --- | --- | --- |
| SearchAlbums | `/api/search/get` | s、type:10、limit、offset；eapi／JSON |
| SearchArtists | `/api/search/get` | s、type:100、limit、offset；eapi／JSON |
| SearchPlaylists | `/api/search/get` | s、type:1000、limit、offset；eapi／JSON |
| SearchPodcasts | `/api/search/voicelist/get` | keyword、scene:normal、limit、offset、e_r:true；eapi／EapiAutoJson |
| SearchVoices | `/api/search/voice/get` | keyword、scene:normal、limit、offset；eapi／JSON |

响应上限均2MiB，原interfacepc固定目标。只允许keywords／limit／offset3项输入；caller不能传type、scene、crypto、host、e_r、radioId、voiceListId、私有状态或headers。Guest允许发起，不将HTTP／业务成功等同于完整库、订阅／版权／下载权益。

`voicelist_search.js`明确e_r:true；`util/request.js`保留显式data.e_r，不能用“配置默认false”覆盖该证据。EapiRequestMetadata新增基础设施内部encryptedResponse字段，**仅 `/api/search/voicelist/get` 允许true**，SessionApiClient仅依据闭合SearchPodcasts operation启用。其他所有既有路径保持false，ListeningLog profile不变；编码由契约选，不在解密失败后退回其他协议／目标。采用已验证的EapiAutoJson有界处理加密／明文兼容包装，不开放一般caller编码选项。

不接 `/cloudsearch` 的type2000：固定cloudsearch模块没有专门声音分支，不能把一般文档“search或cloudsearch”当作同一协议证据。不接工作台 `/api/voice/workbench/voice/list`，其账号范围／私有与审核状态不能充当消费端全站或任意播客内搜索；doc radioId与JS query.voiceListId差异、voiceFeeType=0被 `||null`覆盖仍为待补证问题。

## 3. 类型、映射与未知语义

`domain/music/entity_search_types.h`提供中性SearchPage、EntitySearchQuery、PodcastSearchQuery／VoiceSearchQuery，以及5种独立结果页。Artist／Album／Playlist沿用既有ID和目录值；PodcastId与PlaylistId、VoiceId与SongId为不同类型，**不从mainSong.id、mainTrackId、队列UUID或UI文字制造声音身份**。当前没有播客／声音播放命令、Voice→Song适配或报告来源策略。

- 音乐实体读取root.result的albums／artists／playlists及各自albumCount／artistCount／playlistCount，可选hasMore。专辑复用albumFrom；艺人只映射ID／name／picUrl／albumSize／musicSize；歌单只选摘要，不发布creator／订阅／所有权／主路径URL。
- 播客和声音读取root.data.resources数组，每行baseInfo。resourceId若存在必须与baseInfo.id匹配；resourceType若存在须分别为voicelist／voice，不能交叉兜底成歌曲／播客。固定公开样本中的两者均存在且一致，缺失字段仍不伪造。
- Podcast只保存ID、name、picUrl、可选desc／programCount；后者是提供方声音条目计数，不宣称本页完整性。
- Voice只保存自身ID、name、coverUrl、可选radio播客摘要、可选providerDuration。duration只保留原数值，不推断单位／实际音频时钟；忽略mainSong、主音轨、creator／个人账号、action URLs、权益字段以及视频附带元数据。
- Canonical正signed64字符串ID；JSON数值ID≤2^53−1，宽ID必须字符串。可选计数／duration为安全非负JSON整数，缺失／null未知；hasMore必须bool。playlist.playCount允许有限非负有界小数，是提供方聚合值，不是个人计数／时长。
- 使用共享文本／图片规则：name／摘要≤4096、合法UTF-16、无NUL；专辑描述沿用已有目录上限。缺失标题为空展示标签，optional字段未知；元数据图片不抓取，非法／不支持地址降不可用。
- 必须正确包装层及数组，单页≤请求limit，显式total≥当页条数；坏行、混合资源、矛盾计数、超限全部失败，不静默跳过／截断。保留顺序／重复，不用短页计算hasMore、不本地推断全库计数或跨页一致性。
- **空声音公开样本返回resources:null、totalCount:0、hasMore:false**，允许该明确零计数的缺失／null资源为已知空页（hasMore若存在不能true）；没有零计数证据不能把缺失／null默认为成功空结果。实体空数组保持合法，缺失实体数组仍失败。
- 服务端检索可能改写／近似关键词，“无匹配”测试字符串也可能返回播客或歌单；本包不重新按字面过滤掉结果、不保证精确子串匹配或完整覆盖搜索索引。

结果快照仅属于原上下文；切号／退出／close隔离晚到结果，caller持有副本仍须自行丢弃。429保留合法Retry-After、无自动重试。搜索不自动入队、补详情／抓整库、播放或上报；音乐搜索得到的目录ID可由caller显式调用已有目录／页／选歌链，播客／声音不假装可直接送进当前SongId播放。

## 4. 授权的公开证据与入口选择

维护者本轮另行授权无Cookie公开只读小样本，使用现有C++加密／HTTP／解码，仅访问固定官方搜索接口。开发probe运行于忽略的 `build/workspace-checks/search-shape-probe.cpp`，不使用Node、账号、匿名bootstrap token、keyring、数据库或媒体。

**2026-10-05 UTC，20次metadata请求**（不是20个功能验收用例）：首轮voice／voicelist／1009三次，随后5次常规实体与资源字段核对，再2次资源命名空间确认，5次非匹配词形状核对，最后5次通过实际production mapper。均HTTP200／code200；limit2。HTTP Cookie为空，加密header也不含MUSIC_U／MUSIC_A／NMTID；只有非凭据PC元数据／每次生成的开发device ID，不记录这些随机标识。原始HTTP／解密JSON仅进程内使用，没有真实库、creator资料、文字标题／描述、签名URL或原始响应落盘；日志仅字段名／类型、计数、长度及固定资源类别。

观察到：

- 实体分别root.result.albums／artists／playlists；专辑样本无hasMore、艺人／歌单有hasMore，均不推断缺失标记。
- 声音和播客root.data.resources/baseInfo，资源类别分别voice／voicelist，wrapper.resourceId匹配baseInfo.id；声音自身ID与内嵌mainSong.id不同。
- 播客voicelist加密响应可解码；普通5次最终公开样本实际经过新mapper，全部成功。
- 旧 `/search type1009`本次限2却返回30个djRadios，其djRadiosCount=30不能当作新接口totalCount／分页语义；只作为探索证据，**未接入为等价分页／自动fallback**。两种结果都出现radio形状，但不宣称跨版本／所有对象ID完全等价。
- 非匹配词：专辑／艺人空数组，声音明确零计数null resources；歌单／播客仍可能返回近似匹配。没有额外全库抓取。

这是当前机器／该时点的有限公开兼容性，不证明账号／地域／所有声音、会员／付费完整播放、工作台权限、分页全量完整性或声学播放。单元fixture仍用合成ID／文案，不能把真实样本保存成版权曲库fixture。最近播放、写入与真实账号访问不在此授权范围。

## 5. 定向离线验证与交接

本机Qt6.11.2 Debug，LC_ALL=C.UTF-8／QT_FATAL_WARNINGS=1。最新7套件（含数据行／init/cleanup，不累加此前重跑）：entity_search44、catalog22、music26、session_api_client24、netease_crypto18、backend_facade8、playlist_discovery54，合计**196 pass**。线程选择entitySearchThroughThread、playlistDiscoveryThroughThread、musicQueriesThroughThread、playbackThroughThread Queue/FM两行、destructionStillDrains，合计**8 pass**。新线程用例5种搜索原token回到调用方，假加密播客响应、Voice ID与mainSong区分，挂起声音请求close取消排空，没有播放／报告／队列自动副作用。

单元覆盖三类实体／资源包装／命名空间、wide ID／重复顺序／未知及零值、23条坏schema、当前已观察空声音null规则、严格payload／e_r单路径门禁、5×Guest／Authenticated原生链（解密请求与加密播客响应）、Cookie忽略、每operation取消／旧token／429无重试、Facade16槽与晚到隔离。实际响应与合成畸形用例分别记证据。

```bash
cmake --build --preset linux-dev-local --target \
  tst_entity_search tst_catalog tst_music tst_session_api_client tst_netease_crypto \
  tst_backend_facade tst_playlist_discovery tst_backend_runtime --parallel 4
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(entity_search|catalog|music|session_api_client|netease_crypto|backend_facade|playlist_discovery)$' \
  -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  entitySearchThroughThread playlistDiscoveryThroughThread musicQueriesThroughThread \
  playbackThroughThread destructionStillDrains
```

现登记33套件，不声明全量Debug／Release或真实认证／keyring矩阵。15个改动C++路径最终active LSP清洁，format／diff无问题；实际14个application编译条目（13手写TU＋AUTOMOC）的Ninja依赖没有infrastructure／Network／Multimedia／Sql／Quick／Qml／Gui／JSON／OpenSSL泄漏，模板UI diff=0，456个本地Markdown链接无失效目标（跳过代码标记）。固定清单440项hash验证通过，仍164／40／236历史审计决定；未移除必要include或写压制注释。分步头文件更新的SearchPage／SearchPodcasts／encryptedResponse诊断经真实构建与刷新复核；真正缺失的test profile include已添加，.moc由AUTOMOC生成；scoped enum／inline方法被no-bit-fields误报已记录false-positive。

忽略的 `entity-search-*.log`为configure／build／定向／线程记录，`search-public-*-schema.log`为结构摘要。应用入口不接后端，public probe不是产品UI。下一包再核对公开消费端详情／内容列表与歌单／专辑／播客／声音最近播放；播客声音播放另包。工作台、歌单内搜索仍待契约补证，不用本地过滤冒充服务端搜索，不自动下载整歌单或开启任何报告。
