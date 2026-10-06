# 歌单标签编辑与隐私歌单单向公开

## 1. 范围与证据

维护者确认本包实现标签编辑及将隐私歌单公开。后端先行，仅离线合成验证；**没有真实账号、服务权限、当前响应或最终远端可见性验收**，不启动UI或恢复−462公开窗口。

歌单介绍就是已有 `PlaylistDescription` 的description：可以独立修改或显式空字符串清空，4096 UTF16本地预算，见[基础写契约](playlist-management-writes.md)。本包复用且回归，不重复造介绍接口；与标签／公开不是原子事务。专辑／艺人／歌曲资料读取不因此变成可编辑。

完整核对固定api-enhanced `a8c781fd64faab17fedfd46e0615a2609307f163` 的playlist_tags_update、playlist_privacy、相邻playlist_update／desc_update，home.md、interface.d.ts、util/option.js／config.json／request.js默认链。独立tags文档示例一个标签；综合更新文档说明多个标签用`;`分隔、只能使用规定标签。复用这个文本编码，不采用batch或它的缺省空串。公开文档只说明当前用户隐私歌单公开；wrapper固定privacy0，不提供反向参数。泛Response不是完整响应schema或实网权限证明。

两wrapper未指定crypto、未启用checkToken，固定encrypt=true默认Eapi、encryptResponse=false返回JSON。无需新SDK；这不证明未来服务无额外挑战。[整歌单收藏v2 SDK门禁](collection-writes.md)不变，不能借这两个无SDK操作绕过它。封面上传／上传凭据／私人文件／SHARED／VIDEO／任意隐私转换／可靠对账／自动同步另包。

## 2. Core动作与输入

`playlist_tag_edit_types.h`新增两个封闭值，CollectionChange从11→13种；原mutateCollection入口／8槽，不另造Manager／线程／JSON port。

### 标签工厂

`PlaylistTagEdit::fromCatalogue(originalToken, playlist, source, selectedTags)`：

- valid原SessionToken及规范正signed64 PlaylistId。
- source必须是PlaylistTagsKind::Catalogue，非空tags且不超过已有500条本地读预算；Hot／Premium或PlaylistDetails.tags不能冒充Catalogue。
- 所有source.tags名称符合已有validPlaylistCategory；selected逐一存在于source.tags，拒绝source.all名称及特殊“全部”筛选项。
- selected为**1..3个唯一名称**，每个最多128 UTF16，有效Unicode、无首尾空白／C0／DEL，不含`;`，不静默trim／规范化／去重。这是Cirrus本地预算，**不是服务最大值或官方标签数量证明**。
- 保留原顺序／文字，包括合法引号、反斜线、Unicode；只保留选择／目标ID／原token，不携带整个分类DTO。空选择拒绝：固定独立接口没有足够清空语义证据，**本包不开放标签清空**。

复用已有[分类只读接口](playlist-discovery-reads.md)，由调用方**显式**读取后调用工厂；写不会自动读取分类。source token必须等于当前写token，换号／重新登录后需显式重读，不能重标旧读上下文。Catalogue为公开查询，不额外绑定source account；目标写仍捕获并核验原已认证账号。工厂只能验证结构／名称成员关系，普通读DTO与传入token仍可由调用方构造；不是密码学provenance、完整分类、真实官方名单、实时支持、所有权或编辑许可证明。

### 单向公开

`PlaylistMakePublic{playlist}`只接受一个有效PlaylistId，无privacy bool／枚举／10入口。没有public→private动作，不自动判断owner／当前privacy／影响范围，不把creator或subscribed当权限。一次显式调用，可能被服务拒绝；更改描述或标签不会隐式公开。

## 3. 固定协议与写结果

| operation | wrapper路径（非官方逻辑路径） | 官方逻辑路径 | 业务载荷 |
| --- | --- | --- | --- |
| PlaylistTagsUpdate | `/playlist/tags/update` | `/api/playlist/tags/update` | id规范字符串；tags为保序`;`拼接字符串 |
| PlaylistMakePublic | `/playlist/privacy` | `/api/playlist/update/privacy` | id规范字符串；privacy为固定**数字0** |

两项Default目标／Eapi／JSON／64KiB／Pc固定header与e_r:false，线缆路径为interfacepc.music.163.com/eapi/...；非用户配置host／header／crypto／SDK／timestamp。MUSIC_U必需，MUSIC_A不足，响应Set-Cookie忽略。

内部封闭输入：tags为id字符串及1..3字符串的typed JSON array；基础设施校验后转wire字符串，不接受调用方任意分号文本、空列表、数字ID、任意privacy或额外键。公开内部只有id，privacy永远由实现给0。宽ID不转double；当前服务兼容性未实证。

沿用[收藏写](collection-writes.md)的非空eventId、原账号／token／generation、共享8槽／在途UUID去重及NotSent／Acknowledged／Rejected／Unknown。来源不匹配为发送前NotSent/StaleContext（Facade可直接返回0）；异账号InvalidRequest；发送后取消／超时／切号／close保守Unknown，晚到不复活旧成功。UUID不是服务器幂等键；Rejected或取消也不保证远端零副作用。两项不交付createdPlaylist。

数字或合法字符串code200只表Acknowledged，不证明标签已替换／未合并、隐私已变、全球可见、审核完成、第三方缓存更新或传播时延。无自动重试／刷新／核验／库更新／播放／报告／持久outbox／CAS或撤销承诺。公开可能暴露原私有资料，未来UI须明确确认目标与单向语义；后端接受请求不代替用户确认。

## 4. 离线验证与交接

Qt6.11.2 Debug，LC_ALL=C.UTF-8／QT_FATAL_WARNINGS=1。最新8套件：playlist_tag_writes41、playlist_writes122、playlist_order59、collections189、playlist_discovery54、catalog_metadata66、session_api_client24、backend_facade8，**563 pass**；选定9条线程数据行**11 pass**（含init/cleanup），0fail/skip。CTest40→41、registry70→72（独立公钥链另计），非全41／Release／真实业务或UI验收；旧730＋11不累加。

新suite：工厂kind／成员关系／all／重复／Unicode／1..3及128预算／source500边界／空清除拒绝，2path／payload／crypto／闭合键／固定numeric0／宽ID，4次实际native Eapi解密、原Cookie不轮换，28响应行含200字符串／403／512／−462／429／502／坏schema，Guest／MUSIC_A／跨号／旧source token、发送前后取消／失效／原token／Timeout、Facade8槽／UUID／同token切phase或账号／晚到、不自动读／播放／报告。旧介绍独立修改／显式清空也回归。

线程用例：合成登录3请求→1次显式生产Catalogue mapper读取→工厂→6次显式写（标签／公开／描述／清空／标签403／挂起公开close），总10请求。完成在调用线程、原token／eventId，close Unknown／真实资源排空；无隐式分类／详情刷新、播放、报告或库修改。排空不证明公开撤销。

8个改动C++路径fresh all-server active LSP clean；实际14 application对象（含AUTOMOC）／6940 dependency entries，基础设施／JSON／非Core Qt／OpenSSL泄漏0，UI变化0。初始paired enum／preamble误报经实际构建、编译DB刷新及主动复查解决；保留直接声明头、`<type_traits>`／`<utility>`、Q_OBJECT／moc，无源码suppression。测试保持fatal warnings，没有生产降级。

```bash
cmake --preset linux-dev-local
cmake --build --preset linux-dev-local --target tst_playlist_tag_writes tst_playlist_writes \
  tst_playlist_order tst_collections tst_playlist_discovery tst_catalog_metadata \
  tst_session_api_client tst_backend_facade tst_backend_runtime --parallel 4
cp build/linux-dev-local/compile_commands.json build/compile_commands.json
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 ctest --test-dir build/linux-dev-local \
  -R '^(playlist_tag_writes|playlist_writes|playlist_order|collections|playlist_discovery|catalog_metadata|session_api_client|backend_facade)$' -V --no-tests=error
LC_ALL=C.UTF-8 QT_FATAL_WARNINGS=1 build/linux-dev-local/tests/tst_backend_runtime \
  playlistTagsThroughThread playlistWritesThroughThread playlistOrderThroughThread \
  catalogMetadataThroughThread collectionsThroughThread libraryThroughThread \
  playbackThroughThread destructionStillDrains
```

交接检查记录在ignored build/workspace-checks/playlist-tags-final-{build,focused,thread}.log、playlist-tags-{boundaries,format,diff-check,fixed-hash,links,secrets}.log；本机cache／真实凭据不交付，本地完整提交、不推送或打包发布。clang-format dry-run／git diff --check／固定440模块hash校验通过；639个本地Markdown文件引用有效（排除inline／fenced code，不验证外链／anchor）；有限live-looking Cookie模式扫描0 findings，不把有限扫描当全面秘密保证。真实标签名单／替换与清空语义／权限／保护歌单／可见性效果／并发／可靠核验仍待独立授权；封面流程不在本包，后续[封面更新](playlist-cover-update.md)已独立合成接入／Gui仅基础设施；本页563＋11仍为原包时点，UI最后。需求FR-011部分、领域15、TC-PLAYLIST-TAGS及R-024同步索引，不整体标完成产品需求。
