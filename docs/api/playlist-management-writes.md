# 自建歌单创建、改名与描述写入

## 1. 范围与证据

维护者确认的小包：创建普通／隐私音乐歌单、单独改名、单独修改描述。仅合成离线接入；**没有真实账号、权限、当前服务响应或远端副作用验收**。删除、歌曲排序、标签、封面／文件上传、协作SHARED／VIDEO歌单、可见性转换、整歌单收藏、UI及自动对账不在本包。

固定参考仍为 `a8c781fd64faab17fedfd46e0615a2609307f163`。完整核对 `playlist_create.js`、`playlist_name_update.js`、`playlist_desc_update.js`、`playlist_update.js`，home.md新建／单独改名／描述章节、TS签名，复用已完整核对的options／request／config链。create显式Weapi，后两项默认encrypt:true→Eapi；三项固定wrapper没有启用checkToken SDK。TS只有generic Response，**不提供创建响应schema证明**；参考不是官方文档或实网支持列表。原440模块／164采用候选／40待定／236排除审计不改为交付状态。

不使用playlist_update的batch及手拼JSON；改名与描述是两个明确动作，每次只发送一次，不伪装多字段原子编辑。整歌单收藏／取消仍遵守[原v2门禁](collection-writes.md)，未注册、换协议、注入token或执行SDK。未访问真实凭据／keyring、账号、业务服务、媒体、私人文件，也未继续此前−462窗口。

## 2. Core入口与输入

在已有 `domain/music/collection_types.h` 和窄 `ICollectionGateway` 写链增添三个封闭variant，**不另造Manager／网络服务／线程，不把写加入只读MusicQuery**。历史端口／入口名保留，不把创建误称为收藏动作：

- `PlaylistCreate{name, privacy}`：name有意义非全空白、合法UTF-16、1..128 code units；Public为默认，固定wire文本"0"，Private为"10"；未知枚举拒绝。创建类型固定NORMAL，不接收type字符串、分享／视频参数。
- `PlaylistRename{PlaylistId, name}`：规范正signed64十进制ID及同一名称校验；wide ID保持文本。
- `PlaylistDescription{PlaylistId, description}`：0..4096合法UTF-16；**明确空字符串表示清空**，不是遗漏字段／默认改写。允许TAB／LF／CR，拒绝其他C0和DEL；name拒绝所有C0／DEL。文本不trim／Unicode normalize／富文本处理，名称首尾空白若有实质内容原样保留；不证明服务端也不规范化。

上述128／4096是Cirrus预算，不是服务端上限。文本只是显示／业务参数，不能当路径、URL、HTML或诊断内容。quotes／反斜线／换行由JSON编码器处理，不拼业务JSON。

调用 `BackendFacade::mutateCollection(CollectionMutation{nonNullEventId, change})`；完成 `collectionCompleted(id, originalToken, CollectionResult)`，取消cancelCollection。Facade捕获原accountId／token／generation；未Ready／Guest／Committing、坏参数、共享8槽满、同UUID在途返回0，无派发／完成信号。共享槽位与原五类写统一有界；不同UUID并发没有远端顺序／最终状态保证。UUID只本地关联，不是创建幂等键。

Gateway再次校验Authenticated、原token／规范账号一致；当前token＋异账号→NotSent／InvalidRequest，旧token→NotSent／StaleContext。Native必须MUSIC_U，MUSIC_A不足；Set-Cookie不采用。不从creator／subscribed／列表顺序／元数据推定自建或可编辑权限，也不接收isOwner开关；服务端仍可能拒绝，权限UI另验收。

## 3. 封闭官方契约

新增3 operation，当前registry **62→65**；Default官方目标、JSON响应64KiB，无任意URL／host／crypto／profile／header／csrf／e_r／checkToken／timestamp覆盖。

| operation | 官方逻辑路径 | 固定payload | crypto |
| --- | --- | --- | --- |
| PlaylistCreate | `/api/playlist/create` | name文本、privacy文本"0"或"10"、固定type:"NORMAL" | 显式Weapi |
| PlaylistRename | `/api/playlist/update/name` | id文本、name文本 | 默认Eapi |
| PlaylistDescription | `/api/playlist/desc/update` | id文本、desc文本，空字符串保留 | 默认Eapi |

包装路由 `/playlist/name/update` 不是官方逻辑路径 `/api/playlist/update/name`。内部参数白名单create仅name／privacy（严格文本）；编辑仅id／name或id／desc。固定type不由调用者传；缺键、null、bool／数值文本替身、未知privacy、多余键均拒绝。不采用JS `||`默认／数值强转，不发送eventId／accountId为uid。Eapi保持Pc header与e_r:false；Weapi保留原浏览器／csrf链。未降级接口或执行参考Node服务。

## 4. 创建身份候选、结果与Unknown

沿用[四种写结果](collection-writes.md)：NotSent、Acknowledged、Rejected、Unknown。发送前确认未派发才NotSent；合法非200码为Rejected；发送后的网络／HTTP／解码／超时／取消／失效为Unknown。HTTP200、requestId、Ready、元数据或本地受理不证明远端成功／可编辑权；Rejected也不保证绝无部分副作用。429信息只保留合法Retry-After，不自动重试。

`CollectionResult`新增可选 `createdPlaylist: PlaylistId`，**仅创建的合法Ack且原上下文有效才发布**。候选业务码200响应要求root.playlist是object，playlist.id是规范正ID；JSON数字≤2^53−1，宽值用规范signed64文本。可选root.id若出现必须同样规范且与playlist.id一致，null／0／错类型／不一致失败。没有root.id、data.id或其他路径fallback；忽略其他playlist／作者／曲目／权限字段，不自动映射整个歌单／拉详情。

仅码200但身份缺失、错误或矛盾→**Unknown／InvalidResponse，保留providerCode=200，不发布ID**：这是创建结果不能确认，不是“没创建”。真实服务可能使用其他schema，本轮未证明这个候选完整或当前兼容；后续补证应修契约，不能静默兜底猜ID。改名／描述的Ack只确认合法码200响应，不要求虚构资源回显，也不携带createdPlaylist。超时／拒绝的body即使带ID也不发布。

Facade再次拒绝创建Ack中的缺失／非规范ID；非创建或非Ack剥离该字段。取消／切号／同token认证态或账号变化／关闭／错误generation的结果保留原token／eventId且不发布ID，晚到Ack不能复活或归给新账号。已交付DTO副本不能自动擦除，消费者仍须清理旧账号内容。createdPlaylist只是当前响应身份，不证明所有权、编辑许可、隐私保护、库可见性或未来账号可使用它。

**不盲重试创建，不把同名当唯一身份，不因取消声称远端撤销。** 无同名查询／自动回读、库刷新／乐观更新、outbox／缓存／SQL、自动播放／报告或整批事务。调用方可另发已有有界账号歌单／指定ID详情读取，仍不证明创建请求关联、完整库／跨页一致性或多字段原子效果。本包没有新的可靠创建对账协议；明确核验流程仍待独立契约。

## 5. 离线验证与交接

Qt6.11.2 Debug，LC_ALL=C.UTF-8／QT_FATAL_WARNINGS=1。最新七套件：playlist_writes122、collections189、catalog22、library21、listening_report18、session_api_client24、backend_facade8，**404 pass**；另playlistWrites／collections／library／listeningReport／playback（queue/FM）／destruction七条线程数据行，**9 pass**（含init/cleanup），均无fail/skip。登记37→38套件；不是全38／Release／真实服务／SDK／UI验收，之前335＋9不累加。

新suite：5种输入模式（公开／私有创建、改名、描述／清空）、独立官方路径／载荷／固定NORMAL／crypto／64KiB断言、10次native显式写与原Cookie不轮换、改名／描述实际Eapi解密（Pc header／e_r:false）及quotes／反斜线／换行保真；创建Weapi核对固定wire路由／form并另有独立业务payload期望，不声称逐次独立解密Weapi。70行响应矩阵，22个有效JSON的坏创建身份fixture、数字／signed64最大值／echo一致性、空描述／UTF16／预算／未知枚举／多余字段／SDK覆写拒绝、Guest／MUSIC_A／异账号／旧token、发送前后取消／失效、8槽／UUID、同token认证／账号变化、坏／缺创建delivery、晚到／错误generation及无ID泄漏；旧五类写与报告／目录／个人库回归。

线程用例使用fake secrets／完整合成登录、两个同名创建返回不同ID（不作同名去重或唯一性依据）、单独改名／描述／清空、码200身份缺失Unknown及403拒绝均无隐式请求，挂起创建close Unknown且资源真实排空。原token／eventId、调用线程及无读／播放／报告验证；排空不是远端撤销。

9个修改C++路径fresh active LSP确认clean。实际Ninja：14个application对象／6924 dependency entries，无基础设施、JSON、非Core Qt或OpenSSL穿透；UI差异0。生成前moc／编译数据库旧诊断经实际AUTOMOC构建、DB刷新、文件存在性及active复查确认解决；头内scoped enum／普通方法bitfield启发式误报已核实，未删Q_OBJECT／moc／必要include或加suppression。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_playlist_writes tst_collections tst_catalog \
  tst_library tst_listening_report tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(playlist_writes|collections|catalog|library|listening_report|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playlistWritesThroughThread collectionsThroughThread libraryThroughThread listeningReportThroughThread \
  playbackThroughThread destructionStillDrains
```

clang-format dry-run、git diff --check、固定440模块hash校验通过；569个本地Markdown文件引用有效（排除inline／fenced code，不验证外链／anchor）。live-looking Cookie有限模式扫描0 findings，不是全面秘密保证。

ignored日志 `build/workspace-checks/playlist-writes-final-{build,focused,thread}.log`及playlist-writes-{boundaries,format,diff-check,fixed-hash,links}.log、生成／DB检查单列；本机cache／凭据不交付，只本地完整提交，不推送。未交付删除／排序／封面／标签／可见性转换／整歌单收藏SDK、可靠创建对账／同步、真实响应／权限／隐私／副作用和UI验收；后续建议仅是路线，不自动授权实现或真实访问。
